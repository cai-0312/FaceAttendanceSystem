#include "ServerLogin.h"
#include "ui_ServerLogin.h" 

#include <QMouseEvent>
#include <QSqlDatabase>
#include <QSqlError>
#include <QMessageBox>
#include <QSettings>
#include <QSqlQuery>

// 构造函数中实例化 ui 指针并调用 setupUi
ServerLogin::ServerLogin(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ServerLoginClass)
{
    ui->setupUi(this);

    // 隐藏系统边框并设置背景透明
    this->setWindowFlags(Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground);

    // 绑定登录按钮信号槽
    //connect(ui->btn_ServerLogin, &QPushButton::clicked, this, &ServerLogin::on_btn_ServerLogin_clicked);

    // 绑定右上角最小化和关闭按钮的基础功能
    connect(ui->btn_Min, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(ui->btn_Close, &QPushButton::clicked, this, &QWidget::close);
}

// 析构函数中释放 ui 内存
ServerLogin::~ServerLogin()
{
    delete ui;
}

void ServerLogin::on_btn_ServerLogin_clicked()
{
    // 1. 获取界面输入（此时 inputUser 和 inputPwd 代表的是你 user 表里的考勤系统管理员）
    QString host = ui->lineEdit_DBHost->text().trimmed();
    QString portStr = ui->lineEdit_DBPort->text().trimmed();
    QString inputUser = ui->lineEdit_DBUser->text().trimmed();
    QString inputPwd = ui->lineEdit_DBPwd->text().trimmed();

    // 基础非空校验
    if (host.isEmpty() || portStr.isEmpty() || inputUser.isEmpty() || inputPwd.isEmpty()) {
        QMessageBox::warning(this, "输入不完整", "请填写完整的登录信息！");
        return;
    }

    // ==========================================
    // 🔑 第一层：使用 ODBC 绕过 Qt 的 MySQL 驱动限制
    // ==========================================
    QSqlDatabase db;
    if (QSqlDatabase::contains("DB_CONN_MAIN")) {
        db = QSqlDatabase::database("DB_CONN_MAIN");
    }
    else {
        // 🚀 核心改动 1：把 QMYSQL 换成 QODBC
        db = QSqlDatabase::addDatabase("QODBC", "DB_CONN_MAIN");
    }

    // 🚀 核心改动 2：组合 DSN 字符串 (注意把里面的 root 密码改成你真实的密码)
    QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=%1;PORT=%2;DATABASE=attendance_db;UID=root;PWD=root;")
        .arg(host).arg(portStr);

    db.setDatabaseName(dsn);

    ui->btn_ServerLogin->setText("正在验证...");
    ui->btn_ServerLogin->setEnabled(false);

    if (!db.open()) {
        QMessageBox::critical(this, "底层连接失败", "ODBC 连接失败，请检查密码或 ODBC 驱动是否安装！\n" + db.lastError().text());
        ui->btn_ServerLogin->setText("连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
        return;
    }

    // ==========================================
    // 👤 第二层：去 user 表里验证界面输入的账号密码
    // ==========================================
    QSqlQuery query(db);

    // 确保你的数据库表名是 user，字段名是 account 和 password
    query.prepare("SELECT * FROM users WHERE account = :user AND password = :pwd");
    query.bindValue(":user", inputUser);
    query.bindValue(":pwd", inputPwd);

    if (!query.exec()) {
        QMessageBox::critical(this, "查询失败", "执行 user 表查询时出错：\n" + query.lastError().text());
        ui->btn_ServerLogin->setText("连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
        return;
    }

    // 3. 判断是否查到了匹配的数据
    if (query.next()) {
        // 登录成功，保存上一次成功连接的配置（不存密码更安全）
        QSettings cfg("AttendanceServer", "DBConfig");
        cfg.setValue("host", host);
        cfg.setValue("port", portStr);
        cfg.setValue("user", inputUser);

        QMessageBox::information(this, "认证成功", "超级管理员身份核验通过！");

        // 触发跳转信号给 main.cpp
        emit loginSuccessful();
    }
    else {
        // 没查到数据
        QMessageBox::warning(this, "认证失败", "账号或密码错误，请检查！");
        ui->btn_ServerLogin->setText("连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
    }
}

// ==========================================
// 🖱️ 无边框窗口拖拽实现区域
// ==========================================

// 鼠标按下时，记录当前相对坐标
void ServerLogin::mousePressEvent(QMouseEvent* event)
{
    // 只有左键按下才允许拖动
    if (event->button() == Qt::LeftButton) {
        // 自动兼容 Qt5 和 Qt6 的坐标获取方式
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
#else
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
#endif
        event->accept();
    }
}

// 鼠标移动时，更新窗口位置
void ServerLogin::mouseMoveEvent(QMouseEvent* event)
{
    // 持续按住左键移动时
    if (event->buttons() & Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        move(event->globalPosition().toPoint() - m_dragPosition);
#else
        move(event->globalPos() - m_dragPosition);
#endif
        event->accept();
    }
}