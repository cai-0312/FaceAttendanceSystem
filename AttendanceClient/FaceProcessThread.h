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
    explicit FaceProcessThread(QObject* parent = nullptr);
    ~FaceProcessThread();

    void stop();
    void updateRegisteredUsers(const std::map<QString, cv::Mat>& users);
    void setPage(int page);
    void requestRegister(QString name);
    void setCurrentUser(const QString& name) { QMutexLocker locker(&mutex); m_currentUser = name; }
    void forceReleaseCamera() { QMutexLocker locker(&mutex); if (capture.isOpened()) capture.release(); }

    void setDetectParams(float confThresh, float nmsThresh, float recogThresh) {
        QMutexLocker locker(&paramMutex);
        m_confThreshold = confThresh;
        m_nmsThreshold = nmsThresh;
        m_recogThreshold = recogThresh;
    }

    void setCameraParams(int camIndex, int width, int height) {
        QMutexLocker locker(&paramMutex);
        m_camIndex = camIndex;
        m_camWidth = width;
        m_camHeight = height;
    }

    void setDeepSeekApiKey(const QString& apiKey) {
        QMutexLocker locker(&paramMutex);
        m_deepseekApiKey = apiKey;
    }

    void setDashScopeApiKey(const QString& apiKey) {
        QMutexLocker locker(&paramMutex);
        m_dashscopeApiKey = apiKey;
    }

    void setTtsVoice(const QString& voiceName) {
        QMutexLocker locker(&paramMutex);
        m_ttsVoice = voiceName;
    }

    void setPunchCooldown(int seconds) {
        QMutexLocker locker(&paramMutex);
        m_punchCooldownSec = seconds;
    }

    void setTtsEnabled(bool enabled) {
        QMutexLocker locker(&paramMutex);
        m_ttsEnabled = enabled;
    }

    void setLivenessEnabled(bool enabled) {
        QMutexLocker locker(&paramMutex);
        m_livenessEnabled = enabled;
    }

protected:
    void run() override;

signals:
    void frameReady(QImage img, QStringList names, QByteArray currentFeature);
    void registerFeatureReady(QString name, QByteArray featureBytes);
    void registerFailed(QString errorMsg);
    void internalPunchSuccess(QString name, QDateTime time);
    void ttsStatusChanged(QString status);
    void punchResult(QString name, double similarity, QDateTime time);
    void unknownFaceDetected(QImage faceSnapshot);
    void livenessResult(bool alive);

private slots:
    void requestAiGreeting(QString name, QDateTime time);
    void requestQwenTTS(const QString& text);

private:
    bool isRunning;
    int currentPage;
    QString pendingRegisterName;
    int registerRetryCount = 0;
    cv::VideoCapture capture;
    RetinaFaceDecoder* faceEngine = nullptr;
    cv::dnn::Net arcfaceNet;
    std::map<QString, cv::Mat> registeredUsers;
    std::map<QString, QDateTime> lastPunchTime;
    QMutex mutex;
    QString m_currentUser;

    QMutex paramMutex;
    float m_confThreshold = 0.50f;
    float m_nmsThreshold = 0.40f;
    float m_recogThreshold = 0.65f;
    int m_camIndex = 0;
    int m_camWidth = 640;
    int m_camHeight = 480;

    int m_punchCooldownSec = 60;
    bool m_ttsEnabled = true;
    bool m_livenessEnabled = true;

    QNetworkAccessManager* m_netManager = nullptr;

    QString m_deepseekApiKey = "sk-54ccee7e91ab405a94c622d9419a91e9";

    QString m_dashscopeApiKey = "sk-5f248bb37a764211a8d3c21c75c262ee";
    QString m_ttsVoice = "Cherry";
    QString m_ttsModel = "qwen3-tts-flash";

    void downloadAndPlayAudio(const QString& audioUrl);
    void playLocalAudio(const QString& filePath);
    cv::Mat extractFeature(const cv::Mat& frame, const FaceDetectInfo& face);

    // ====================== 活体检测（基于RetinaFace 5关键点） ======================
    // 利用 pts[0]=左眼中心, pts[1]=右眼中心 裁切眼部区域
    // 通过多帧眼部像素变化的标准差判断活体（真人眨眼有波动，照片无波动）
    cv::Mat m_prevLeftEye;
    cv::Mat m_prevRightEye;
    std::deque<float> m_eyeDiffHistory;
    int m_livenessFrameCount = 7;        // 7帧 × 300ms ≈ 2秒完成检测
    float m_livenessThreshold = 1.2f;    // 帧数少时阈值相应降低
    bool m_currentAlive = false;

    cv::Mat cropEyeRegion(const cv::Mat& frame, const cv::Point2f& eyeCenter, float eyeDist);
    bool updateLiveness(const cv::Mat& frame, const FaceDetectInfo& face);
    void resetLiveness();
};