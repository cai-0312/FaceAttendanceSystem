#include "FaceProcessThread.h"
#include <QPainter>
#include <QDateTime>
#include <QThread>
#include <QDebug>
#include <QJsonDocument>
#include <QSharedMemory>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <cmath>
#include <numeric>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// 构造函数，初始化网络对象和人脸模型
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
            // 启用CUDA加速
            arcfaceNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            arcfaceNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
        }
    }
    catch (...) {
        qWarning() << "[FaceProcess] ArcFace model load failed.";
    }
}
// 析构函数，释放资源
FaceProcessThread::~FaceProcessThread() {
    stop();
    if (faceEngine) delete faceEngine;
}
// 停止线程
void FaceProcessThread::stop() { isRunning = false; wait(); }
// 更新注册用户列表
void FaceProcessThread::updateRegisteredUsers(const std::map<QString, cv::Mat>& users) {
    QMutexLocker locker(&mutex);
    registeredUsers = users;
}
// 设置当前页面
void FaceProcessThread::setPage(int page) {
    QMutexLocker locker(&mutex);
    currentPage = page;
}
// 发起人脸注册
void FaceProcessThread::requestRegister(QString name) {
    QMutexLocker locker(&mutex);
    pendingRegisterName = name;
    registerRetryCount = 0;
}
// 从人脸图像提取特征向量
cv::Mat FaceProcessThread::extractFeature(const cv::Mat& frame, const FaceDetectInfo& face) {
    // 五个对齐参考点
    static const float REF_PTS[5][2] = {
        {38.2946f, 51.6963f}, {73.5318f, 51.5014f},
        {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f}
    };
    std::vector<cv::Point2f> src_pts, dst_pts;
    for (int i = 0; i < 5; ++i) {
        src_pts.push_back(face.pts[i]);
        dst_pts.push_back(cv::Point2f(REF_PTS[i][0], REF_PTS[i][1]));
    }
    // 对齐人脸
    cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
    cv::Mat aligned;
    cv::warpAffine(frame, aligned, transform, cv::Size(112, 112));
    // 提取特征
    cv::Mat blob = cv::dnn::blobFromImage(aligned, 1.0 / 127.5, cv::Size(112, 112),
        cv::Scalar(127.5, 127.5, 127.5), true, false);
    arcfaceNet.setInput(blob);
    cv::Mat feat = arcfaceNet.forward().reshape(1, 1).clone();
    // 归一化特征
    cv::normalize(feat, feat, 1.0, 0.0, cv::NORM_L2);
    return feat;
}
// 裁剪眼部区域用于活体检测
cv::Mat FaceProcessThread::cropEyeRegion(const cv::Mat& frame, const cv::Point2f& eyeCenter, float eyeDist) {
    // 根据眼距计算区域大小
    int halfW = (int)(eyeDist * 0.35f);
    int halfH = (int)(eyeDist * 0.18f);
    int x = std::max(0, (int)eyeCenter.x - halfW);
    int y = std::max(0, (int)eyeCenter.y - halfH);
    int w = std::min(halfW * 2, frame.cols - x);
    int h = std::min(halfH * 2, frame.rows - y);
    if (w <= 4 || h <= 4) return cv::Mat();
    cv::Mat roi = frame(cv::Rect(x, y, w, h));
    // 转为灰度图
    cv::Mat gray;
    if (roi.channels() == 3)
        cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
    else
        gray = roi.clone();
    // 统一尺寸
    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(36, 16));
    return resized;
}
// 重置活体检测状态
void FaceProcessThread::resetLiveness() {
    m_prevLeftEye = cv::Mat();
    m_prevRightEye = cv::Mat();
    m_eyeDiffHistory.clear();
    m_currentAlive = false;
}
// 基于眼部变化判断活体
bool FaceProcessThread::updateLiveness(const cv::Mat& frame, const FaceDetectInfo& face) {
    cv::Point2f leftEye = face.pts[0];
    cv::Point2f rightEye = face.pts[1];
    // 计算眼距
    float eyeDist = std::sqrt(
        (rightEye.x - leftEye.x) * (rightEye.x - leftEye.x) +
        (rightEye.y - leftEye.y) * (rightEye.y - leftEye.y));
    if (eyeDist < 15.0f) return m_currentAlive;
    // 裁剪眼部区域
    cv::Mat curLeft = cropEyeRegion(frame, leftEye, eyeDist);
    cv::Mat curRight = cropEyeRegion(frame, rightEye, eyeDist);
    if (curLeft.empty() || curRight.empty()) return m_currentAlive;
    // 首帧保存参考图
    if (m_prevLeftEye.empty() || m_prevRightEye.empty()) {
        m_prevLeftEye = curLeft.clone();
        m_prevRightEye = curRight.clone();
        return m_currentAlive;
    }
    // 计算区域差异
    cv::Mat diffL, diffR;
    cv::absdiff(curLeft, m_prevLeftEye, diffL);
    cv::absdiff(curRight, m_prevRightEye, diffR);
    float meanDiff = ((float)cv::mean(diffL)[0] + (float)cv::mean(diffR)[0]) * 0.5f;
    m_prevLeftEye = curLeft.clone();
    m_prevRightEye = curRight.clone();
    // 记录差异历史
    m_eyeDiffHistory.push_back(meanDiff);
    if ((int)m_eyeDiffHistory.size() > m_livenessFrameCount)
        m_eyeDiffHistory.pop_front();
    // 根据差异波动判断是否活体
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
        // 波动足够则判定为活体
        m_currentAlive = (stddev > m_livenessThreshold);
        if (!m_currentAlive) {
            qDebug() << "[Liveness] BLOCKED - stddev:" << stddev << "< threshold:" << m_livenessThreshold;
        }
    }
    return m_currentAlive;
}
// 通过 AI 生成欢迎语
void FaceProcessThread::requestAiGreeting(QString name)
{
    if (!m_netManager) return;
    QUrl url("http://127.0.0.1:11434/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject json;
    json["model"] = "deepseek-r1:8b";
    json["max_tokens"] = 400;
    json["temperature"] = 0.8;
    // 构建提示词
    QJsonArray messages;
    QJsonObject sysMsg, userMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = "你是一个考勤机语音助手。请根据员工姓名和时间，直接输出一句热情的欢迎语（15字以内）。请尽量缩短思考过程，直接给出结果。";
    userMsg["role"] = "user";
    QDateTime now = QDateTime::currentDateTime();
    QString timeStr = now.toString("hh:mm");
    QString period = (now.time().hour() < 12) ? "上午" : "下午";
    userMsg["content"] = QString("员工姓名：%1，打卡时间：%2（%3）").arg(name, timeStr, period);
    messages.append(sysMsg);
    messages.append(userMsg);
    json["messages"] = messages;
    QJsonDocument doc(json);
    QNetworkReply* reply = m_netManager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        onDeepseekReply(reply);
        });
}
// 处理 AI 回复并提取欢迎语
void FaceProcessThread::onDeepseekReply(QNetworkReply* reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        QJsonObject replyObj = jsonDoc.object();
        if (replyObj.contains("choices") && replyObj["choices"].isArray()) {
            QString greeting = replyObj["choices"].toArray()[0].toObject()["message"].toObject()["content"].toString();
            // 去除思考标签
            QRegularExpression re("<think>.*?</think>", QRegularExpression::DotMatchesEverythingOption);
            greeting.remove(re);
            int thinkIndex = greeting.indexOf("<think>");
            if (thinkIndex != -1) {
                greeting = greeting.left(thinkIndex);
            }
            greeting = greeting.trimmed();
            // 使用默认欢迎语兜底
            if (greeting.isEmpty()) {
                greeting = QString("打卡成功，欢迎您！");
            }
            requestTts(greeting);
        }
    }
    else {
        requestTts(QString("打卡成功，欢迎您！"));
    }
    reply->deleteLater();
}
// 使用系统语音播放欢迎语
void FaceProcessThread::requestTts(QString text)
{
    if (text.isEmpty() || !m_ttsEnabled) return;
    // 清理特殊字符
    QString cleanText = text;
    cleanText.remove(QRegularExpression("<[^>]*>"));
    cleanText.remove(QRegularExpression("[\\*`#]"));
    cleanText.replace("'", "''");
    // 通过 PowerShell 调用语音合成
    QString command = QString("Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).Speak('%1');").arg(cleanText);
    QProcess::startDetached("powershell", QStringList() << "-WindowStyle" << "Hidden" << "-Command" << command);
}
// 主循环，处理识别、注册和活体检测
void FaceProcessThread::run() {
    // 读取参数
    int camIdx, camW, camH;
    {
        QMutexLocker locker(&paramMutex);
        camIdx = m_camIndex;
        camW = m_camWidth;
        camH = m_camHeight;
    }
    QSharedMemory* camLock = nullptr;
    int assignedCam = 0;
    // 分配摄像头
    for (int i = 0; i < 5; ++i) {
        QString lockKey = QString("Attendance_CamLock_%1").arg(i);
        QSharedMemory* mem = new QSharedMemory(lockKey);

        if (mem->create(1)) {
            assignedCam = i;
            camLock = mem;
            break;
        }
        else {
            delete mem;
        }
    }
    {
        QMutexLocker locker(&paramMutex);
        m_camIndex = assignedCam;
    }
    // 打开摄像头
    capture.open(assignedCam, cv::CAP_DSHOW);
    if (!capture.isOpened()) {
        if (camLock) { camLock->detach(); delete camLock; }
        return;
    }
    emit cameraConnected(assignedCam);
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
        // 读取线程安全参数
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
        // 人脸注册处理
        if (!regName.isEmpty() && faceEngine && !arcfaceNet.empty()) {
            std::vector<FaceDetectInfo> faces = faceEngine->detect(frame, curConf, curNms);
            // 仅允许单人注册
            if (faces.size() == 1) {
                cv::Mat feat = extractFeature(frame, faces[0]);
                emit registerFeatureReady(regName, QByteArray((const char*)feat.data, feat.total() * feat.elemSize()));
                QMutexLocker locker(&mutex);
                pendingRegisterName.clear();
            }
            // 超时处理
            if (registerRetryCount > 60) {
                emit registerFailed("录入超时");
                QMutexLocker locker(&mutex);
                pendingRegisterName.clear();
            }
            continue;
        }
        if (page != 0 && page != 1) { QThread::msleep(100); continue; }
        // 人脸识别处理
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
                    // 提取特征并比对已注册用户
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
                    // 非当前登录用户则标记为非本人
                    if (bestName != "未知访客" && bestName != m_currentUser) {
                        bestName = "非本人";
                    }
                    if (!feat.empty() && (bestName == m_currentUser || currentFeatureBytes.isEmpty())) {
                        currentFeatureBytes = QByteArray((const char*)feat.data, feat.total() * feat.elemSize());
                    }
                    currentNames.append(bestName);
                    // 陌生人处理
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
                    // 活体检测和打卡
                    if (bestName != "未知访客" && bestName == m_currentUser) {

                        bool alive = true;
                        if (livenessOn) {
                            alive = updateLiveness(frame, face);
                            emit livenessResult(alive);
                        }
                        // 通过活体检测后打卡
                        if (alive) {
                            QDateTime now = QDateTime::currentDateTime();
                            // 检查冷却时间
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
        // 绘制检测框和标签
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        QImage img((const uchar*)rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
        QImage finalImg = img.copy();
        QPainter painter(&finalImg);
        painter.setFont(QFont("Microsoft YaHei", 18, QFont::Bold));
        for (const auto& res : cachedResults) {
            // 根据结果选择框颜色
            if (res.name == m_currentUser && res.alive)
                painter.setPen(Qt::green);
            else if (res.name == m_currentUser && !res.alive)
                painter.setPen(QColor(255, 165, 0));
            else
                painter.setPen(Qt::red);
            painter.drawRect(res.rect.x, res.rect.y, res.rect.width, res.rect.height);
            QString label = res.name;
            if (res.name == m_currentUser && !res.alive) label += " [检测中]";
            painter.drawText(res.rect.x, res.rect.y - 10, label);
        }
        painter.end();
        emit frameReady(finalImg, currentNames.isEmpty() ? QStringList{ m_currentUser } : currentNames, currentFeatureBytes);
        QThread::msleep(33);
    }
    capture.release();
    if (camLock) {
        camLock->detach();
        delete camLock;
    }
}