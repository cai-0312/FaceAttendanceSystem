#include "ProfileModule.h"
#include "NetworkHelper.h" 
#include <QFileDialog>
#include <QInputDialog>
#include <QGraphicsDropShadowEffect>
#include <QCryptographicHash>
#include <QScrollArea>
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
#include <QHostAddress>
#include <QNetworkInterface>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QTimer>
// 构造函数，绑定头像点击事件并注入界面
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
// 处理头像标签的点击事件
bool ProfileModule::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (watched == m_avatarLabel) {
            onChangeAvatarClicked();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
// 重构个人资料页布局并绑定功能按钮
void ProfileModule::injectAdvancedUI() {
    if (!m_editBtn || !m_editBtn->parentWidget()) return;
    QWidget* parentW = m_editBtn->parentWidget();
    QString baseStyle = "QPushButton { color: #FFFFFF; border-radius: 6px; font-family: 'Microsoft YaHei'; font-size: 14px; font-weight: bold; min-height: 36px; padding: 0 16px; border: none; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";
    QString cardStyle = "QFrame { background-color: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 12px; }";
    QLabel* avatarLabel = parentW->window()->findChild<QLabel*>("label_Avatar");
    QComboBox* statusCombo = parentW->window()->findChild<QComboBox*>("comboBox_Status");
    QPushButton* btnChangeAvatar = parentW->window()->findChild<QPushButton*>("btn_ChangeAvatar");
    QFormLayout* formLay = parentW->window()->findChild<QFormLayout*>("formLayout_Profile");
    QLabel* qrCodeLabel = parentW->window()->findChild<QLabel*>("label_QRCode");
    QLabel* qrTitleLabel = parentW->window()->findChild<QLabel*>("label_QRTitle");
    QLabel* nameLabel = parentW->window()->findChild<QLabel*>("label_ProfileName");
    QLabel* deptLabel = parentW->window()->findChild<QLabel*>("label_ProfileDept");
    QLabel* roleLabel = parentW->window()->findChild<QLabel*>("label_ProfileRole");
    if (!avatarLabel || !formLay || !nameLabel) return;
    // 设置头像控件样式
    if (avatarLabel) {
        avatarLabel->setFixedSize(130, 130);
        avatarLabel->setStyleSheet("background: transparent; border: none; padding: 0px;");
    }
    if (m_editBtn) m_editBtn->hide();
    if (m_avatarBtn) m_avatarBtn->hide();
    QPushButton* oldPwdBtn = parentW->window()->findChild<QPushButton*>("btn_ChangePassword");
    if (oldPwdBtn) oldPwdBtn->hide();
    if (statusCombo) {
        statusCombo->clear();
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_online.svg"), "在线");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_meeting.svg"), "会议中");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_not_disturb.svg"), "请勿打扰");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_on_business_trip.svg"), "出差中");
        statusCombo->addItem(QIcon("../../AttendanceClient/icon_library/Profile/icon_on_vacation.svg"), "休假中");
        statusCombo->setFixedSize(120, 32);
        statusCombo->setObjectName("comboBox_Status_Final");
        statusCombo->setStyleSheet(
            "QComboBox#comboBox_Status_Final {"
            "  border: 1px solid #DEE0E3;"
            "  border-radius: 16px;" 
            "  background-color: #FFFFFF;"
            "  padding-left: 15px;"
            "  font-weight: bold;"
            "  color: #1F2329;"
            "  font-size: 13px;"
            "}"
            "QComboBox#comboBox_Status_Final:hover { border: 1px solid #3370FF; }"
            "QComboBox#comboBox_Status_Final::drop-down { border: none; background: transparent; width: 30px; }"
            "QComboBox#comboBox_Status_Final::down-arrow {"
            "  image: url('../../AttendanceClient/icon_library/Profile/icon_arrow_down.svg');"
            "  width: 14px; height: 14px;"
            "}"
            "QComboBox#comboBox_Status_Final QAbstractItemView {"
            "  border: 1px solid #E5E6EB;"
            "  border-radius: 8px;"
            "  background-color: #FFFFFF;"
            "  selection-background-color: #F2F3F5;"
            "  selection-color: #1F2329;"
            "  outline: none;"
            "  padding: 4px;"
            "}"
        );
    }
    QVBoxLayout* mainCardLay = parentW->window()->findChild<QVBoxLayout*>("vL_Card");
    if (!mainCardLay) return;
    mainCardLay->setSpacing(20);
    QWidget* lineSep = parentW->window()->findChild<QWidget*>("line_separator");
    if (lineSep) { lineSep->hide(); mainCardLay->removeWidget(lineSep); }
    QHBoxLayout* bottomLay = parentW->window()->findChild<QHBoxLayout*>("hL_BottomArea");
    if (bottomLay) mainCardLay->removeItem(bottomLay);
    QHBoxLayout* avCenterLay = parentW->window()->findChild<QHBoxLayout*>("hL_AvatarCenter");
    if (avCenterLay) mainCardLay->removeItem(avCenterLay);
    // 构建顶部个人名片区
    QFrame* headerFrame = new QFrame(parentW->window());
    headerFrame->setStyleSheet("QFrame { background-color: #F7F8FA; border-radius: 12px; border: 1px solid #E5E6EB; }");
    QHBoxLayout* hLayHeader = new QHBoxLayout(headerFrame);
    hLayHeader->setContentsMargins(25, 20, 25, 20);
    hLayHeader->setSpacing(20);
    QVBoxLayout* avLay = new QVBoxLayout();
    avLay->addWidget(avatarLabel, 0, Qt::AlignCenter);
    if (btnChangeAvatar) avLay->addWidget(btnChangeAvatar, 0, Qt::AlignCenter);
    hLayHeader->addLayout(avLay);
    QVBoxLayout* infoLay = new QVBoxLayout();
    infoLay->setAlignment(Qt::AlignVCenter);
    QHBoxLayout* nameTitleLay = new QHBoxLayout();
    QLabel* headerNameLabel = new QLabel(nameLabel->text());
    headerNameLabel->setObjectName("label_HeaderName");
    headerNameLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #1F2329; border: none; background: transparent;");
    nameTitleLay->addWidget(headerNameLabel);
    QLabel* badgeLabel = new QLabel(QString("%1 · %2").arg(deptLabel ? deptLabel->text() : "").arg(roleLabel ? roleLabel->text() : ""));
    badgeLabel->setObjectName("label_HeaderBadge");
    badgeLabel->setStyleSheet("font-size: 14px; color: #4E5969; font-weight: bold; border: none; background: transparent; margin-left: 10px;");
    nameTitleLay->addWidget(badgeLabel);
    nameTitleLay->addStretch();
    infoLay->addLayout(nameTitleLay);
    infoLay->addSpacing(8);
    if (statusCombo) infoLay->addWidget(statusCombo);
    hLayHeader->addLayout(infoLay, 1);
    QHBoxLayout* topActionLay = new QHBoxLayout();
    QString iconBase = "../../AttendanceClient/icon_library/Profile/";
    // 创建顶部操作按钮
    QPushButton* helpBtn = new QPushButton(" 操作指引与考勤规范");
    helpBtn->setIcon(QIcon(iconBase + "icon_operation.svg"));
    helpBtn->setIconSize(QSize(16, 16)); // 设置合适的图标尺寸
    helpBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #F2F3F5; color: #4E5969; border: 1px solid #DEE0E3; } QPushButton:hover { background-color: #E5E6EB; }");
    m_faceBtn = new QPushButton(" 重新采集人脸");
    m_faceBtn->setIcon(QIcon(iconBase + "btn_face_redo.svg"));
    m_faceBtn->setIconSize(QSize(16, 16)); // 设置合适的图标尺寸
    m_faceBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #3370FF; } QPushButton:hover { background-color: #4E83FF; }");
    topActionLay->addWidget(helpBtn);
    topActionLay->addWidget(m_faceBtn);
    hLayHeader->addLayout(topActionLay);
    mainCardLay->insertWidget(0, headerFrame);
    // 构建中部内容区
    QHBoxLayout* contentLay = new QHBoxLayout();
    contentLay->setSpacing(20);
    // 左侧信息卡片
    QFrame* baseInfoFrame = new QFrame(parentW->window());
    baseInfoFrame->setStyleSheet(cardStyle);
    QVBoxLayout* baseInfoLay = new QVBoxLayout(baseInfoFrame);
    baseInfoLay->setContentsMargins(25, 20, 25, 20);
    QLabel* baseInfoTitle = new QLabel("基本信息");
    baseInfoTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #1F2329; border: none;");
    baseInfoLay->addWidget(baseInfoTitle);
    baseInfoLay->addSpacing(15);
    // 组装表单内容
    QVBoxLayout* formWrapperLay = new QVBoxLayout();
    formWrapperLay->setSpacing(12);
    if (formLay) {
        for (int i = 0; i < formLay->rowCount(); ++i) {
            QLayoutItem* fieldItem = formLay->itemAt(i, QFormLayout::FieldRole);
            if (!fieldItem || !fieldItem->widget()) continue;
            QWidget* fieldWidget = fieldItem->widget();
            QWidget* labelWidget = formLay->labelForField(fieldWidget);
            QLabel* titleLabel = qobject_cast<QLabel*>(labelWidget);
            if (!titleLabel) continue;
            // 将每一行包装成独立卡片
            QWidget* rowContainer = new QWidget(baseInfoFrame);
            rowContainer->setObjectName("infoRowBox");
            rowContainer->setStyleSheet("QWidget#infoRowBox { background-color: #F7F8FA; border: 1px solid #E5E6EB; border-radius: 6px; }");
            rowContainer->setFixedHeight(40);
            QHBoxLayout* rowLay = new QHBoxLayout(rowContainer);
            rowLay->setContentsMargins(15, 0, 15, 0);
            titleLabel->setStyleSheet("QLabel { font-size: 13px; color: #4E5969; font-weight: bold; background: transparent; border: none; }");
            rowLay->addWidget(titleLabel);
            QLabel* dataLabel = qobject_cast<QLabel*>(fieldWidget);
            if (dataLabel) { dataLabel->setStyleSheet("QLabel { font-size: 13px; color: #1F2329; background: transparent; border: none; }"); }
            rowLay->addWidget(fieldWidget);
            rowLay->addStretch();
            formWrapperLay->addWidget(rowContainer);
        }
    }
    formWrapperLay->addStretch();
    baseInfoLay->addLayout(formWrapperLay);
    contentLay->addWidget(baseInfoFrame, 6);
    // 构建右侧状态和二维码区域
    QVBoxLayout* rightColLay = new QVBoxLayout();
    rightColLay->setSpacing(20);
    // 生成终端状态卡片
    renderDiagnosticsCard();
    QFrame* diagFrame = parentW->window()->findChild<QFrame*>("frame_Diagnostics");
    if (diagFrame) {
        diagFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        rightColLay->addWidget(diagFrame);
    }
    // 创建二维码卡片
    QFrame* qrFrame = new QFrame(parentW->window());
    qrFrame->setStyleSheet(cardStyle);
    QVBoxLayout* qrFrameLay = new QVBoxLayout(qrFrame);
    qrFrameLay->setContentsMargins(20, 20, 20, 20);
    qrFrameLay->setAlignment(Qt::AlignCenter);
    if (qrCodeLabel) {
        qrCodeLabel->setFixedSize(140, 140);
        qrCodeLabel->setStyleSheet("background: white; border: 1px solid #E5E6EB; border-radius: 8px;");
        qrFrameLay->addWidget(qrCodeLabel, 0, Qt::AlignCenter);
    }
    if (qrTitleLabel) {
        qrTitleLabel->setStyleSheet("color: #8F959E; font-size: 12px; margin-top: 5px;");
        qrFrameLay->addWidget(qrTitleLabel, 0, Qt::AlignCenter);
    }
    rightColLay->addWidget(qrFrame);
    rightColLay->addStretch();
    contentLay->addLayout(rightColLay, 4);
    mainCardLay->insertLayout(1, contentLay);
    // 构建底部操作栏
    QFrame* footerFrame = new QFrame(parentW->window());
    footerFrame->setStyleSheet(cardStyle + "QFrame { background-color: #F8FAFF; border-color: #BED0FF; }");
    QHBoxLayout* footerLay = new QHBoxLayout(footerFrame);
    footerLay->setContentsMargins(20, 15, 20, 15);
    m_pwdBtn = new QPushButton(" 修改登录密码");
    m_pwdBtn->setIcon(QIcon(iconBase + "btn_password.svg"));
    m_pwdBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #F56C6C; } QPushButton:hover { background-color: #F78989; }");
    m_exportPdfBtn = new QPushButton(" 导出个人入职档案");
    m_exportPdfBtn->setIcon(QIcon(iconBase + "icon_export_personal.svg"));
    m_exportPdfBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #E6A23C; } QPushButton:hover { background-color: #EBB563; }");
    m_settingsBtn = new QPushButton(" 系统设置");
    m_settingsBtn->setIcon(QIcon(iconBase + "btn_preference.svg"));
    m_settingsBtn->setStyleSheet(baseStyle + "QPushButton { background-color: #722ED1; } QPushButton:hover { background-color: #8D51DE; }");
    footerLay->addWidget(m_pwdBtn);
    footerLay->addWidget(m_exportPdfBtn);
    footerLay->addStretch();
    footerLay->addWidget(m_settingsBtn);
    mainCardLay->insertWidget(2, footerFrame);
    // 绑定功能按钮事件
    connect(m_pwdBtn, &QPushButton::clicked, this, &ProfileModule::onChangePasswordClicked);
    connect(m_faceBtn, &QPushButton::clicked, this, &ProfileModule::onReRegisterFaceClicked);
    connect(m_exportPdfBtn, &QPushButton::clicked, this, &ProfileModule::onExportProfilePdfClicked);
    connect(m_settingsBtn, &QPushButton::clicked, this, &ProfileModule::onPreferencesClicked);
    connect(helpBtn, &QPushButton::clicked, this, [this]() { renderHelpAccordion(); });
    // 让性别和电话可点击编辑
    if (m_genderLabel) {
        m_genderLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        connect(m_genderLabel, &QLabel::linkActivated, this, [this]() { onEditGenderClicked(); });
    }
    if (m_phoneLabel) {
        m_phoneLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        connect(m_phoneLabel, &QLabel::linkActivated, this, [this]() { onEditPhoneClicked(); });
    }
}
// 从服务器加载个人资料并刷新界面
void ProfileModule::loadUserProfile(const QString& username) {
    m_currentUser = username;
    QWidget* window = m_avatarLabel ? m_avatarLabel->window() : nullptr;
    if (m_currentUser.isEmpty()) {
        if (m_nameLabel) m_nameLabel->setText("错误：登录名丢失！");
        return;
    }
    if (m_nameLabel) m_nameLabel->setText("正在努力拉取数据...");
    // 请求用户资料
    QJsonObject req;
    req["type"] = "query_user_profile";
    req["name"] = username;
    QJsonObject res = NetworkHelper::request(req);
    if (res.isEmpty()) {
        if (m_nameLabel) m_nameLabel->setText("错误：网络断开或服务器未响应！");
        return;
    }
    if (res["status"].toString() != "success") {
        QString errMsg = res["msg"].toString();
        if (errMsg.isEmpty()) errMsg = "未知数据库错误，请检查服务端状态！";
        if (m_nameLabel) m_nameLabel->setText(QString("<font color='red'>服务端拒绝: %1</font>").arg(errMsg));
        return;
    }
    QString formattedId = QString("%1").arg(res["id"].toInt(), 3, 10, QChar('0'));
    QString jobTitle = res["job_title"].toString();
    QString role = res["role"].toString();
    QString dept = res["department"].toString();
    QString genderStr = res["gender"].toString().isEmpty() ? "未知" : res["gender"].toString();
    QString phoneStr = res["phone"].toString().isEmpty() ? "未设置" : res["phone"].toString();
    QString realName = res["real_name"].toString();
    if (realName.isEmpty()) realName = username;
    // 刷新基础信息标签
    if (m_nameLabel) m_nameLabel->setText(realName);
    if (m_deptLabel) m_deptLabel->setText(dept);
    if (m_genderLabel) {
        m_genderLabel->setText(QString("%1 &nbsp;<a href='edit_gender' style='color:#50C0C0; text-decoration:none; font-size:13px;'>"
            "<img src='../../AttendanceClient/icon_library/Profile/btn_edit.svg' width='14' height='14' align='middle'> 修改</a>").arg(genderStr));
    }
    if (m_phoneLabel) {
        m_phoneLabel->setText(QString("%1 &nbsp;<a href='edit_phone' style='color:#50C0C0; text-decoration:none; font-size:13px;'>"
            "<img src='../../AttendanceClient/icon_library/Profile/btn_edit.svg' width='14' height='14' align='middle'> 修改</a>").arg(phoneStr));
    }
    // 同步顶部标题栏信息
    if (window) {
        QLabel* headerName = window->findChild<QLabel*>("label_HeaderName");
        if (headerName) headerName->setText(realName);

        QLabel* headerBadge = window->findChild<QLabel*>("label_HeaderBadge");
        if (headerBadge) headerBadge->setText(QString("%1 · %2").arg(dept, jobTitle.isEmpty() || jobTitle == "未分配" ? role : jobTitle));
    }
    // 更新左侧说明标签
    if (window) {
        QString absIconBase = QFileInfo("../../AttendanceClient/icon_library/Profile/").absoluteFilePath() + "/";
        QLabel* lb1 = window->findChild<QLabel*>("lb_1");
        if (!lb1) lb1 = window->findChild<QLabel*>("label_employee_id");
        if (lb1) lb1->setText(QString("<img src='%1icon_employee_id.svg' width='16' height='16' align='middle'>&nbsp;员工工号：").arg(absIconBase));
        QLabel* lb2 = window->findChild<QLabel*>("lb_2");
        if (!lb2) lb2 = window->findChild<QLabel*>("label_gender");
        if (lb2) lb2->setText(QString("<img src='%1%2' width='16' height='16' align='middle'>&nbsp;员工性别：")
            .arg(absIconBase, genderStr == "女" ? "icon_girl.svg" : (genderStr == "男" ? "icon_male.svg" : "icon_genderless.svg")));
        QLabel* lb3 = window->findChild<QLabel*>("lb_3");
        if (!lb3) lb3 = window->findChild<QLabel*>("label_phone");
        if (lb3) lb3->setText(QString("<img src='%1icon_phone.svg' width='16' height='16' align='middle'>&nbsp;联系电话：").arg(absIconBase));
        QLabel* lb4 = window->findChild<QLabel*>("lb_4");
        if (!lb4) lb4 = window->findChild<QLabel*>("label_department");
        if (lb4) lb4->setText(QString("<img src='%1icon_department.svg' width='16' height='16' align='middle'>&nbsp;所属部门：").arg(absIconBase));
        QLabel* lb5 = window->findChild<QLabel*>("lb_5");
        if (!lb5) lb5 = window->findChild<QLabel*>("label_job");
        if (lb5) lb5->setText(QString("<img src='%1icon_job.svg' width='16' height='16' align='middle'>&nbsp;企业职务：").arg(absIconBase));
        QLabel* lb6 = window->findChild<QLabel*>("lb_name");
        if (!lb6) lb6 = window->findChild<QLabel*>("label_Name");
        if (lb6) lb6->setText(QString("<img src='%1icon_name.svg' width='16' height='16' align='middle'>&nbsp;员工姓名：").arg(absIconBase));
        QLabel* idLabel = window->findChild<QLabel*>("label_ProfileEmpId");
        if (idLabel) idLabel->setText(formattedId);
        QLabel* roleLabel = window->findChild<QLabel*>("label_ProfileRole");
        if (roleLabel) roleLabel->setText(jobTitle.isEmpty() || jobTitle == "未分配" ? role : jobTitle);
    }
    // 生成电子名片二维码内容
    QString cardData = QString::fromUtf8(
        "┌──────────────┐\n"
        "│  员工电子名片  │\n"
        "├──────────────┤\n"
        "│👤 姓名: %1\n"
        "│🔢 工号: %2\n"
        "│🏢 部门: %3\n"
        "│💼 职务: %4\n"
        "│📞 电话: %5\n"
        "└──────────────┘")
        .arg(realName, formattedId, dept, jobTitle, phoneStr);
    {
        // 生成二维码并显示到界面
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
            if (qrLabel) {
                qrLabel->setPixmap(QPixmap::fromImage(qrImg));
                qrLabel->setScaledContents(true);
            }
        }
    }
    // 加载头像数据
    QString avatarPath = res["avatar_path"].toString();
    QString avatarBase64 = res["avatar_base64"].toString();
    bool hasFilePath = !avatarPath.isEmpty() && avatarPath.contains("/") && !avatarPath.startsWith("/9j/");
    if (hasFilePath) {
        // 头像为路径时先读取文件内容
        QJsonObject avReq;
        avReq["type"] = "query_avatar_file";
        avReq["avatar_path"] = avatarPath;
        QJsonObject avRes = NetworkHelper::request(avReq);
        if (avRes["status"].toString() == "success") avatarBase64 = avRes["avatar_data"].toString();
    }
    // 渲染圆形头像
    if (!avatarBase64.isEmpty()) {
        QFuture<QImage> future = QtConcurrent::run([avatarBase64]() -> QImage {
            QImage result, img;
            img.loadFromData(QByteArray::fromBase64(avatarBase64.toUtf8()));
            if (!img.isNull()) {
                // 设置头像大小
                int size = 130;
                // 裁剪为正方形
                QImage scaledImg = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                QImage squareImg = scaledImg.copy((scaledImg.width() - size) / 2, (scaledImg.height() - size) / 2, size, size);
                // 绘制圆形透明头像
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
        // 头像生成完成后刷新控件
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher]() {
            QImage resImg = watcher->result();
            if (!resImg.isNull() && m_avatarLabel) {
                m_avatarLabel->setPixmap(QPixmap::fromImage(resImg));
                m_avatarLabel->setScaledContents(false);
                m_avatarLabel->setAlignment(Qt::AlignCenter);
            }
            else if (m_avatarLabel) {
                // 默认头像使用圆形占位
                QImage d(130, 130, QImage::Format_ARGB32_Premultiplied);
                d.fill(Qt::transparent);
                QPainter p(&d); p.setRenderHint(QPainter::Antialiasing); p.setBrush(Qt::darkCyan); p.setPen(Qt::NoPen);
                p.drawEllipse(0, 0, 130, 130); p.end();
                m_avatarLabel->setPixmap(QPixmap::fromImage(d));
            }
            watcher->deleteLater();
            });
        watcher->setFuture(future);
    }
    else if (m_avatarLabel) {
        // 无头像时显示默认占位
        QImage d(130, 130, QImage::Format_ARGB32_Premultiplied);
        d.fill(Qt::transparent);
        QPainter p(&d); p.setRenderHint(QPainter::Antialiasing); p.setBrush(Qt::darkCyan); p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, 130, 130); p.end();
        m_avatarLabel->setPixmap(QPixmap::fromImage(d));
    }
    renderDiagnosticsCard();
}
// 修改性别
void ProfileModule::onEditGenderClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("修改性别");
    dialog.resize(250, 80);
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
// 修改手机号
void ProfileModule::onEditPhoneClicked() {
    QInputDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("修改手机号");
    dialog.setLabelText("请输入11位手机号:");
    dialog.setOkButtonText("确认修改");
    dialog.setCancelButtonText("取消");
    QLineEdit* lineEdit = dialog.findChild<QLineEdit*>();
    if (lineEdit) {
        QRegularExpression regx("[0-9]{11}");
        QValidator* validator = new QRegularExpressionValidator(regx, lineEdit);
        lineEdit->setValidator(validator);
        lineEdit->setMaxLength(11);
    }
    if (dialog.exec() == QDialog::Accepted) {
        QString newPhone = dialog.textValue().trimmed();
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
// 修改登录密码
void ProfileModule::onChangePasswordClicked() {
    QWidget* pw = (QWidget*)this->parent();
    QDialog dialog(pw);
    dialog.setWindowTitle("修改登录密码");
    dialog.resize(360, 140);
    QFormLayout form(&dialog);
    QLineEdit* oldPwdEdit = new QLineEdit(&dialog); oldPwdEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* newPwdEdit = new QLineEdit(&dialog); newPwdEdit->setEchoMode(QLineEdit::Password);
    newPwdEdit->setPlaceholderText("至少8位，必须包含字母 and 数字");
    QLineEdit* confirmPwdEdit = new QLineEdit(&dialog); confirmPwdEdit->setEchoMode(QLineEdit::Password);
    form.addRow("旧密码:", oldPwdEdit);
    form.addRow("新密码:", newPwdEdit);
    form.addRow("确认新密码:", confirmPwdEdit);
    QLabel* ruleLabel = new QLabel("密码规则: 长度≥8位, 必须同时包含字母 and 数字");
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
        if (!hasL || !hasD) { QMessageBox::warning(pw, "密码强度不足", "新密码必须同时包含字母 and 数字！"); return; }
        if (newPwd == oldPwd) { QMessageBox::warning(pw, "错误", "新密码不能与旧密码相同！"); return; }
        // 与登录链路一致，对密码做 SHA256 哈希后再传输，避免明文泄露
        auto hashPwd = [](const QString& plain) -> QString {
            return QString(QCryptographicHash::hash(plain.toUtf8(), QCryptographicHash::Sha256).toHex());
        };
        QJsonObject req;
        req["type"] = "verify_and_update_password";
        req["name"] = m_currentUser;
        req["old_pwd"] = hashPwd(oldPwd);
        req["new_pwd"] = hashPwd(newPwd);
        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() == "success") QMessageBox::information(pw, "成功", "密码修改成功！下次登录请使用新密码。");
        else { QString e = res["msg"].toString(); QMessageBox::warning(pw, "修改失败", e.isEmpty() ? "服务端未响应" : e); }
    }
}
// 修改头像并上传到服务端
void ProfileModule::onChangeAvatarClicked()
{
    QWidget* parentWidget = (QWidget*)this->parent();
    QString filePath = QFileDialog::getOpenFileName(parentWidget, "选择新头像", "", "Images (*.png *.jpg *.jpeg)");
    if (filePath.isEmpty()) return;
    QPixmap pix(filePath);
    if (pix.isNull()) {
        QMessageBox::warning(parentWidget, "错误", "无法加载图片！");
        return;
    }
    QString currentName = m_nameLabel->text().trimmed();
    if (currentName.isEmpty()) return;
    // 裁剪头像为正方形
    QPixmap scaledPix = pix.scaled(100, 100, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    int x = (scaledPix.width() - 100) / 2;
    int y = (scaledPix.height() - 100) / 2;
    QPixmap finalPix = scaledPix.copy(x, y, 100, 100);
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceClient/avatars/" + currentName;
    QString localDir = QDir::cleanPath(rawPath) + "/";
    QDir dir;
    if (!dir.exists(localDir)) dir.mkpath(localDir);
    QString fileName = currentName + ".jpg";
    QString localPath = localDir + fileName;
    QFile oldFile(localPath);
    if (oldFile.exists()) {
        QString randomCode = QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + QString::number(QRandomGenerator::global()->bounded(1000, 9999));
        QString backupPath = localDir + currentName + "_" + randomCode + ".jpg";
        oldFile.rename(backupPath);
    }
    finalPix.save(localPath, "JPG", 90);
    // 生成圆形预览图
    QPainterPath path;
    path.addEllipse(0, 0, 100, 100);
    QPixmap roundedPix(100, 100);
    roundedPix.fill(Qt::transparent);
    QPainter painter(&roundedPix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, finalPix);
    m_avatarLabel->setPixmap(roundedPix);
    // 上传头像数据到服务端
    QByteArray imgData;
    QBuffer buffer(&imgData);
    buffer.open(QIODevice::WriteOnly);
    finalPix.save(&buffer, "JPG", 80); 
    QJsonObject req;
    req["type"] = "upload_avatar";
    req["name"] = currentName;  
    req["avatar_data"] = QString(imgData.toBase64());
    NetworkHelper::sendAsync(req);
    QMessageBox::information(parentWidget, "成功", "头像修改成功！");
}
// 导出个人入职档案为 PDF
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
    // 设置版面参数
    int width = writer.width();
    int height = writer.height();
    int margin = 200;
    // 重新读取个人资料用于导出
    QJsonObject req;
    req["type"] = "query_user_profile";
    req["name"] = m_currentUser;
    QJsonObject res = NetworkHelper::request(req);
    QString cleanGender = res["gender"].toString().isEmpty() ? "未知" : res["gender"].toString();
    QString cleanPhone = res["phone"].toString().isEmpty() ? "未设置" : res["phone"].toString();
    QString cleanDept = res["department"].toString();
    painter.setFont(QFont("Microsoft YaHei", 24, QFont::Bold));
    painter.drawText(0, 0, width, 400, Qt::AlignCenter, "员工入职登记档案");
    // 绘制表格边框
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
    // 填写基础信息
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
    // 写入页脚时间
    painter.setFont(QFont("Microsoft YaHei", 10, QFont::Normal));
    painter.setPen(Qt::gray);
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    painter.drawText(margin, 2600, width - 2 * margin, 200, Qt::AlignRight, "打印时间：" + timeStr);
    painter.drawText(margin, 2600, width - 2 * margin, 200, Qt::AlignLeft, "注：此档案由系统自动生成");
    painter.end();
    QMessageBox::information((QWidget*)this->parent(), "导出成功",
        "员工档案已成功导出为 PDF");
}
// 发起人脸重录申请
void ProfileModule::onReRegisterFaceClicked() {
    QWidget* pw = (QWidget*)this->parent();
    // 查询可选审批人
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
    dialog.resize(280, 280);
    QFormLayout* form = new QFormLayout(&dialog);
    form->setContentsMargins(25, 25, 25, 25);
    form->setSpacing(15);
    // 输入框样式
    QString modernInputStyle =
        "QComboBox, QTextEdit { border: 1px solid #DCDFE6; border-radius: 4px; padding: 6px 10px; background: white; color: #606266; font-size: 13px; min-height: 28px; }"
        "QComboBox:hover, QTextEdit:hover { border-color: #C0C4CC; }"
        "QComboBox:focus, QTextEdit:focus { border-color: #409EFF; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 25px; border-left: none; }"
        "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #909399; }";
    QLabel* tip = new QLabel("人脸特征重采集需经审批通过后方可执行，请填写申请理由并选择审批人！");
    tip->setStyleSheet("color: #E6A23C; font-size: 16px; margin-bottom: 8px; line-height: 1.5;");
    form->addRow(tip);
    // 填写申请理由
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
    // 准备审批人下拉框
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
        form->addRow("第一审批人:", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (dept == "总经办") {
        levels = 2; fillGM(app1); fillHR(app2);
        form->addRow("第一审批人:", app1);
        form->addRow("第二审批人:", app2);
        app3->setVisible(false);
    }
    else if (dept == "人力资源部" && jobTitle == "部门经理") {
        levels = 1; fillGM(app1);
        form->addRow("第一审批人:", app1);
        app2->setVisible(false); app3->setVisible(false);
    }
    else if (dept == "人力资源部") {
        levels = 2; fillGM(app1); fillHR(app2);
        form->addRow("第一审批人:", app1);
        form->addRow("第二审批人:", app2);
        app3->setVisible(false);
    }
    else if (jobTitle == "部门经理") {
        levels = 2; fillGM(app1); fillHR(app2);
        form->addRow("第一审批人:", app1);
        form->addRow("第二审批人:", app2);
        app3->setVisible(false);
    }
    else {
        levels = 3; fillDeptMgr(app1); fillGM(app2); fillHR(app3);
        form->addRow("第一审批人:", app1);
        form->addRow("第二审批人:", app2);
        form->addRow("第三审批人:", app3);
    }
    // 提交和取消按钮
    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    bb->button(QDialogButtonBox::Ok)->setText("提交申请");
    bb->button(QDialogButtonBox::Ok)->setStyleSheet("QPushButton { background-color: #67C23A; color: white; border-radius: 4px; padding: 6px 20px; font-weight: bold; border: none; } QPushButton:hover { background-color: #85CE61; }");
    bb->button(QDialogButtonBox::Cancel)->setText("取消");
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
        // 组装审批链
        QStringList chainList;
        chainList << app1->currentData().toString();
        if (levels >= 2 && app2->count() > 0) chainList << app2->currentData().toString();
        if (levels >= 3 && app3->count() > 0) chainList << app3->currentData().toString();
        QJsonObject req;
        req["type"] = "face_reregister_request";
        req["applicant"] = m_currentUser;
        req["reason"] = reason;
        req["approver"] = chainList.join(",");
        // 提交申请
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
// 打开系统偏好设置
void ProfileModule::onPreferencesClicked() {
    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("系统偏好设置");
    dialog.setWindowIcon(QIcon("../../AttendanceClient/icon_library/Profile/btn_preference.svg"));
    dialog.resize(320, 160);
    QVBoxLayout layout(&dialog);
    layout.setSpacing(15);
    layout.setContentsMargins(20, 20, 20, 20);
    QSettings settings("config.ini", QSettings::IniFormat);
    bool voiceEnabled = settings.value("Preferences/VoiceEnabled", true).toBool();
    bool popupEnabled = settings.value("Preferences/PopupEnabled", true).toBool();
    // 语音开关
    QCheckBox* voiceCheck = new QCheckBox(" 开启打卡成功/失败语音播报");
    voiceCheck->setChecked(voiceEnabled);
    voiceCheck->setCursor(Qt::PointingHandCursor);
    QString customVoiceCheckStyle =
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
    voiceCheck->setStyleSheet(customVoiceCheckStyle);
    QCheckBox* popupCheck = new QCheckBox(" 接收系统全局广播弹窗");
    popupCheck->setChecked(popupEnabled);
    popupCheck->setCursor(Qt::PointingHandCursor);
    QString customBroadcastCheckStyle =
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
        "   image: url(../../AttendanceClient/icon_library/Profile/icon_broadcast_fork.svg);"
        "}"
        "QCheckBox::indicator:checked {"
        "   image: url(../../AttendanceClient/icon_library/Profile/icon_broadcast_tick.svg);"
        "}";
    popupCheck->setStyleSheet(customBroadcastCheckStyle);
    layout.addWidget(voiceCheck);
    layout.addWidget(popupCheck);
    QDialogButtonBox buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
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
// 渲染终端状态卡片
void ProfileModule::renderDiagnosticsCard()
{
    QWidget* window = m_avatarLabel ? m_avatarLabel->window() : nullptr;
    if (!window) return;
    if (window->findChild<QFrame*>("frame_Diagnostics")) return;
    // 获取本机网络和硬件状态
    QString localIP = "未知";
    for (const QHostAddress& addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            localIP = addr.toString();
            break;
        }
    }
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    bool cameraOnline = !cameras.isEmpty();
    QString cameraName = cameraOnline ? cameras.first().description() : "未检测到摄像头";
    QString version = "v1.0.3";
    QString sysTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    // 创建状态卡片
    QFrame* diagFrame = new QFrame(window);
    diagFrame->setObjectName("frame_Diagnostics");
    diagFrame->setFixedWidth(260);
    diagFrame->setStyleSheet(
        "QFrame#frame_Diagnostics {"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E5E6EB;"
        "  border-radius: 12px;"
        "}");
    QVBoxLayout* diagLay = new QVBoxLayout(diagFrame);
    diagLay->setContentsMargins(15, 15, 15, 15);
    diagLay->setSpacing(8);
    QLabel* titleLabel = new QLabel("终端运行状态");
    titleLabel->setStyleSheet("font-family:'Microsoft YaHei'; font-size:13px; font-weight:bold; color:#1F2329; border:none;");
    titleLabel->setAlignment(Qt::AlignCenter);
    diagLay->addWidget(titleLabel);
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color:#DEE0E3; border:none; background-color:#DEE0E3; max-height:1px;");
    diagLay->addWidget(line);
    auto addDiagItem = [&](const QString& icon, const QString& label, const QString& value, bool isGreen) {
        QHBoxLayout* row = new QHBoxLayout();
        row->setSpacing(6);
        QLabel* iconLbl = new QLabel();
        iconLbl->setPixmap(QIcon(icon).pixmap(16, 16));
        iconLbl->setFixedSize(16, 16);
        iconLbl->setStyleSheet("border:none;");
        QLabel* nameLbl = new QLabel(label);
        nameLbl->setStyleSheet("font-size:11px; color:#8F959E; border:none;");
        QLabel* valLbl = new QLabel(value);
        valLbl->setStyleSheet(QString("font-size:11px; font-weight:bold; color:%1; border:none;")
            .arg(isGreen ? "#00B42A" : "#F53F3F"));
        valLbl->setWordWrap(true);
        row->addWidget(iconLbl);
        row->addWidget(nameLbl);
        row->addStretch();
        row->addWidget(valLbl);
        diagLay->addLayout(row);
        };
    QString iconBase2 = "../../AttendanceClient/icon_library/Profile/";
    // 填充状态项
    addDiagItem(iconBase2 + "icon_LAN.svg", "局域网 IP", localIP, true);
    addDiagItem(iconBase2 + (cameraOnline ? "icon_cameras_on.svg" : "icon_cameras_off.svg"), "摄像头", cameraOnline ? "在线" : "离线", cameraOnline);
    QLabel* camNameLbl = new QLabel("  " + cameraName);
    camNameLbl->setObjectName("RealCameraNameLabel"); 
    camNameLbl->setStyleSheet("font-size:10px; color:#8F959E; border:none;");
    camNameLbl->setWordWrap(true);
    diagLay->addWidget(camNameLbl);
    QLabel* timeValLabel = new QLabel(sysTime);
    timeValLabel->setObjectName("diagTimeLabel");
    timeValLabel->setStyleSheet("font-size:11px; font-weight:bold; color:#00B42A; border:none;");
    QHBoxLayout* timeRow = new QHBoxLayout();
    timeRow->setSpacing(6);
    QLabel* timeIcon = new QLabel();
    timeIcon->setPixmap(QIcon(iconBase2 + "icon_time.svg").pixmap(16, 16));
    timeIcon->setFixedSize(16, 16);
    timeIcon->setStyleSheet("border:none;");
    QLabel* timeNameLbl = new QLabel("系统时间");
    timeNameLbl->setStyleSheet("font-size:11px; color:#8F959E; border:none;");
    timeRow->addWidget(timeIcon);
    timeRow->addWidget(timeNameLbl);
    timeRow->addStretch();
    diagLay->addLayout(timeRow);
    diagLay->addWidget(timeValLabel);
    QTimer* timer = new QTimer(diagFrame);
    connect(timer, &QTimer::timeout, [timeValLabel]() {
        timeValLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        });
    timer->start(1000);
    diagLay->addStretch();
    QLabel* summaryLabel = new QLabel(cameraOnline ? "✓ 终端状态正常" : "✗ 摄像头异常");
    summaryLabel->setAlignment(Qt::AlignCenter);
    summaryLabel->setStyleSheet(QString(
        "font-size:12px; font-weight:bold; color:white; border:none; "
        "background-color:%1; border-radius:10px; padding:6px;")
        .arg(cameraOnline ? "#00B42A" : "#F53F3F"));
    diagLay->addWidget(summaryLabel);
}
void ProfileModule::renderHelpAccordion()
{
    QWidget* window = m_avatarLabel ? m_avatarLabel->window() : nullptr;
    if (!window) return;
    QDialog dialog(window);
    dialog.setWindowTitle("操作指引与考勤规范");
    dialog.resize(480, 520);
    dialog.setStyleSheet("QDialog { background-color: #FFFFFF; }");
    QVBoxLayout* mainLay = new QVBoxLayout(&dialog);
    mainLay->setContentsMargins(20, 20, 20, 20);
    QScrollArea* scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    QWidget* scrollWidget = new QWidget();
    QVBoxLayout* helpLay = new QVBoxLayout(scrollWidget);
    helpLay->setSpacing(8);
    // 添加可折叠帮助项
    auto addAccordionItem = [&](const QString& title, const QString& content) {
        QPushButton* headerBtn = new QPushButton("▸ " + title);
        headerBtn->setCursor(Qt::PointingHandCursor);
        headerBtn->setStyleSheet(
            "QPushButton { text-align:left; border:none; background:#F7F8FA; "
            "font-family:'Microsoft YaHei'; font-size:14px; font-weight:bold; "
            "color:#1F2329; padding:12px; border-radius: 6px; }"
            "QPushButton:hover { background-color:#F2F3F5; color:#3370FF; }");
        QTextBrowser* bodyBrowser = new QTextBrowser();
        bodyBrowser->setOpenExternalLinks(false);
        bodyBrowser->setVisible(false);
        bodyBrowser->setStyleSheet(
            "QTextBrowser { border:1px solid #E5E6EB; background:#FFFFFF; border-radius:6px; "
            "font-size:13px; color:#4E5969; padding:12px; }");
        bodyBrowser->setHtml(content);
        bodyBrowser->setMinimumHeight(140);
        connect(headerBtn, &QPushButton::clicked, [headerBtn, bodyBrowser]() {
            bool visible = !bodyBrowser->isVisible();
            bodyBrowser->setVisible(visible);
            headerBtn->setText((visible ? "▾ " : "▸ ") + headerBtn->text().mid(2));
            });
        helpLay->addWidget(headerBtn);
        helpLay->addWidget(bodyBrowser);
        };
    // 读取帮助配置文件
    QString filePath = QCoreApplication::applicationDirPath() + "/config/help_docs.json";
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
            QJsonArray arr = doc.array();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject obj = arr[i].toObject();
                QString title = obj["title"].toString();
                QString content = obj["content"].toString();
                addAccordionItem(title, content);
            }
        }
    }
    helpLay->addStretch();
    scrollArea->setWidget(scrollWidget);
    mainLay->addWidget(scrollArea);
    QPushButton* closeBtn = new QPushButton("我知道了", &dialog);
    closeBtn->setStyleSheet("QPushButton { background-color: #3370FF; color: white; border-radius: 6px; padding: 10px; font-weight: bold; border: none; } QPushButton:hover { background-color: #4E83FF; }");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    mainLay->addWidget(closeBtn);
    dialog.exec();
}
// 更新摄像头编号对应的设备名称
void ProfileModule::updateCameraId(int camId)
{
    QWidget* window = m_avatarLabel ? m_avatarLabel->window() : nullptr;
    if (!window) return;
    QLabel* camNameLbl = window->findChild<QLabel*>("RealCameraNameLabel");
    if (camNameLbl) {
        QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        QString realCamName = "未知设备";
        if (camId >= 0 && camId < cameras.size()) {
            realCamName = cameras.at(camId).description();
        }
        camNameLbl->setText(QString("  [ID:%1] %2").arg(camId).arg(realCamName));
        camNameLbl->setStyleSheet("font-size:10px; color:#8F959E; border:none;");
    }
}