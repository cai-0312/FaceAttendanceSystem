#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "FaceProcessThread.h"
#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "FaceProcessThread.h"
#include "RecordModule.h"
#include "PunchModule.h"
#include "UserModule.h"
#include "RegisterModule.h"
#include "ProfileModule.h"
#include "ChatModule.h"
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QMessageBox>
#include <QDebug>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#include <QFileDialog>

MainWidget::MainWidget(QString loginName, QString role, QWidget* parent)
    : QWidget(parent), ui(new Ui::MainWidget), m_loginName(loginName), m_role(role)
{
    ui->setupUi(this);
    initDatabase();

    // ⛔ 【核心风控】：如果角色是“普通员工”，直接隐藏第 4 个导航按钮（员工管理，索引为3）
    // 这样做的好处是不用删除 UI 控件，索引也不会乱！
    if (m_role == "普通员工") {
        ui->listWidget_Nav->item(3)->setHidden(true);
    }

    // ========================================================
    // 1. 实例化各个独立模块，并将【真实姓名】分发下去！
    // ========================================================
    recordModule = new RecordModule(ui->tableView_Records, ui->dateEdit_Filter, ui->btn_FilterDate, this);
    recordModule->setCurrentUser(m_loginName); // ★ 数据隔离：考勤记录只查本人

    punchModule = new PunchModule(
        ui->label_Camera_Punch, ui->btn_manualPunch,
        ui->label_MorningTime, ui->label_MorningStatus,
        ui->label_EveningTime, ui->label_EveningStatus,
        ui->btn_RuleSettings, ui->btn_LeaveRequest, ui->btn_LeaveApprove, ui->label_CurrentTime, // ★ 传参增加！
        m_role, m_loginName, this
    );

    // 即使普通员工看不见这个页面，后台依然初始化防崩溃
    userModule = new UserModule(ui->tableView_Users, ui->comboBox_FilterDept, ui->btn_FilterDept, this);

    registerModule = new RegisterModule(this);

    profileModule = new ProfileModule(ui->label_Avatar, ui->label_ProfileName, ui->label_ProfileDept, ui->label_ProfileGender, ui->btn_ChangeAvatar, this);
    profileModule->loadUserProfile(m_loginName); // ★ 数据隔离：个人信息只查本人

    chatModule = new ChatModule(
        ui->listWidget_Contacts, ui->textBrowser_Chat,
        ui->lineEdit_ChatMessage, ui->label_ChatTarget,
        ui->btn_Emoji, ui->btn_Folder, ui->btn_History, ui->btn_MoreOpt,
        ui->lineEdit_ChatSearch, // ★ 新增搜索框指针
        this
    );

    // 实例化 AI 助手模块 
    aIAssistantModule = new AIAssistantModule(
        ui->textBrowser_AI,
        ui->lineEdit_AIInput,
        ui->btn_SendAI,
        ui->btn_ClearAIHistory,
        m_loginName,
        this
    );
    chatModule->connectToServer("127.0.0.1", 9999, m_loginName);

    aiThread = new FaceProcessThread(this);

    // ========================================================
    // 2. 信号总线中心 (保持你原有的逻辑不变)
    // ========================================================
    connect(ui->btn_Export, &QPushButton::clicked, recordModule, &RecordModule::exportToCsv);
    connect(ui->btn_deleteUser, &QPushButton::clicked, userModule, &UserModule::deleteSelectedUser);
    connect(ui->btn_Register, &QPushButton::clicked, registerModule, &RegisterModule::triggerRegistration);
    connect(ui->btn_SendChat, &QPushButton::clicked, chatModule, &ChatModule::sendMessage);
    connect(ui->lineEdit_ChatMessage, &QLineEdit::returnPressed, chatModule, &ChatModule::sendMessage);
    connect(punchModule, &PunchModule::requestSendChat, chatModule, &ChatModule::sendSystemMessage);

    // ========================================================
    // 3. 跨模块高级通信 (保持你原有的逻辑不变)
    // ========================================================
    connect(registerModule, &RegisterModule::startRegistration, aiThread, &FaceProcessThread::requestRegister);
    connect(aiThread, &FaceProcessThread::registerFeatureReady, registerModule, &RegisterModule::onFeatureReady);
    connect(aiThread, &FaceProcessThread::registerFailed, registerModule, &RegisterModule::onRegisterFailed);

    connect(userModule, &UserModule::dataChanged, this, &MainWidget::loadRegisteredUsers);
    connect(registerModule, &RegisterModule::dataChanged, this, &MainWidget::loadRegisteredUsers);

    // ========================================================
    // 4. 左侧导航栏路由（带权限拦截）
    // ========================================================
    connect(ui->listWidget_Nav, &QListWidget::currentRowChanged, this, [=](int index) {
        ui->stackedWidget->setCurrentIndex(index);
        aiThread->setPage(index);

        if (index == 2) {
            recordModule->refreshTable();
        }
        else if (index == 3) {
            // ★ 二次防御：就算代码出 Bug 让普通员工点进来了，也不给他们查数据
            if (m_role == "管理员") {
                userModule->refreshTable();
            }
            else {
                QMessageBox::warning(this, "越权拦截", "您没有权限访问员工管理！");
            }
        }
        else if (index == 4) {
            // 聊天
        }
        else if (index == 5) {
            profileModule->loadUserProfile(m_loginName);
        }
        });

    // ========================================================
    // 5. 画面渲染与打卡风控（零延迟刷新 + 满3次冻结2分钟）
    // ========================================================
    connect(aiThread, &FaceProcessThread::frameReady, this, [=](QImage img, QStringList names) {
        int page = ui->stackedWidget->currentIndex();
        if (page == 0) {
            punchModule->renderFrame(img);
            QDateTime now = QDateTime::currentDateTime();

            for (const QString& name : names) {
                punchModule->updateRecognizedName(name); // 防作弊变量更新

                // 🚀 获取当前识别到的人的风控状态
                PunchState& state = m_punchStates[name];

                // ---------------- 风控算法开始 ----------------

                // 1. 如果已经打了 3 次卡，检查是否还在 2 分钟 (120秒) 的“小黑屋”惩罚期内
                if (state.punchCount >= 3) {
                    if (state.lastPunchTime.isValid() && state.lastPunchTime.secsTo(now) < 120) {
                        continue; // ⛔ 拦截：还在 2 分钟冻结期内，直接无视该人脸！
                    }
                    else {
                        // ✅ 解封：已经过了 2 分钟，清零次数，允许重新打卡
                        state.punchCount = 0;
                    }
                }

                // 2. 基础物理防抖：防止同一秒内摄像头拍到多次，导致1秒内触发3次打卡 (设为 5 秒防抖)
                if (state.lastPunchTime.isValid() && state.lastPunchTime.secsTo(now) < 5) {
                    continue; // 5秒内的重复人脸直接跳过
                }

                // ---------------- 执行打卡并落库 ----------------

                QString autoStatus = punchModule->calculatePunchStatus(now.time()); // 智能计算迟到早退

                QSqlQuery insertQuery;
                insertQuery.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (:name, :time, :status)");
                insertQuery.bindValue(":name", name);
                insertQuery.bindValue(":time", now.toString("yyyy-MM-dd HH:mm:ss"));
                insertQuery.bindValue(":status", autoStatus);

                if (insertQuery.exec()) {
                    // 更新风控状态
                    state.lastPunchTime = now;
                    state.punchCount++; // 成功打卡，次数 +1

                    // ★ 零延迟刷新逻辑：如果AI识别到的是当前登录账号本人，立刻让 UI 变色！
                    if (name == m_loginName) {
                        punchModule->loadTodayPunchStatus(); // 立刻读取并显示在卡片上
                        // 如果刚好是第3次，触发提示（可选）
                        if (state.punchCount == 3) {
                            qDebug() << "警告：已达3次上限，进入2分钟打卡冻结期！";
                        }
                    }
                }
            }
        }
        else if (page == 1) {
            ui->label_Camera_Register->setPixmap(QPixmap::fromImage(img).scaled(ui->label_Camera_Register->size(), Qt::KeepAspectRatio));
        }
        });

    // 💡 保证数据库拥有这些字段（静默升级，防崩溃）
    QSqlQuery alterQ;
    alterQ.exec("ALTER TABLE users ADD COLUMN phone VARCHAR(20) DEFAULT '未设置'");
    alterQ.exec("ALTER TABLE users ADD COLUMN gender VARCHAR(10) DEFAULT '未知'");
    alterQ.exec("ALTER TABLE users ADD COLUMN avatar LONGTEXT");

    // 绑定更改头像按钮
    // 强制先断开之前的连接，防止重复触发双弹窗！
    ui->btn_ChangeAvatar->disconnect();
    connect(ui->btn_ChangeAvatar, &QPushButton::clicked, this, &MainWidget::onBtnChangeAvatarClicked);

    // 每次切换到个人信息页时，刷新数据
    connect(ui->listWidget_Nav, &QListWidget::currentRowChanged, this, [=](int row) {
        if (row == 5) loadProfileInfo(); // 第6个选项卡是个人信息
        });

    // 引擎启动
    loadRegisteredUsers();
    aiThread->start();

    

    
}

MainWidget::~MainWidget() {
    aiThread->stop();
    delete ui;
}

void MainWidget::initDatabase() {
    if (!QSqlDatabase::contains("qt_sql_default_connection")) {
        // ★ 核心改变 1：放弃 QMYSQL，使用万能的 QODBC 引擎！
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC");

        // ★ 核心改变 2：使用 ODBC 专属的连接字符串拼接法则
        // 注意：如果你下载的 ODBC 驱动不是 8.0 版本，请把大括号里的名字改成你实际装的版本号
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
            "SERVER=127.0.0.1;"
            "PORT=3305;"            // 👈 完美匹配你 phpStudy 的端口
            "DATABASE=attendance_db;"
            "UID=root;"
            "PWD=root;");           // 👈 phpStudy 默认密码通常是 root，请核对

        db.setDatabaseName(dsn);

        if (!db.open()) {
            qDebug() << "ODBC Connection Error:" << db.lastError().text();
            QMessageBox::critical(this, "数据库连接失败",
                "ODBC 无法连接到 MySQL！\n" + db.lastError().text());
        }
        else {
            qDebug() << "MySQL(via ODBC) connected successfully!";
        }
    }
}

void MainWidget::loadRegisteredUsers() {
    std::map<QString, cv::Mat> users;
    QSqlQuery query("SELECT name, feature FROM users");
    while (query.next()) {
        QString dbName = query.value(0).toString();
        QByteArray dbFeatureBytes = query.value(1).toByteArray();
        
        // 👇 ★ 核心修复：如果数据库里这个人的 feature 是 Null，直接跳过！不加入 AI 引擎！
        if (dbFeatureBytes.isEmpty() || dbFeatureBytes.isNull()) {
            continue; 
        }

        cv::Mat featureMat(1, 512, CV_32F);
        memcpy(featureMat.data, dbFeatureBytes.constData(), dbFeatureBytes.size());
        users[dbName] = featureMat;
    }
    aiThread->updateRegisteredUsers(users); 
}




// 🚀 加载当前用户的绝美名片资料
void MainWidget::loadProfileInfo() {
    QSqlQuery query;
    query.prepare("SELECT id, department, role, gender, phone, avatar FROM users WHERE name = :name");
    query.bindValue(":name", m_loginName);

    if (query.exec() && query.next()) {
        int rawId = query.value(0).toInt();
        QString formattedId = QString("%1").arg(rawId, 3, 10, QChar('0')); // 001格式化

        ui->label_ProfileName->setText(m_loginName);
        ui->label_ProfileEmpId->setText(formattedId);
        ui->label_ProfileDept->setText(query.value(1).toString());
        ui->label_ProfileRole->setText(query.value(2).toString());
        ui->label_ProfileGender->setText(query.value(3).toString());
        ui->label_ProfilePhone->setText(query.value(4).toString());

        // 加载并渲染头像
        QString avatarBase64 = query.value(5).toString();
        if (!avatarBase64.isEmpty()) {
            QByteArray imgData = QByteArray::fromBase64(avatarBase64.toUtf8());
            QImage img;
            img.loadFromData(imgData);
            QImage circleImg = makeCircularAvatar(img, 120); // 生成 120x120 圆形
            ui->label_Avatar->setPixmap(QPixmap::fromImage(circleImg));
        }
        else {
            // 如果没有上传头像，显示默认文字
            ui->label_Avatar->setText("暂无");
        }
    }
}

// 🚀 1. 终极高清、透明、居中裁剪圆形头像算法
QImage MainWidget::makeCircularAvatar(const QImage& src, int size) {
    // 1. 先进行高质量等比缩放，让短边刚好等于 target size，防止变形
    QImage scaledImg = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    // 2. 取缩放后图片的中心点，抠出一个 1:1 的正方形
    int cropX = (scaledImg.width() - size) / 2;
    int cropY = (scaledImg.height() - size) / 2;
    QImage squareImg = scaledImg.copy(cropX, cropY, size, size);

    // 3. 绘制最终的高清圆形图片（关键点：必须使用 ARGB32_Premultiplied 格式）
    QImage result(size, size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent); // ★ 核心修复：强制用透明底色填充整个画布！

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing); // 开启抗锯齿，边缘更丝滑
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addEllipse(0, 0, size, size); // 在透明画布上画一个圆轨
    painter.setClipPath(path);         // 设置蒙版裁剪

    painter.drawImage(0, 0, squareImg); // 将正方形图片画入这个圆轨内

    return result;
}

// 🚀 2. 更改头像按钮点击事件 (修复了JPG压缩导致四角变黑的问题)
void MainWidget::onBtnChangeAvatarClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择新头像", "", "图片文件 (*.png *.jpg *.jpeg)");
    if (filePath.isEmpty()) return;

    QImage img(filePath);
    if (img.isNull()) {
        QMessageBox::warning(this, "错误", "无法读取该图片！");
        return;
    }

    // 1. UI 界面瞬间更新为完美居中的透明大圆圆 (120x120 像素)
    QImage circleImg = makeCircularAvatar(img, 120);
    ui->label_Avatar->setPixmap(QPixmap::fromImage(circleImg));

    // 2. 将这只透明大圆图像，压缩并转为 Base64 存库！
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    // ★ 核心修复：必须存为 PNG 格式，才能保留四个角的透明！JPG 不支持透明。
    circleImg.save(&buffer, "PNG");
    QString base64Data = QString(ba.toBase64());

    // 3. 同步到 MySQL 数据库
    QSqlQuery updateQuery;
    updateQuery.prepare("UPDATE users SET avatar = :av WHERE name = :name");
    updateQuery.bindValue(":av", base64Data);
    updateQuery.bindValue(":name", m_loginName);

    if (updateQuery.exec()) {
        QMessageBox::information(this, "成功", "圆形头像已智能裁剪并更新！");
    }
    else {
        QMessageBox::critical(this, "错误", "头像同步到数据库失败：" + updateQuery.lastError().text());
    }
}