#include "RetinaFaceDecoder.h"
#include <QMessageBox>
// 用于边框回归与特征解码计算
const float variances[2] = { 0.1f, 0.2f };
const int steps[3] = { 8, 16, 32 };
const int min_sizes[3][2] = { {16, 32}, {64, 128}, {256, 512} };
// 加载神经网络模型并进行异常捕获，防止因路径错误或模型损坏导致程序异常终止
RetinaFaceDecoder::RetinaFaceDecoder(const std::string& onnx_path) {
    try {
        // 尝试从指定的本地文件路径加载 ONNX 格式的深度神经网络模型
        net = cv::dnn::readNetFromONNX(onnx_path);
        if (!net.empty()) {
            // 配置 OpenCV 的计算后端为默认引擎，并设置目标运行平台为 CPU
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
    }
    catch (const cv::Exception& e) {
        // 异常防御机制：若模型加载失败，则通过 Qt 阻塞弹窗显示底层的详细错误栈信息
        QString errorDetail = QString::fromStdString(e.what());
        QMessageBox::critical(nullptr, "模型加载失败", "模型解析遇到严重异常：\n\n" + errorDetail);
    }
}
// 根据输入图像的分辨率，在不同的特征金字塔层级上生成基础锚框集合
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
// 核心检测算法：执行图像预处理、前向传播推理、坐标解码回归以及非极大值抑制（NMS）
std::vector<FaceDetectInfo> RetinaFaceDecoder::detect(const cv::Mat& image, float conf_threshold, float nms_threshold) {
    std::vector<FaceDetectInfo> faces;
    if (image.empty() || net.empty()) return faces;
    // 将输入分辨率统一约束转换为 640x480，以严格匹配当前 ONNX 模型的静态输入张量要求
    int img_w = 640;
    int img_h = 480;
    // 图像预处理阶段：扣除通道均值并转化为深度神经网络标准输入所需的 4D Blob 数据格式
    cv::Mat blob = cv::dnn::blobFromImage(image, 1.0, cv::Size(img_w, img_h), cv::Scalar(104, 117, 123), false, false);
    net.setInput(blob);
    // 获取网络模型的所有独立输出层名称，并执行单次完整的前向传播推理计算
    std::vector<cv::String> outNames = net.getUnconnectedOutLayersNames();
    std::vector<cv::Mat> outs;
    net.forward(outs, outNames);
    // 输出层解析：根据矩阵的最终维度特征，分别剥离出坐标偏移量、置信度分数以及面部关键点特征
    cv::Mat loc, conf, landms;
    for (const auto& out : outs) {
        int last_dim = out.size[out.dims - 1];
        if (last_dim == 4) loc = out;
        else if (last_dim == 2) conf = out;
        else if (last_dim == 10) landms = out;
    }
    // 若任一核心输出矩阵为空，说明网络结构解析失败或模型版本不匹配，安全返回空结果
    if (loc.empty() || conf.empty() || landms.empty()) return faces;
    // 动态构建与当前处理分辨率相匹配的先验锚框数据集合
    create_anchors(img_w, img_h);
    int num_anchors = priors.size();
    const float* loc_data = (const float*)loc.data;
    const float* conf_data = (const float*)conf.data;
    const float* landms_data = (const float*)landms.data;
    std::vector<cv::Rect> bboxes;
    std::vector<float> confidences;
    std::vector<std::vector<cv::Point2f>> landmarks_list;
    // 边框解码循环：遍历每一个先验框，应用边框回归公式计算出真实的人脸物理坐标
    for (int i = 0; i < num_anchors; ++i) {
        float score = conf_data[i * 2 + 1];
        // 直接剔除低于置信度阈值的无效背景框，以大幅节约后续矩阵计算的资源开销
        if (score < conf_threshold) continue;
        cv::Rect2f prior = priors[i];
        // 利用神经网络输出的位置偏移量与先验框结合，逆向还原得到真实的人脸矩形边界
        float cx = prior.x + loc_data[i * 4 + 0] * variances[0] * prior.width;
        float cy = prior.y + loc_data[i * 4 + 1] * variances[0] * prior.height;
        float w = prior.width * exp(loc_data[i * 4 + 2] * variances[1]);
        float h = prior.height * exp(loc_data[i * 4 + 3] * variances[1]);
        bboxes.push_back(cv::Rect((cx - w / 2.0f) * img_w, (cy - h / 2.0f) * img_h, w * img_w, h * img_h));
        confidences.push_back(score);
        // 解码并还原面部五个核心关键点（双眼、鼻尖、双侧嘴角）的绝对像素坐标
        std::vector<cv::Point2f> pts(5);
        for (int j = 0; j < 5; ++j) {
            float lx = prior.x + landms_data[i * 10 + j * 2 + 0] * variances[0] * prior.width;
            float ly = prior.y + landms_data[i * 10 + j * 2 + 1] * variances[0] * prior.height;
            pts[j] = cv::Point2f(lx * img_w, ly * img_h);
        }
        landmarks_list.push_back(pts);
    }
    // NMS 算法处理：执行非极大值抑制机制，消除指向同一张人脸的高重叠率冗余矩形框
    std::vector<int> indices;
    cv::dnn::NMSBoxes(bboxes, confidences, conf_threshold, nms_threshold, indices);
    // 结果封装：将经过层层算法过滤后的有效人脸数据装载至结构体对象中交由上层业务使用
    for (int idx : indices) {
        FaceDetectInfo info;
        info.rect = bboxes[idx];
        info.score = confidences[idx];
        for (int j = 0; j < 5; j++) {
            info.pts[j] = landmarks_list[idx][j];
        }
        faces.push_back(info);
    }
    return faces;
}