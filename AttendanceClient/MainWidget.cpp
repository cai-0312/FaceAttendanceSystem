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
#include "AttendanceClient.h"
// 构造函数，初始化主界面并创建各业务模块
MainWidget::MainWidget(QString loginName, QString role, QWidget* parent, AttendanceClient* loginWindow)
    : QWidget(parent), ui(new Ui::MainWidget), m_loginName(loginName), m_role(role), m_loginWindow(loginWindow)
{
    ui->setupUi(this);
    // 设置导航栏图标
    QString iconBase = "../../AttendanceClient/icon_library/";
    QStringList navIcons = { "Navigation_Bar/nav_home.svg", "Navigation_Bar/nav_punch.svg",
                             "Navigation_Bar/nav_face.svg", "Navigation_Bar/nav_record.svg",
                             "Navigation_Bar/nav_staff.svg", "Navigation_Bar/nav_chat.svg",
                             "Navigation_Bar/nav_ai.svg", "Navigation_Bar/nav_profile.svg" };
    ui->listWidget_Nav->setIconSize(QSize(22, 22));
    for (int i = 0; i < navIcons.size() && i < ui->listWidget_Nav->count(); i++) {
        QIcon icon(iconBase + navIcons[i]);
        if (!icon.isNull()) ui->listWidget_Nav->item(i)->setIcon(icon);
    }
    // 设置打卡页按钮图标
    ui->btn_AppealRequest->setIcon(QIcon(iconBase + "Punch/btn_appeal_request.svg"));
    ui->btn_AppealRequest->setIconSize(QSize(18, 18));
    ui->btn_AppealApprove->setIcon(QIcon(iconBase + "Punch/btn_appeal_approve.svg"));
    ui->btn_AppealApprove->setIconSize(QSize(18, 18));
    ui->btn_LeaveRequest->setIcon(QIcon(iconBase + "Punch/btn_leave_request.svg"));
    ui->btn_LeaveRequest->setIconSize(QSize(18, 18));
    ui->btn_LeaveApprove->setIcon(QIcon(iconBase + "Punch/btn_leave_approve.svg"));
    ui->btn_LeaveApprove->setIconSize(QSize(18, 18));
    ui->btn_RuleSettings->setIcon(QIcon(iconBase + "Punch/btn_rule.svg"));
    ui->btn_RuleSettings->setIconSize(QSize(18, 18));
    ui->btn_manualPunch->setIcon(QIcon(iconBase + "Punch/btn_manual_punch.svg"));
    ui->btn_manualPunch->setIconSize(QSize(18, 18));
    // 设置人脸录入页按钮图标
    ui->btn_Register->setIcon(QIcon(iconBase + "MainWidget/btn_camera.svg"));
    ui->btn_Register->setIconSize(QSize(18, 18));
    // 设置考勤图例
    if (ui->leg1) { ui->leg1->setText("<span style='color:#00B42A; font-size:16px;'>●</span> 正常"); }
    if (ui->leg2) { ui->leg2->setText("<span style='color:#F53F3F; font-size:16px;'>●</span> 异常"); }
    if (ui->leg3) { ui->leg3->setText("<span style='color:#3370FF; font-size:16px;'>●</span> 请假"); }
    // 设置数据简报标题图标
    if (ui->label_SummaryTitle) {
        QPixmap reportIcon = QIcon(iconBase + "Record/icon_report.svg").pixmap(20, 20);
        ui->label_SummaryTitle->setPixmap(reportIcon);
        ui->label_SummaryTitle->setText("");
    }
    // 在标题前插入图标
    if (ui->label_SummaryTitle && ui->label_SummaryTitle->parentWidget()) {
        QLayout* parentLay = ui->label_SummaryTitle->parentWidget()->layout();
        QHBoxLayout* hLay = qobject_cast<QHBoxLayout*>(parentLay);
        if (hLay) {
            QLabel* reportIconLabel = new QLabel(this);
            reportIconLabel->setPixmap(QIcon(iconBase + "Record/icon_report.svg").pixmap(20, 20));
            reportIconLabel->setFixedSize(24, 24);
            reportIconLabel->setStyleSheet("border:none;");
            hLay->insertWidget(0, reportIconLabel);
            ui->label_SummaryTitle->setText("考勤数据简报");
        }
    }
    if (ui->label_HomeTitle) {
        ui->label_HomeTitle->setText(
            "<img src='../../AttendanceClient/icon_library/Home/icon_home_visual_data.svg' width='26' height='26' align='middle'> "
            "<span style='font-family: \"Microsoft YaHei\"; font-size: 22px; font-weight: bold; color: #1F2329;'>"
            "考勤数据可视化控制台</span>"
        );
    }
    // 查询当前用户的部门和真实姓名
    QJsonObject uReq;
    uReq["type"] = "query_user_dept";
    uReq["name"] = m_loginName;
    QJsonObject uRes = NetworkHelper::request(uReq);
    QString dept = uRes["department"].toString();
    if (uRes.contains("real_name")) {
        m_loginName = uRes["real_name"].toString();
    }
    // 按权限隐藏无访问权限的菜单项
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
    // 创建考勤记录模块
    recordModule = new RecordModule(ui->tableView_Records, ui->calendarWidget_Attendance,
        ui->label_SummaryData, ui->label_DetailDate, ui->lineEdit_SearchName,
        ui->btn_FilterDate, ui->btn_Export, m_loginName, m_role, this);
    // 创建打卡模块
    punchModule = new PunchModule(ui->label_Camera_Punch, ui->btn_manualPunch,
        ui->label_MorningTime, ui->label_MorningStatus, ui->label_EveningTime, ui->label_EveningStatus,
        ui->btn_RuleSettings, ui->btn_LeaveRequest, ui->btn_LeaveApprove, ui->btn_AppealRequest,
        ui->btn_AppealApprove, ui->label_CurrentTime, m_role, m_loginName, this);
    // 创建员工管理模块
    userModule = new UserModule(ui->tableView_Users, ui->comboBox_FilterDept, ui->btn_FilterDept, this);
    // 创建人脸录入模块
    registerModule = new RegisterModule(ui->label_Camera_Register, this);
    // 创建智能问答模块
    m_aiModule = new AIAssistantModule(ui->textBrowser_AI, ui->lineEdit_AIInput, ui->btn_SendAI, ui->btn_ClearAIHistory, m_loginName, this);
    // 创建首页大屏模块
    QString jobTitle = uRes["job_title"].toString();
    homeModule = new HomeModule(ui->verticalLayout_Home, m_role, m_loginName, dept, jobTitle, this);
    // 创建聊天模块并连接服务器
    chatModule = new ChatModule(ui->listWidget_Contacts, ui->textBrowser_Chat, ui->lineEdit_ChatMessage,
        ui->label_ChatTarget, ui->btn_Emoji, ui->btn_Folder, ui->btn_History, ui->btn_MoreOpt, ui->lineEdit_ChatSearch, this);
    chatModule->connectToServer(NetworkHelper::serverIp(), NetworkHelper::serverPort(), m_loginName);
    // 创建个人资料修改按钮
    QPushButton* btnEditProfile = new QPushButton(" 修改性别与电话", this);
    btnEditProfile->setIcon(QIcon(iconBase + "MainWidget/btn_edit.svg"));
    btnEditProfile->setIconSize(QSize(18, 18));
    if (ui->formLayout_Profile) {
        ui->formLayout_Profile->addRow("", btnEditProfile);
    }
    // 创建个人资料模块
    profileModule = new ProfileModule(ui->label_Avatar, ui->label_ProfileName, ui->label_ProfileDept,
        ui->label_ProfileGender, ui->label_ProfilePhone, ui->btn_ChangeAvatar, btnEditProfile, this);
    // 创建系统广播按钮
    QPushButton* btnBroadcast = new QPushButton(" 发布系统广播", this);
    btnBroadcast->setIcon(QIcon(iconBase + "MainWidget/btn_broadcast.svg"));
    btnBroadcast->setIconSize(QSize(18, 18));
    btnBroadcast->setMinimumSize(130, 35);
    btnBroadcast->setCursor(Qt::PointingHandCursor);
    btnBroadcast->setStyleSheet("QPushButton { background-color: #F56C6C; color: white; border-radius: 6px; font-weight: bold; } QPushButton:hover { background-color: #F78989; }");
    if (m_role != "管理员登录") {
        btnBroadcast->hide();
    }
    // 将广播按钮放入主页顶部
    if (ui->verticalLayout_Home && ui->label_HomeTitle) {
        QHBoxLayout* topHomeLayout = new QHBoxLayout();
        topHomeLayout->addWidget(ui->label_HomeTitle);
        topHomeLayout->addStretch();
        topHomeLayout->addWidget(btnBroadcast);
        QLayoutItem* oldTitleItem = ui->verticalLayout_Home->takeAt(0);
        if (oldTitleItem) delete oldTitleItem;
        ui->verticalLayout_Home->insertLayout(0, topHomeLayout);
    }
    // 绑定广播发布事件
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
    // 监听广播并弹出通知
    connect(chatModule, &ChatModule::broadcastReceived, this, [=](QString from, QString msg) {
        NoticePopup* popup = new NoticePopup(QString("系统公告 (发布者: %1)").arg(from), msg);
        popup->showAnimation();
        });
    // 启动人脸处理线程
    aiThread = new FaceProcessThread(this);
    aiThread->setCurrentUser(m_loginName);
    // 绑定界面操作事件
    connect(ui->btn_deleteUser, &QPushButton::clicked, userModule, &UserModule::deleteSelectedUser);
    connect(ui->btn_Register, &QPushButton::clicked, registerModule, &RegisterModule::triggerRegistration);
    connect(ui->btn_SendChat, &QPushButton::clicked, chatModule, &ChatModule::sendMessage);
    connect(ui->lineEdit_ChatMessage, &QLineEdit::returnPressed, chatModule, &ChatModule::sendMessage);
    // 将考勤消息推送到聊天中
    connect(punchModule, &PunchModule::requestSendChat, this, [this](QString msg) {
        chatModule->sendSystemMessage(m_loginName, msg);
        });
    // 绑定人脸录入事件
    connect(registerModule, &RegisterModule::startRegistration, aiThread, &FaceProcessThread::requestRegister);
    connect(aiThread, &FaceProcessThread::registerFeatureReady, registerModule, &RegisterModule::onFeatureReady);
    connect(aiThread, &FaceProcessThread::registerFailed, registerModule, &RegisterModule::onRegisterFailed);
    // 数据变化后延迟刷新特征库
    connect(registerModule, &RegisterModule::dataChanged, this, [this]() {
        QTimer::singleShot(2000, this, &MainWidget::loadRegisteredUsers);
        });
    connect(userModule, &UserModule::dataChanged, this, &MainWidget::loadRegisteredUsers);
    // 绑定首页快捷操作
    connect(homeModule, &HomeModule::requestQuickLeave, punchModule, &PunchModule::onLeaveRequestClicked);
    connect(homeModule, &HomeModule::requestQuickAppeal, punchModule, &PunchModule::onAppealRequestClicked);
    connect(homeModule, &HomeModule::requestApproveLeave, punchModule, &PunchModule::onLeaveApproveClicked);
    connect(homeModule, &HomeModule::requestApproveAppeal, punchModule, &PunchModule::onAppealApproveClicked);
    // 个人资料页跳转到人脸录入页
    connect(profileModule, &ProfileModule::requestFaceReRegister, this, [=](QString currentName) {
        ui->listWidget_Nav->setCurrentRow(2);
        ui->stackedWidget->setCurrentIndex(2);
        QMessageBox::information(this, "准备就绪", "请核对名字后，点击【开始录入】进行人脸覆写！");
        });
    connect(aiThread, &FaceProcessThread::cameraConnected, profileModule, &ProfileModule::updateCameraId);
    // 处理导航切换并刷新对应页面
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
    // 处理人脸线程返回的画面和识别结果
    connect(aiThread, &FaceProcessThread::frameReady, this, [=](QImage img, QStringList names, QByteArray currentFeatureBytes) {
        int currentPage = ui->stackedWidget->currentIndex();
        if (currentPage == 1) {
            punchModule->renderFrame(img);
                // 缓存当前人脸特征
            if (!currentFeatureBytes.isEmpty()) {
                punchModule->updateCurrentFaceFeature(currentFeatureBytes);
            }
                // 更新识别到的姓名显示
            for (const QString& name : names) {
                punchModule->updateRecognizedName(name);
                // 过滤非本人人员
                if (name == "未知访客" || name == "非本人" || name != m_loginName) {
                    continue;
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
    connect(aiThread, &FaceProcessThread::internalPunchSuccess, this, [=](QString name, QDateTime time) {
        int currentPage = ui->stackedWidget->currentIndex();
        if (currentPage != 1) return; // 不在打卡页面不发包
        // 防止底层误发，再次核验必须是本人
        if (name == "未知访客" || name == "非本人" || name != m_loginName) return;
        // 提取实时加密的特征矩阵字节流
        QByteArray secureFeature = punchModule->getCurrentFeatureBytes();
        if (secureFeature.isEmpty()) return;
        //打卡数据包仅包含加密的 Base64 特征，绝不传递明文姓名或判定结果
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

    // 监听在线状态变化
    if (ui->comboBox_Status) {
        connect(ui->comboBox_Status, &QComboBox::currentTextChanged, this, &MainWidget::onStatusChanged);
    }
    // 创建网络管理对象
    m_netManager = new QNetworkAccessManager(this);
    // 启动线程并加载初始数据
    if (aiThread) aiThread->start();
    loadRegisteredUsers();
    // 启动时加载个人资料和首页数据
    if (profileModule) profileModule->loadUserProfile(m_loginName);
    if (homeModule) homeModule->refreshDashboard();
    // 启动首页定时刷新
    QTimer* dashboardTimer = new QTimer(this);
    connect(dashboardTimer, &QTimer::timeout, this, [this]() {
        // 仅首页激活时刷新
        if (ui->stackedWidget->currentIndex() == 0 && homeModule) {
            homeModule->refreshDashboard();
        }
        });
    dashboardTimer->start(30000);
    // 创建退出登录按钮
    QPushButton* btnLogout = new QPushButton("  退出登录", this);
    btnLogout->setIcon(QIcon(iconBase + "Navigation_Bar/nav_logout.svg"));
    btnLogout->setIconSize(QSize(22, 22));
    btnLogout->setCursor(Qt::PointingHandCursor);
    btnLogout->setFixedHeight(50);
    QFont navFont("Microsoft YaHei", 11);
    navFont.setBold(true);
    btnLogout->setFont(navFont);
    btnLogout->setStyleSheet(
        "QPushButton { background-color: transparent; color: #8F959E; border: none; "
        "border-radius: 8px; padding-left: 5px; "
        "text-align: left; margin: 0px 8px 12px 8px; }"
        "QPushButton:hover { background-color: #2B2F3A; color: #E8EAEF; }"
    );
    // 用容器包裹导航栏和退出按钮
    QHBoxLayout* mainHLayout = qobject_cast<QHBoxLayout*>(this->layout());
    if (mainHLayout) {
        mainHLayout->removeWidget(ui->listWidget_Nav);
        QWidget* navContainer = new QWidget(this);
        navContainer->setFixedWidth(170);
        navContainer->setStyleSheet("background-color: #1C1F2A;");
        QVBoxLayout* navVLay = new QVBoxLayout(navContainer);
        navVLay->setContentsMargins(0, 0, 0, 0);
        navVLay->setSpacing(0);
        navVLay->addWidget(ui->listWidget_Nav, 1);
        navVLay->addWidget(btnLogout, 0);
        // 放回主布局左侧
        mainHLayout->insertWidget(0, navContainer);
    }
    connect(btnLogout, &QPushButton::clicked, this, [this]() {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("退出登录");
        msgBox.setText("确定要退出当前账号吗？\n退出后需要重新登录。");
        msgBox.setIcon(QMessageBox::Question);
        QPushButton* btnYes = msgBox.addButton("确定", QMessageBox::YesRole);
        QPushButton* btnNo = msgBox.addButton("取消", QMessageBox::NoRole);
        msgBox.setDefaultButton(btnNo);
        msgBox.exec();
        if (msgBox.clickedButton() == btnYes) {
            if (aiThread) {
                aiThread->forceReleaseCamera();
                aiThread->stop();
            }
            if (chatModule) {
                QMetaObject::invokeMethod(chatModule, "disconnectFromServer", Qt::DirectConnection);
            }
            this->hide();
            if (m_loginWindow) {
                m_loginWindow->showLoginReady();
            }
            else {
                qApp->quit();
            }
        }
        });
}
// 析构函数，释放线程和界面资源
MainWidget::~MainWidget() {
    if (aiThread) aiThread->stop();
    delete ui;
}
// 拉取人脸特征库并发送给线程
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
// 上报当前在线状态
void MainWidget::onStatusChanged(const QString& status) {
    if (status.isEmpty()) return;
    QJsonObject req;
    req["type"] = "status_update";
    req["name"] = m_loginName;
    req["status"] = status;
    NetworkHelper::sendAsync(req);
}
// 关闭窗口时释放资源
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
// 强制切换到指定导航页
void MainWidget::forceNavigateTo(int navIndex) {
    if (ui->listWidget_Nav && navIndex >= 0 && navIndex < ui->listWidget_Nav->count()) {
        ui->listWidget_Nav->setCurrentRow(navIndex);
        ui->stackedWidget->setCurrentIndex(navIndex);
    }
}