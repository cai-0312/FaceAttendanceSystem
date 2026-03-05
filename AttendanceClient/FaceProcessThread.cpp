#include "FaceProcessThread.h"
#include <QPainter>
#include <QDebug>

FaceProcessThread::FaceProcessThread(QObject* parent) : QThread(parent), isRunning(false), currentPage(0) {
    // 设置人脸检测模型路径并初始化检测引擎
    QString detectModel = "D:/models/retinaface_static.onnx";
    faceEngine = new RetinaFaceDecoder(detectModel.toStdString());

    // 设置人脸识别模型路径并初始化识别网络
    QString recogModel = "D:/models/arcface_mobilefacenet.onnx";
    try {
        arcfaceNet = cv::dnn::readNetFromONNX(recogModel.toStdString());
        if (!arcfaceNet.empty()) {
            // 配置OpenCV深度学习后端，设置为在CPU上运行
            arcfaceNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            arcfaceNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
    }
    catch (...) {
        // 捕获潜在加载异常
    }
}

// 析构函数：确保线程安全停止并释放模型内存
FaceProcessThread::~FaceProcessThread() {
    stop();
    if (faceEngine) delete faceEngine;
}

// 停止线程：修改运行标志并等待子线程安全退出
void FaceProcessThread::stop() {
    isRunning = false;
    wait();
}

// 更新注册用户列表：通过互斥锁保证多线程环境下的人脸特征库同步安全
void FaceProcessThread::updateRegisteredUsers(const std::map<QString, cv::Mat>& users) {
    QMutexLocker locker(&mutex);
    registeredUsers = users;
}

// 设置当前界面页码：决定线程进入识别、录入还是省电模式
void FaceProcessThread::setPage(int page) {
    QMutexLocker locker(&mutex);
    currentPage = page;
}

// 请求人脸录入：设置待录入人员姓名，由死循环捕获并处理
void FaceProcessThread::requestRegister(QString name) {
    QMutexLocker locker(&mutex);
    pendingRegisterName = name;
}

// 线程核心主循环：负责视频采集、人脸检测、特征对齐与识别比对
void FaceProcessThread::run() {
    if (!capture.open(0)) return;
    isRunning = true;

    while (isRunning) {
        cv::Mat frame;
        capture >> frame;
        if (frame.empty()) continue;

        // 获取线程共享变量的局部副本，减少锁的持有时间
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

        // 分支 1：处理人脸特征录入逻辑
        if (!regName.isEmpty() && faceEngine && !arcfaceNet.empty()) {
            std::vector<FaceDetectInfo> faces = faceEngine->detect(frame);
            if (faces.size() != 1) {
                // 录入页必须保证只有一人，否则为了特征准确性拒绝录入
                emit registerFailed("录入失败：请确保画面中仅有一张清晰的人脸！");
            }
            else {
                // 执行仿射变换实现人脸对齐
                float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                std::vector<cv::Point2f> src_pts, dst_pts;
                for (int i = 0; i < 5; ++i) { src_pts.push_back(faces[0].pts[i]); dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1])); }
                cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
                cv::Mat aligned_face;
                cv::warpAffine(frame, aligned_face, transform, cv::Size(112, 112));

                // 提取128维特征向量并进行L2归一化
                cv::Mat blob = cv::dnn::blobFromImage(aligned_face, 1.0 / 127.5, cv::Size(112, 112), cv::Scalar(127.5, 127.5, 127.5), true, false);
                arcfaceNet.setInput(blob);
                cv::Mat feature = arcfaceNet.forward();
                cv::normalize(feature, feature);

                QByteArray featureBytes((const char*)feature.data, feature.total() * feature.elemSize());
                emit registerFeatureReady(regName, featureBytes);
            }
            continue;
        }

        // 分支 2：省电模式，当用户切换到申诉或规则页时降低摄像头处理频率
        if (page != 0 && page != 1) {
            QThread::msleep(30);
            continue;
        }

        // 分支 3：日常打卡页与录入监控页的实时渲染
        std::vector<FaceDetectInfo> faces;
        if (faceEngine) faces = faceEngine->detect(frame);

        QStringList currentRecognizedNames;

        // 处理打卡首页：执行检测、对齐、识别与画面文字绘制
        if (page == 0) {
            struct MatchResult { cv::Rect rect; QString name; };
            std::vector<MatchResult> results;

            for (const auto& face : faces) {
                // 绘制人脸框与五个关键点
                cv::rectangle(frame, face.rect, cv::Scalar(0, 255, 0), 2);
                for (int j = 0; j < 5; ++j) cv::circle(frame, face.pts[j], 2, cv::Scalar(0, 0, 255), 2);

                if (!arcfaceNet.empty()) {
                    // 对齐并提取特征
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

                    // 在注册库中执行余弦相似度比对，阈值设定为 0.45
                    QString bestName = "未知访客";
                    double maxSim = 0.0;
                    for (auto it = usersCopy.begin(); it != usersCopy.end(); ++it) {
                        double sim = currentFeature.dot(it->second);
                        if (sim > maxSim) {
                            maxSim = sim;
                            if (sim > 0.45) bestName = it->first;
                        }
                    }

                    // 处理打卡成功提示语逻辑
                    QString displayText = bestName;
                    if (bestName != "未知访客") {
                        currentRecognizedNames.append(bestName);
                        QDateTime currentTime = QDateTime::currentDateTime();
                        // 判定是否记录为最后一次打卡时间，间隔需大于60秒以防频繁触发
                        if (lastPunchTime.find(bestName) == lastPunchTime.end() || lastPunchTime[bestName].secsTo(currentTime) > 60) {
                            lastPunchTime[bestName] = currentTime;
                        }
                        // 近5秒内打卡成功的，在画面中显示成功状态
                        if (lastPunchTime.find(bestName) != lastPunchTime.end() && lastPunchTime[bestName].secsTo(QDateTime::currentDateTime()) <= 5) {
                            displayText += " (打卡成功!)";
                        }
                    }
                    results.push_back({ face.rect, displayText });
                }
            }

            // 图像格式转换并利用QPainter进行跨线程安全的界面元素绘制
            cv::Mat rgbFrame;
            cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
            QImage img((const unsigned char*)(rgbFrame.data), rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);

            QImage finalImg = img.copy();
            QPainter painter(&finalImg);
            painter.setFont(QFont("Microsoft YaHei", 20, QFont::Bold));
            painter.setPen(QPen(Qt::green, 3));
            for (const auto& res : results) painter.drawText(res.rect.x, std::max(25, res.rect.y - 10), res.name);
            painter.end();

            emit frameReady(finalImg, currentRecognizedNames);

        }
        // 处理人脸录入页：仅绘制检测框，不进行特征比对，保证流畅度
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