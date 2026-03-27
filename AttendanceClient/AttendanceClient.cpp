#include "AttendanceClient.h"
#include "ui_AttendanceClient.h"
#include "MainWidget.h"
#include "NetworkHelper.h"
#include <QMessageBox>
#include <QGraphicsDropShadowEffect> 
#include <QRegularExpression> 
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QAction>
#include <QCryptographicHash>

// 工具函数：SHA-256哈希（密码传输加密）
static QString hashPassword(const QString& plainPwd) {
    QByteArray hash = QCryptographicHash::hash(plainPwd.toUtf8(), QCryptographicHash::Sha256);
    return QString(hash.toHex());
}

// 构造函数
AttendanceClient::AttendanceClient(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AttendanceClientClass)
{
    ui->setupUi(this);
    this->setWindowFlags(Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground);

    // 添加窗口高级阴影
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 0);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setBlurRadius(25);
    ui->frame_Background->setGraphicsEffect(shadow);

    connect(ui->btn_Close, &QPushButton::clicked, this, &AttendanceClient::on_btn_Close_clicked);
    connect(ui->btn_Min, &QPushButton::clicked, this, &AttendanceClient::on_btn_Min_clicked);
    ui->stackedWidget->setCurrentIndex(0);

    // 统一资源路径（绝对限定在 AttendanceClient 文件夹下）
    QString iconBase = "../../AttendanceClient/icon_library/AttendanceClient/";

    // ==========================================================
    // 1. ⭐️ 核心：注入 SVG 图标，并实现“输入时隐藏图标”的微交互
    // ==========================================================
    // --- 登录页 ---
    QAction* actLogAcc = ui->lineEdit_Account->addAction(QIcon(iconBase + "icon_account.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_Account, &QLineEdit::textChanged, this, [=](const QString& text) { actLogAcc->setVisible(text.isEmpty()); });

    QAction* actLogPwd = ui->lineEdit_pwd->addAction(QIcon(iconBase + "icon_password.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_pwd, &QLineEdit::textChanged, this, [=](const QString& text) { actLogPwd->setVisible(text.isEmpty()); });

    // --- 注册页 ---
    QAction* actRegAcc = ui->lineEdit_RegAccount->addAction(QIcon(iconBase + "icon_account.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegAccount, &QLineEdit::textChanged, this, [=](const QString& text) { actRegAcc->setVisible(text.isEmpty()); });

    QAction* actRegName = ui->lineEdit_RegName->addAction(QIcon(iconBase + "icon_name.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegName, &QLineEdit::textChanged, this, [=](const QString& text) { actRegName->setVisible(text.isEmpty()); });

    QAction* actRegPwd = ui->lineEdit_RegPwd->addAction(QIcon(iconBase + "icon_password.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegPwd, &QLineEdit::textChanged, this, [=](const QString& text) { actRegPwd->setVisible(text.isEmpty()); });

    QAction* actRegPhone = ui->lineEdit_RegPhone->addAction(QIcon(iconBase + "icon_phone.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegPhone, &QLineEdit::textChanged, this, [=](const QString& text) { actRegPhone->setVisible(text.isEmpty()); });

    // ==========================================================
    // 2. 清理按钮硬编码 Emoji 并绑定 SVG 图标
    // ==========================================================
    ui->btn_Login->setText(" 立即登录");
    ui->btn_Login->setIcon(QIcon(iconBase + "btn_login.svg"));
    ui->btn_Login->setIconSize(QSize(18, 18));

    ui->btn_ConfirmRegister->setText(" 确认注册");
    ui->btn_ConfirmRegister->setIcon(QIcon(iconBase + "btn_register.svg"));
    ui->btn_ConfirmRegister->setIconSize(QSize(18, 18));

    ui->btn_BackLogin->setText(" 返回登录");
    ui->btn_GoRegister->setText(" 立即注册");

    // ==========================================================
    // 3. 密码显隐小眼睛及表单联动代理配置
    // ==========================================================
    m_isPwdVisible = false;
    ui->lineEdit_pwd->setEchoMode(QLineEdit::Password);
    m_pwdAction = ui->lineEdit_pwd->addAction(QIcon(iconBase + "icon_eye_closed.svg"), QLineEdit::TrailingPosition);
    m_pwdAction->setToolTip("显示密码");
    connect(m_pwdAction, &QAction::triggered, this, &AttendanceClient::togglePasswordVisibility);
    ui->lineEdit_pwd->setCursor(Qt::IBeamCursor);

    ui->comboBox_Role->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegGender->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegDept->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegJobTitle->setItemDelegate(new QStyledItemDelegate(this));
    for (int i = 0; i < ui->comboBox_Role->count(); ++i) {
        if (ui->comboBox_Role->itemText(i).contains("管理员")) {
            ui->comboBox_Role->setItemIcon(i, QIcon(iconBase + "icon_admin.svg"));
        }
        else {
            ui->comboBox_Role->setItemIcon(i, QIcon(iconBase + "icon_staff.svg"));
        }
    }
    ui->comboBox_Role->setIconSize(QSize(16, 16));
    // 部门-职务联动
    connect(ui->comboBox_RegDept, &QComboBox::currentTextChanged, this, [=](const QString& dept) {
        ui->comboBox_RegJobTitle->clear();
        if (dept == "总经办") {
            ui->comboBox_RegJobTitle->addItems({ "总经理", "行政助理", "财务总监" });
        }
        else if (dept == "人力资源部") {
            ui->comboBox_RegJobTitle->addItems({ "部门经理", "招聘专员", "员工关系专员", "培训与发展专员" });
        }
        else if (dept == "财务部") {
            ui->comboBox_RegJobTitle->addItems({ "部门经理", "财务分析师", "会计", "出纳" });
        }
        else if (dept == "销售部") {
            ui->comboBox_RegJobTitle->addItems({ "部门经理", "区域销售经理", "销售代表", "客户服务代表" });
        }
        else if (dept == "研发部") {
            ui->comboBox_RegJobTitle->addItems({ "部门经理", "软件工程师", "测试工程师", "产品经理" });
        }
        else if (dept == "市场部") {
            ui->comboBox_RegJobTitle->addItems({ "部门经理", "市场营销专员", "品牌管理专员", "公关专员" });
        }
        else if (dept == "客户服务部") {
            ui->comboBox_RegJobTitle->addItems({ "部门经理", "客户服务代表", "技术支持" });
        }
        });
    emit ui->comboBox_RegDept->currentTextChanged(ui->comboBox_RegDept->currentText());
}

AttendanceClient::~AttendanceClient() {
    delete ui;
}

// 退出登录后重新显示登录界面
void AttendanceClient::showLoginReady() {
    ui->lineEdit_Account->clear();
    ui->lineEdit_pwd->clear();
    ui->stackedWidget->setCurrentIndex(0);
    if (mainWindow) {
        mainWindow->deleteLater();
        mainWindow = nullptr;
    }
    this->show();
    this->activateWindow();
}

// 切换密码可见状态
void AttendanceClient::togglePasswordVisibility() {
    m_isPwdVisible = !m_isPwdVisible;
    QString iconBase = "../../AttendanceClient/icon_library/AttendanceClient/";
    if (m_isPwdVisible) {
        ui->lineEdit_pwd->setEchoMode(QLineEdit::Normal);
        m_pwdAction->setIcon(QIcon(iconBase + "icon_eye_open.svg"));
        m_pwdAction->setToolTip("隐藏密码");
    }
    else {
        ui->lineEdit_pwd->setEchoMode(QLineEdit::Password);
        m_pwdAction->setIcon(QIcon(iconBase + "icon_eye_closed.svg"));
        m_pwdAction->setToolTip("显示密码");
    }
}

// 无边框窗口拖拽支持
void AttendanceClient::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void AttendanceClient::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
}

void AttendanceClient::on_btn_Close_clicked() { this->close(); }
void AttendanceClient::on_btn_Min_clicked() { this->showMinimized(); }
void AttendanceClient::on_btn_GoRegister_clicked() { ui->stackedWidget->setCurrentIndex(1); }
void AttendanceClient::on_btn_BackLogin_clicked() { ui->stackedWidget->setCurrentIndex(0); }

// 登录校验与 SHA-256 哈希加密
void AttendanceClient::on_btn_Login_clicked() {
    QString account = ui->lineEdit_Account->text().trimmed();
    QString pwd = ui->lineEdit_pwd->text().trimmed();
    QString role = ui->comboBox_Role->currentText();
    if (account.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "提示", "工号和密码不能为空！");
        return;
    }
    QJsonObject req;
    req["type"] = "client_login_auth";
    req["account"] = account;
    req["pwd"] = hashPassword(pwd);
    req["role"] = role;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        QString realName = res["real_name"].toString();
        bool hasFace = res["has_face"].toBool(true);
        QMessageBox::information(this, "成功", "登录成功！欢迎 " + realName);
        mainWindow = new MainWidget(realName, role, nullptr, this);
        mainWindow->show();
        this->hide();
        if (!hasFace) {
            QMessageBox::warning(mainWindow, "人脸绑定提醒",
                "检测到您尚未录入人脸特征，无法进行人脸打卡！\n系统将自动跳转至人脸录入页面，请先完成人脸采集。");
            mainWindow->forceNavigateTo(2);
        }
    }
    else {
        if (res.isEmpty()) {
            QMessageBox::critical(this, "服务器连接失败", "无法连接到考勤总控服务器！\n请检查管理员是否已启动服务端程序。");
        }
        else {
            QMessageBox::critical(this, "失败", "登录失败：工号、密码或角色错误！");
        }
    }
}

// 注册时强制普通权限 + 密码复杂度校验 + SHA-256哈希
void AttendanceClient::on_btn_ConfirmRegister_clicked() {
    QString account = ui->lineEdit_RegAccount->text().trimmed();
    QString name = ui->lineEdit_RegName->text().trimmed();
    QString pwd = ui->lineEdit_RegPwd->text().trimmed();
    QString phone = ui->lineEdit_RegPhone->text().trimmed();
    QString gender = ui->comboBox_RegGender->currentText();
    QString dept = ui->comboBox_RegDept->currentText();
    QString jobTitle = ui->comboBox_RegJobTitle->currentText();
    if (account.isEmpty() || name.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "提示", "工号、姓名、密码为必填项！");
        return;
    }
    QRegularExpression reChinese("[\\x{4e00}-\\x{9fa5}]");
    if (reChinese.match(account).hasMatch()) {
        QMessageBox::warning(this, "格式错误", "员工账号不允许包含中文字符，请使用字母或数字！");
        return;
    }
    if (pwd.length() < 8) {
        QMessageBox::warning(this, "密码强度不足", "密码长度不能少于8位！");
        return;
    }
    QRegularExpression reLetter("[a-zA-Z]");
    QRegularExpression reDigit("[0-9]");
    if (!reLetter.match(pwd).hasMatch() || !reDigit.match(pwd).hasMatch()) {
        QMessageBox::warning(this, "密码强度不足", "密码必须同时包含英文字母和数字！");
        return;
    }
    QRegularExpression rePhone("^\\d{11}$");
    if (!phone.isEmpty() && !rePhone.match(phone).hasMatch()) {
        QMessageBox::warning(this, "格式错误", "手机号码格式不正确，必须为11位纯数字！");
        return;
    }
    if (phone.isEmpty()) phone = "未设置";

    QString assignRole = "普通登录";
    QJsonObject req;
    req["type"] = "client_register_account";
    req["account"] = account;
    req["pwd"] = hashPassword(pwd);
    req["name"] = name;
    req["role"] = assignRole;
    req["dept"] = dept;
    req["job_title"] = jobTitle;
    req["phone"] = phone;
    req["gender"] = gender;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        QString successMsg = QString(
            "账号注册成功！\n"
            "系统已为您分配默认权限 [普通登录]。\n"
            "如需管理权限，请联系人资经理进行审批授权。\n"
            "已为您跳转至登录页。"
        );
        QMessageBox::information(this, "注册成功", successMsg);
        ui->lineEdit_RegAccount->clear();
        ui->lineEdit_RegName->clear();
        ui->lineEdit_RegPwd->clear();
        ui->lineEdit_RegPhone->clear();
        ui->stackedWidget->setCurrentIndex(0);
    }
    else {
        if (res.isEmpty()) {
            QMessageBox::critical(this, "服务器错误", "无法连接到服务端，注册失败！");
        }
        else {
            QMessageBox::warning(this, "注册失败", "该账号可能已被占用！\n" + res["msg"].toString());
        }
    }
}