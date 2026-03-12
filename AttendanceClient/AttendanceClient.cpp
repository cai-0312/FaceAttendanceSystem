#include "AttendanceClient.h"
#include "ui_AttendanceClient.h"
#include "MainWidget.h"
#include <QMessageBox>
#include <QGraphicsDropShadowEffect> 
#include <QRegularExpression> 
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStyledItemDelegate> // 🚀 用于渲染高级下拉框
#include <QPainter>
#include <QPixmap>
#include <QIcon>

// 🚀 核心修复：提取为静态工具函数，方便程序启动和点击时随时调用，强行放大 emoji
static QIcon createEmojiIcon(const QString& text, const QString& colorHex) {
    QPixmap pix(26, 26);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QColor(colorHex));
    QFont font = painter.font();
    font.setPixelSize(16); // 保证图标足够大、清晰
    painter.setFont(font);
    painter.drawText(pix.rect(), Qt::AlignCenter, text);
    return QIcon(pix);
}

// 🚀 核心通讯组件：登录和注册必须通过服务器网关进行验证 (逻辑完全未动)
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

AttendanceClient::AttendanceClient(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AttendanceClientClass)
{
    ui->setupUi(this);

    this->setWindowFlags(Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground);

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 0);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setBlurRadius(25);
    ui->frame_Background->setGraphicsEffect(shadow);

    connect(ui->btn_Close, &QPushButton::clicked, this, &AttendanceClient::on_btn_Close_clicked);
    connect(ui->btn_Min, &QPushButton::clicked, this, &AttendanceClient::on_btn_Min_clicked);

    ui->stackedWidget->setCurrentIndex(0);

    // 🚀 核心修复：初始化时默认隐藏密码，并强行渲染高对比度的大尺寸猴子图标
    m_isPwdVisible = false;
    ui->lineEdit_pwd->setEchoMode(QLineEdit::Password);
    m_pwdAction = ui->lineEdit_pwd->addAction(createEmojiIcon("🙈", "#86909C"), QLineEdit::TrailingPosition);
    m_pwdAction->setToolTip("显示密码");

    connect(m_pwdAction, &QAction::triggered, this, &AttendanceClient::togglePasswordVisibility);
    ui->lineEdit_pwd->setCursor(Qt::IBeamCursor);

    // 🚀 注入高级渲染代理，保证 UI 文件中的 QAbstractItemView QSS 生效！
    ui->comboBox_Role->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegGender->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegDept->setItemDelegate(new QStyledItemDelegate(this));
    ui->comboBox_RegJobTitle->setItemDelegate(new QStyledItemDelegate(this));

    // 绑定部门下拉框和职位下拉框的联动 (逻辑未动)
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

    // 初始化触发一次联动更新
    emit ui->comboBox_RegDept->currentTextChanged(ui->comboBox_RegDept->currentText());
}

AttendanceClient::~AttendanceClient() {
    delete ui;
}

// 🚀 核心修复：点击时调用独立的绘图函数切换图标外观
void AttendanceClient::togglePasswordVisibility() {
    m_isPwdVisible = !m_isPwdVisible;

    if (m_isPwdVisible) {
        ui->lineEdit_pwd->setEchoMode(QLineEdit::Normal);
        // 睁眼，亮蓝色
        m_pwdAction->setIcon(createEmojiIcon("👀", "#165DFF"));
        m_pwdAction->setToolTip("隐藏密码");
    }
    else {
        ui->lineEdit_pwd->setEchoMode(QLineEdit::Password);
        // 闭眼，深灰色
        m_pwdAction->setIcon(createEmojiIcon("🙈", "#86909C"));
        m_pwdAction->setToolTip("显示密码");
    }
}

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

// 🚀 登录验证通过 TCP 转发给服务器 (逻辑未动)
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
    req["pwd"] = pwd;
    req["role"] = role;

    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        QString realName = res["real_name"].toString();
        QMessageBox::information(this, "成功", "登录成功！欢迎 " + role + "：" + realName);

        mainWindow = new MainWidget(realName, role);
        mainWindow->show();
        this->hide();
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

// 🚀 注册账号通过 TCP 转发给服务器 (逻辑未动)
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
        QMessageBox::warning(this, "格式错误", "员工账号（工号）不允许包含中文字符，请使用字母或数字！");
        return;
    }

    QRegularExpression rePhone("^\\d{11}$");
    if (!phone.isEmpty() && !rePhone.match(phone).hasMatch()) {
        QMessageBox::warning(this, "格式错误", "手机号码格式不正确，必须为11位纯数字！");
        return;
    }

    if (phone.isEmpty()) {
        phone = "未设置";
    }

    QString assignRole = "普通登录";
    if (dept == "总经办" || dept == "财务部" || jobTitle.contains("部门经理") || jobTitle == "总经理" || jobTitle == "财务总监") {
        assignRole = "管理员登录";
    }

    QJsonObject req;
    req["type"] = "client_register_account";
    req["account"] = account;
    req["pwd"] = pwd;
    req["name"] = name;
    req["role"] = assignRole;
    req["dept"] = dept;
    req["job_title"] = jobTitle;
    req["phone"] = phone;
    req["gender"] = gender;

    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        QString successMsg = QString("账号注册成功！\n系统已根据您的部门及职务自动为您分配 [%1] 权限。\n已为您跳转至登录页。").arg(assignRole);
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
            QMessageBox::warning(this, "注册失败", "该工号可能已被占用或数据库错误！\n" + res["msg"].toString());
        }
    }
}