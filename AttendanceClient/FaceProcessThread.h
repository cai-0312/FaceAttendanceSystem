#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <QThread>
#include <QMutex>
#include <QImage>
#include <QDateTime>
#include <map>
#include <deque>
#include <opencv2/opencv.hpp>
#include "RetinaFaceDecoder.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDir>
#include <QFile>
class FaceProcessThread : public QThread {
    Q_OBJECT
public:
    explicit FaceProcessThread(QObject* parent = nullptr); // 构造函数：初始化人脸处理线程和资源
    ~FaceProcessThread(); // 析构函数：清理资源并确保线程安全停止
    void stop(); // 停止线程主循环并释放相机等资源
    void updateRegisteredUsers(const std::map<QString, cv::Mat>& users); // 更新内存中已注册用户的特征库
    void setPage(int page); // 设置当前用户列表分页索引
    void requestRegister(QString name); // 发起注册请求以登记新用户
    void setCurrentUser(const QString& name) { QMutexLocker locker(&mutex); m_currentUser = name; } // 线程安全地设置当前用户
    void forceReleaseCamera() { QMutexLocker locker(&mutex); if (capture.isOpened()) capture.release(); } // 强制释放摄像头资源
    void setDetectParams(float confThresh, float nmsThresh, float recogThresh) { // 设置检测与识别阈值
        QMutexLocker locker(&paramMutex);
        m_confThreshold = confThresh;
        m_nmsThreshold = nmsThresh;
        m_recogThreshold = recogThresh;
    }
    void setCameraParams(int camIndex, int width, int height) { // 设置摄像头索引与分辨率参数
        QMutexLocker locker(&paramMutex);
        m_camIndex = camIndex;
        m_camWidth = width;
        m_camHeight = height;
    }
    void setPunchCooldown(int seconds) { // 设置打卡冷却时间以防止重复打卡
        QMutexLocker locker(&paramMutex);
        m_punchCooldownSec = seconds;
    }
    void setTtsEnabled(bool enabled) { // 启用或禁用 TTS 语音播报
        QMutexLocker locker(&paramMutex);
        m_ttsEnabled = enabled;
    }
    void setLivenessEnabled(bool enabled) { // 启用或禁用活体检测
        QMutexLocker locker(&paramMutex);
        m_livenessEnabled = enabled;
    }
protected:
    void run() override; // 线程主循环：捕获帧、检测人脸、比对特征并发出信号
signals:
    void frameReady(QImage img, QStringList names, QByteArray currentFeature); // 当新帧处理完成时发出用于 UI 刷新的信号
    void registerFeatureReady(QString name, QByteArray featureBytes); // 当人脸特征提取完成用于注册时发出
    void registerFailed(QString errorMsg); // 注册失败时发出错误信息
    void internalPunchSuccess(QString name, QDateTime time); // 内部可信打卡成功事件
    void ttsStatusChanged(QString status); // TTS 状态变更通知
    void punchResult(QString name, double similarity, QDateTime time); // 打卡比对结果通知
    void unknownFaceDetected(QImage faceSnapshot); // 未识别到匹配人脸时发出快照
    void livenessResult(bool alive); // 活体检测结果通知
    void cameraConnected(int camId); // 摄像头连接成功通知
private slots:
    void requestAiGreeting(QString name); // 向 AI 请求欢迎语并处理返回
    void onDeepseekReply(QNetworkReply* reply); // 处理 Deepseek 或远端服务的异步回复
    void requestTts(QString text); // 请求生成并播放 TTS 音频
private:
    bool isRunning; // 线程运行标志位
    int currentPage; // 当前处理的用户页码
    QString pendingRegisterName; // 待注册用户名
    int registerRetryCount = 0; // 人脸注册重试计数
    cv::VideoCapture capture; // OpenCV 视频捕获对象
    RetinaFaceDecoder* faceEngine = nullptr; // 人脸检测与关键点解码器指针
    cv::dnn::Net arcfaceNet; // 人脸特征提取的神经网络模型
    std::map<QString, cv::Mat> registeredUsers; // 已注册用户的特征矩阵映射
    std::map<QString, QDateTime> lastPunchTime; // 记录每个用户上次打卡时间用于冷却策略
    QMutex mutex; // 保护共享资源的互斥锁
    QString m_currentUser; // 当前选中或正在注册的用户
    QMutex paramMutex; // 保护参数修改的互斥锁
    float m_confThreshold = 0.50f; // 检测置信度阈值
    float m_nmsThreshold = 0.15f; // 非极大值抑制阈值
    float m_recogThreshold = 0.85f; // 识别相似度阈值
    int m_camIndex = 0; // 摄像头索引
    int m_camWidth = 640; // 摄像头采集宽度
    int m_camHeight = 480; // 摄像头采集高度
    int m_punchCooldownSec = 60; // 打卡冷却秒数，防止短时间重复打卡
    bool m_ttsEnabled = true; // 是否启用语音播报
    bool m_livenessEnabled = true; // 是否启用活体检测
    QNetworkAccessManager* m_netManager = nullptr; // 网络请求管理器用于异步调用外部服务
    cv::Mat extractFeature(const cv::Mat& frame, const FaceDetectInfo& face); // 从检测到的人脸区域提取特征向量
    cv::Mat m_prevLeftEye; // 上一帧左眼区域图像用于活体检测对比
    cv::Mat m_prevRightEye; // 上一帧右眼区域图像用于活体检测对比
    std::deque<float> m_eyeDiffHistory; // 连续帧眼部差异历史用于活体决策
    int m_livenessFrameCount = 7;        // 连续帧数用于活体判断的窗口大小（约 2 秒）
    float m_livenessThreshold = 1.2f;    // 活体判定的累积差异阈值
    bool m_currentAlive = false; // 当前活体判定结果
    cv::Mat cropEyeRegion(const cv::Mat& frame, const cv::Point2f& eyeCenter, float eyeDist); // 裁剪并返回眼部区域图像
    bool updateLiveness(const cv::Mat& frame, const FaceDetectInfo& face); // 更新活体检测状态并返回结果
    void resetLiveness(); // 重置活体检测的内部状态
};