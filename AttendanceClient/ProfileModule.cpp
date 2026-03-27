#include "ProfileModule.h"
#include "NetworkHelper.h" 
#include <QFileDialog>
#include <QInputDialog>
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
#include "qrcodegen.hpp"
#include <QTextEdit>
#include <QUrl>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
// 构造函数：初始化组件指针并绑定基础的鼠标点击事件过滤器
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
    if (m_avatarBtn) {
        connect(m_avatarBtn, &QPushButton::clicked, this, &ProfileModule::onChangeAvatarClicked);
    }
    injectAdvancedUI();
}
// 拦截事件：处理用户点击头像标签时的响应逻辑
bool ProfileModule::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (watched == m_avatarLabel) {
            onChangeAvatarClicked();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
// 动态重构UI界面：隐藏静态界面上的多余按钮，并动态生成高级操作网格面板
void ProfileModule::injectAdvancedUI() {
    if (!m_editBtn || !m_editBtn->parentWidget()) return;
    QWidget* parentW = m_editBtn->parentWidget();
    QComboBox* statusCombo = parentW->window()->findChild<QComboBox*>("comboBox_Status");
    if (statusCombo) {
        statusCombo->clear(); 
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_online.svg"), "在线");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_meeting.svg"), "会议中");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_not_disturb.svg"), "请勿打扰");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_on_business_trip.svg"), "出差中");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_on_vacation.svg"), "休假中");
        statusCombo->setIconSize(QSize(16, 16)); 
        statusCombo->setCursor(Qt::PointingHandCursor);
    }
    QLabel* qrTitleLabel = parentW->window()->findChild<QLabel*>("label_QRTitle");
    if (qrTitleLabel) {
        qrTitleLabel->setText("<img src='../../AttendanceClient/icon_library/Profile/icon_phone.svg' width='14' height='14' align='middle'> 扫一扫 查看联系人");
    }
    if (m_editBtn) m_editBtn->hide();
    QPushButton* oldPwdBtn = parentW->window()->findChild<QPushButton*>("btn_ChangePassword");
    if (oldPwdBtn) oldPwdBtn->hide();
    if (m_avatarBtn) m_avatarBtn->hide();
    // 绑定富文本超链接事件，实现属性修改的轻量化交互
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
    QString baseStyle = "QPushButton {""   color: #FFFFFF;""   border-radius: 6px;""   font-family: 'Microsoft YaHei';""   font-size: 15px;""   font-weight: bold;""   min-height: 42px;""   border: none;""}""QPushButton:pressed {""   padding-top: 2px;""   padding-left: 2px;""}";
    m_pwdBtn = new QPushButton(" 修改登录密码");
    m_pwdBtn->setIcon(QIcon("../../AttendanceClient/icon_library/Profile/btn_password.svg"));
    m_pwdBtn->setIconSize(QSize(18, 18));
    m_pwdBtn->setCursor(Qt::PointingHandCursor);
    m_pwdBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pwdBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #F56C6C; } QPushButton:hover { background-color: #F78989; }");
    m_faceBtn = new QPushButton(" 重新采集人脸");
    m_faceBtn->setIcon(QIcon("../../AttendanceClient/icon_library/Profile/btn_face_redo.svg"));
    m_faceBtn->setIconSize(QSize(18, 18));
    m_faceBtn->setCursor(Qt::PointingHandCursor);
    m_faceBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_faceBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #67C23A; } QPushButton:hover { background-color: #85CE61; }");
    m_exportPdfBtn = new QPushButton(" 导出个人入职档案");
    m_exportPdfBtn->setIcon(QIcon("../../AttendanceClient/icon_library/Profile/btn_export_personal.svg"));
    m_exportPdfBtn->setIconSize(QSize(18, 18));
    m_exportPdfBtn->setCursor(Qt::PointingHandCursor);
    m_exportPdfBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_exportPdfBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #E6A23C; } QPushButton:hover { background-color: #EBB563; }");
    m_settingsBtn = new QPushButton(" 系统设置");
    m_settingsBtn->setIcon(QIcon("../../AttendanceClient/icon_library/Profile/btn_preference.svg"));
    m_settingsBtn->setIconSize(QSize(18, 18));
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
// 核心业务：向服务器请求个人信息档案并完成前端界面的同步渲染
void ProfileModule::loadUserProfile(const QString& username) {
    m_currentUser = username;
    // 验证当前上下文中的用户标识是否合法传递
    if (m_currentUser.isEmpty()) {
        if (m_nameLabel) m_nameLabel->setText("错误：登录名丢失，未能获取数据！");
        return;
    }
    if (m_nameLabel) m_nameLabel->setText("正在努力拉取数据...");
    QJsonObject req;
    req["type"] = "query_user_profile";
    req["name"] = username;
    QJsonObject res = NetworkHelper::request(req);
    // 验证网络层响应结果的完整性
    if (res.isEmpty()) {
        if (m_nameLabel) m_nameLabel->setText("错误：网络断开或服务器未响应！");
        return;
    }
    // 校验服务端业务层级是否返回正常状态
    if (res["status"].toString() != "success") {
        QString errMsg = res["msg"].toString();
        if (errMsg.isEmpty()) errMsg = "未知数据库错误，请检查服务端状态！";
        if (m_nameLabel) m_nameLabel->setText(QString("<font color='red'>服务端拒绝: %1</font>").arg(errMsg));
        return;
    }
    // 解析档案数据字段
    QString formattedId = QString("%1").arg(res["id"].toInt(), 3, 10, QChar('0'));
    QString jobTitle = res["job_title"].toString();
    QString role = res["role"].toString();
    QString dept = res["department"].toString();
    QString genderStr = res["gender"].toString().isEmpty() ? "未知" : res["gender"].toString();
    QString phoneStr = res["phone"].toString().isEmpty() ? "未设置" : res["phone"].toString();
    QString realName = res["real_name"].toString();
    if (realName.isEmpty()) realName = username;
    QString iconPath = "";
    int iconSize = 24; 
    if (role.contains("管理员")) {
        iconPath = "../../AttendanceClient/icon_library/Profile/icon_admin.svg";
        iconSize = 30; 
    }
    else {
        iconPath = "../../AttendanceClient/icon_library/Profile/icon_staff.svg";
        iconSize = 24; 
    }
    if (m_nameLabel) {
        m_nameLabel->setText(QString(
            "<img src='%1' width='%2' height='%3' align='middle'>&nbsp;"
            "<span style='font-family: \"Microsoft YaHei\"; font-size: 20px; font-weight: bold; color: #1F2329;'>"
            "姓名: %4</span>").arg(iconPath).arg(iconSize).arg(iconSize).arg(realName));
    }
    if (m_deptLabel) m_deptLabel->setText(dept);
    if (m_genderLabel) {
        m_genderLabel->setText(QString("%1 &nbsp;<a href='edit_gender' style='color:#1456F0; text-decoration:none; font-size:13px;'>"
            "<img src='../../AttendanceClient/icon_library/Profile/btn_edit.svg' width='14' height='14' align='middle'> 修改</a>").arg(genderStr));
    }
    if (m_phoneLabel) {
        m_phoneLabel->setText(QString("%1 &nbsp;<a href='edit_phone' style='color:#1456F0; text-decoration:none; font-size:13px;'>"
            "<img src='../../AttendanceClient/icon_library/Profile/btn_edit.svg' width='14' height='14' align='middle'> 修改</a>").arg(phoneStr));
    }
    QWidget* window = m_avatarLabel ? m_avatarLabel->window() : nullptr;
    if (window) {
        QLabel* idLabel = window->findChild<QLabel*>("label_ProfileEmpId");
        if (idLabel) idLabel->setText(formattedId);
        QLabel* roleLabel = window->findChild<QLabel*>("label_ProfileRole");
        if (roleLabel) roleLabel->setText(jobTitle.isEmpty() || jobTitle == "未分配" ? role : jobTitle);
    }
    QString cardData = QString::fromUtf8(
        "══════════════\n"
        "  员工电子名片\n"
        "══════════════\n"
        "姓名: %1\n"
        "工号: %2\n"
        "部门: %3\n"
        "职务: %4\n"
        "电话: %5\n"
        "══════════════")
        .arg(realName, formattedId, dept, jobTitle, phoneStr);
    {
        using namespace qrcodegen;
        QrCode qr = QrCode::encodeText(cardData.toUtf8().constData(), QrCode::Ecc::LOW);
        int sz = qr.getSize();
        int cell = qMax(3, 160 / (sz + 8));
        int border = 4;
        int imgPx = (sz + border * 2) * cell;
        QImage qrImg(imgPx, imgPx, QImage::Format_RGB32);
        qrImg.fill(Qt::white);
        QPainter qrP(&qrImg);
        qrP.setPen(Qt::NoPen);
        qrP.setBrush(Qt::black);
        for (int y = 0; y < sz; y++)
            for (int x = 0; x < sz; x++)
                if (qr.getModule(x, y))
                    qrP.drawRect((x + border) * cell, (y + border) * cell, cell, cell);
        qrP.end();
        if (window) {
            QLabel* qrLabel = window->findChild<QLabel*>("label_QRCode");
            if (qrLabel) qrLabel->setPixmap(QPixmap::fromImage(qrImg));
        }
    }
    // ===== 问题4：头像加载（优先文件路径，兼容旧Base64）=====
    QString avatarPath = res["avatar_path"].toString();
    QString avatarBase64 = res["avatar_base64"].toString();
    bool hasFilePath = !avatarPath.isEmpty() && avatarPath.contains("/") && !avatarPath.startsWith("/9j/");
    if (hasFilePath) {
        QJsonObject avReq;
        avReq["type"] = "query_avatar_file";
        avReq["avatar_path"] = avatarPath;
        QJsonObject avRes = NetworkHelper::request(avReq);
        if (avRes["status"].toString() == "success") avatarBase64 = avRes["avatar_data"].toString();
    }
    if (!avatarBase64.isEmpty()) {
        QFuture<QImage> future = QtConcurrent::run([avatarBase64]() -> QImage {
            QImage result, img;
            img.loadFromData(QByteArray::fromBase64(avatarBase64.toUtf8()));
            if (!img.isNull()) {
                int size = 130;
                QImage scaledImg = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                QImage squareImg = scaledImg.copy((scaledImg.width() - size) / 2, (scaledImg.height() - size) / 2, size, size);
                result = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
                result.fill(Qt::transparent);
                QPainter painter(&result);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                QPainterPath path; path.addEllipse(0, 0, size, size);
                painter.setClipPath(path);
                painter.drawImage(0, 0, squareImg);
                painter.end();
            }
            return result;
            });
        QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher]() {
            QImage resImg = watcher->result();
            if (!resImg.isNull() && m_avatarLabel) { m_avatarLabel->setPixmap(QPixmap::fromImage(resImg)); m_avatarLabel->setScaledContents(true); }
            else if (m_avatarLabel) { QPixmap d(130, 130); d.fill(Qt::darkCyan); m_avatarLabel->setPixmap(d); }
            watcher->deleteLater();
            });
        watcher->setFuture(future);
    }
    else if (m_avatarLabel) { QPixmap d(130, 130); d.fill(Qt::darkCyan); m_avatarLabel->setPixmap(d); }
}
// 弹出对应表单修改用户性别属性并同步至服务端
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
        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() != "success") {
            QMessageBox::warning((QWidget*)this->parent(), "错误", "更新失败: " + res["msg"].toString());
        }
        loadUserProfile(m_currentUser);
    }
}
// 弹出对应表单校验并修改手机号码字段
void ProfileModule::onEditPhoneClicked() {
    bool ok;
    QInputDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("修改手机号");
    dialog.setLabelText("请输入11位手机号:");
    dialog.setOkButtonText("确认修改");
    dialog.setCancelButtonText("取消");
    // 配置输入校验器，限制手机号输入格式必须为纯数字且最高11位长度
    QLineEdit* lineEdit = dialog.findChild<QLineEdit*>();
    if (lineEdit) {
        QRegularExpression regx("[0-9]{11}");
        QValidator* validator = new QRegularExpressionValidator(regx, lineEdit);
        lineEdit->setValidator(validator);
        lineEdit->setMaxLength(11);
    }
    if (dialog.exec() == QDialog::Accepted) {
        QString newPhone = dialog.textValue().trimmed();
        // 提交前执行最终的长度合法性逻辑拦截
        if (newPhone.length() != 11) {
            QMessageBox::warning((QWidget*)this->parent(), "格式错误", "手机号必须为11位数字！");
            return;
        }
        QJsonObject req;
        req["type"] = "update_profile_field";
        req["name"] = m_currentUser;
        req["field"] = "phone";
        req["value"] = newPhone;
        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() == "success") {
            QMessageBox::information((QWidget*)this->parent(), "成功", "联系电话已更新。");
            loadUserProfile(m_currentUser);
        }
        else {
            QMessageBox::warning((QWidget*)this->parent(), "错误", "更新失败: " + res["msg"].toString());
        }
    }
}
// 弹出交互窗口修改系统登录密码
void ProfileModule::onChangePasswordClicked() {
    QWidget* pw = (QWidget*)this->parent();
    QDialog dialog(pw);
    dialog.setWindowTitle("修改登录密码");
    dialog.resize(360, 140);
    QFormLayout form(&dialog);
    QLineEdit* oldPwdEdit = new QLineEdit(&dialog); oldPwdEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* newPwdEdit = new QLineEdit(&dialog); newPwdEdit->setEchoMode(QLineEdit::Password);
    newPwdEdit->setPlaceholderText("至少8位，必须包含字母和数字");
    QLineEdit* confirmPwdEdit = new QLineEdit(&dialog); confirmPwdEdit->setEchoMode(QLineEdit::Password);
    form.addRow("旧密码:", oldPwdEdit);
    form.addRow("新密码:", newPwdEdit);
    form.addRow("确认新密码:", confirmPwdEdit);
    QLabel* ruleLabel = new QLabel("密码规则: 长度≥8位, 必须同时包含字母和数字");
    ruleLabel->setStyleSheet("color:#909399;font-size:11px;");
    form.addRow("", ruleLabel);
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QString oldPwd = oldPwdEdit->text(), newPwd = newPwdEdit->text();
        if (oldPwd.isEmpty()) { QMessageBox::warning(pw, "错误", "请输入旧密码！"); return; }
        if (newPwd.isEmpty() || newPwd != confirmPwdEdit->text()) { QMessageBox::warning(pw, "错误", "新密码为空或两次不一致！"); return; }
        if (newPwd.length() < 8) { QMessageBox::warning(pw, "密码强度不足", "新密码长度必须至少8位！"); return; }
        bool hasL = false, hasD = false;
        for (const QChar& ch : newPwd) { if (ch.isLetter()) hasL = true; if (ch.isDigit()) hasD = true; }
        if (!hasL || !hasD) { QMessageBox::warning(pw, "密码强度不足", "新密码必须同时包含字母和数字！"); return; }
        if (newPwd == oldPwd) { QMessageBox::warning(pw, "错误", "新密码不能与旧密码相同！"); return; }
        QJsonObject req;
        req["type"] = "verify_and_update_password";
        req["name"] = m_currentUser;
        req["old_pwd"] = oldPwd;
        req["new_pwd"] = newPwd;
        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() == "success") QMessageBox::information(pw, "成功", "密码修改成功！下次登录请使用新密码。");
        else { QString e = res["msg"].toString(); QMessageBox::warning(pw, "修改失败", e.isEmpty() ? "服务端未响应" : e); }
    }
}
// ===== 问题4：头像上传改为文件系统存储 =====
void ProfileModule::onChangeAvatarClicked() {
    if (m_currentUser.isEmpty()) return;
    QWidget* pw = (QWidget*)this->parent();
    QString filePath = QFileDialog::getOpenFileName(pw, "选择个人头像", "", "图片文件 (*.png *.jpg *.jpeg *.bmp)");
    if (filePath.isEmpty()) return;
    QImage img;
    if (!img.load(filePath)) { QMessageBox::warning(pw, "错误", "无法加载该图片！"); return; }
    QImage scaledImg = img.scaled(256, 256, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    int side = qMin(scaledImg.width(), scaledImg.height());
    QImage squareImg = scaledImg.copy((scaledImg.width() - side) / 2, (scaledImg.height() - side) / 2, side, side);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    squareImg.save(&buffer, "JPG", 70);

    // 客户端本地保存一份到项目源码目录下的 avatars/姓名/
    QString localDir = QCoreApplication::applicationDirPath() + "/../../AttendanceClient/avatars/" + m_currentUser + "/";
    QDir().mkpath(localDir);
    QString localPath = localDir + m_currentUser + ".jpg";
    QFile localFile(localPath);
    if (localFile.open(QIODevice::WriteOnly)) {
        localFile.write(bytes);
        localFile.close();
    }

    // 发送给服务端存储
    QJsonObject req;
    req["type"] = "upload_avatar_file";
    req["name"] = m_currentUser;
    req["avatar_data"] = QString(bytes.toBase64());
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() != "success") QMessageBox::warning(pw, "错误", "头像上传失败: " + res["msg"].toString());
    else QMessageBox::information(pw, "成功", "头像已修改。");
    loadUserProfile(m_currentUser);
}
// 调度底层打印引擎，将用户的档案记录以原生绘图模式导出为PDF电子文件
void ProfileModule::onExportProfilePdfClicked() {
    if (m_currentUser.isEmpty()) return;
    QString defaultName = QString("%1_入职登记表.pdf").arg(m_currentUser);
    QString filePath = QFileDialog::getSaveFileName((QWidget*)this->parent(),
        "导出个人入职档案", defaultName, "*.pdf");
    if (filePath.isEmpty()) return;
    // 初始化PDF记录器参数
    QPdfWriter writer(filePath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(20, 20, 20, 20));
    writer.setResolution(300);
    QPainter painter(&writer);
    if (!painter.isActive()) return;
    int width = writer.width();
    int height = writer.height();
    int margin = 200;
    QJsonObject req;
    req["type"] = "query_user_profile";
    req["name"] = m_currentUser;
    QJsonObject res = NetworkHelper::request(req);
    QString cleanGender = res["gender"].toString().isEmpty() ? "未知" : res["gender"].toString();
    QString cleanPhone = res["phone"].toString().isEmpty() ? "未设置" : res["phone"].toString();
    QString cleanDept = res["department"].toString();
    // 绘制表头与基础结构框架
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
    // 定义动态填表绘图闭包函数
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
    // 叠加绘制用户照片视图，未命中时展示占位文本
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
    // 附印表尾页脚信息
    painter.setFont(QFont("Microsoft YaHei", 10, QFont::Normal));
    painter.setPen(Qt::gray);
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    painter.drawText(margin, 2600, width - 2 * margin, 200, Qt::AlignRight, "打印时间：" + timeStr);
    painter.drawText(margin, 2600, width - 2 * margin, 200, Qt::AlignLeft, "注：此档案由系统自动生成");
    painter.end();
    QMessageBox::information((QWidget*)this->parent(), "导出成功",
        "员工档案已成功导出为 PDF");
}
// ===== 问题2：人脸重采必须走审批流（管理员直接重采）=====
void ProfileModule::onReRegisterFaceClicked() {
    QWidget* pw = (QWidget*)this->parent();

    // 统一使用 query_approval_candidates 获取角色信息和审批人（与PunchModule一致）
    QJsonObject cReq; cReq["type"] = "query_approval_candidates"; cReq["name"] = m_currentUser;
    QJsonObject cRes = NetworkHelper::request(cReq);
    if (cRes.isEmpty() || !cRes.contains("my_role")) {
        QMessageBox::critical(pw, "错误", "无法获取审批信息，请检查网络！");
        return;
    }

    QString role = cRes["my_role"].toString();
    QString dept = cRes["my_dept"].toString();
    QString jobTitle = cRes["my_job"].toString();
    QJsonArray hrArr = cRes["hr_list"].toArray();
    QJsonArray gmArr = cRes["gm_list"].toArray();
    QJsonArray mgrArr = cRes["mgr_list"].toArray();

    QDialog dialog(pw);
    dialog.setWindowTitle("人脸重录申请单");
    dialog.resize(500, 450);
    QFormLayout* form = new QFormLayout(&dialog);
    form->setContentsMargins(25, 25, 25, 25);
    form->setSpacing(15);

    QString modernInputStyle =
        "QComboBox, QTextEdit { border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 10px; background: white; color: #606266; font-size: 13px; min-height: 28px; }"
        "QComboBox:hover, QTextEdit:hover { border-color: #C0C4CC; }"
        "QComboBox:focus, QTextEdit:focus { border-color: #409EFF; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 25px; border-left: none; }"
        "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #909399; }";

    QLabel* tip = new QLabel("人脸特征重采集需经审批通过后方可执行。\n请填写申请理由并选择审批人。");
    tip->setStyleSheet("color: #E6A23C; font-size: 16px; margin-bottom: 8px; line-height: 1.5;");
    form->addRow(tip);

    QTextEdit* reasonEdit = new QTextEdit(&dialog);
    reasonEdit->setPlaceholderText("请输入人脸重录原因（如：面部变化、原照片质量差等）");
    reasonEdit->setMaximumHeight(80);
    reasonEdit->setStyleSheet(modernInputStyle);
    form->addRow("申请理由:", reasonEdit);

    auto fillFromArray = [](QComboBox* cb, const QJsonArray& arr, const QString& prefix) {
        cb->clear();
        for (int i = 0; i < arr.size(); ++i) {
            QString name = arr[i].toString();
            cb->addItem(prefix + name, name);
        }
        };

    QComboBox* app1 = new QComboBox(&dialog), * app2 = new QComboBox(&dialog), * app3 = new QComboBox(&dialog);
    app1->setStyleSheet(modernInputStyle);
    app2->setStyleSheet(modernInputStyle);
    app3->setStyleSheet(modernInputStyle);

    auto fillHR = [&](QComboBox* cb) { fillFromArray(cb, hrArr, "人资经理: "); };
    auto fillGM = [&](QComboBox* cb) { fillFromArray(cb, gmArr, "总经理: "); };
    auto fillDeptMgr = [&](QComboBox* cb) { fillFromArray(cb, mgrArr, "部门经理: "); };

    int levels = 0;
    if (dept == "总经办" && jobTitle == "总经理") {
        levels = 1; fillHR(app1);
        form->addRow("第一审批人(人资经理):", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (dept == "总经办") {
        levels = 2; fillGM(app1); fillHR(app2);
        form->addRow("第一审批人(总经理):", app1);
        form->addRow("第二审批人(人资经理):", app2);
        app3->setVisible(false);
    }
    else if (dept == "人力资源部" && jobTitle == "部门经理") {
        levels = 1; fillGM(app1);
        form->addRow("第一审批人(总经理):", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (dept == "人力资源部") {
        levels = 2; fillGM(app1); fillHR(app2);
        form->addRow("第一审批人(总经理):", app1);
        form->addRow("第二审批人(人资经理):", app2);
        app3->setVisible(false);
    }
    else if (jobTitle == "部门经理") {
        levels = 2; fillGM(app1); fillHR(app2);
        form->addRow("第一审批人(总经理):", app1);
        form->addRow("第二审批人(人资经理):", app2);
        app3->setVisible(false);
    }
    else {
        levels = 3; fillDeptMgr(app1); fillGM(app2); fillHR(app3);
        form->addRow("第一审批人(部门经理):", app1);
        form->addRow("第二审批人(总经理):", app2);
        form->addRow("第三审批人(人资经理):", app3);
    }

    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    bb->button(QDialogButtonBox::Ok)->setText("提交申请");
    bb->button(QDialogButtonBox::Ok)->setStyleSheet("QPushButton { background-color: #67C23A; color: white; border-radius: 4px; padding: 6px 20px; font-weight: bold; border: none; } QPushButton:hover { background-color: #85CE61; }");
    bb->button(QDialogButtonBox::Cancel)->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #606266; border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 20px; } QPushButton:hover { color: #409EFF; border-color: #c6e2ff; background-color: #ecf5ff; }");
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        if (app1->count() == 0) {
            QMessageBox::critical(pw, "失败", "无有效的第一审批人，请联系管理员！");
            return;
        }
        QString reason = reasonEdit->toPlainText().trimmed();
        if (reason.isEmpty()) { QMessageBox::warning(pw, "提示", "请填写申请理由！"); return; }

        QStringList chainList;
        chainList << app1->currentData().toString();
        if (levels >= 2 && app2->count() > 0) chainList << app2->currentData().toString();
        if (levels >= 3 && app3->count() > 0) chainList << app3->currentData().toString();

        QJsonObject req;
        req["type"] = "face_reregister_request";
        req["applicant"] = m_currentUser;
        req["reason"] = reason;
        req["approver"] = chainList.join(",");

        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() == "success") {
            QMessageBox::information(pw, "申请已提交",
                QString("人脸重录申请已提交,可在【我的申请进度】查看状态。")
                .arg(levels).arg(chainList.join(" → ")));
        }
        else {
            QMessageBox::warning(pw, "提交失败", res["msg"].toString());
        }
    }
}
// 唤出本地设置交互面板并将偏好属性持久化至配置文件
void ProfileModule::onPreferencesClicked() {
    QDialog dialog((QWidget*)this->parent());
    // 顺手把标题的 Emoji 换成了更正式的文本，并设置窗口图标
    dialog.setWindowTitle("系统偏好设置");
    dialog.setWindowIcon(QIcon("../../AttendanceClient/icon_library/Profile/btn_preference.svg"));
    dialog.resize(320, 160);
    QVBoxLayout layout(&dialog);
    layout.setSpacing(15);
    layout.setContentsMargins(20, 20, 20, 20);
    QSettings settings("config.ini", QSettings::IniFormat);
    bool voiceEnabled = settings.value("Preferences/VoiceEnabled", true).toBool();
    bool popupEnabled = settings.value("Preferences/PopupEnabled", true).toBool();
    QCheckBox* voiceCheck = new QCheckBox(" 开启打卡成功/失败语音播报");
    voiceCheck->setChecked(voiceEnabled);
    voiceCheck->setCursor(Qt::PointingHandCursor);
    // 注入 SVG 样式表，替换原生的勾选框
    QString customCheckStyle =
        "QCheckBox {"
        "   spacing: 10px;"
        "   font-family: 'Microsoft YaHei';"
        "   font-size: 14px;"
        "   color: #303133;"
        "}"
        "QCheckBox::indicator {"
        "   width: 24px;"
        "   height: 24px;"
        "}"
        "QCheckBox::indicator:unchecked {"
        "   image: url(../../AttendanceClient/icon_library/Profile/icon_voice_off.svg);"
        "}"
        "QCheckBox::indicator:checked {"
        "   image: url(../../AttendanceClient/icon_library/Profile/icon_voice_on.svg);"
        "}";
    voiceCheck->setStyleSheet(customCheckStyle);
    QCheckBox* popupCheck = new QCheckBox(" 接收系统全局广播弹窗");
    popupCheck->setChecked(popupEnabled);
    // 建议：为了视觉统一，全局广播弹窗也可以用普通样式修饰一下，或者未来补充对应的SVG
    popupCheck->setStyleSheet("QCheckBox { spacing: 10px; font-family: 'Microsoft YaHei'; font-size: 14px; color: #303133; }");
    popupCheck->setCursor(Qt::PointingHandCursor);

    layout.addWidget(voiceCheck);
    layout.addWidget(popupCheck);

    QDialogButtonBox buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    // 美化一下按钮
    buttonBox.button(QDialogButtonBox::Save)->setStyleSheet("QPushButton { background-color: #409EFF; color: white; border-radius: 4px; padding: 6px 15px; border: none; } QPushButton:hover { background-color: #66b1ff; }");
    buttonBox.button(QDialogButtonBox::Cancel)->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #606266; border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 15px; } QPushButton:hover { color: #409EFF; border-color: #c6e2ff; background-color: #ecf5ff; }");

    layout.addWidget(&buttonBox);

    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        settings.setValue("Preferences/VoiceEnabled", voiceCheck->isChecked());
        settings.setValue("Preferences/PopupEnabled", popupCheck->isChecked());
        QMessageBox::information((QWidget*)this->parent(), "设置已完成", "重启后生效。");
    }
}