#ifndef RETINAFACEDECODER_H
#define RETINAFACEDECODER_H
#include "AttendanceClient.h"
#include <opencv2/opencv.hpp>
#include <vector>
struct FaceDetectInfo {
    cv::Rect rect; // 人脸区域
    float score; // 置信度
    cv::Point2f pts[5]; // 五个关键点
};
class RetinaFaceDecoder {
public:
    RetinaFaceDecoder(const std::string& onnx_path); // 加载并初始化模型
    std::vector<FaceDetectInfo> detect(const cv::Mat& image, float conf_threshold = 0.85f, float nms_threshold = 0.15f); // 检测人脸并返回结果
private:
    cv::dnn::Net net; // DNN 网络对象
    std::vector<cv::Rect2f> priors; // 先验框列表
    void create_anchors(int w, int h); // 生成锚框
};
#endif 