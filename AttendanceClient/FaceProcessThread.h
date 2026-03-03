#pragma once
#include <QThread>
#include <QMutex>
#include <QImage>
#include <QDateTime>
#include <map>
#include <opencv2/opencv.hpp>
#include "RetinaFaceDecoder.h"

// 专门跑深度学习 AI 的后台子线程类
class FaceProcessThread : public QThread {
    Q_OBJECT
public:
    explicit FaceProcessThread(QObject* parent = nullptr);
    ~FaceProcessThread();

    void stop();
    void updateRegisteredUsers(const std::map<QString, cv::Mat>& users);
    void setPage(int page);
    void requestRegister(QString name);

protected:
    void run() override; // 线程的独立“心脏”

signals:
    void frameReady(QImage img, QStringList recognizedNames);
    void registerFeatureReady(QString name, QByteArray featureBytes);
    void registerFailed(QString errorMsg);

private:
    bool isRunning;
    int currentPage;
    QString pendingRegisterName;

    cv::VideoCapture capture;
    RetinaFaceDecoder* faceEngine = nullptr;
    cv::dnn::Net arcfaceNet;

    std::map<QString, cv::Mat> registeredUsers;
    std::map<QString, QDateTime> lastPunchTime;
    QMutex mutex;
};