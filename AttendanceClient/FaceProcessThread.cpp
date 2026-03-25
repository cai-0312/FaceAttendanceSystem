#include "FaceProcessThread.h"
#include <QPainter>
#include <QDateTime>
#include <QThread>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <cmath>
#include <numeric>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// ====================== 构造 / 析构 ======================

FaceProcessThread::FaceProcessThread(QObject* parent)
    : QThread(parent), isRunning(false), currentPage(0)
{
    m_netManager = new QNetworkAccessManager(this);
    connect(this, &FaceProcessThread::internalPunchSuccess, this, &FaceProcessThread::requestAiGreeting);

    QString detectModel = "D:/models/retinaface-resnet50.onnx";
    faceEngine = new RetinaFaceDecoder(detectModel.toStdString());

    QString recogModel = "D:/models/w600k_r50.onnx";
    try {
        arcfaceNet = cv::dnn::readNetFromONNX(recogModel.toStdString());
        if (!arcfaceNet.empty()) {
            arcfaceNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            arcfaceNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
        }
    }
    catch (...) {
        qWarning() << "[FaceProcess] ArcFace model load failed.";
    }
}

FaceProcessThread::~FaceProcessThread() {
    stop();
    mciSendStringW(L"close ai_voice", NULL, 0, NULL);
    if (faceEngine) delete faceEngine;
}

void FaceProcessThread::stop() { isRunning = false; wait(); }

void FaceProcessThread::updateRegisteredUsers(const std::map<QString, cv::Mat>& users) {
    QMutexLocker locker(&mutex);
    registeredUsers = users;
}

void FaceProcessThread::setPage(int page) {
    QMutexLocker locker(&mutex);
    currentPage = page;
}

void FaceProcessThread::requestRegister(QString name) {
    QMutexLocker locker(&mutex);
    pendingRegisterName = name;
    registerRetryCount = 0;
}

// ====================== 公共：人脸对齐 + 特征提取 ======================

cv::Mat FaceProcessThread::extractFeature(const cv::Mat& frame, const FaceDetectInfo& face) {
    static const float REF_PTS[5][2] = {
        {38.2946f, 51.6963f}, {73.5318f, 51.5014f},
        {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f}
    };

    std::vector<cv::Point2f> src_pts, dst_pts;
    for (int i = 0; i < 5; ++i) {
        src_pts.push_back(face.pts[i]);
        dst_pts.push_back(cv::Point2f(REF_PTS[i][0], REF_PTS[i][1]));
    }

    cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
    cv::Mat aligned;
    cv::warpAffine(frame, aligned, transform, cv::Size(112, 112));

    cv::Mat blob = cv::dnn::blobFromImage(aligned, 1.0 / 127.5, cv::Size(112, 112),
        cv::Scalar(127.5, 127.5, 127.5), true, false);
    arcfaceNet.setInput(blob);
    cv::Mat feat = arcfaceNet.forward().reshape(1, 1).clone();
    cv::normalize(feat, feat, 1.0, 0.0, cv::NORM_L2);
    return feat;
}

// ====================== 活体检测（基于RetinaFace 5关键点） ======================

cv::Mat FaceProcessThread::cropEyeRegion(const cv::Mat& frame, const cv::Point2f& eyeCenter, float eyeDist) {
    int halfW = (int)(eyeDist * 0.35f);
    int halfH = (int)(eyeDist * 0.18f);
    int x = std::max(0, (int)eyeCenter.x - halfW);
    int y = std::max(0, (int)eyeCenter.y - halfH);
    int w = std::min(halfW * 2, frame.cols - x);
    int h = std::min(halfH * 2, frame.rows - y);
    if (w <= 4 || h <= 4) return cv::Mat();

    cv::Mat roi = frame(cv::Rect(x, y, w, h));
    cv::Mat gray;
    if (roi.channels() == 3)
        cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
    else
        gray = roi.clone();

    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(36, 16));
    return resized;
}

void FaceProcessThread::resetLiveness() {
    m_prevLeftEye = cv::Mat();
    m_prevRightEye = cv::Mat();
    m_eyeDiffHistory.clear();
    m_currentAlive = false;
}

bool FaceProcessThread::updateLiveness(const cv::Mat& frame, const FaceDetectInfo& face) {
    cv::Point2f leftEye = face.pts[0];
    cv::Point2f rightEye = face.pts[1];

    float eyeDist = std::sqrt(
        (rightEye.x - leftEye.x) * (rightEye.x - leftEye.x) +
        (rightEye.y - leftEye.y) * (rightEye.y - leftEye.y));

    if (eyeDist < 15.0f) return m_currentAlive;

    cv::Mat curLeft = cropEyeRegion(frame, leftEye, eyeDist);
    cv::Mat curRight = cropEyeRegion(frame, rightEye, eyeDist);
    if (curLeft.empty() || curRight.empty()) return m_currentAlive;

    if (m_prevLeftEye.empty() || m_prevRightEye.empty()) {
        m_prevLeftEye = curLeft.clone();
        m_prevRightEye = curRight.clone();
        return m_currentAlive;
    }

    cv::Mat diffL, diffR;
    cv::absdiff(curLeft, m_prevLeftEye, diffL);
    cv::absdiff(curRight, m_prevRightEye, diffR);
    float meanDiff = ((float)cv::mean(diffL)[0] + (float)cv::mean(diffR)[0]) * 0.5f;

    m_prevLeftEye = curLeft.clone();
    m_prevRightEye = curRight.clone();

    m_eyeDiffHistory.push_back(meanDiff);
    if ((int)m_eyeDiffHistory.size() > m_livenessFrameCount)
        m_eyeDiffHistory.pop_front();

    if ((int)m_eyeDiffHistory.size() >= m_livenessFrameCount) {
        float sum = 0.0f, sumSq = 0.0f;
        for (float v : m_eyeDiffHistory) {
            sum += v;
            sumSq += v * v;
        }
        float n = (float)m_eyeDiffHistory.size();
        float mean = sum / n;
        float variance = sumSq / n - mean * mean;
        float stddev = std::sqrt(std::max(0.0f, variance));

        m_currentAlive = (stddev > m_livenessThreshold);

        if (!m_currentAlive) {
            qDebug() << "[Liveness] BLOCKED - stddev:" << stddev << "< threshold:" << m_livenessThreshold;
        }
    }

    return m_currentAlive;
}

// ====================== DeepSeek 问候语生成 ======================

void FaceProcessThread::requestAiGreeting(QString name, QDateTime time) {
    bool ttsOn;
    {
        QMutexLocker locker(&paramMutex);
        ttsOn = m_ttsEnabled;
    }
    if (!ttsOn) return;

    QString timeStr = time.toString("HH:mm");

    QString deepseekKey;
    { QMutexLocker locker(&paramMutex); deepseekKey = m_deepseekApiKey; }

    QJsonObject body;
    body["model"] = "deepseek-chat";
    body["messages"] = QJsonArray{
        QJsonObject{{"role", "system"}, {"content",
            QString("你是企业智能考勤系统的语音助手。"
                    "根据员工姓名和当前时间，生成一句自然亲切的问候语。"
                    "要求：15字以内，直接用于语音播报，禁止输出标点符号和表情包。")}},
        QJsonObject{{"role", "user"}, {"content",
            QString("员工%1在%2打卡。").arg(name, timeStr)}}
    };

    QNetworkRequest req(QUrl("https://api.deepseek.com/v1/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + deepseekKey).toUtf8());

    QNetworkReply* reply = m_netManager->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            QString aiText = root["choices"].toArray()[0].toObject()
                ["message"].toObject()["content"].toString().trimmed();
            qDebug() << "[DeepSeek]" << aiText;
            this->requestQwenTTS(aiText);
        }
        else {
            qWarning() << "[DeepSeek] Failed:" << reply->errorString();
        }
        reply->deleteLater();
        });
}

void FaceProcessThread::requestQwenTTS(const QString& text) {
    QString apiKey, voice, model;
    bool ttsOn;
    {
        QMutexLocker locker(&paramMutex);
        ttsOn = m_ttsEnabled;
        apiKey = m_dashscopeApiKey;
        voice = m_ttsVoice;
        model = m_ttsModel;
    }

    if (!ttsOn) return;
    if (apiKey.isEmpty()) {
        qWarning() << "[Qwen-TTS] API Key not set.";
        return;
    }

    QJsonObject input;
    input["text"] = text;
    input["voice"] = voice;

    QJsonObject body;
    body["model"] = model;
    body["input"] = input;

    QUrl url("https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    QNetworkReply* reply = m_netManager->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isNull()) {
                QJsonObject root = doc.object();
                QString audioUrl = root["output"].toObject()["audio"].toObject()["url"].toString();
                if (!audioUrl.isEmpty()) {
                    downloadAndPlayAudio(audioUrl);
                }
                else {
                    qWarning() << "[Qwen-TTS] No audio URL in response.";
                }
            }
            else {
                qWarning() << "[Qwen-TTS] Invalid JSON response.";
            }
        }
        else {
            qWarning() << "[Qwen-TTS] Network error:" << reply->errorString();
        }
        reply->deleteLater();
        });
}
// ====================== 音频下载 + 播放 ======================

void FaceProcessThread::downloadAndPlayAudio(const QString& audioUrl) {
    QUrl url(audioUrl);
    QNetworkRequest req;
    req.setUrl(url);
    QNetworkReply* reply = m_netManager->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray audioData = reply->readAll();

            mciSendStringW(L"close ai_voice", NULL, 0, NULL);

            QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
            QString path = QDir::toNativeSeparators(QDir::tempPath() + "/qwen3_tts_" + ts + ".wav");
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(audioData);
                f.close();
                this->playLocalAudio(path);
            }
            else {
                qWarning() << "[Qwen3-TTS] Write failed:" << path;
            }
        }
        else {
            qWarning() << "[Qwen3-TTS] Download failed:" << reply->errorString();
        }
        reply->deleteLater();
        });
}

void FaceProcessThread::playLocalAudio(const QString& filePath) {
    std::wstring wp = filePath.toStdWString();
    mciSendStringW(L"close ai_voice", NULL, 0, NULL);

    std::wstring type = filePath.endsWith(".mp3", Qt::CaseInsensitive) ? L"mpegvideo" : L"waveaudio";
    std::wstring cmd = L"open \"" + wp + L"\" type " + type + L" alias ai_voice";
    MCIERROR err = mciSendStringW(cmd.c_str(), NULL, 0, NULL);

    if (err == 0) {
        mciSendStringW(L"play ai_voice", NULL, 0, NULL);
        emit ttsStatusChanged("语音播报中...");
    }
    else if (type == L"waveaudio") {
        mciSendStringW(L"close ai_voice", NULL, 0, NULL);
        cmd = L"open \"" + wp + L"\" type mpegvideo alias ai_voice";
        if (mciSendStringW(cmd.c_str(), NULL, 0, NULL) == 0) {
            mciSendStringW(L"play ai_voice", NULL, 0, NULL);
            emit ttsStatusChanged("语音播报中...");
        }
    }
}

// ====================== 人脸识别主循环 ======================

void FaceProcessThread::run() {
    int camIdx, camW, camH;
    {
        QMutexLocker locker(&paramMutex);
        camIdx = m_camIndex;
        camW = m_camWidth;
        camH = m_camHeight;
    }

    if (!capture.open(camIdx, cv::CAP_MSMF)) {
        emit registerFailed("摄像头打开失败！");
        return;
    }
    capture.set(cv::CAP_PROP_FRAME_WIDTH, camW);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, camH);
    capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    isRunning = true;
    qint64 lastDetectTime = 0;

    struct MatchResult { cv::Rect rect; QString name; bool alive; };
    std::vector<MatchResult> cachedResults;
    QByteArray currentFeatureBytes;
    QStringList currentNames;

    while (isRunning) {
        cv::Mat frame;
        capture >> frame;
        if (frame.empty()) { QThread::msleep(10); continue; }

        int page;
        QString regName;
        std::map<QString, cv::Mat> usersCopy;
        {
            QMutexLocker locker(&mutex);
            page = currentPage;
            usersCopy = registeredUsers;
            if (!pendingRegisterName.isEmpty()) {
                regName = pendingRegisterName;
                registerRetryCount++;
            }
        }

        float curConf, curNms, curRecog;
        int cooldown;
        bool livenessOn;
        {
            QMutexLocker locker(&paramMutex);
            curConf = m_confThreshold;
            curNms = m_nmsThreshold;
            curRecog = m_recogThreshold;
            cooldown = m_punchCooldownSec;
            livenessOn = m_livenessEnabled;
        }

        // ---- 注册逻辑 ----
        if (!regName.isEmpty() && faceEngine && !arcfaceNet.empty()) {
            std::vector<FaceDetectInfo> faces = faceEngine->detect(frame, curConf, curNms);
            if (faces.size() == 1) {
                cv::Mat feat = extractFeature(frame, faces[0]);
                emit registerFeatureReady(regName, QByteArray((const char*)feat.data, feat.total() * feat.elemSize()));
                QMutexLocker locker(&mutex);
                pendingRegisterName.clear();
            }
            if (registerRetryCount > 60) {
                emit registerFailed("录入超时");
                QMutexLocker locker(&mutex);
                pendingRegisterName.clear();
            }
            continue;
        }

        if (page != 0 && page != 1) { QThread::msleep(100); continue; }

        // ---- 识别逻辑 ----
        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - lastDetectTime >= 300) {
            std::vector<FaceDetectInfo> faces;
            if (faceEngine) faces = faceEngine->detect(frame, curConf, curNms);

            cachedResults.clear();
            currentFeatureBytes.clear(); 
            currentNames.clear();        

            if (page == 0) {
                for (const auto& face : faces) {
                    if (arcfaceNet.empty()) continue;

                    cv::Mat feat = extractFeature(frame, face);
                    QString bestName = "未知访客";
                    double maxSim = 0.0;

                    for (auto it = usersCopy.begin(); it != usersCopy.end(); ++it) {
                        double sim = feat.dot(it->second);
                        if (sim > maxSim && sim > curRecog) {
                            maxSim = sim;
                            bestName = it->first;
                        }
                    }
                    if (bestName != "未知访客" && bestName != m_currentUser) {
                        bestName = "非本人";
                    }

                    // 👇 新增：提取特征并转存为 Base64 安全上报准备的字节流 👇
                    if (!feat.empty() && (bestName == m_currentUser || currentFeatureBytes.isEmpty())) {
                        currentFeatureBytes = QByteArray((const char*)feat.data, feat.total() * feat.elemSize());
                    }
                    currentNames.append(bestName);

                    // 陌生人告警
                    if (bestName == "未知访客") {
                        cv::Rect safeRect = face.rect & cv::Rect(0, 0, frame.cols, frame.rows);
                        if (safeRect.area() > 0) {
                            cv::Mat faceRoi = frame(safeRect).clone();
                            cv::Mat faceRgb;
                            cv::cvtColor(faceRoi, faceRgb, cv::COLOR_BGR2RGB);
                            QImage snap((const uchar*)faceRgb.data, faceRgb.cols, faceRgb.rows,
                                faceRgb.step, QImage::Format_RGB888);
                            emit unknownFaceDetected(snap.copy());
                        }
                        resetLiveness();
                    }

                    // 活体检测 + 打卡逻辑
                    if (bestName != "未知访客" && bestName == m_currentUser) {

                        bool alive = true;
                        if (livenessOn) {
                            alive = updateLiveness(frame, face);
                            emit livenessResult(alive);
                        }

                        if (alive) {
                            QDateTime now = QDateTime::currentDateTime();
                            if (lastPunchTime.find(bestName) == lastPunchTime.end() ||
                                lastPunchTime[bestName].secsTo(now) > cooldown) {
                                lastPunchTime[bestName] = now;
                                emit punchResult(bestName, maxSim, now);
                                emit internalPunchSuccess(bestName, now);
                                resetLiveness();
                            }
                        }
                    }

                    cachedResults.push_back({ face.rect, bestName, livenessOn ? m_currentAlive : true });
                }
            }
            lastDetectTime = nowMs;
        }

        // ---- 画框渲染 ----
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        QImage img((const uchar*)rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
        QImage finalImg = img.copy();
        QPainter painter(&finalImg);
        painter.setFont(QFont("Microsoft YaHei", 18, QFont::Bold));
        for (const auto& res : cachedResults) {
            if (res.name == m_currentUser && res.alive)
                painter.setPen(Qt::green);
            else if (res.name == m_currentUser && !res.alive)
                painter.setPen(QColor(255, 165, 0));  // 橙色：识别到但活体检测未通过
            else
                painter.setPen(Qt::red);

            painter.drawRect(res.rect.x, res.rect.y, res.rect.width, res.rect.height);

            QString label = res.name;
            if (res.name == m_currentUser && !res.alive)
                label += " [检测中]";

            painter.drawText(res.rect.x, res.rect.y - 10, label);
        }
        painter.end();
        emit frameReady(finalImg, currentNames.isEmpty() ? QStringList{ m_currentUser } : currentNames, currentFeatureBytes);

        QThread::msleep(33);
    }
    capture.release();
}
