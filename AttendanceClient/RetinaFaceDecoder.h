#pragma once
#include "AttendanceClient.h"
#include <opencv2/opencv.hpp>
#include <vector>

// 人脸检测信息结构体：包含位置、可信度评分及关键点坐标
struct FaceDetectInfo {
    cv::Rect rect;          // 目标人脸的矩形区域
    float score;            // 检测结果的置信度得分
    cv::Point2f pts[5];     // 五个核心面部特征点：双眼、鼻尖及两侧嘴角
};

class RetinaFaceDecoder {
public:
    RetinaFaceDecoder(const std::string& onnx_path);

    // 图像检测接口：通过置信度阈值和非极大值抑制（NMS）阈值过滤并返回人脸信息列表
    std::vector<FaceDetectInfo> detect(const cv::Mat& image, float conf_threshold = 0.6f, float nms_threshold = 0.4f);

private:
    // OpenCV 深度神经网络对象
    cv::dnn::Net net;
    // 存储根据图像尺寸生成的预设先验框列表
    std::vector<cv::Rect2f> priors;
    // 私有算法函数：根据当前输入图像的宽度和高度计算并生成先验框
    void create_anchors(int w, int h);
};