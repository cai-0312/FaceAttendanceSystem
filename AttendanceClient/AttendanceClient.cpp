#include "AttendanceClient.h"
#include "ui_AttendanceClient.h"
#include "MainWidget.h"
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSqlDatabase>
#include <QGraphicsDropShadowEffect> 
#include <QRegularExpression> 

AttendanceClient::AttendanceClient(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AttendanceClientClass),
    m_isPwdVisible(false)
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
    ui->lineEdit_pwd->setEchoMode(QLineEdit::Password);

    m_pwdAction = ui->lineEdit_pwd->addAction(QIcon(), QLineEdit::TrailingPosition);
    m_pwdAction->setText("👁️");
    connect(m_pwdAction, &QAction::triggered, this, &AttendanceClient::togglePasswordVisibility);
    ui->lineEdit_pwd->setCursor(Qt::IBeamCursor);

    // 绑定部门下拉框和职位下拉框的联动
    connect(ui->comboBox_RegDept, &QComboBox::currentTextChanged, this, [=](const QString& dept) {
        ui->comboBox_RegJobTitle->clear();
        if (dept == "总裁办") {
            ui->comboBox_RegJobTitle->addItems({ "总裁", "行政助理", "财务总监" });
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

    if (!QSqlDatabase::contains("qt_sql_default_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC");
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
            "SERVER=127.0.0.1;PORT=3305;DATABASE=attendance_db;"
            "UID=root;PWD=root;");
        db.setDatabaseName(dsn);
        if (!db.open()) {
            QMessageBox::critical(this, "数据库启动失败", "无法连接到 MySQL！\n" + db.lastError().text());
        }
    }
}

AttendanceClient::~AttendanceClient() {
    delete ui;
}

void AttendanceClient::togglePasswordVisibility() {
    m_isPwdVisible = !m_isPwdVisible;
    if (m_isPwdVisible) {
        ui->lineEdit_pwd->setEchoMode(QLineEdit::Normal);
        m_pwdAction->setText("🙈");
    }
    else {
        ui->lineEdit_pwd->setEchoMode(QLineEdit::Password);
        m_pwdAction->setText("👁️");
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

void AttendanceClient::on_btn_Login_clicked() {
    QString account = ui->lineEdit_Account->text().trimmed();
    QString pwd = ui->lineEdit_pwd->text().trimmed();
    QString role = ui->comboBox_Role->currentText();

    if (account.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "提示", "工号和密码不能为空！");
        return;
    }

    QString sql = QString("SELECT name, department FROM users WHERE account = '%1' AND password = '%2' AND role = '%3'")
        .arg(account, pwd, role);
    QSqlQuery query(sql);

    if (query.next()) {
        QString realName = query.value(0).toString();
        QMessageBox::information(this, "成功", "登录成功！欢迎 " + role + "：" + realName);

        mainWindow = new MainWidget(realName, role);
        mainWindow->show();
        this->hide();
    }
    else {
        QMessageBox::critical(this, "失败", "登录失败：工号、密码或角色错误！");
    }
}

void AttendanceClient::on_btn_ConfirmRegister_clicked() {
    QString account = ui->lineEdit_RegAccount->text().trimmed();
    QString name = ui->lineEdit_RegName->text().trimmed();
    QString pwd = ui->lineEdit_RegPwd->text().trimmed();
    QString phone = ui->lineEdit_RegPhone->text().trimmed();
    QString gender = ui->comboBox_RegGender->currentText();
    QString dept = ui->comboBox_RegDept->currentText();
    QString jobTitle = ui->comboBox_RegJobTitle->currentText();

    // 1. 基础非空校验
    if (account.isEmpty() || name.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "提示", "工号、姓名、密码为必填项！");
        return;
    }

    // 2. 账号（工号）中文字符拦截校验
    QRegularExpression reChinese("[\\x{4e00}-\\x{9fa5}]");
    if (reChinese.match(account).hasMatch()) {
        QMessageBox::warning(this, "格式错误", "员工账号（工号）不允许包含中文字符，请使用字母或数字！");
        return;
    }

    // 3. 手机号 11 位纯数字校验
    QRegularExpression rePhone("^\\d{11}$");
    if (!phone.isEmpty() && !rePhone.match(phone).hasMatch()) {
        QMessageBox::warning(this, "格式错误", "手机号码格式不正确，必须为11位纯数字！");
        return;
    }

    if (phone.isEmpty()) {
        phone = "未设置";
    }

    // 4. 权限自动分配逻辑：总裁办全员、财务部全员、以及其他部门的“部门经理”或高级管理层自动具有管理员权限
    QString assignRole = "普通员工";
    if (dept == "总裁办" || dept == "财务部" || jobTitle.contains("部门经理") || jobTitle == "总裁" || jobTitle == "财务总监") {
        assignRole = "管理员";
    }

    // 5. 数据入库（规避驱动绑值问题，使用原生拼接）
    QString sql = QString("INSERT INTO users (account, password, name, role, department, job_title, phone, gender) "
        "VALUES ('%1', '%2', '%3', '%4', '%5', '%6', '%7', '%8')")
        .arg(account, pwd, name, assignRole, dept, jobTitle, phone, gender);

    QSqlQuery query;
    if (query.exec(sql)) {
        QString successMsg = QString("账号注册成功！\n系统已根据您的部门及职务自动为您分配 [%1] 权限。\n已为您跳转至登录页。").arg(assignRole);
        QMessageBox::information(this, "注册成功", successMsg);

        ui->lineEdit_RegAccount->clear();
        ui->lineEdit_RegName->clear();
        ui->lineEdit_RegPwd->clear();
        ui->lineEdit_RegPhone->clear();
        ui->stackedWidget->setCurrentIndex(0);
    }
    else {
        QMessageBox::warning(this, "注册失败", "该工号可能已被占用或数据库错误！\n" + query.lastError().text());
    }
}