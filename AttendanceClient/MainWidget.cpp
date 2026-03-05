#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "FaceProcessThread.h"
#include "RecordModule.h"
#include "PunchModule.h"
#include "UserModule.h"
#include "RegisterModule.h"
#include "ProfileModule.h"
#include "ChatModule.h"
#include "AIAssistantModule.h"
#include "HomeModule.h" 

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
#include <QNetworkRequest>
#include <QUrl>
#include <QPushButton> 

MainWidget::MainWidget(QString loginName, QString role, QWidget* parent)
    : QWidget(parent), ui(new Ui::MainWidget), m_loginName(loginName), m_role(role)
{
    ui->setupUi(this);
    initDatabase();

    if (m_role == "普通员工") {
        ui->listWidget_Nav->item(4)->setHidden(true);
    }

    recordModule = new RecordModule(ui->tableView_Records, ui->calendarWidget_Attendance, ui->label_SummaryData, ui->label_DetailDate, ui->lineEdit_SearchName, ui->btn_FilterDate, ui->btn_Export, m_loginName, m_role, this);
    punchModule = new PunchModule(ui->label_Camera_Punch, ui->btn_manualPunch, ui->label_MorningTime, ui->label_MorningStatus, ui->label_EveningTime, ui->label_EveningStatus, ui->btn_RuleSettings, ui->btn_LeaveRequest, ui->btn_LeaveApprove, ui->btn_AppealRequest, ui->btn_AppealApprove, ui->label_CurrentTime, m_role, m_loginName, this);
    userModule = new UserModule(ui->tableView_Users, ui->comboBox_FilterDept, ui->btn_FilterDept, this);
    registerModule = new RegisterModule(this);

    // ==========================================
    // 🚀 核心升级：动态注入“修改资料”按钮
    // ==========================================
    QPushButton* btnEditProfile = new QPushButton("✏️ 修改性别与电话", this);
    btnEditProfile->setMinimumSize(120, 36);
    btnEditProfile->setCursor(Qt::PointingHandCursor);
    btnEditProfile->setStyleSheet("QPushButton { background-color: #3370FF; color: white; border: none; border-radius: 6px; font-weight: bold; } QPushButton:hover { background-color: #4E83FF; }");
    ui->formLayout_Profile->addRow("", btnEditProfile);

    // 修改资料的弹窗逻辑（避开ODBC绑值Bug，直接拼SQL）
    connect(btnEditProfile, &QPushButton::clicked, this, [=]() {
        QDialog dialog(this); dialog.setWindowTitle("修改个人资料"); dialog.resize(300, 150);
        QFormLayout form(&dialog);
        QComboBox* genderCombo = new QComboBox(&dialog); genderCombo->addItems({ "男", "女", "保密" });
        if (ui->label_ProfileGender) genderCombo->setCurrentText(ui->label_ProfileGender->text());
        QLineEdit* phoneEdit = new QLineEdit(&dialog); phoneEdit->setPlaceholderText("请输入新的联系电话");
        if (ui->label_ProfilePhone && ui->label_ProfilePhone->text() != "未设置" && ui->label_ProfilePhone->text() != "-") {
            phoneEdit->setText(ui->label_ProfilePhone->text());
        }
        form.addRow("性别:", genderCombo); form.addRow("联系电话:", phoneEdit);
        QDialogButtonBox buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
        form.addRow(&buttonBox);
        connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            // 精准打击：同时匹配 account 或 name，直接拼装SQL
            QString sql = QString("UPDATE users SET gender = '%1', phone = '%2' WHERE account = '%3' OR name = '%3'")
                .arg(genderCombo->currentText(), phoneEdit->text().trimmed(), m_loginName);
            QSqlQuery query;
            if (query.exec(sql)) {
                QMessageBox::information(this, "成功", "资料修改成功！");
                this->loadProfileInfo(); // 立即刷新界面
            }
            else {
                QMessageBox::critical(this, "失败", "数据库更新失败：" + query.lastError().text());
            }
        }
        });

    profileModule = new ProfileModule(ui->label_Avatar, ui->label_ProfileName, ui->label_ProfileDept, ui->label_ProfileGender, ui->label_ProfilePhone, ui->btn_ChangeAvatar, btnEditProfile, this);
    chatModule = new ChatModule(ui->listWidget_Contacts, ui->textBrowser_Chat, ui->lineEdit_ChatMessage, ui->label_ChatTarget, ui->btn_Emoji, ui->btn_Folder, ui->btn_History, ui->btn_MoreOpt, ui->lineEdit_ChatSearch, this);
    m_aiModule = new AIAssistantModule(ui->textBrowser_AI, ui->lineEdit_AIInput, ui->btn_SendAI, ui->btn_ClearAIHistory, m_loginName, this);
    homeModule = new HomeModule(ui->vL_Pie, ui->vL_Bar, ui->vL_Line, this);
    chatModule->connectToServer("127.0.0.1", 9999, m_loginName);
    aiThread = new FaceProcessThread(this);

    connect(ui->btn_deleteUser, &QPushButton::clicked, userModule, &UserModule::deleteSelectedUser);
    connect(ui->btn_Register, &QPushButton::clicked, registerModule, &RegisterModule::triggerRegistration);
    connect(ui->btn_SendChat, &QPushButton::clicked, chatModule, &ChatModule::sendMessage);
    connect(ui->lineEdit_ChatMessage, &QLineEdit::returnPressed, chatModule, &ChatModule::sendMessage);
    connect(punchModule, &PunchModule::requestSendChat, chatModule, &ChatModule::sendSystemMessage);
    connect(registerModule, &RegisterModule::startRegistration, aiThread, &FaceProcessThread::requestRegister);
    connect(aiThread, &FaceProcessThread::registerFeatureReady, registerModule, &RegisterModule::onFeatureReady);
    connect(aiThread, &FaceProcessThread::registerFailed, registerModule, &RegisterModule::onRegisterFailed);
    connect(userModule, &UserModule::dataChanged, this, &MainWidget::loadRegisteredUsers);
    connect(registerModule, &RegisterModule::dataChanged, this, &MainWidget::loadRegisteredUsers);

    connect(ui->listWidget_Nav, &QListWidget::currentRowChanged, this, [=](int row) {
        ui->stackedWidget->setCurrentIndex(row);
        if (aiThread) aiThread->setPage(row);
        if (row == 0) { homeModule->refreshDashboard(); }
        else if (row == 3) { recordModule->refreshData(); }
        else if (row == 4) {
            if (m_role == "管理员" || m_role == "超级管理员") { userModule->refreshTable(); }
            else { QMessageBox::warning(this, "越权拦截", "您没有权限访问员工管理！"); }
        }
        else if (row == 7) { loadProfileInfo(); }
        });

    connect(aiThread, &FaceProcessThread::frameReady, this, [=](QImage img, QStringList names) {
        int page = ui->stackedWidget->currentIndex();
        if (page == 1) {
            punchModule->renderFrame(img);
            QDateTime now = QDateTime::currentDateTime();
            for (const QString& name : names) {
                punchModule->updateRecognizedName(name);
                PunchState& state = m_punchStates[name];
                if (state.punchCount >= 3) {
                    if (state.lastPunchTime.isValid() && state.lastPunchTime.secsTo(now) < 120) continue;
                    else state.punchCount = 0;
                }
                if (state.lastPunchTime.isValid() && state.lastPunchTime.secsTo(now) < 5) continue;
                QString autoStatus = punchModule->calculatePunchStatus(now.time());
                QSqlQuery insertQuery;
                insertQuery.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (:name, :time, :status)");
                insertQuery.bindValue(":name", name); insertQuery.bindValue(":time", now.toString("yyyy-MM-dd HH:mm:ss")); insertQuery.bindValue(":status", autoStatus);
                if (insertQuery.exec()) {
                    state.lastPunchTime = now; state.punchCount++;
                    if (name == m_loginName) { punchModule->loadTodayPunchStatus(); }
                }
            }
        }
        else if (page == 2) {
            ui->label_Camera_Register->setPixmap(QPixmap::fromImage(img).scaled(ui->label_Camera_Register->size(), Qt::KeepAspectRatio));
        }
        });

    QSqlQuery alterQ;
    alterQ.exec("ALTER TABLE users ADD COLUMN phone VARCHAR(20) DEFAULT '未设置'");
    alterQ.exec("ALTER TABLE users ADD COLUMN gender VARCHAR(10) DEFAULT '未知'");
    alterQ.exec("ALTER TABLE users ADD COLUMN avatar LONGTEXT");
    alterQ.exec("ALTER TABLE users ADD COLUMN status_icon VARCHAR(50) DEFAULT '🟢 在线'");
    m_netManager = new QNetworkAccessManager(this);

    ui->btn_ChangeAvatar->disconnect();
    connect(ui->btn_ChangeAvatar, &QPushButton::clicked, this, &MainWidget::onBtnChangeAvatarClicked);
    connect(ui->comboBox_Status, &QComboBox::currentTextChanged, this, &MainWidget::onStatusChanged);
    connect(ui->btn_ChangePassword, &QPushButton::clicked, this, &MainWidget::onChangePasswordClicked);

    loadRegisteredUsers();
    aiThread->start();
    loadProfileInfo();
    homeModule->refreshDashboard();
}

MainWidget::~MainWidget() {
    aiThread->stop();
    delete ui;
}

void MainWidget::initDatabase() {
    if (!QSqlDatabase::contains("qt_sql_default_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC");
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3305;DATABASE=attendance_db;UID=root;PWD=root;");
        db.setDatabaseName(dsn);
        if (!db.open()) QMessageBox::critical(this, "数据库连接失败", "ODBC 无法连接到 MySQL！\n" + db.lastError().text());
    }
}

void MainWidget::loadRegisteredUsers() {
    std::map<QString, cv::Mat> users;
    QSqlQuery query("SELECT name, feature FROM users");
    while (query.next()) {
        QString dbName = query.value(0).toString();
        QByteArray dbFeatureBytes = query.value(1).toByteArray();
        if (dbFeatureBytes.isEmpty() || dbFeatureBytes.isNull()) continue;
        cv::Mat featureMat(1, 512, CV_32F);
        memcpy(featureMat.data, dbFeatureBytes.constData(), dbFeatureBytes.size());
        users[dbName] = featureMat;
    }
    aiThread->updateRegisteredUsers(users);
}

// 加载当前登录用户的个人资料信息
void MainWidget::loadProfileInfo() {
    // 新增查询 job_title 字段
    QString sql = QString("SELECT id, department, role, gender, phone, avatar, name, job_title FROM users WHERE account = '%1' OR name = '%1'").arg(m_loginName);
    QSqlQuery query(sql);

    if (query.next()) {
        QString formattedId = QString("%1").arg(query.value(0).toInt(), 3, 10, QChar('0'));
        QString realName = query.value(6).toString();
        QString jobTitle = query.value(7).toString(); // 取出企业职务
        QString role = query.value(2).toString();

        ui->label_ProfileName->setText(realName);
        ui->label_ProfileEmpId->setText(formattedId);
        ui->label_ProfileDept->setText(query.value(1).toString());

        // 如果该员工没有配置具体的职务，则降级显示他的权限角色
        ui->label_ProfileRole->setText(jobTitle.isEmpty() || jobTitle == "未分配" ? role : jobTitle);

        QString gender = query.value(3).toString();
        QString phone = query.value(4).toString();
        ui->label_ProfileGender->setText(gender.isEmpty() ? "未知" : gender);
        ui->label_ProfilePhone->setText(phone.isEmpty() ? "未设置" : phone);

        QString avatarBase64 = query.value(5).toString();
        if (!avatarBase64.isEmpty()) {
            QByteArray imgData = QByteArray::fromBase64(avatarBase64.toUtf8());
            QImage img; img.loadFromData(imgData);
            ui->label_Avatar->setPixmap(QPixmap::fromImage(makeCircularAvatar(img, 120)));
        }
        else {
            ui->label_Avatar->setText("暂无头像");
        }

        QString cardData = QString("【员工电子名片】\n姓名: %1\n工号: %2\n部门: %3\n职务: %4").arg(realName, formattedId, query.value(1).toString(), jobTitle);
        generateQRCode(cardData);
    }
}

QImage MainWidget::makeCircularAvatar(const QImage& src, int size) {
    QImage scaledImg = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    int cropX = (scaledImg.width() - size) / 2; int cropY = (scaledImg.height() - size) / 2;
    QImage squareImg = scaledImg.copy(cropX, cropY, size, size);
    QImage result(size, size, QImage::Format_ARGB32_Premultiplied); result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path; path.addEllipse(0, 0, size, size);
    painter.setClipPath(path); painter.drawImage(0, 0, squareImg);
    return result;
}

// ★ 核心修复：更改头像SQL拼装
void MainWidget::onBtnChangeAvatarClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择新头像", "", "图片文件 (*.png *.jpg *.jpeg)");
    if (filePath.isEmpty()) return;
    QImage img(filePath);
    if (img.isNull()) { QMessageBox::warning(this, "错误", "无法读取该图片！"); return; }

    QImage circleImg = makeCircularAvatar(img, 120);
    ui->label_Avatar->setPixmap(QPixmap::fromImage(circleImg));
    QByteArray ba; QBuffer buffer(&ba); buffer.open(QIODevice::WriteOnly); circleImg.save(&buffer, "PNG");

    QString sql = QString("UPDATE users SET avatar = '%1' WHERE account = '%2' OR name = '%2'").arg(QString(ba.toBase64()), m_loginName);
    QSqlQuery updateQuery;
    if (updateQuery.exec(sql)) {
        QMessageBox::information(this, "成功", "圆形头像已智能裁剪并更新！");
    }
}

void MainWidget::onStatusChanged(const QString& status) {
    if (status.isEmpty()) return;
    QString sql = QString("UPDATE users SET status_icon = '%1' WHERE account = '%2' OR name = '%2'").arg(status, m_loginName);
    QSqlQuery query(sql);
}

// ★ 核心修复：更改密码SQL拼装
void MainWidget::onChangePasswordClicked() {
    QDialog dialog(this); dialog.setWindowTitle("修改登录密码"); dialog.resize(300, 200);
    QFormLayout form(&dialog);
    QLineEdit* oldPwdEdit = new QLineEdit(&dialog); oldPwdEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* newPwdEdit = new QLineEdit(&dialog); newPwdEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* confirmPwdEdit = new QLineEdit(&dialog); confirmPwdEdit->setEchoMode(QLineEdit::Password);

    form.addRow("旧密码:", oldPwdEdit); form.addRow("新密码:", newPwdEdit); form.addRow("确认新密码:", confirmPwdEdit);
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        if (newPwdEdit->text() != confirmPwdEdit->text()) { QMessageBox::warning(this, "错误", "两次输入的新密码不一致！"); return; }

        QString sql = QString("SELECT password FROM users WHERE account = '%1' OR name = '%1'").arg(m_loginName);
        QSqlQuery query(sql);

        if (query.next()) {
            if (query.value(0).toString() != oldPwdEdit->text()) {
                QMessageBox::warning(this, "错误", "旧密码输入错误！"); return;
            }
            QString updateSql = QString("UPDATE users SET password = '%1' WHERE account = '%2' OR name = '%2'").arg(newPwdEdit->text(), m_loginName);
            QSqlQuery updateQ;
            if (updateQ.exec(updateSql)) QMessageBox::information(this, "成功", "密码修改成功！");
        }
    }
}

void MainWidget::generateQRCode(const QString& dataStr) {
    QUrl url("https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=" + QUrl::toPercentEncoding(dataStr));
    QNetworkReply* reply = m_netManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            QPixmap pixmap; pixmap.loadFromData(reply->readAll());
            ui->label_QRCode->setPixmap(pixmap);
        }
        reply->deleteLater();
        });
}