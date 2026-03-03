#pragma once
#include "AttendanceClient.h"
#include <opencv2/opencv.hpp>
#include <vector>

// 定义人脸检测结果结构体
struct FaceDetectInfo {
    cv::Rect rect;          // 人脸方框
    float score;            // 置信度 (有多大把握是人脸)
    cv::Point2f pts[5];     // 5个面部关键点: 左眼、右眼、鼻尖、左嘴角、右嘴角
};

class RetinaFaceDecoder {
public:
    // 构造函数，传入 ONNX 模型路径
    RetinaFaceDecoder(const std::string& onnx_path);

    // 核心检测函数
    std::vector<FaceDetectInfo> detect(const cv::Mat& image, float conf_threshold = 0.6f, float nms_threshold = 0.4f);

private:
    cv::dnn::Net net;
    std::vector<cv::Rect2f> priors; // 先验框 (Anchors)
    void create_anchors(int w, int h); // 生成先验框的数学公式
};

