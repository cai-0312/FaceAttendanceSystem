#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "NetworkHelper.h"
#define NOMINMAX
#include <windows.h>
#include <QMessageBox>
#include <QDebug>
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

MainWidget::MainWidget(QString loginName, QString role, QWidget* parent)
    : QWidget(parent), ui(new Ui::MainWidget), m_loginName(loginName), m_role(role)
{
    ui->setupUi(this);
    QJsonObject uReq;
    uReq["type"] = "query_user_dept";
    uReq["name"] = m_loginName;
    QJsonObject uRes = NetworkHelper::request(uReq);

    QString dept = uRes["department"].toString();
    if (uRes.contains("real_name")) m_loginName = uRes["real_name"].toString();

    if (dept != "人力资源部") {
        if (ui->listWidget_Nav->item(4)) {
            ui->listWidget_Nav->item(4)->setHidden(true);
        }
    }

    if (m_role == "普通登录") {
        ui->listWidget_Nav->item(4)->setHidden(true);
    }
    QString originalAccount = loginName;

    recordModule = new RecordModule(ui->tableView_Records, ui->calendarWidget_Attendance, ui->label_SummaryData, ui->label_DetailDate, ui->lineEdit_SearchName, ui->btn_FilterDate, ui->btn_Export, m_loginName, m_role, this);
    punchModule = new PunchModule(ui->label_Camera_Punch, ui->btn_manualPunch, ui->label_MorningTime, ui->label_MorningStatus, ui->label_EveningTime, ui->label_EveningStatus, ui->btn_RuleSettings, ui->btn_LeaveRequest, ui->btn_LeaveApprove, ui->btn_AppealRequest, ui->btn_AppealApprove, ui->label_CurrentTime, m_role, m_loginName, this);
    userModule = new UserModule(ui->tableView_Users, ui->comboBox_FilterDept, ui->btn_FilterDept, this);
    registerModule = new RegisterModule(ui->label_Camera_Register, this);
    m_aiModule = new AIAssistantModule(ui->textBrowser_AI, ui->lineEdit_AIInput, ui->btn_SendAI, ui->btn_ClearAIHistory, m_loginName, this);
    homeModule = new HomeModule(ui->verticalLayout_Home, m_role, m_loginName, this);

    // 注意：聊天模块内部有单独的 TCP 长连接，使用 NetworkHelper::serverIp() 获取统一配置的地址
    chatModule = new ChatModule(ui->listWidget_Contacts, ui->textBrowser_Chat, ui->lineEdit_ChatMessage, ui->label_ChatTarget, ui->btn_Emoji, ui->btn_Folder, ui->btn_History, ui->btn_MoreOpt, ui->lineEdit_ChatSearch, this);
    chatModule->connectToServer(NetworkHelper::serverIp(), NetworkHelper::serverPort(), m_loginName);

    QPushButton* btnEditProfile = new QPushButton("✏️ 修改性别与电话", this);
    if (ui->formLayout_Profile) {
        ui->formLayout_Profile->addRow("", btnEditProfile);
    }

    profileModule = new ProfileModule(ui->label_Avatar, ui->label_ProfileName, ui->label_ProfileDept, ui->label_ProfileGender, ui->label_ProfilePhone, ui->btn_ChangeAvatar, btnEditProfile, this);

    QPushButton* btnBroadcast = new QPushButton("📢 发布系统广播", this);
    btnBroadcast->setMinimumSize(130, 35);
    btnBroadcast->setCursor(Qt::PointingHandCursor);
    btnBroadcast->setStyleSheet("QPushButton { background-color: #F56C6C; color: white; border-radius: 6px; font-weight: bold; } QPushButton:hover { background-color: #F78989; }");

    if (m_role != "管理员登录" && m_role != "超级管理员") {
        btnBroadcast->hide();
    }

    if (ui->verticalLayout_Home && ui->label_HomeTitle) {
        QHBoxLayout* topHomeLayout = new QHBoxLayout();
        topHomeLayout->addWidget(ui->label_HomeTitle);
        topHomeLayout->addStretch();
        topHomeLayout->addWidget(btnBroadcast);
        QLayoutItem* oldTitleItem = ui->verticalLayout_Home->takeAt(0);
        if (oldTitleItem) delete oldTitleItem;
        ui->verticalLayout_Home->insertLayout(0, topHomeLayout);
    }

    // 🚀 核心改造 2：发布系统公告不再写入数据库，交由服务器记录
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

                // ✅ 替换为统一的异步发送
                NetworkHelper::sendAsync(req);

                QMessageBox::information(this, "发送成功", "全员广播已下发，并已同步至首页系统公告看板！");
                if (homeModule) homeModule->refreshDashboard();
            }
        }
        });

    connect(chatModule, &ChatModule::broadcastReceived, this, [=](QString from, QString msg) {
        NoticePopup* popup = new NoticePopup(QString("系统公告 (发布者: %1)").arg(from), msg);
        popup->showAnimation();
        });

    aiThread = new FaceProcessThread(this);
    aiThread->setCurrentUser(m_loginName);
    connect(ui->btn_deleteUser, &QPushButton::clicked, userModule, &UserModule::deleteSelectedUser);
    connect(ui->btn_Register, &QPushButton::clicked, registerModule, &RegisterModule::triggerRegistration);
    connect(ui->btn_SendChat, &QPushButton::clicked, chatModule, &ChatModule::sendMessage);
    connect(ui->lineEdit_ChatMessage, &QLineEdit::returnPressed, chatModule, &ChatModule::sendMessage);
    connect(punchModule, &PunchModule::requestSendChat, this, [this](QString msg) {chatModule->sendSystemMessage(m_loginName, msg); });
    connect(registerModule, &RegisterModule::startRegistration, aiThread, &FaceProcessThread::requestRegister);
    connect(aiThread, &FaceProcessThread::registerFeatureReady, registerModule, &RegisterModule::onFeatureReady);
    connect(aiThread, &FaceProcessThread::registerFailed, registerModule, &RegisterModule::onRegisterFailed);

    connect(registerModule, &RegisterModule::dataChanged, this, [this]() {
        QTimer::singleShot(2000, this, &MainWidget::loadRegisteredUsers);
        });
    connect(userModule, &UserModule::dataChanged, this, &MainWidget::loadRegisteredUsers);

    connect(homeModule, &HomeModule::requestQuickLeave, punchModule, &PunchModule::onLeaveRequestClicked);
    connect(homeModule, &HomeModule::requestQuickAppeal, punchModule, &PunchModule::onAppealRequestClicked);
    connect(homeModule, &HomeModule::requestApproveLeave, punchModule, &PunchModule::onLeaveApproveClicked);
    connect(homeModule, &HomeModule::requestApproveAppeal, punchModule, &PunchModule::onAppealApproveClicked);

    connect(profileModule, &ProfileModule::requestFaceReRegister, this, [=](QString currentName) {
        ui->listWidget_Nav->setCurrentRow(2);
        ui->stackedWidget->setCurrentIndex(2);
        QMessageBox::information(this, "准备就绪", "请在右侧输入框核对名字后，点击【开始录入】进行人脸特征覆写！\n(名字已自动填入)");
        if (ui->lineEdit_RegisterName) ui->lineEdit_RegisterName->setText(currentName);
        });

    connect(ui->listWidget_Nav, &QListWidget::currentRowChanged, this, [=](int row) {
        ui->stackedWidget->setCurrentIndex(row);
        if (aiThread) {
            if (row == 1) aiThread->setPage(0);
            else if (row == 2) aiThread->setPage(1);
            else aiThread->setPage(-1);
        }

        if (row == 0) homeModule->refreshDashboard();
        else if (row == 3) {
            recordModule->refreshData();
        }
        else if (row == 4) {
            if (m_role == "管理员登录" || m_role == "超级管理员" || m_role == "经理") userModule->refreshTable();
            else QMessageBox::warning(this, "越权拦截", "您没有权限访问员工管理！");
        }
        else if (row == 7) {
            // 每次点击“个人中心”都会主动刷一次最新的数据
            profileModule->loadUserProfile(m_loginName);
        }
        });

    connect(aiThread, &FaceProcessThread::frameReady, this, [=](QImage img, QStringList names) {
        int currentPage = ui->stackedWidget->currentIndex();
        if (currentPage == 1) {
            punchModule->renderFrame(img);
            QDateTime now = QDateTime::currentDateTime();
            for (const QString& name : names) {
                punchModule->updateRecognizedName(name);
                if (name == "未知访客" || name == "非本人" || name != m_loginName) continue;

                PunchState& state = m_punchStates[name];
                if (state.punchCount >= 3) {
                    if (state.lastPunchTime.isValid() && state.lastPunchTime.secsTo(now) < 120) continue;
                    else state.punchCount = 0;
                }
                if (state.lastPunchTime.isValid() && state.lastPunchTime.secsTo(now) < 5) continue;

                QJsonObject req;
                req["type"] = "punch_request";
                req["name"] = name;

                // ✅ 替换为统一的异步发送
                NetworkHelper::sendAsync(req);

                state.lastPunchTime = now;
                state.punchCount++;

                if (name == m_loginName) {
                    QTimer::singleShot(500, punchModule, &PunchModule::loadTodayPunchStatus);
                }
            }
        }
        else if (currentPage == 2) {
            if (ui->label_Camera_Register) {
                ui->label_Camera_Register->setPixmap(QPixmap::fromImage(img).scaled(
                    ui->label_Camera_Register->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
        });

    connect(ui->comboBox_Status, &QComboBox::currentTextChanged, this, &MainWidget::onStatusChanged);

    m_netManager = new QNetworkAccessManager(this);

    if (aiThread) aiThread->start();
    loadRegisteredUsers();

    // 🚀 致命修复：启动时触发个人信息加载
    if (profileModule) profileModule->loadUserProfile(m_loginName);
    if (homeModule) homeModule->refreshDashboard();
}

MainWidget::~MainWidget() {
    if (aiThread) aiThread->stop();
    delete ui;
}

// 🚀 核心改造 3：通过 TCP 从服务器下载人脸特征库（防止多线程崩溃）
void MainWidget::loadRegisteredUsers() {
    QJsonObject req;
    req["type"] = "query_face_features";

    // ✅ 替换为使用统一的 NetworkHelper 请求（能安全接收超大体积的人脸特征库）
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
            qDebug() << "✅ 安全同步完成：人脸特征库已加载，当前总人数：" << users.size();
        }
    }
}

void MainWidget::onStatusChanged(const QString& status) {
    if (status.isEmpty()) return;

    QJsonObject req;
    req["type"] = "status_update";
    req["name"] = m_loginName;
    req["status"] = status;

    // ✅ 替换为统一的异步发送
    NetworkHelper::sendAsync(req);
}

void MainWidget::closeEvent(QCloseEvent* event) {
    event->accept();
    this->hide();

    // 安全释放摄像头
    if (aiThread) {
        aiThread->forceReleaseCamera();
        aiThread->terminate();
        aiThread->wait(500);
    }

    // 🚀 安全退出修复：抛弃粗暴的 TerminateProcess，改用 Qt 官方的事件循环退出
    // 防止直接杀进程导致的 TCP Socket 报 "远程主机强迫关闭了一个现有的连接" 错误
    qApp->quit();
}