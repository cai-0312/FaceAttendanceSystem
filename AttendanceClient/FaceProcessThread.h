#pragma once

#include <QThread>
#include <QMutex>
#include <QImage>
#include <QDateTime>
#include <map>
#include <opencv2/opencv.hpp>
#include "RetinaFaceDecoder.h"

class FaceProcessThread : public QThread {
    Q_OBJECT
public:
    explicit FaceProcessThread(QObject* parent = nullptr);
    ~FaceProcessThread();

    // 停止线程：中断视频采集循环并安全退出线程
    void stop();

    // 更新特征库：将数据库中已加载的人脸特征同步至本识别线程
    void updateRegisteredUsers(const std::map<QString, cv::Mat>& users);

    // 设置页码：根据 UI 状态控制识别策略（如考勤页与注册页的逻辑区分）
    void setPage(int page);

    // 注册请求：提交待录入的人员姓名，触发对应特征的抓取与上报流程
    void requestRegister(QString name);

    // 设置当前登录用户：用于判断镜头前的人脸是否为“本人”
    void setCurrentUser(const QString& name) {
        QMutexLocker locker(&mutex);
        m_currentUser = name;
    }

    // 强制释放摄像头：应对外部组件抢占硬件时的紧急释放接口
    void forceReleaseCamera() {
        QMutexLocker locker(&mutex);
        if (capture.isOpened()) {
            capture.release();
        }
    }

protected:
    // 线程主执行函数：执行摄像头读取、活体/人脸检测、特征对齐与比对的核心死循环
    void run() override;

signals:
    // 视频帧就绪信号：将处理后（画好方框与文字）的画面发送给 UI 进行渲染
    void frameReady(QImage img, QStringList recognizedNames);

    // 特征提取成功信号：返回 ArcFace 提取的 128 维/512 维高维特征向量
    void registerFeatureReady(QString name, QByteArray featureBytes);

    // 注册失败信号：当人脸录入超时或无清晰正脸时反馈异常
    void registerFailed(QString errorMsg);

private:
    bool isRunning;                     // 线程运行状态控制标志
    int currentPage;                    // 记录当前 UI 所处的页面索引（0: 打卡, 1: 注册等）
    QString pendingRegisterName;        // 存储当前正在进行注册录入的人员姓名
    int registerRetryCount = 0;         // 注册抓取重试计数器（用于超时判定）

    cv::VideoCapture capture;           // OpenCV 视频流采集对象
    RetinaFaceDecoder* faceEngine = nullptr; // 人脸检测引擎（负责寻找人脸框和5个关键点）
    cv::dnn::Net arcfaceNet;            // 人脸特征提取网络（负责将人脸转为高维特征进行比对）

    std::map<QString, cv::Mat> registeredUsers; // 内存特征库：已注册用户的姓名与特征矩阵映射
    std::map<QString, QDateTime> lastPunchTime; // 打卡频率防抖：记录各人员上一次成功识别的时间
    QMutex mutex;                       // 线程锁：保护多线程场景下共享数据的读写安全

    QString m_currentUser;              // 保存当前系统登录的用户名，用于“非本人”逻辑判定
};