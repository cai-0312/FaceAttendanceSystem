#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "NetworkHelper.h"
#define NOMINMAX
#include <windows.h>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton> 
#include <QDialog>
#include <QDialogButtonBox>
#include <QtConcurrent>
#include <QFuture>
#include <QThread>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QApplication> 
// 构造函数：初始化主界面 UI 容器，并依次实例化所有核心业务模块
MainWidget::MainWidget(QString loginName, QString role, QWidget* parent)
    : QWidget(parent), ui(new Ui::MainWidget), m_loginName(loginName), m_role(role)
{
    ui->setupUi(this);
    // 发起网络请求获取当前登录用户的部门及真实姓名信息
    QJsonObject uReq;
    uReq["type"] = "query_user_dept";
    uReq["name"] = m_loginName;
    QJsonObject uRes = NetworkHelper::request(uReq);
    QString dept = uRes["department"].toString();
    if (uRes.contains("real_name")) {
        m_loginName = uRes["real_name"].toString();
    }
    // 权限控制：根据用户所属部门及角色，动态隐藏没有访问权限的导航菜单项
    if (dept != "人力资源部") {
        if (ui->listWidget_Nav->item(4)) {
            ui->listWidget_Nav->item(4)->setHidden(true);
        }
    }
    if (m_role == "普通登录") {
        if (ui->listWidget_Nav->item(4)) {
            ui->listWidget_Nav->item(4)->setHidden(true);
        }
    }
    // 实例化考勤记录与数据检索模块
    recordModule = new RecordModule(ui->tableView_Records, ui->calendarWidget_Attendance,
        ui->label_SummaryData, ui->label_DetailDate, ui->lineEdit_SearchName,
        ui->btn_FilterDate, ui->btn_Export, m_loginName, m_role, this);
    // 实例化实时人脸打卡与规则设置模块
    punchModule = new PunchModule(ui->label_Camera_Punch, ui->btn_manualPunch,
        ui->label_MorningTime, ui->label_MorningStatus, ui->label_EveningTime, ui->label_EveningStatus,
        ui->btn_RuleSettings, ui->btn_LeaveRequest, ui->btn_LeaveApprove, ui->btn_AppealRequest,
        ui->btn_AppealApprove, ui->label_CurrentTime, m_role, m_loginName, this);
    // 实例化企业员工花名册管理模块
    userModule = new UserModule(ui->tableView_Users, ui->comboBox_FilterDept, ui->btn_FilterDept, this);
    // 实例化新员工人脸特征采集与入库模块
    registerModule = new RegisterModule(ui->label_Camera_Register, this);
    // 实例化基于大语言模型的智能问答助手模块
    m_aiModule = new AIAssistantModule(ui->textBrowser_AI, ui->lineEdit_AIInput, ui->btn_SendAI, ui->btn_ClearAIHistory, m_loginName, this);
    // 实例化主页大屏数据可视化模块
    homeModule = new HomeModule(ui->verticalLayout_Home, m_role, m_loginName, this);
    // 实例化企业内部即时通讯模块，并建立底层 TCP 长连接
    chatModule = new ChatModule(ui->listWidget_Contacts, ui->textBrowser_Chat, ui->lineEdit_ChatMessage,
        ui->label_ChatTarget, ui->btn_Emoji, ui->btn_Folder, ui->btn_History, ui->btn_MoreOpt, ui->lineEdit_ChatSearch, this);
    chatModule->connectToServer(NetworkHelper::serverIp(), NetworkHelper::serverPort(), m_loginName);
    // 动态创建并注入个人信息修改按钮
    QPushButton* btnEditProfile = new QPushButton("✏️ 修改性别与电话", this);
    if (ui->formLayout_Profile) {
        ui->formLayout_Profile->addRow("", btnEditProfile);
    }
    // 实例化个人中心与档案展示模块
    profileModule = new ProfileModule(ui->label_Avatar, ui->label_ProfileName, ui->label_ProfileDept,
        ui->label_ProfileGender, ui->label_ProfilePhone, ui->btn_ChangeAvatar, btnEditProfile, this);
    // 动态创建全局系统广播发布按钮，仅对管理员开放
    QPushButton* btnBroadcast = new QPushButton("📢 发布系统广播", this);
    btnBroadcast->setMinimumSize(130, 35);
    btnBroadcast->setCursor(Qt::PointingHandCursor);
    btnBroadcast->setStyleSheet("QPushButton { background-color: #F56C6C; color: white; border-radius: 6px; font-weight: bold; } QPushButton:hover { background-color: #F78989; }");

    if (m_role != "管理员登录") {
        btnBroadcast->hide();
    }
    // 将广播按钮嵌入到主页顶部布局中
    if (ui->verticalLayout_Home && ui->label_HomeTitle) {
        QHBoxLayout* topHomeLayout = new QHBoxLayout();
        topHomeLayout->addWidget(ui->label_HomeTitle);
        topHomeLayout->addStretch();
        topHomeLayout->addWidget(btnBroadcast);
        QLayoutItem* oldTitleItem = ui->verticalLayout_Home->takeAt(0);
        if (oldTitleItem) delete oldTitleItem;
        ui->verticalLayout_Home->insertLayout(0, topHomeLayout);
    }
    // 绑定发布系统公告事件：通过弹窗采集内容并交由服务端处理与分发
    connect(btnBroadcast, &QPushButton::clicked, this, [=]() {
        QDialog dialog(this);
        dialog.setWindowTitle("起草全局系统广播");
        dialog.resize(450, 300);
        QVBoxLayout layout(&dialog);
        QLabel* tipLabel = new QLabel("广播内容将同步至首页公告板，并向在线员工弹窗：", &dialog);
        tipLabel->setStyleSheet("color: #909399; font-size: 12px;");
        layout.addWidget(tipLabel);
        QTextEdit* textEdit = new QTextEdit(&dialog);
        textEdit->setPlaceholderText("请输入放假通知、新员工欢迎等内容...");
        textEdit->setStyleSheet("border: 1px solid #DCDFE6; border-radius: 4px; padding: 10px; font-size: 14px;");
        layout.addWidget(textEdit);

        QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
        buttonBox.button(QDialogButtonBox::Ok)->setText("立即发送");
        buttonBox.button(QDialogButtonBox::Ok)->setStyleSheet("background-color: #F56C6C; color: white;");
        layout.addWidget(&buttonBox);
        connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            QString msg = textEdit->toPlainText().trimmed();
            if (!msg.isEmpty()) {
                chatModule->sendBroadcast(msg);
                QJsonObject req;
                req["type"] = "publish_announcement";
                req["publisher"] = m_loginName;
                req["content"] = msg;
                NetworkHelper::sendAsync(req);
                QMessageBox::information(this, "发送成功", "全员广播已下发，并已同步至首页系统公告看板！");
                if (homeModule) homeModule->refreshDashboard();
            }
        }
        });
    // 监听网络广播信号并触发右下角气泡弹窗通知
    connect(chatModule, &ChatModule::broadcastReceived, this, [=](QString from, QString msg) {
        NoticePopup* popup = new NoticePopup(QString("系统公告 (发布者: %1)").arg(from), msg);
        popup->showAnimation();
        });
    // 启动负责计算密集型任务的独立人脸特征识别线程
    aiThread = new FaceProcessThread(this);
    aiThread->setCurrentUser(m_loginName);
    // 绑定各类UI交互事件到底层业务逻辑模块
    connect(ui->btn_deleteUser, &QPushButton::clicked, userModule, &UserModule::deleteSelectedUser);
    connect(ui->btn_Register, &QPushButton::clicked, registerModule, &RegisterModule::triggerRegistration);
    connect(ui->btn_SendChat, &QPushButton::clicked, chatModule, &ChatModule::sendMessage);
    connect(ui->lineEdit_ChatMessage, &QLineEdit::returnPressed, chatModule, &ChatModule::sendMessage);
    // 将考勤模块的系统消息推送至聊天会话中
    connect(punchModule, &PunchModule::requestSendChat, this, [this](QString msg) {
        chatModule->sendSystemMessage(m_loginName, msg);
        });
    // 绑定人脸特征采集与注册事件关联
    connect(registerModule, &RegisterModule::startRegistration, aiThread, &FaceProcessThread::requestRegister);
    connect(aiThread, &FaceProcessThread::registerFeatureReady, registerModule, &RegisterModule::onFeatureReady);
    connect(aiThread, &FaceProcessThread::registerFailed, registerModule, &RegisterModule::onRegisterFailed);
    // 监听数据变动信号以触发特征库的延时重载
    connect(registerModule, &RegisterModule::dataChanged, this, [this]() {
        QTimer::singleShot(2000, this, &MainWidget::loadRegisteredUsers);
        });
    connect(userModule, &UserModule::dataChanged, this, &MainWidget::loadRegisteredUsers);
    // 绑定首页大屏卡片的快捷操作到打卡请假模块
    connect(homeModule, &HomeModule::requestQuickLeave, punchModule, &PunchModule::onLeaveRequestClicked);
    connect(homeModule, &HomeModule::requestQuickAppeal, punchModule, &PunchModule::onAppealRequestClicked);
    connect(homeModule, &HomeModule::requestApproveLeave, punchModule, &PunchModule::onLeaveApproveClicked);
    connect(homeModule, &HomeModule::requestApproveAppeal, punchModule, &PunchModule::onAppealApproveClicked);
    // 个人资料页触发重新人脸采集的导航跳转
    connect(profileModule, &ProfileModule::requestFaceReRegister, this, [=](QString currentName) {
        ui->listWidget_Nav->setCurrentRow(2);
        ui->stackedWidget->setCurrentIndex(2);
        QMessageBox::information(this, "准备就绪", "请核对名字后，点击【开始录入】进行人脸覆写！");
        });
    // 处理侧边导航栏切换事件，调度显示面板及通知关联子模块进行数据懒加载
    connect(ui->listWidget_Nav, &QListWidget::currentRowChanged, this, [=](int row) {
        ui->stackedWidget->setCurrentIndex(row);
        if (aiThread) {
            if (row == 1) aiThread->setPage(0);
            else if (row == 2) aiThread->setPage(1);
            else aiThread->setPage(-1);
        }
        if (row == 0) {
            homeModule->refreshDashboard();
        }
        else if (row == 3) {
            recordModule->refreshData();
        }
        else if (row == 4) {
            if (m_role == "管理员登录" || m_role == "经理") {
                userModule->refreshTable(ui->comboBox_FilterDept->currentText());
            }
            else {
                QMessageBox::warning(this, "越权拦截", "您没有权限访问员工管理！");
            }
        }
        else if (row == 7) {
            profileModule->loadUserProfile(m_loginName);
        }
        });
    // 监听子线程人脸处理完成返回的视频帧及识别结果并做后续考勤业务判断
    connect(aiThread, &FaceProcessThread::frameReady, this, [=](QImage img, QStringList names, QByteArray currentFeatureBytes) {
        int currentPage = ui->stackedWidget->currentIndex();
        if (currentPage == 1) {
            punchModule->renderFrame(img);

            // 实时缓存最新的加密特征，供真正的安全打卡网络发包使用
            if (!currentFeatureBytes.isEmpty()) {
                punchModule->updateCurrentFaceFeature(currentFeatureBytes);
            }

            // 遍历视野内的所有人脸，更新 UI 显示名字
            for (const QString& name : names) {
                punchModule->updateRecognizedName(name);

                // 【严格保留的原有逻辑】：过滤非法人员。如果不是本人，绝不允许进行任何考勤判定！
                // 界面层会根据这个判定画出红框或报警，此处代码予以保留。
                if (name == "未知访客" || name == "非本人" || name != m_loginName) {
                    continue;
                }
                // 注意：旧代码中的 m_punchStates 防抖逻辑已移交到底层 aiThread 的 lastPunchTime 冷却机制处理，此处变得极其干净。
            }
        }
        else if (currentPage == 2) {
            if (ui->label_Camera_Register) {
                ui->label_Camera_Register->setPixmap(QPixmap::fromImage(img).scaled(
                    ui->label_Camera_Register->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
        });
    connect(aiThread, &FaceProcessThread::internalPunchSuccess, this, [=](QString name, QDateTime time) {
        int currentPage = ui->stackedWidget->currentIndex();
        if (currentPage != 1) return; // 不在打卡页面不发包

        // 【双重保险】：防止底层误发，再次核验必须是本人
        if (name == "未知访客" || name == "非本人" || name != m_loginName) return;

        // 提取实时加密的特征矩阵字节流
        QByteArray secureFeature = punchModule->getCurrentFeatureBytes();
        if (secureFeature.isEmpty()) return;

        // 严格遵循《开题报告》：打卡数据包仅包含加密的 Base64 特征，绝不传递明文姓名或判定结果
        QJsonObject req;
        req["type"] = "secure_punch_request";
        req["feature"] = QString(secureFeature.toBase64());

        // 丢入并发线程池，向服务端发起 1:N 特征盲比对，绝对不卡死 UI 画面
        QtConcurrent::run([req, this]() {
            QJsonObject res = NetworkHelper::request(req, 2000, 3000);
            if (res["status"].toString() == "success") {
                // 服务端核验特征一致并落库成功，触发前端打卡记录面板刷新
                QTimer::singleShot(500, punchModule, &PunchModule::loadTodayPunchStatus);
            }
            });
        });

    // 监听用户在线状态切换并向服务器上报
    if (ui->comboBox_Status) {
        connect(ui->comboBox_Status, &QComboBox::currentTextChanged, this, &MainWidget::onStatusChanged);
    }
    m_netManager = new QNetworkAccessManager(this);
    // 启动硬件线程和首次数据拉取
    if (aiThread) aiThread->start();
    loadRegisteredUsers();
    // 启动时触发个人信息及主页大屏数据加载
    if (profileModule) profileModule->loadUserProfile(m_loginName);
    if (homeModule) homeModule->refreshDashboard();
}
// 析构函数：保证多线程被安全释放
MainWidget::~MainWidget() {
    if (aiThread) aiThread->stop();
    delete ui;
}
// 通过网络接口向服务器拉取完整的人脸特征库，映射解包后投递给算法子线程
void MainWidget::loadRegisteredUsers() {
    QJsonObject req;
    req["type"] = "query_face_features";
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        std::map<QString, cv::Mat> users;
        QJsonArray arr = res["data"].toArray();

        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            QString dbName = o["name"].toString();
            QByteArray dbFeatureBytes = QByteArray::fromBase64(o["feature"].toString().toUtf8());
            if (dbFeatureBytes.isEmpty() || dbFeatureBytes.isNull()) continue;
            int floatCount = dbFeatureBytes.size() / sizeof(float);
            cv::Mat featureMat(1, floatCount, CV_32F);
            memcpy(featureMat.data, dbFeatureBytes.constData(), dbFeatureBytes.size());
            users[dbName] = featureMat.clone();
        }
        if (aiThread) {
            aiThread->updateRegisteredUsers(users);
        }
    }
}
// 触发用户实时状态更新，向系统服务通报当前在线状态
void MainWidget::onStatusChanged(const QString& status) {
    if (status.isEmpty()) return;
    QJsonObject req;
    req["type"] = "status_update";
    req["name"] = m_loginName;
    req["status"] = status;
    NetworkHelper::sendAsync(req);
}
// 拦截窗口关闭事件，安全释放摄像头硬件占用并断开所有子模块
void MainWidget::closeEvent(QCloseEvent* event) {
    event->accept();
    this->hide();
    if (aiThread) {
        aiThread->forceReleaseCamera();
        aiThread->terminate();
        aiThread->wait(500);
    }
    qApp->quit();
}