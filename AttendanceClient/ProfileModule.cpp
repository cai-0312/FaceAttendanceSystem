#include "ProfileModule.h"
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSettings>
#include <QCheckBox>
#include <QBuffer>
#include <QFormLayout>
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QLineEdit>
#include <QPdfWriter>
#include <QPainter>
#include <QPainterPath>
#include <QPageLayout>
#include <QPageSize>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QRandomGenerator> 
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 🚀 核心通讯工具：保证数据同步完整到达
static QJsonObject requestDataFromServer(const QJsonObject& jsonRequest) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", 9999);
    QJsonObject responseJson;
    if (socket.waitForConnected(2000)) {
        QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);
        socket.waitForBytesWritten(1000);
        if (socket.waitForReadyRead(5000)) {
            QByteArray responseData;
            while (socket.waitForReadyRead(50) || socket.bytesAvailable() > 0) {
                responseData += socket.readAll();
                if (responseData.endsWith("\n")) break;
            }
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isNull()) responseJson = doc.object();
        }
        socket.disconnectFromHost();
    }
    return responseJson;
}

static void sendCommandToServer(const QJsonObject& json) {
    requestDataFromServer(json);
}

ProfileModule::ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel,
    QLabel* genderLabel, QLabel* phoneLabel,
    QPushButton* avatarBtn, QPushButton* editBtn, QObject* parent)
    : QObject(parent), m_avatarLabel(avatarLabel), m_nameLabel(nameLabel),
    m_deptLabel(deptLabel), m_genderLabel(genderLabel), m_phoneLabel(phoneLabel),
    m_avatarBtn(avatarBtn), m_editBtn(editBtn)
{
    if (m_avatarLabel) {
        m_avatarLabel->setCursor(Qt::PointingHandCursor);
        m_avatarLabel->installEventFilter(this);
    }
    if (m_avatarBtn) connect(m_avatarBtn, &QPushButton::clicked, this, &ProfileModule::onChangeAvatarClicked);

    injectAdvancedUI();
}

bool ProfileModule::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (watched == m_avatarLabel) {
            onChangeAvatarClicked();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void ProfileModule::injectAdvancedUI() {
    if (!m_editBtn || !m_editBtn->parentWidget()) return;
    QWidget* parentW = m_editBtn->parentWidget();

    if (m_editBtn) m_editBtn->hide();
    QPushButton* oldPwdBtn = parentW->window()->findChild<QPushButton*>("btn_ChangePassword");
    if (oldPwdBtn) oldPwdBtn->hide();
    if (m_avatarBtn) m_avatarBtn->hide();

    if (m_genderLabel) {
        m_genderLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        m_genderLabel->setOpenExternalLinks(false);
        connect(m_genderLabel, &QLabel::linkActivated, this, [this]() { onEditGenderClicked(); });
    }
    if (m_phoneLabel) {
        m_phoneLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        m_phoneLabel->setOpenExternalLinks(false);
        connect(m_phoneLabel, &QLabel::linkActivated, this, [this]() { onEditPhoneClicked(); });
    }

    QVBoxLayout* mainLay = qobject_cast<QVBoxLayout*>(parentW->layout());
    if (!mainLay) return;
    if (parentW->findChild<QGridLayout*>("AdvancedGridLay")) return;

    QGridLayout* gridLay = new QGridLayout();
    gridLay->setObjectName("AdvancedGridLay");
    gridLay->setSpacing(15);
    gridLay->setContentsMargins(5, 20, 5, 5);

    QString baseStyle =
        "QPushButton {"
        "   color: #FFFFFF;"
        "   border-radius: 6px;"
        "   font-family: 'Microsoft YaHei';"
        "   font-size: 15px;"
        "   font-weight: bold;"
        "   min-height: 42px;"
        "   border: none;"
        "}"
        "QPushButton:pressed {"
        "   padding-top: 2px;"
        "   padding-left: 2px;"
        "}";

    m_pwdBtn = new QPushButton("🔑 修改登录密码");
    m_pwdBtn->setCursor(Qt::PointingHandCursor);
    m_pwdBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pwdBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #F56C6C; } QPushButton:hover { background-color: #F78989; }");

    m_faceBtn = new QPushButton("🔄 重新采集人脸");
    m_faceBtn->setCursor(Qt::PointingHandCursor);
    m_faceBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_faceBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #67C23A; } QPushButton:hover { background-color: #85CE61; }");

    m_exportPdfBtn = new QPushButton("🖨️ 导出个人入职档案");
    m_exportPdfBtn->setCursor(Qt::PointingHandCursor);
    m_exportPdfBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_exportPdfBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #E6A23C; } QPushButton:hover { background-color: #EBB563; }");

    m_settingsBtn = new QPushButton("🎨 客户端设置");
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    m_settingsBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_settingsBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #722ED1; } QPushButton:hover { background-color: #8D51DE; }");

    gridLay->addWidget(m_pwdBtn, 0, 0);
    gridLay->addWidget(m_faceBtn, 0, 1);
    gridLay->addWidget(m_exportPdfBtn, 1, 0);
    gridLay->addWidget(m_settingsBtn, 1, 1);

    mainLay->addLayout(gridLay);

    connect(m_pwdBtn, &QPushButton::clicked, this, &ProfileModule::onChangePasswordClicked);
    connect(m_faceBtn, &QPushButton::clicked, this, &ProfileModule::onReRegisterFaceClicked);
    connect(m_exportPdfBtn, &QPushButton::clicked, this, &ProfileModule::onExportProfilePdfClicked);
    connect(m_settingsBtn, &QPushButton::clicked, this, &ProfileModule::onPreferencesClicked);
}

// 🚀 核心改造 1：向服务器拉取个人信息与巨型头像数据
void ProfileModule::loadUserProfile(const QString& username) {
    m_currentUser = username;
    if (m_currentUser.isEmpty()) return;

    QJsonObject req;
    req["type"] = "query_user_profile";
    req["name"] = username;
    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        QString formattedId = QString("%1").arg(res["id"].toInt(), 3, 10, QChar('0'));
        QString jobTitle = res["job_title"].toString();
        QString role = res["role"].toString();
        QString dept = res["department"].toString();
        QString genderStr = res["gender"].toString().isEmpty() ? "未知" : res["gender"].toString();
        QString phoneStr = res["phone"].toString().isEmpty() ? "未设置" : res["phone"].toString();
        QString realName = res["real_name"].toString();
        if (realName.isEmpty()) realName = username;

        if (m_nameLabel) m_nameLabel->setText("👤 姓名: " + realName);
        if (m_deptLabel) m_deptLabel->setText(dept);
        if (m_genderLabel) m_genderLabel->setText(QString("%1 &nbsp;<a href='edit_gender' style='color:#1456F0; text-decoration:none; font-size:13px;'>[✎修改]</a>").arg(genderStr));
        if (m_phoneLabel) m_phoneLabel->setText(QString("%1 &nbsp;<a href='edit_phone' style='color:#1456F0; text-decoration:none; font-size:13px;'>[✎修改]</a>").arg(phoneStr));

        QWidget* window = m_avatarLabel ? m_avatarLabel->window() : nullptr;
        if (window) {
            QLabel* idLabel = window->findChild<QLabel*>("label_ProfileEmpId");
            if (idLabel) idLabel->setText(formattedId);
            QLabel* roleLabel = window->findChild<QLabel*>("label_ProfileRole");
            if (roleLabel) roleLabel->setText(jobTitle.isEmpty() || jobTitle == "未分配" ? role : jobTitle);
        }

        // 异步二维码
        QString cardData = QString("【员工信息】\n姓名: %1\n工号: %2\n部门: %3\n职务: %4\n电话: %5").arg(realName, formattedId, dept, jobTitle, phoneStr);
        QNetworkAccessManager* mgr = new QNetworkAccessManager(this);
        QUrl url("http://api.qrserver.com/v1/create-qr-code/?size=150x150&data=" + QUrl::toPercentEncoding(cardData));
        QNetworkReply* reply = mgr->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, mgr, window]() {
            if (reply->error() == QNetworkReply::NoError) {
                QPixmap pixmap;
                pixmap.loadFromData(reply->readAll());
                if (window) {
                    QLabel* qrLabel = window->findChild<QLabel*>("label_QRCode");
                    if (qrLabel) qrLabel->setPixmap(pixmap);
                }
            }
            reply->deleteLater();
            mgr->deleteLater();
            });

        // 渲染服务器返回的 Base64 头像
        QString avatarBase64 = res["avatar_base64"].toString();
        if (!avatarBase64.isEmpty()) {
            QFuture<QImage> future = QtConcurrent::run([avatarBase64]() -> QImage {
                QImage result;
                QImage img;
                img.loadFromData(QByteArray::fromBase64(avatarBase64.toUtf8()));
                if (!img.isNull()) {
                    int size = 130;
                    QImage scaledImg = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    int cropX = (scaledImg.width() - size) / 2;
                    int cropY = (scaledImg.height() - size) / 2;
                    QImage squareImg = scaledImg.copy(cropX, cropY, size, size);
                    result = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
                    result.fill(Qt::transparent);
                    QPainter painter(&result);
                    painter.setRenderHint(QPainter::Antialiasing);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform);
                    QPainterPath path;
                    path.addEllipse(0, 0, size, size);
                    painter.setClipPath(path);
                    painter.drawImage(0, 0, squareImg);
                    painter.end();
                }
                return result;
                });

            QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(this);
            connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher]() {
                QImage resImg = watcher->result();
                if (!resImg.isNull() && m_avatarLabel) {
                    m_avatarLabel->setPixmap(QPixmap::fromImage(resImg));
                }
                else if (m_avatarLabel) {
                    QPixmap defaultAvatar(130, 130); defaultAvatar.fill(Qt::darkCyan);
                    m_avatarLabel->setPixmap(defaultAvatar);
                }
                watcher->deleteLater();
                });
            watcher->setFuture(future);
        }
        else if (m_avatarLabel) {
            QPixmap defaultAvatar(130, 130); defaultAvatar.fill(Qt::darkCyan);
            m_avatarLabel->setPixmap(defaultAvatar);
        }
    }
}

// 🚀 核心改造 2：通过网络修改性别
void ProfileModule::onEditGenderClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("修改性别");
    dialog.resize(250, 100);
    QFormLayout form(&dialog);
    QComboBox* genderCombo = new QComboBox(&dialog);
    genderCombo->addItems({ "男", "女", "保密" });
    form.addRow("性别:", genderCombo);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QJsonObject req;
        req["type"] = "update_profile_field";
        req["name"] = m_currentUser;
        req["field"] = "gender";
        req["value"] = genderCombo->currentText();
        sendCommandToServer(req);
        loadUserProfile(m_currentUser);
    }
}

// 🚀 核心改造 3：通过网络修改电话
void ProfileModule::onEditPhoneClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("修改联系电话");
    dialog.resize(280, 100);
    QFormLayout form(&dialog);
    QLineEdit* phoneEdit = new QLineEdit(&dialog);
    phoneEdit->setPlaceholderText("请输入新手机号");
    form.addRow("手机号:", phoneEdit);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QJsonObject req;
        req["type"] = "update_profile_field";
        req["name"] = m_currentUser;
        req["field"] = "phone";
        req["value"] = phoneEdit->text().trimmed();
        sendCommandToServer(req);
        loadUserProfile(m_currentUser);
    }
}

// 🚀 核心改造 4：通过网络验证并修改密码
void ProfileModule::onChangePasswordClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("修改登录密码");
    dialog.resize(320, 200);
    QFormLayout form(&dialog);
    QLineEdit* oldPwdEdit = new QLineEdit(&dialog); oldPwdEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* newPwdEdit = new QLineEdit(&dialog); newPwdEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* confirmPwdEdit = new QLineEdit(&dialog); confirmPwdEdit->setEchoMode(QLineEdit::Password);
    form.addRow("旧密码:", oldPwdEdit);
    form.addRow("新密码:", newPwdEdit);
    form.addRow("确认新密码:", confirmPwdEdit);
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        if (newPwdEdit->text().isEmpty() || newPwdEdit->text() != confirmPwdEdit->text()) {
            QMessageBox::warning((QWidget*)this->parent(), "错误", "密码为空或两次输入不一致！"); return;
        }

        QJsonObject req;
        req["type"] = "verify_and_update_password";
        req["name"] = m_currentUser;
        req["old_pwd"] = oldPwdEdit->text();
        req["new_pwd"] = newPwdEdit->text();
        QJsonObject res = requestDataFromServer(req);

        if (res["status"].toString() == "success") {
            QMessageBox::information((QWidget*)this->parent(), "成功", "密码修改成功！下次登录生效。");
        }
        else {
            QMessageBox::warning((QWidget*)this->parent(), "错误", res["msg"].toString());
        }
    }
}

// 🚀 核心改造 5：上传巨型头像数据走 TCP
void ProfileModule::onChangeAvatarClicked() {
    if (m_currentUser.isEmpty()) return;
    QString filePath = QFileDialog::getOpenFileName((QWidget*)this->parent(), "选择个人写真/头像", "", "图片文件 (*.png *.jpg *.jpeg *.bmp)");
    if (filePath.isEmpty()) return;
    QPixmap newAvatar;
    if (!newAvatar.load(filePath)) {
        QMessageBox::warning((QWidget*)this->parent(), "错误", "无法加载该图片，请检查格式！");
        return;
    }

    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    newAvatar.save(&buffer, "PNG");

    QJsonObject req;
    req["type"] = "update_profile_field";
    req["name"] = m_currentUser;
    req["field"] = "avatar";
    req["value"] = QString(bytes.toBase64());
    sendCommandToServer(req);

    QMessageBox::information((QWidget*)this->parent(), "成功", "专属头像已上传并持久化保存。");
    loadUserProfile(m_currentUser);
}

void ProfileModule::onExportProfilePdfClicked() {
    if (m_currentUser.isEmpty()) return;

    QString defaultName = QString("%1_入职登记表.pdf").arg(m_currentUser);
    QString filePath = QFileDialog::getSaveFileName((QWidget*)this->parent(),
        "导出个人入职档案", defaultName, "*.pdf");

    if (filePath.isEmpty()) return;

    QPdfWriter writer(filePath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(20, 20, 20, 20));
    writer.setResolution(300);

    QPainter painter(&writer);
    if (!painter.isActive()) return;

    int width = writer.width();
    int height = writer.height();
    int margin = 200;

    // 为了 PDF，再请求一次基础信息保证不缺数据
    QJsonObject req;
    req["type"] = "query_user_profile";
    req["name"] = m_currentUser;
    QJsonObject res = requestDataFromServer(req);

    QString cleanGender = res["gender"].toString().isEmpty() ? "未知" : res["gender"].toString();
    QString cleanPhone = res["phone"].toString().isEmpty() ? "未设置" : res["phone"].toString();
    QString cleanDept = res["department"].toString();

    painter.setFont(QFont("Microsoft YaHei", 24, QFont::Bold));
    painter.drawText(0, 0, width, 400, Qt::AlignCenter, "员工入职登记档案");

    painter.setPen(QPen(Qt::black, 5));
    painter.drawRect(margin, 500, width - 2 * margin, 2000);

    painter.setPen(QPen(Qt::black, 2));
    for (int i = 1; i <= 4; ++i) {
        int y = 500 + i * 400;
        painter.drawLine(margin, y, width - margin, y);
    }
    painter.drawLine(margin + 500, 500, margin + 500, 2500);
    painter.drawLine(width - 800, 500, width - 800, 1300);

    painter.setFont(QFont("Microsoft YaHei", 12));
    auto drawCellLabel = [&](int row, QString text) {
        painter.drawText(margin + 50, 500 + row * 400, 400, 400, Qt::AlignVCenter, text);
        };
    auto drawCellValue = [&](int row, QString text) {
        painter.drawText(margin + 550, 500 + row * 400, 1000, 400, Qt::AlignVCenter, text);
        };

    drawCellLabel(0, "真实姓名"); drawCellValue(0, m_currentUser);
    drawCellLabel(1, "所属部门"); drawCellValue(1, cleanDept);
    drawCellLabel(2, "联系电话"); drawCellValue(2, cleanPhone);
    drawCellLabel(3, "员工性别"); drawCellValue(3, cleanGender);

    int randomNum = QRandomGenerator::global()->bounded(1000, 10000);
    drawCellLabel(4, "档案编号"); drawCellValue(4, QString("EMP-2026-%1").arg(randomNum));

    QPixmap avatar;
    if (m_avatarLabel && !m_avatarLabel->pixmap().isNull()) {
        avatar = m_avatarLabel->pixmap();
        QRect photoRect(width - 750, 550, 600, 700);
        painter.drawPixmap(photoRect, avatar);
        painter.setPen(QPen(Qt::black, 2));
        painter.drawRect(photoRect);
    }
    else {
        painter.drawText(width - 750, 500, 600, 800, Qt::AlignCenter, "（未上传照片）");
    }

    painter.setFont(QFont("Microsoft YaHei", 10, QFont::Normal));
    painter.setPen(Qt::gray);
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    painter.drawText(margin, 2600, width - 2 * margin, 200, Qt::AlignRight, "打印时间：" + timeStr);
    painter.drawText(margin, 2600, width - 2 * margin, 200, Qt::AlignLeft, "注：此档案由人脸识别考勤系统自动生成");

    painter.end();

    QMessageBox::information((QWidget*)this->parent(), "导出成功",
        "员工档案已成功导出为 PDF，可直接连接打印机打印。");
}

void ProfileModule::onReRegisterFaceClicked() {
    if (QMessageBox::question((QWidget*)this->parent(), "重新录入人脸", "为了提高识别率，建议您重新录入人脸特征。\n\n点击【确认】将跳转，原有人脸特征将被覆盖。") == QMessageBox::Yes) {
        emit requestFaceReRegister(m_currentUser);
    }
}

void ProfileModule::onPreferencesClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("🎨 客户端偏好设置");
    dialog.resize(300, 150);
    QVBoxLayout layout(&dialog);
    QSettings settings("config.ini", QSettings::IniFormat);
    bool voiceEnabled = settings.value("Preferences/VoiceEnabled", true).toBool();
    bool popupEnabled = settings.value("Preferences/PopupEnabled", true).toBool();
    QCheckBox* voiceCheck = new QCheckBox("🔊 开启打卡成功/失败语音播报");
    voiceCheck->setChecked(voiceEnabled);
    QCheckBox* popupCheck = new QCheckBox("📢 接收系统全局广播弹窗");
    popupCheck->setChecked(popupEnabled);
    layout.addWidget(voiceCheck);
    layout.addWidget(popupCheck);
    QDialogButtonBox buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    layout.addWidget(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        settings.setValue("Preferences/VoiceEnabled", voiceCheck->isChecked());
        settings.setValue("Preferences/PopupEnabled", popupCheck->isChecked());
        QMessageBox::information((QWidget*)this->parent(), "设置已同步", "配置已存入本地 config.ini，重启后依然生效。");
    }
}