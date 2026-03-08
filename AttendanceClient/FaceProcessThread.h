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

    // 停止线程：中断视频采集循环并退出线程
    void stop();
    // 更新特征库：将数据库加载的人脸特征同步至识别线程
    void updateRegisteredUsers(const std::map<QString, cv::Mat>& users);
    // 设置页码：根据 UI 切换状态调整识别策略或进入省电模式
    void setPage(int page);
    // 注册请求：提交待录入的人员姓名以启动特征抓取流程
    void requestRegister(QString name);

    void setCurrentUser(const QString& name) {
        QMutexLocker locker(&mutex);
        m_currentUser = name;
    }

    void forceReleaseCamera() {
        QMutexLocker locker(&mutex);
        if (capture.isOpened()) {
            capture.release(); // 强行关闭摄像头硬件连接
        }
    }

protected:
    // 线程主执行函数：包含摄像头循环读取、检测、对齐及比对逻辑
    void run() override;

signals:
    // 视频帧就绪信号：发送处理后的画面及识别结果供 UI 渲染
    void frameReady(QImage img, QStringList recognizedNames);
    // 特征提取成功信号：返回计算完成的 128 维特征向量二进制数据
    void registerFeatureReady(QString name, QByteArray featureBytes);
    // 注册失败信号：反馈录入过程中的异常信息
    void registerFailed(QString errorMsg);

private:
    // 线程运行状态控制标志
    bool isRunning;
    // 记录当前 UI 所处的页面索引
    int currentPage;
    // 存储当前正在进行录入的人员姓名
    QString pendingRegisterName;
    int registerRetryCount = 0;

    // 视频采集对象
    cv::VideoCapture capture;
    // 人脸检测引擎（RetinaFace 算法）指针
    RetinaFaceDecoder* faceEngine = nullptr;
    // 人脸特征提取网络（ArcFace 算法）
    cv::dnn::Net arcfaceNet;

    // 内存特征库：存储已注册用户的姓名与特征矩阵映射
    std::map<QString, cv::Mat> registeredUsers;
    // 考勤频率控制：记录各人员上一次成功识别的时间
    std::map<QString, QDateTime> lastPunchTime;
    // 互斥锁：保护多线程环境下共享数据的读写安全
    QMutex mutex;

    QString m_currentUser; // 新增：保存当前登录的用户名


    
};