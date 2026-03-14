#include "FaceProcessThread.h"
#include <QPainter>
#include <QDebug>
#include <QDateTime>
#include <QThread>

// 构造函数：初始化加载人脸检测模型 (RetinaFace) 与特征识别模型 (ArcFace)
FaceProcessThread::FaceProcessThread(QObject* parent) : QThread(parent), isRunning(false), currentPage(0) {
    QString detectModel = "D:/models/retinaface_static.onnx";
    faceEngine = new RetinaFaceDecoder(detectModel.toStdString());

    QString recogModel = "D:/models/w600k_r50.onnx";
    try {
        arcfaceNet = cv::dnn::readNetFromONNX(recogModel.toStdString());
        if (!arcfaceNet.empty()) {
            // 开启 CUDA 硬件加速以降低 CPU 占用
            arcfaceNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            arcfaceNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
        }
    }
    catch (...) {
        qDebug() << "模型加载异常！";
    }
}

// 析构函数：释放引擎内存并安全退出线程
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
    registerRetryCount = 0; // 重置录入扫描倒计时
}

void FaceProcessThread::run() {
    bool cameraOpened = false;

    // 尝试遍历 0 到 9 号的所有摄像头资源
    for (int camId = 0; camId < 10; ++camId) {
        // 🚀 终极修复 1：抛弃老旧且容易死锁的 DSHOW，改用现代的 MSMF 后端！
        // Microsoft Media Foundation 对多进程和 USB 拓展坞的并发支持极其稳定
        if (capture.open(camId, cv::CAP_MSMF)) {

            // 依然保持 MJPEG 压缩，拯救拓展坞的带宽
            capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            capture.set(cv::CAP_PROP_FPS, 30);

            bool gotValidFrame = false;
            cv::Mat testFrame;

            // 🚀 终极修复 2：增加“黑屏假死”雷达检测！
            // 读 30 帧（约1.5秒的热机时间），不仅要求矩阵非空，还要求不能是纯黑！
            for (int i = 0; i < 30; ++i) {
                capture.read(testFrame);
                if (!testFrame.empty()) {
                    // 计算画面的平均亮度 (BGR三个通道)
                    cv::Scalar meanVal = cv::mean(testFrame);
                    // 纯黑画面的亮度接近 0，只要有任何光线或噪点，均值都会大于 1.0
                    if (meanVal[0] > 1.0 || meanVal[1] > 1.0 || meanVal[2] > 1.0) {
                        gotValidFrame = true; // 画面有真实内容，抢占成功！
                        break;
                    }
                }
                QThread::msleep(50);
            }

            if (gotValidFrame) {
                cameraOpened = true;
                qDebug() << "🚀 客户端成功独占摄像头 ID:" << camId << " (画面正常)";
                break; // 当前客户端成功抢到一个真正有画面的摄像头，退出遍历
            }
            else {
                // 💡 极其重要：如果是黑屏或空帧，必须无情释放它！让程序去尝试下一个 ID！
                qDebug() << "⚠️ 摄像头 ID:" << camId << " 黑屏或被占用，立即释放并尝试下一个...";
                capture.release();
            }
        }
    }

    if (!cameraOpened) {
        qDebug() << "❌ 致命错误：所有摄像头均被占用或带宽不足，打开失败！";
        return;
    }

    isRunning = true;
    qint64 lastDetectTime = 0;

    // 缓存识别结果，防止UI闪烁
    struct MatchResult { cv::Rect rect; QString name; };
    std::vector<MatchResult> cachedResults;
    QStringList currentRecognizedNames;

    while (isRunning) {
        cv::Mat frame;
        capture >> frame;

        // 🚀 修复 3：如果遇到瞬间的网络或驱动丢帧，不要死循环空转 CPU，给它喘息时间
        if (frame.empty()) {
            QThread::msleep(10);
            continue;
        }

        int page;
        QString regName;
        std::map<QString, cv::Mat> usersCopy;

        // 锁内拷贝共享变量，防止主线程和工作线程冲突
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
        // 分支 1：人脸录入注册逻辑
        // =====================================
        if (!regName.isEmpty() && faceEngine && !arcfaceNet.empty()) {
            std::vector<FaceDetectInfo> faces = faceEngine->detect(frame);
            if (faces.size() == 1) { // 仅当画面中只有一张脸时才允许录入
                // 依据5个特征点执行仿射变换，将人脸对齐裁剪为标准的 112x112 大小
                float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                std::vector<cv::Point2f> src_pts, dst_pts;
                for (int i = 0; i < 5; ++i) { src_pts.push_back(faces[0].pts[i]); dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1])); }
                cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
                cv::Mat aligned_face;
                cv::warpAffine(frame, aligned_face, transform, cv::Size(112, 112));

                // 输入 ArcFace 网络进行特征提取
                cv::Mat blob = cv::dnn::blobFromImage(aligned_face, 1.0 / 127.5, cv::Size(112, 112), cv::Scalar(127.5, 127.5, 127.5), true, false);
                arcfaceNet.setInput(blob);
                cv::Mat feature = arcfaceNet.forward();

                // 归一化特征向量
                feature = feature.reshape(1, 1).clone();
                cv::normalize(feature, feature, 1.0, 0.0, cv::NORM_L2);

                QByteArray featureBytes((const char*)feature.data, feature.total() * feature.elemSize());
                emit registerFeatureReady(regName, featureBytes);

                // 录入成功，重置状态
                QMutexLocker locker(&mutex);
                pendingRegisterName.clear();
            }
            continue; // 录入模式下跳过下方常规识别打卡
        }

        // 仅在指定页面下才执行识别分析以节约算力
        if (page != 0 && page != 1) { QThread::msleep(100); continue; }

        // =====================================
        // 分支 2：考勤打卡识别逻辑
        // =====================================
        qint64 currentTimeMs = QDateTime::currentMSecsSinceEpoch();
        if (currentTimeMs - lastDetectTime >= 300) { // 控制检测频率，每300ms比对一次
            std::vector<FaceDetectInfo> faces;
            if (faceEngine) faces = faceEngine->detect(frame);

            cachedResults.clear();
            currentRecognizedNames.clear();

            if (page == 0) { // 打卡页面模式
                for (const auto& face : faces) {
                    if (!arcfaceNet.empty()) {
                        // 抠图对齐
                        float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                        std::vector<cv::Point2f> src_pts, dst_pts;
                        for (int i = 0; i < 5; ++i) { src_pts.push_back(face.pts[i]); dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1])); }
                        cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
                        cv::Mat aligned_face;
                        cv::warpAffine(frame, aligned_face, transform, cv::Size(112, 112));

                        // 提取当前画面特征
                        cv::Mat blob = cv::dnn::blobFromImage(aligned_face, 1.0 / 127.5, cv::Size(112, 112), cv::Scalar(127.5, 127.5, 127.5), true, false);
                        arcfaceNet.setInput(blob);
                        cv::Mat currentFeature = arcfaceNet.forward();

                        currentFeature = currentFeature.reshape(1, 1).clone();
                        cv::normalize(currentFeature, currentFeature, 1.0, 0.0, cv::NORM_L2);

                        QString bestName = "未知访客";
                        double maxSim = 0.0;

                        // 遍历特征库求点积相似度
                        for (auto it = usersCopy.begin(); it != usersCopy.end(); ++it) {
                            double sim = currentFeature.dot(it->second);
                            if (sim > maxSim) {
                                maxSim = sim;
                                if (sim > 0.90) bestName = it->first; // 阈值设定
                            }
                        }

                        // 【修改点】：重新梳理的结果判定逻辑
                        QString displayText;
                        if (usersCopy.empty() || bestName == "未知访客") {
                           
                            displayText = "未知访客";
                            bestName = "未知访客"; // 同步状态，用于下方UI框变红
                        }
                        else if (bestName != m_currentUser) {
                            // 库中存在记录，但不是当前登录系统账号的本人
                            displayText = "非本人";
                            bestName = "非本人";
                        }
                        else {
                            // 识别成功且为本人，进入打卡防抖逻辑
                            displayText = bestName;
                            currentRecognizedNames.append(bestName);
                            QDateTime currentTime = QDateTime::currentDateTime();

                            // 防止连续重复写入打卡，限制1分钟内仅记录一次初次识别
                            if (lastPunchTime.find(bestName) == lastPunchTime.end() || lastPunchTime[bestName].secsTo(currentTime) > 60) {
                                lastPunchTime[bestName] = currentTime;
                            }
                            // 距离首次成功判定5秒内，显示打卡成功提示
                            if (lastPunchTime.find(bestName) != lastPunchTime.end() && lastPunchTime[bestName].secsTo(QDateTime::currentDateTime()) <= 5) {
                                displayText += " (打卡成功!)";
                            }
                        }
                        cachedResults.push_back({ face.rect, displayText });
                    }
                }
            }
            else if (page == 1) {
                // 其它页面仅画人脸框，不进行计算比对
                for (const auto& face : faces) cachedResults.push_back({ face.rect, "" });
            }
            lastDetectTime = currentTimeMs;
        }

        // =====================================
        // 画布渲染逻辑
        // =====================================
        cv::Mat rgbFrame;
        cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
        QImage img((const unsigned char*)(rgbFrame.data), rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
        QImage finalImg = img.copy();

        QPainter painter(&finalImg);
        if (page == 0) {
            painter.setFont(QFont("Microsoft YaHei", 18, QFont::Bold));
            for (const auto& res : cachedResults) {
                // 根据整合后的判定状态绘制红/绿标识框
                if (res.name == "未知访客" || res.name == "非本人") {
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
        QThread::msleep(33); // 锁定约 30 FPS 刷新率，降低系统功耗
    }
    capture.release();
}