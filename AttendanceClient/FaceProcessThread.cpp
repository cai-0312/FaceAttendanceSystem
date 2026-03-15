#include "FaceProcessThread.h"
#include <QPainter>
#include <QDateTime>
#include <QThread>
// 构造函数：初始化加载人脸检测模型 (RetinaFace) 与特征识别模型 (ArcFace)
FaceProcessThread::FaceProcessThread(QObject* parent) : QThread(parent), isRunning(false), currentPage(0) {
    // 初始化人脸位置与关键点检测模型
    QString detectModel = "D:/models/retinaface_static.onnx";
    faceEngine = new RetinaFaceDecoder(detectModel.toStdString());
    // 初始化人脸特征提取模型
    QString recogModel = "D:/models/w600k_r50.onnx";
    try {
        arcfaceNet = cv::dnn::readNetFromONNX(recogModel.toStdString());
        if (!arcfaceNet.empty()) {
            // 开启 CUDA 硬件加速以降低 CPU 占用，提升推理帧率
            arcfaceNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            arcfaceNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
        }
    }
    catch (...) {
        // 模型加载异常处理逻辑
    }
}
// 析构函数：释放引擎内存并安全退出线程
FaceProcessThread::~FaceProcessThread() {
    stop();
    if (faceEngine) {
        delete faceEngine;
        faceEngine = nullptr;
    }
}
// 停止线程运行
void FaceProcessThread::stop() {
    isRunning = false;
    wait();
}
// 线程安全地更新内存中的人脸特征库
void FaceProcessThread::updateRegisteredUsers(const std::map<QString, cv::Mat>& users) {
    QMutexLocker locker(&mutex);
    registeredUsers = users;
}
// 线程安全地更新当前页面索引
void FaceProcessThread::setPage(int page) {
    QMutexLocker locker(&mutex);
    currentPage = page;
}
// 线程安全地发起人脸注册请求
void FaceProcessThread::requestRegister(QString name) {
    QMutexLocker locker(&mutex);
    pendingRegisterName = name;
    registerRetryCount = 0; // 重置录入扫描倒计时
}
// 核心工作线程：负责摄像头流读取、人脸检测、特征对齐比对以及画面渲染
void FaceProcessThread::run() {
    bool cameraOpened = false;
    // 遍历 0 到 9 号的所有摄像头资源
    for (int camId = 0; camId < 10; ++camId) {
        // 采用 MSMF 后端，提升多进程和硬件设备的并发稳定性
        if (capture.open(camId, cv::CAP_MSMF)) {
            // 采用 MJPEG 压缩传输，节约设备数据带宽
            capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            capture.set(cv::CAP_PROP_FPS, 30);
            bool gotValidFrame = false;
            cv::Mat testFrame;
            // 摄像头异常状态检测：读取前 30 帧，确保画面非空且非纯黑
            for (int i = 0; i < 30; ++i) {
                capture.read(testFrame);
                if (!testFrame.empty()) {
                    // 计算画面的平均亮度 (BGR三个通道)
                    cv::Scalar meanVal = cv::mean(testFrame);
                    // 纯黑画面的亮度接近 0，只要有光线或噪点，均值均应大于 1.0
                    if (meanVal[0] > 1.0 || meanVal[1] > 1.0 || meanVal[2] > 1.0) {
                        gotValidFrame = true; // 画面有真实内容，捕获成功
                        break;
                    }
                }
                QThread::msleep(50);
            }
            if (gotValidFrame) {
                cameraOpened = true;
                break; // 成功捕获到一个有效的摄像头，退出遍历
            }
            else {
                // 若为黑屏或空帧，则立即释放资源并尝试下一个设备
                capture.release();
            }
        }
    }
    if (!cameraOpened) {
        // 所有摄像头均被占用或带宽不足，打开失败
        return;
    }
    isRunning = true;
    qint64 lastDetectTime = 0;
    // 缓存识别结果结构体，防止 UI 渲染闪烁
    struct MatchResult { cv::Rect rect; QString name; };
    std::vector<MatchResult> cachedResults;
    QStringList currentRecognizedNames;
    while (isRunning) {
        cv::Mat frame;
        capture >> frame;
        // 处理瞬间的网络或驱动丢帧，挂起当前线程让出 CPU 时间片
        if (frame.empty()) {
            QThread::msleep(10);
            continue;
        }
        int page;
        QString regName;
        std::map<QString, cv::Mat> usersCopy;
        // 锁内拷贝共享变量，防止主线程和工作线程数据读写冲突
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
        // 分支 1：人脸录入注册逻辑
        if (!regName.isEmpty() && faceEngine && !arcfaceNet.empty()) {
            std::vector<FaceDetectInfo> faces = faceEngine->detect(frame);
            if (faces.size() == 1) { // 仅当画面中只有一张脸时才允许录入
                // 依据5个特征点执行仿射变换，将人脸对齐裁剪为标准的 112x112 尺寸
                float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                std::vector<cv::Point2f> src_pts, dst_pts;
                for (int i = 0; i < 5; ++i) {
                    src_pts.push_back(faces[0].pts[i]);
                    dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1]));
                }
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
                // 发送提取完毕的高维特征二进制数据
                QByteArray featureBytes((const char*)feature.data, feature.total() * feature.elemSize());
                emit registerFeatureReady(regName, featureBytes);
                // 录入成功，重置待注册状态
                QMutexLocker locker(&mutex);
                pendingRegisterName.clear();
            }
            continue; // 录入模式下跳过常规的识别打卡逻辑
        }
        // 仅在考勤打卡页 (0) 或注册页 (1) 下才执行识别分析以节约算力
        if (page != 0 && page != 1) {
            QThread::msleep(100);
            continue;
        }
        // 分支 2：考勤打卡识别逻辑
        qint64 currentTimeMs = QDateTime::currentMSecsSinceEpoch();
        if (currentTimeMs - lastDetectTime >= 300) { // 控制检测频率，每 300ms 比对一次
            std::vector<FaceDetectInfo> faces;
            if (faceEngine) faces = faceEngine->detect(frame);
            cachedResults.clear();
            currentRecognizedNames.clear();
            if (page == 0) { // 考勤打卡页面模式
                for (const auto& face : faces) {
                    if (!arcfaceNet.empty()) {
                        // 依据关键点进行人脸图像的仿射变换与对齐
                        float ref_pts[5][2] = { {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f}, {41.5493f, 92.3655f}, {70.7299f, 92.2041f} };
                        std::vector<cv::Point2f> src_pts, dst_pts;
                        for (int i = 0; i < 5; ++i) {
                            src_pts.push_back(face.pts[i]);
                            dst_pts.push_back(cv::Point2f(ref_pts[i][0], ref_pts[i][1]));
                        }
                        cv::Mat transform = cv::estimateAffinePartial2D(src_pts, dst_pts);
                        cv::Mat aligned_face;
                        cv::warpAffine(frame, aligned_face, transform, cv::Size(112, 112));
                        // 提取当前画面人脸特征
                        cv::Mat blob = cv::dnn::blobFromImage(aligned_face, 1.0 / 127.5, cv::Size(112, 112), cv::Scalar(127.5, 127.5, 127.5), true, false);
                        arcfaceNet.setInput(blob);
                        cv::Mat currentFeature = arcfaceNet.forward();
                        // 特征归一化
                        currentFeature = currentFeature.reshape(1, 1).clone();
                        cv::normalize(currentFeature, currentFeature, 1.0, 0.0, cv::NORM_L2);
                        QString bestName = "未知访客";
                        double maxSim = 0.0;
                        // 遍历内存特征库求点积相似度
                        for (auto it = usersCopy.begin(); it != usersCopy.end(); ++it) {
                            double sim = currentFeature.dot(it->second);
                            if (sim > maxSim) {
                                maxSim = sim;
                                if (sim > 0.90) bestName = it->first; // 相似度阈值设定
                            }
                        }
                        // 处理识别结果判定逻辑
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
                // 注册等其它页面仅画人脸框，不进行计算比对以节约性能
                for (const auto& face : faces) {
                    cachedResults.push_back({ face.rect, "" });
                }
            }
            lastDetectTime = currentTimeMs;
        }
        // 画布渲染逻辑
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
            // 纯检测模式绘制蓝色框
            painter.setPen(QPen(Qt::blue, 3));
            for (const auto& res : cachedResults) {
                painter.drawRect(res.rect.x, res.rect.y, res.rect.width, res.rect.height);
            }
        }
        painter.end();
        // 抛出准备好的画面和识别人员列表供 UI 层更新
        emit frameReady(finalImg, currentRecognizedNames);
        QThread::msleep(33); // 锁定约 30 FPS 刷新率，降低系统功耗
    }
    capture.release();
}