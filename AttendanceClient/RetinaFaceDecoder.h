#ifndef RETINAFACEDECODER_H
#define RETINAFACEDECODER_H
#include "AttendanceClient.h"
#include <opencv2/opencv.hpp>
#include <vector>
struct FaceDetectInfo {                            // 人脸检测信息结构体：包含位置、可信度评分及关键点坐标
    cv::Rect rect;                                // 目标人脸区域：算法解码出的人脸矩形物理边界
    float score;                                // 置信度得分：神经网络输出的该区域为人脸的概率值
    cv::Point2f pts[5];                        // 面部五个特征点（双眼、鼻尖及两侧嘴角）的坐标
};
class RetinaFaceDecoder {
public:
    RetinaFaceDecoder(const std::string& onnx_path);       // 构造函数：初始化解码器引擎并加载预训练的ONNX神经网络模型
    std::vector<FaceDetectInfo> detect(const cv::Mat& image, float conf_threshold = 0.6f, float nms_threshold = 0.4f); // 图像检测接口：通过置信度与非极大值抑制过滤并返回人脸信息列表
private:
    cv::dnn::Net net;                             // 网络引擎：OpenCV深度神经网络计算对象句柄
    std::vector<cv::Rect2f> priors;               // 存储根据特定图像尺寸生成的预设特征层先验框列表
    void create_anchors(int w, int h);            // 锚框生成算法：根据当前输入图像的宽度和高度计算并生成所有特征层先验框
};
#endif // RETINAFACEDECODER_H