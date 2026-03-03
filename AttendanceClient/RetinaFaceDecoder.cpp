#include "RetinaFaceDecoder.h"
#include <QMessageBox> // ★ 新增：为了使用弹窗

// RetinaFace MobileNet0.25 官方参数
const float variances[2] = { 0.1f, 0.2f };
const int steps[3] = { 8, 16, 32 };
const int min_sizes[3][2] = { {16, 32}, {64, 128}, {256, 512} };

// 👇 === 替换你的构造函数 === 👇
RetinaFaceDecoder::RetinaFaceDecoder(const std::string& onnx_path) {
    try {
        // 尝试读取模型
        net = cv::dnn::readNetFromONNX(onnx_path);
        if (!net.empty()) {
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
    }
    catch (const cv::Exception& e) {
        // ★ 核心防御：拦截崩溃！如果报错，程序不会卡死，而是弹出一个写满真实原因的错误框
        QString errorDetail = QString::fromStdString(e.what());
        QMessageBox::critical(nullptr, "模型加载失败", "OpenCV 真实死因：\n\n" + errorDetail);
    }
}

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

std::vector<FaceDetectInfo> RetinaFaceDecoder::detect(const cv::Mat& image, float conf_threshold, float nms_threshold) {
    std::vector<FaceDetectInfo> faces;
    if (image.empty() || net.empty()) return faces;

    // 强制使用固定的 640x480 分辨率，严格匹配静态 ONNX 模型的输入
    int img_w = 640;
    int img_h = 480;

    // 1. 图像预处理并送入神经网络
    cv::Mat blob = cv::dnn::blobFromImage(image, 1.0, cv::Size(img_w, img_h), cv::Scalar(104, 117, 123), false, false);
    net.setInput(blob);

    std::vector<cv::String> outNames = net.getUnconnectedOutLayersNames();
    std::vector<cv::Mat> outs;
    net.forward(outs, outNames);

    // 2. 动态区分 3 个输出矩阵 (坐标偏移, 置信度, 关键点偏移)
    cv::Mat loc, conf, landms;
    for (const auto& out : outs) {
        int last_dim = out.size[out.dims - 1];
        if (last_dim == 4) loc = out;
        else if (last_dim == 2) conf = out;
        else if (last_dim == 10) landms = out;
    }

    if (loc.empty() || conf.empty() || landms.empty()) return faces;

    // 3. 生成先验框 (Anchors)
    create_anchors(img_w, img_h);
    int num_anchors = priors.size();

    const float* loc_data = (const float*)loc.data;
    const float* conf_data = (const float*)conf.data;
    const float* landms_data = (const float*)landms.data;

    std::vector<cv::Rect> bboxes;
    std::vector<float> confidences;
    std::vector<std::vector<cv::Point2f>> landmarks_list;

    // 4. 遍历所有 Anchors，进行数学反算解码
    for (int i = 0; i < num_anchors; ++i) {
        float score = conf_data[i * 2 + 1];
        if (score < conf_threshold) continue;

        cv::Rect2f prior = priors[i];

        // 还原人脸边框
        float cx = prior.x + loc_data[i * 4 + 0] * variances[0] * prior.width;
        float cy = prior.y + loc_data[i * 4 + 1] * variances[0] * prior.height;
        float w = prior.width * exp(loc_data[i * 4 + 2] * variances[1]);
        float h = prior.height * exp(loc_data[i * 4 + 3] * variances[1]);

        bboxes.push_back(cv::Rect((cx - w / 2.0f) * img_w, (cy - h / 2.0f) * img_h, w * img_w, h * img_h));
        confidences.push_back(score);

        // 还原 5 个面部关键点
        std::vector<cv::Point2f> pts(5);
        for (int j = 0; j < 5; ++j) {
            float lx = prior.x + landms_data[i * 10 + j * 2 + 0] * variances[0] * prior.width;
            float ly = prior.y + landms_data[i * 10 + j * 2 + 1] * variances[0] * prior.height;
            pts[j] = cv::Point2f(lx * img_w, ly * img_h);
        }
        landmarks_list.push_back(pts);
    }

    // 5. NMS (非极大值抑制) - 过滤掉重复的重叠框
    std::vector<int> indices;
    cv::dnn::NMSBoxes(bboxes, confidences, conf_threshold, nms_threshold, indices);

    for (int idx : indices) {
        FaceDetectInfo info;
        info.rect = bboxes[idx];
        info.score = confidences[idx];
        for (int j = 0; j < 5; j++) info.pts[j] = landmarks_list[idx][j];
        faces.push_back(info);
    }

    return faces;
}