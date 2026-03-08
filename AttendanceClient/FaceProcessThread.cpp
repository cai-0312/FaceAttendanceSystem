#include "FaceProcessThread.h"
#include <QPainter>
#include <QDebug>
#include <QDateTime>

FaceProcessThread::FaceProcessThread(QObject* parent) : QThread(parent), isRunning(false), currentPage(0) {
    QString detectModel = "D:/models/retinaface_static.onnx";
    faceEngine = new RetinaFaceDecoder(detectModel.toStdString());

    QString recogModel = "D:/models/arcface_mobilefacenet.onnx";
    try {
        arcfaceNet = cv::dnn::readNetFromONNX(recogModel.toStdString());
        if (!arcfaceNet.empty()) {
            arcfaceNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            arcfaceNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
        }
    }
    catch (...) {
        qDebug() << "模型加载异常！";
    }
}

FaceProcessThread::~FaceProcessThread() {
    stop();
    if (faceEngine) delete faceEngine;
}

void FaceProcessThread::stop() {
    isRunning = false;
    wait();
}

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
    registerRetryCount = 0; // 重置扫描倒计时
}

void FaceProcessThread::run() {
    bool cameraOpened = false;

    for (int camId = 0; camId < 10; ++camId) {
        if (capture.open(camId, cv::CAP_DSHOW)) {
            capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            capture.set(cv::CAP_PROP_FPS, 30);

            bool gotFrame = false;
            cv::Mat testFrame;
            for (int i = 0; i < 15; ++i) {
                capture.read(testFrame);
                if (!testFrame.empty()) { gotFrame = true; break; }
                QThread::msleep(50);
            }
            if (gotFrame) { cameraOpened = true; break; }
            else { capture.release(); }
        }
    }

    if (!cameraOpened) return;

    isRunning = true;
    qint64 lastDetectTime = 0;
    struct MatchResult { cv::Rect rect; QString name; };
    std::vector<MatchResult> cachedResults;
    QStringList currentRecognizedNames;

    while (isRunning) {
        cv::Mat frame;
        capture >> frame;
        if (frame.empty()) continue;

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
                if (registerRetryCount > 60) {
                    emit registerFailed("录入超时：未检测到清晰的正脸，请调整重试！");
                    pendingRegisterName.clear();
                    regName.clear();
                }
            }
        }

        // =====================================
        // 分支 1：人脸录入逻辑
        // =====================================
        if (!regName.isEmpty() && faceEngine && !arcfaceNet.empty()) {
            std::vector<FaceDetectInfo> faces = faceEngine->detect(frame);
            if (faces.size() == 1) { // 必须稳定捕捉到一张脸
                float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                std::vector<cv::Point2f> src_pts, dst_pts;
                for (int i = 0; i < 5; ++i) { src_pts.push_back(faces[0].pts[i]); dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1])); }
                cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
                cv::Mat aligned_face;
                cv::warpAffine(frame, aligned_face, transform, cv::Size(112, 112));

                cv::Mat blob = cv::dnn::blobFromImage(aligned_face, 1.0 / 127.5, cv::Size(112, 112), cv::Scalar(127.5, 127.5, 127.5), true, false);
                arcfaceNet.setInput(blob);
                cv::Mat feature = arcfaceNet.forward();

                // 🚀 核心物理修复：压平多维矩阵，并使用深拷贝保证内存绝对连续！
                feature = feature.reshape(1, 1).clone();
                cv::normalize(feature, feature, 1.0, 0.0, cv::NORM_L2);

                QByteArray featureBytes((const char*)feature.data, feature.total() * feature.elemSize());
                emit registerFeatureReady(regName, featureBytes);

                QMutexLocker locker(&mutex);
                pendingRegisterName.clear();
            }
            continue;
        }

        if (page != 0 && page != 1) { QThread::msleep(100); continue; }

        // =====================================
        // 分支 2：考勤打卡识别逻辑
        // =====================================
        qint64 currentTimeMs = QDateTime::currentMSecsSinceEpoch();
        if (currentTimeMs - lastDetectTime >= 300) {
            std::vector<FaceDetectInfo> faces;
            if (faceEngine) faces = faceEngine->detect(frame);

            cachedResults.clear();
            currentRecognizedNames.clear();

            if (page == 0) {
                for (const auto& face : faces) {
                    if (!arcfaceNet.empty()) {
                        float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                        std::vector<cv::Point2f> src_pts, dst_pts;
                        for (int i = 0; i < 5; ++i) { src_pts.push_back(face.pts[i]); dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1])); }
                        cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
                        cv::Mat aligned_face;
                        cv::warpAffine(frame, aligned_face, transform, cv::Size(112, 112));

                        cv::Mat blob = cv::dnn::blobFromImage(aligned_face, 1.0 / 127.5, cv::Size(112, 112), cv::Scalar(127.5, 127.5, 127.5), true, false);
                        arcfaceNet.setInput(blob);
                        cv::Mat currentFeature = arcfaceNet.forward();

                        // 🚀 核心物理修复：压平比对矩阵！
                        currentFeature = currentFeature.reshape(1, 1).clone();
                        cv::normalize(currentFeature, currentFeature, 1.0, 0.0, cv::NORM_L2);

                        QString bestName = "未知访客";
                        double maxSim = 0.0;

                        // 遍历库中所有已注册的人脸特征
                        for (auto it = usersCopy.begin(); it != usersCopy.end(); ++it) {
                            double sim = currentFeature.dot(it->second);
                            if (sim > maxSim) {
                                maxSim = sim;
                                if (sim > 0.40) bestName = it->first; // 相似度阈值调整为 0.40 增加宽容度
                            }
                        }

                        // 🎯 核心防伪与状态判定
                        QString displayText;
                        if (usersCopy.empty()) {
                            displayText = "特征库为空"; // 如果内存没读到库，直接告诉你！
                        }
                        else if (bestName == "未知访客") {
                            // 没找到人，直接把最大相似度打印在屏幕上！
                            displayText = QString("未知访客").arg(maxSim, 0, 'f', 2);
                        }
                        else if (bestName != m_currentUser) {
                            // 查到脸了，但是和当前登录的账号不匹配
                            displayText = QString("非本人").arg(bestName);
                            bestName = "非本人"; // 篡改名字，阻断写入
                        }
                        else {
                            // 是本人！
                            displayText = bestName;
                            currentRecognizedNames.append(bestName);
                            QDateTime currentTime = QDateTime::currentDateTime();
                            if (lastPunchTime.find(bestName) == lastPunchTime.end() || lastPunchTime[bestName].secsTo(currentTime) > 60) {
                                lastPunchTime[bestName] = currentTime;
                            }
                            if (lastPunchTime.find(bestName) != lastPunchTime.end() && lastPunchTime[bestName].secsTo(QDateTime::currentDateTime()) <= 5) {
                                displayText += " (打卡成功!)";
                            }
                        }
                        cachedResults.push_back({ face.rect, displayText });
                    }
                }
            }
            else if (page == 1) {
                for (const auto& face : faces) cachedResults.push_back({ face.rect, "" });
            }
            lastDetectTime = currentTimeMs;
        }

        // 高速渲染画面
        cv::Mat rgbFrame;
        cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
        QImage img((const unsigned char*)(rgbFrame.data), rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
        QImage finalImg = img.copy();

        QPainter painter(&finalImg);
        if (page == 0) {
            painter.setFont(QFont("Microsoft YaHei", 18, QFont::Bold));
            for (const auto& res : cachedResults) {
                // 🎨 视觉优化：未知访客、非本人、空库 一律画红框！本人画绿框！
                if (res.name.contains("未知访客") || res.name.contains("非本人") || res.name.contains("特征库为空")) {
                    painter.setPen(QPen(Qt::red, 3));
                }
                else {
                    painter.setPen(QPen(Qt::green, 3));
                }
                painter.drawRect(res.rect.x, res.rect.y, res.rect.width, res.rect.height);
                painter.drawText(res.rect.x, std::max(25, res.rect.y - 10), res.name);
            }
        }
        else if (page == 1) {
            painter.setPen(QPen(Qt::blue, 3));
            for (const auto& res : cachedResults) painter.drawRect(res.rect.x, res.rect.y, res.rect.width, res.rect.height);
        }
        painter.end();

        emit frameReady(finalImg, currentRecognizedNames);
        QThread::msleep(33);
    }
    capture.release();
}