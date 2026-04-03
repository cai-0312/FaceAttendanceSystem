#include "RetinaFaceDecoder.h"
#include <QMessageBox>
// 检测参数
const float variances[2] = { 0.1f, 0.2f };
const int steps[3] = { 8, 16, 32 };
const int min_sizes[3][2] = { {16, 32}, {64, 128}, {256, 512} };
// 构造函数，加载模型并启用加速
RetinaFaceDecoder::RetinaFaceDecoder(const std::string& onnx_path) {
    try {
        net = cv::dnn::readNetFromONNX(onnx_path);
        if (!net.empty()) {
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
        }
    }
    catch (const cv::Exception& e) {
        QString errorDetail = QString::fromStdString(e.what());
        QMessageBox::critical(nullptr, "模型加载失败", "模型解析遇到严重异常：\n\n" + errorDetail);
    }
}
// 生成锚框
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
// 检测人脸并返回结果
std::vector<FaceDetectInfo> RetinaFaceDecoder::detect(const cv::Mat& image, float conf_threshold, float nms_threshold) {
    std::vector<FaceDetectInfo> faces;
    if (image.empty() || net.empty()) return faces;
    // 统一输入尺寸
    int img_w = 640;
    int img_h = 480;
    // 预处理输入图像
    cv::Mat blob = cv::dnn::blobFromImage(image, 1.0, cv::Size(img_w, img_h), cv::Scalar(104, 117, 123), false, false);
    net.setInput(blob);
    // 前向推理
    std::vector<cv::String> outNames = net.getUnconnectedOutLayersNames();
    std::vector<cv::Mat> outs;
    net.forward(outs, outNames);
    // 分离输出
    cv::Mat loc, conf, landms;
    for (const auto& out : outs) {
        int last_dim = out.size[out.dims - 1];
        if (last_dim == 4) loc = out;
        else if (last_dim == 2) conf = out;
        else if (last_dim == 10) landms = out;
    }
    // 输出不完整则直接返回
    if (loc.empty() || conf.empty() || landms.empty()) return faces;
    // 生成当前尺寸对应的锚框
    create_anchors(img_w, img_h);
    int num_anchors = priors.size();
    const float* loc_data = (const float*)loc.data;
    const float* conf_data = (const float*)conf.data;
    const float* landms_data = (const float*)landms.data;
    std::vector<cv::Rect> bboxes;
    std::vector<float> confidences;
    std::vector<std::vector<cv::Point2f>> landmarks_list;
    // 解码边框和关键点
    for (int i = 0; i < num_anchors; ++i) {
        float score = conf_data[i * 2 + 1];
        // 跳过低置信度候选框
        if (score < conf_threshold) continue;
        cv::Rect2f prior = priors[i];
        // 还原人脸框
        float cx = prior.x + loc_data[i * 4 + 0] * variances[0] * prior.width;
        float cy = prior.y + loc_data[i * 4 + 1] * variances[0] * prior.height;
        float w = prior.width * exp(loc_data[i * 4 + 2] * variances[1]);
        float h = prior.height * exp(loc_data[i * 4 + 3] * variances[1]);
        bboxes.push_back(cv::Rect((cx - w / 2.0f) * img_w, (cy - h / 2.0f) * img_h, w * img_w, h * img_h));
        confidences.push_back(score);
        // 还原五个关键点
        std::vector<cv::Point2f> pts(5);
        for (int j = 0; j < 5; ++j) {
            float lx = prior.x + landms_data[i * 10 + j * 2 + 0] * variances[0] * prior.width;
            float ly = prior.y + landms_data[i * 10 + j * 2 + 1] * variances[0] * prior.height;
            pts[j] = cv::Point2f(lx * img_w, ly * img_h);
        }
        landmarks_list.push_back(pts);
    }
    // 非极大值抑制
    std::vector<int> indices;
    cv::dnn::NMSBoxes(bboxes, confidences, conf_threshold, nms_threshold, indices);
    // 组装结果
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