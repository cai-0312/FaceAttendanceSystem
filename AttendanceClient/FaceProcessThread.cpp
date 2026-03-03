#include "FaceProcessThread.h"
#include <QPainter>
#include <QDebug>

FaceProcessThread::FaceProcessThread(QObject* parent) : QThread(parent), isRunning(false), currentPage(0) {
    QString detectModel = "D:/models/retinaface_static.onnx";
    faceEngine = new RetinaFaceDecoder(detectModel.toStdString());
    QString recogModel = "D:/models/arcface_mobilefacenet.onnx";
    try {
        arcfaceNet = cv::dnn::readNetFromONNX(recogModel.toStdString());
        if (!arcfaceNet.empty()) {
            arcfaceNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            arcfaceNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
    }
    catch (...) {}
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
}

// 核心死循环，与 UI 彻底分离！
void FaceProcessThread::run() {
    if (!capture.open(0)) return;
    isRunning = true;

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
            regName = pendingRegisterName;
            usersCopy = registeredUsers;
            pendingRegisterName.clear();
        }

        // 分支 1：处理主线程发来的“人脸录入”请求
        if (!regName.isEmpty() && faceEngine && !arcfaceNet.empty()) {
            std::vector<FaceDetectInfo> faces = faceEngine->detect(frame);
            if (faces.size() != 1) {
                emit registerFailed("录入失败：请确保画面中仅有一张清晰的人脸！");
            }
            else {
                float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                std::vector<cv::Point2f> src_pts, dst_pts;
                for (int i = 0; i < 5; ++i) { src_pts.push_back(faces[0].pts[i]); dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1])); }
                cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
                cv::Mat aligned_face;
                cv::warpAffine(frame, aligned_face, transform, cv::Size(112, 112));
                cv::Mat blob = cv::dnn::blobFromImage(aligned_face, 1.0 / 127.5, cv::Size(112, 112), cv::Scalar(127.5, 127.5, 127.5), true, false);
                arcfaceNet.setInput(blob);
                cv::Mat feature = arcfaceNet.forward();
                cv::normalize(feature, feature);

                QByteArray featureBytes((const char*)feature.data, feature.total() * feature.elemSize());
                emit registerFeatureReady(regName, featureBytes);
            }
            continue;
        }

        // 分支 2：省电模式
        if (page != 0 && page != 1) {
            QThread::msleep(30);
            continue;
        }

        // 分支 3：日常打卡/录入页的实时渲染
        std::vector<FaceDetectInfo> faces;
        if (faceEngine) faces = faceEngine->detect(frame);

        QStringList currentRecognizedNames;

        if (page == 0) {
            struct MatchResult { cv::Rect rect; QString name; };
            std::vector<MatchResult> results;

            for (const auto& face : faces) {
                cv::rectangle(frame, face.rect, cv::Scalar(0, 255, 0), 2);
                for (int j = 0; j < 5; ++j) cv::circle(frame, face.pts[j], 2, cv::Scalar(0, 0, 255), 2);

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
                    cv::normalize(currentFeature, currentFeature);

                    QString bestName = "未知访客";
                    double maxSim = 0.0;
                    for (auto it = usersCopy.begin(); it != usersCopy.end(); ++it) {
                        double sim = currentFeature.dot(it->second);
                        if (sim > maxSim) {
                            maxSim = sim;
                            if (sim > 0.45) bestName = it->first;
                        }
                    }

                    QString displayText = bestName;
                    if (bestName != "未知访客") {
                        currentRecognizedNames.append(bestName);
                        QDateTime currentTime = QDateTime::currentDateTime();
                        if (lastPunchTime.find(bestName) == lastPunchTime.end() || lastPunchTime[bestName].secsTo(currentTime) > 60) {
                            lastPunchTime[bestName] = currentTime;
                        }
                        if (lastPunchTime.find(bestName) != lastPunchTime.end() && lastPunchTime[bestName].secsTo(QDateTime::currentDateTime()) <= 5) {
                            displayText += " (打卡成功!)";
                        }
                    }
                    results.push_back({ face.rect, displayText });
                }
            }

            cv::Mat rgbFrame;
            cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
            QImage img((const unsigned char*)(rgbFrame.data), rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);

            // ★ 高级优化：在子线程直接用 QImage 画字，不崩溃！
            QImage finalImg = img.copy();
            QPainter painter(&finalImg);
            painter.setFont(QFont("Microsoft YaHei", 20, QFont::Bold));
            painter.setPen(QPen(Qt::green, 3));
            for (const auto& res : results) painter.drawText(res.rect.x, std::max(25, res.rect.y - 10), res.name);
            painter.end();

            emit frameReady(finalImg, currentRecognizedNames);

        }
        else if (page == 1) {
            for (const auto& face : faces) {
                cv::rectangle(frame, face.rect, cv::Scalar(255, 0, 0), 2);
                for (int j = 0; j < 5; ++j) cv::circle(frame, face.pts[j], 2, cv::Scalar(0, 255, 255), 2);
            }
            cv::Mat rgbFrame;
            cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
            QImage img((const unsigned char*)(rgbFrame.data), rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
            emit frameReady(img.copy(), currentRecognizedNames);
        }

        QThread::msleep(10);
    }
    capture.release();
}