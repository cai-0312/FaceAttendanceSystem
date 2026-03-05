#include "RetinaFaceDecoder.h"
#include <QMessageBox>

// 官方方差参数与网络步长，用于边框回归与解码
const float variances[2] = { 0.1f, 0.2f };
const int steps[3] = { 8, 16, 32 };
const int min_sizes[3][2] = { {16, 32}, {64, 128}, {256, 512} };

// 构造函数：加载模型并进行异常捕获，防止路径错误或模型损坏导致程序崩溃
RetinaFaceDecoder::RetinaFaceDecoder(const std::string& onnx_path) {
    try {
        // 尝试从ONNX路径加载神经网络
        net = cv::dnn::readNetFromONNX(onnx_path);
        if (!net.empty()) {
            // 设置OpenCV后端为默认后端，目标运行平台为CPU
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
    }
    catch (const cv::Exception& e) {
        // 错误防御：若加载失败则通过Qt弹窗显示OpenCV捕获的详细异常信息
        QString errorDetail = QString::fromStdString(e.what());
        QMessageBox::critical(nullptr, "模型加载失败", "OpenCV 真实死因：\n\n" + errorDetail);
    }
}

// 生成先验框：根据输入图像的分辨率，在不同特征层生成基础锚框
void RetinaFaceDecoder::create_anchors(int w, int h) {
    priors.clear();
    for (int k = 0; k < 3; ++k) {
        int step = steps[k];
        int feature_map_w = std::ceil((float)w / step);
        int feature_map_h = std::ceil((float)h / step);
        for (int i = 0; i < feature_map_h; ++i) {
            for (int j = 0; j < feature_map_w; ++j) {
                for (int s = 0; s < 2; ++s) {
                    float min_size = min_sizes[k][s];
                    float cx = (j + 0.5f) * step / w;
                    float cy = (i + 0.5f) * step / h;
                    float sw = min_size / w;
                    float sh = min_size / h;
                    priors.push_back(cv::Rect2f(cx, cy, sw, sh));
                }
            }
        }
    }
}

// 检测函数：执行图像预处理、前向传播、解码回归框以及非极大值抑制
std::vector<FaceDetectInfo> RetinaFaceDecoder::detect(const cv::Mat& image, float conf_threshold, float nms_threshold) {
    std::vector<FaceDetectInfo> faces;
    if (image.empty() || net.empty()) return faces;

    // 将输入分辨率固定为640x480以匹配ONNX模型的静态输入要求
    int img_w = 640;
    int img_h = 480;

    // 图像预处理：减去均值并转化为网络所需的四维Blob格式
    cv::Mat blob = cv::dnn::blobFromImage(image, 1.0, cv::Size(img_w, img_h), cv::Scalar(104, 117, 123), false, false);
    net.setInput(blob);

    // 获取网络所有的输出层名称并进行前向推理
    std::vector<cv::String> outNames = net.getUnconnectedOutLayersNames();
    std::vector<cv::Mat> outs;
    net.forward(outs, outNames);

    // 解析输出：根据矩阵最后维度的不同来区分坐标偏移、置信度和关键点数据
    cv::Mat loc, conf, landms;
    for (const auto& out : outs) {
        int last_dim = out.size[out.dims - 1];
        if (last_dim == 4) loc = out;
        else if (last_dim == 2) conf = out;
        else if (last_dim == 10) landms = out;
    }

    if (loc.empty() || conf.empty() || landms.empty()) return faces;

    // 准备对应分辨率的先验框数据
    create_anchors(img_w, img_h);
    int num_anchors = priors.size();

    const float* loc_data = (const float*)loc.data;
    const float* conf_data = (const float*)conf.data;
    const float* landms_data = (const float*)landms.data;

    std::vector<cv::Rect> bboxes;
    std::vector<float> confidences;
    std::vector<std::vector<cv::Point2f>> landmarks_list;

    // 解码循环：遍历每个先验框并应用公式计算真实的目标坐标
    for (int i = 0; i < num_anchors; ++i) {
        float score = conf_data[i * 2 + 1];
        if (score < conf_threshold) continue;

        cv::Rect2f prior = priors[i];

        // 通过位置偏移和先验框还原出真实的人脸矩形框
        float cx = prior.x + loc_data[i * 4 + 0] * variances[0] * prior.width;
        float cy = prior.y + loc_data[i * 4 + 1] * variances[0] * prior.height;
        float w = prior.width * exp(loc_data[i * 4 + 2] * variances[1]);
        float h = prior.height * exp(loc_data[i * 4 + 3] * variances[1]);

        bboxes.push_back(cv::Rect((cx - w / 2.0f) * img_w, (cy - h / 2.0f) * img_h, w * img_w, h * img_h));
        confidences.push_back(score);

        // 还原面部五个关键点（眼睛、鼻子、嘴角）的原始坐标
        std::vector<cv::Point2f> pts(5);
        for (int j = 0; j < 5; ++j) {
            float lx = prior.x + landms_data[i * 10 + j * 2 + 0] * variances[0] * prior.width;
            float ly = prior.y + landms_data[i * 10 + j * 2 + 1] * variances[0] * prior.height;
            pts[j] = cv::Point2f(lx * img_w, ly * img_h);
        }
        landmarks_list.push_back(pts);
    }

    // 执行非极大值抑制（NMS）：去除同一人脸上的冗余重叠框
    std::vector<int> indices;
    cv::dnn::NMSBoxes(bboxes, confidences, conf_threshold, nms_threshold, indices);

    // 将结果封装进FaceDetectInfo结构体并返回
    for (int idx : indices) {
        FaceDetectInfo info;
        info.rect = bboxes[idx];
        info.score = confidences[idx];
        for (int j = 0; j < 5; j++) info.pts[j] = landmarks_list[idx][j];
        faces.push_back(info);
    }

    return faces;
}