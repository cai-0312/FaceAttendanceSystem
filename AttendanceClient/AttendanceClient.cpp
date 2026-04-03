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
// 密码哈希处理函数，用于传输前加密密码
static QString hashPassword(const QString& plainPwd) {
    QByteArray hash = QCryptographicHash::hash(plainPwd.toUtf8(), QCryptographicHash::Sha256);
    return QString(hash.toHex());
}
// 构造函数，完成界面初始化和控件绑定
AttendanceClient::AttendanceClient(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AttendanceClientClass)
{
    ui->setupUi(this);
    this->setWindowFlags(Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground);
    // 为背景框添加阴影效果
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 0);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setBlurRadius(25);
    ui->frame_Background->setGraphicsEffect(shadow);
    connect(ui->btn_Close, &QPushButton::clicked, this, &AttendanceClient::on_btn_Close_clicked);
    connect(ui->btn_Min, &QPushButton::clicked, this, &AttendanceClient::on_btn_Min_clicked);
    ui->stackedWidget->setCurrentIndex(0);
    // 统一图标资源路径
    QString iconBase = "../../AttendanceClient/icon_library/AttendanceClient/";
    // 登录页输入框图标，输入时自动隐藏
    QAction* actLogAcc = ui->lineEdit_Account->addAction(QIcon(iconBase + "icon_account.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_Account, &QLineEdit::textChanged, this, [=](const QString& text) { actLogAcc->setVisible(text.isEmpty()); });
    QAction* actLogPwd = ui->lineEdit_pwd->addAction(QIcon(iconBase + "icon_password.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_pwd, &QLineEdit::textChanged, this, [=](const QString& text) { actLogPwd->setVisible(text.isEmpty()); });
    // 注册页输入框图标，输入时自动隐藏
    QAction* actRegAcc = ui->lineEdit_RegAccount->addAction(QIcon(iconBase + "icon_account.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegAccount, &QLineEdit::textChanged, this, [=](const QString& text) { actRegAcc->setVisible(text.isEmpty()); });
    QAction* actRegName = ui->lineEdit_RegName->addAction(QIcon(iconBase + "icon_name.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegName, &QLineEdit::textChanged, this, [=](const QString& text) { actRegName->setVisible(text.isEmpty()); });
    QAction* actRegPwd = ui->lineEdit_RegPwd->addAction(QIcon(iconBase + "icon_password.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegPwd, &QLineEdit::textChanged, this, [=](const QString& text) { actRegPwd->setVisible(text.isEmpty()); });
    QAction* actRegPhone = ui->lineEdit_RegPhone->addAction(QIcon(iconBase + "icon_phone.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_RegPhone, &QLineEdit::textChanged, this, [=](const QString& text) { actRegPhone->setVisible(text.isEmpty()); });
    // 设置按钮文字和图标
    ui->btn_Login->setText(" 立即登录");
    ui->btn_Login->setIcon(QIcon(iconBase + "btn_login.svg"));
    ui->btn_Login->setIconSize(QSize(18, 18));
    ui->btn_ConfirmRegister->setText(" 确认注册");
    ui->btn_ConfirmRegister->setIcon(QIcon(iconBase + "btn_register.svg"));
    ui->btn_ConfirmRegister->setIconSize(QSize(18, 18));
    ui->btn_BackLogin->setText(" 返回登录");
    ui->btn_GoRegister->setText(" 没有账号？立即注册");
    // 配置密码显隐按钮和表单代理样式
    m_isPwdVisible = false;
    ui->lineEdit_pwd->setEchoMode(QLineEdit::Password);
    m_pwdAction = ui->lineEdit_pwd->addAction(QIcon(iconBase + "icon_eye_closed.svg"), QLineEdit::TrailingPosition);
    m_pwdAction->setToolTip("显示密码");
    connect(m_pwdAction, &QAction::triggered, this, &AttendanceClient::togglePasswordVisibility);
    ui->lineEdit_pwd->setCursor(Qt::IBeamCursor);
    // 为下拉框设置统一代理样式
    ui->comboBox_Role->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegGender->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegDept->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegJobTitle->setItemDelegate(new QStyledItemDelegate(this));
    // 按角色设置下拉框图标
    for (int i = 0; i < ui->comboBox_Role->count(); ++i) {
        if (ui->comboBox_Role->itemText(i).contains("管理员")) {
            ui->comboBox_Role->setItemIcon(i, QIcon(iconBase + "icon_admin.svg"));
        }
        else {
            ui->comboBox_Role->setItemIcon(i, QIcon(iconBase + "icon_staff.svg"));
        }
    }
    ui->comboBox_Role->setIconSize(QSize(16, 16));
    // 部门变化时同步更新职务列表
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
    // 触发一次部门联动，初始化职务列表
    emit ui->comboBox_RegDept->currentTextChanged(ui->comboBox_RegDept->currentText());
}
// 析构函数，释放界面资源
AttendanceClient::~AttendanceClient() {
    delete ui;
}
// 退出登录后恢复登录界面
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
// 切换密码显示状态
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
// 处理鼠标按下，记录拖拽起点
void AttendanceClient::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}
// 处理鼠标移动，实现窗口拖动
void AttendanceClient::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
}
// 关闭主窗口
void AttendanceClient::on_btn_Close_clicked() { this->close(); }
// 最小化窗口
void AttendanceClient::on_btn_Min_clicked() { this->showMinimized(); }
// 跳转到注册页
void AttendanceClient::on_btn_GoRegister_clicked() { ui->stackedWidget->setCurrentIndex(1); }
// 返回登录页
void AttendanceClient::on_btn_BackLogin_clicked() { ui->stackedWidget->setCurrentIndex(0); }
// 登录校验并发送哈希后的密码
void AttendanceClient::on_btn_Login_clicked() {
    QString account = ui->lineEdit_Account->text().trimmed();
    QString pwd = ui->lineEdit_pwd->text().trimmed();
    QString role = ui->comboBox_Role->currentText();
    if (account.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "提示", "工号和密码不能为空！");
        return;
    }
    // 组装登录请求
    QJsonObject req;
    req["type"] = "client_login_auth";
    req["account"] = account;
    req["pwd"] = hashPassword(pwd);
    req["role"] = role;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        // 登录成功后进入主界面
        QString realName = res["real_name"].toString();
        bool hasFace = res["has_face"].toBool(true);
        QMessageBox::information(this, "成功", "登录成功！欢迎 " + realName);
        mainWindow = new MainWidget(realName, role, nullptr, this);
        mainWindow->show();
        this->hide();
        if (!hasFace) {
            // 未录入人脸时跳转到人脸采集页
            QMessageBox::warning(mainWindow, "人脸绑定提醒",
                "检测到您尚未录入人脸特征，无法进行人脸打卡！\n系统将自动跳转至人脸录入页面，请先完成人脸采集。");
            mainWindow->forceNavigateTo(2);
        }
    }
    else {
        // 处理登录失败或服务器不可达
        if (res.isEmpty()) {
            QMessageBox::critical(this, "服务器连接失败", "无法连接到考勤总控服务器！\n请检查管理员是否已启动服务端程序。");
        }
        else {
            QMessageBox::critical(this, "失败", "登录失败：工号、密码或角色错误！");
        }
    }
}
// 注册时校验信息并提交默认权限账号
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
    // 校验账号不能包含中文
    QRegularExpression reChinese("[\\x{4e00}-\\x{9fa5}]");
    if (reChinese.match(account).hasMatch()) {
        QMessageBox::warning(this, "格式错误", "员工账号不允许包含中文字符，请使用字母或数字！");
        return;
    }
    // 校验密码长度
    if (pwd.length() < 8) {
        QMessageBox::warning(this, "密码强度不足", "密码长度不能少于8位！");
        return;
    }
    // 校验密码必须同时包含字母和数字
    QRegularExpression reLetter("[a-zA-Z]");
    QRegularExpression reDigit("[0-9]");
    if (!reLetter.match(pwd).hasMatch() || !reDigit.match(pwd).hasMatch()) {
        QMessageBox::warning(this, "密码强度不足", "密码必须同时包含英文字母和数字！");
        return;
    }
    // 校验手机号格式
    QRegularExpression rePhone("^\\d{11}$");
    if (!phone.isEmpty() && !rePhone.match(phone).hasMatch()) {
        QMessageBox::warning(this, "格式错误", "手机号码格式不正确，必须为11位纯数字！");
        return;
    }
    // 手机号为空时使用默认值
    if (phone.isEmpty()) phone = "未设置";
    // 组装注册请求，角色由服务端分配，客户端不传递
    QJsonObject req;
    req["type"] = "client_register_account";
    req["account"] = account;
    req["pwd"] = hashPassword(pwd);
    req["name"] = name;
    req["dept"] = dept;
    req["job_title"] = jobTitle;
    req["phone"] = phone;
    req["gender"] = gender;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        // 注册成功后清空表单并返回登录页
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
        // 显示服务端返回的具体错误信息
        if (res.isEmpty()) {
            QMessageBox::critical(this, "服务器错误", "无法连接到服务端，注册失败！");
        }
        else {
            QString serverMsg = res["msg"].toString();
            QMessageBox::warning(this, "注册失败", serverMsg.isEmpty() ? "注册失败，请稍后重试！" : serverMsg);
        }
    }
}