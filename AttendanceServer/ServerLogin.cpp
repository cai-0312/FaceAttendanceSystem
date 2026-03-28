#include "ServerLogin.h"
#include "ui_ServerLogin.h" 
#include <QMouseEvent>
#include <QSqlDatabase>
#include <QSqlError>
#include <QMessageBox>
#include <QSettings>
#include <QSqlQuery>
#include <QGraphicsDropShadowEffect>
// 构造函数：初始化登录界面指针并配置无边框透明窗口属性
ServerLogin::ServerLogin(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ServerLoginClass)
{
    ui->setupUi(this);
    // 隐藏系统默认边框并启用背景透明，支持自定义UI绘制
    this->setWindowFlags(Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground);

    if (ui->lbl_AdminWarning) {
        QString iconBase = "../../AttendanceServer/icon_library/";
        ui->lbl_AdminWarning->setText(
            "<img src='" + iconBase + "icon_warning.svg' width='14' height='14' align='middle'>&nbsp;"
            "<span style='color: #F56C6C; font-weight: bold;'>仅限拥有操作权限的超级管理员登录</span>"
        );
    }
    if (ui->frame_Background) {
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setOffset(0, 0);
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setBlurRadius(25);
        ui->frame_Background->setGraphicsEffect(shadow);
    }

    // 绑定右上角最小化和关闭按钮的基础窗口调度功能
    connect(ui->btn_Min, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(ui->btn_Close, &QPushButton::clicked, this, &QWidget::close);

    // ==========================================================
    // ⭐️ 核心：服务端 SVG 注入与输入隐藏微交互
    // ==========================================================
    QString iconBase = "../../AttendanceServer/icon_library/";

    // 1. 强行清除 .ui 文件里可能存在的 Emoji 提示词
    ui->lineEdit_DBHost->setPlaceholderText("数据库主机地址 (如 127.0.0.1)");
    ui->lineEdit_DBPort->setPlaceholderText("数据库端口 (如 3305)");
    ui->lineEdit_DBUser->setPlaceholderText("超级管理员账号");
    ui->lineEdit_DBPwd->setPlaceholderText("超级管理员密码");

    // 2. 注入 SVG 图标，并绑定“文字变动 -> 图标隐藏”事件
    QAction* actHost = ui->lineEdit_DBHost->addAction(QIcon(iconBase + "icon_host.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_DBHost, &QLineEdit::textChanged, this, [=](const QString& text) { actHost->setVisible(text.isEmpty()); });

    QAction* actPort = ui->lineEdit_DBPort->addAction(QIcon(iconBase + "icon_port.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_DBPort, &QLineEdit::textChanged, this, [=](const QString& text) { actPort->setVisible(text.isEmpty()); });

    QAction* actUser = ui->lineEdit_DBUser->addAction(QIcon(iconBase + "icon_account.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_DBUser, &QLineEdit::textChanged, this, [=](const QString& text) { actUser->setVisible(text.isEmpty()); });

    QAction* actPwd = ui->lineEdit_DBPwd->addAction(QIcon(iconBase + "icon_password.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_DBPwd, &QLineEdit::textChanged, this, [=](const QString& text) { actPwd->setVisible(text.isEmpty()); });

    // 3. 升级启动按钮
    ui->btn_ServerLogin->setText(" 连 接 并 启 动 服 务 器");
    ui->btn_ServerLogin->setIcon(QIcon(iconBase + "btn_login.svg"));
    ui->btn_ServerLogin->setIconSize(QSize(18, 18));
}
// 析构函数：释放自动生成的UI对象内存
ServerLogin::~ServerLogin()
{
    delete ui;
}
// 登录校验逻辑：连接数据库并比对管理员账号密码
void ServerLogin::on_btn_ServerLogin_clicked()
{
    // 提取前端界面输入的数据库连接配置与管理员鉴权凭证
    QString host = ui->lineEdit_DBHost->text().trimmed();
    QString portStr = ui->lineEdit_DBPort->text().trimmed();
    QString inputUser = ui->lineEdit_DBUser->text().trimmed();
    QString inputPwd = ui->lineEdit_DBPwd->text().trimmed();
    // 基础业务数据非空约束校验
    if (host.isEmpty() || portStr.isEmpty() || inputUser.isEmpty() || inputPwd.isEmpty()) {
        QMessageBox::warning(this, "输入不完整", "请填写完整的登录信息！");
        return;
    }
    // 建立底层数据库连接：采用 ODBC 驱动桥接以保障系统跨平台兼容性
    QSqlDatabase db;
    if (QSqlDatabase::contains("DB_CONN_MAIN")) {
        db = QSqlDatabase::database("DB_CONN_MAIN");
    }
    else {
        db = QSqlDatabase::addDatabase("QODBC", "DB_CONN_MAIN");
    }
    // 动态组合 DSN 数据源连接字符串，装配目标主机及端口参数
    QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=%1;PORT=%2;DATABASE=attendance_db;UID=root;PWD=root;")
        .arg(host).arg(portStr);
    db.setDatabaseName(dsn);
    ui->btn_ServerLogin->setText("正在验证...");
    ui->btn_ServerLogin->setEnabled(false);
    // 检测底层驱动连接状态，若握手失败则重置界面并抛出异常详情
    if (!db.open()) {
        QMessageBox::critical(this, "底层连接失败", "ODBC 连接失败，请检查密码或 ODBC 驱动是否安装！\n" + db.lastError().text());
        ui->btn_ServerLogin->setText("连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
        return;
    }
    // 执行身份鉴权：检索系统用户表校验超级管理员账户密码的合法性
    QSqlQuery query(db);
    query.prepare("SELECT * FROM users WHERE account = :user AND password = :pwd");
    query.bindValue(":user", inputUser);
    query.bindValue(":pwd", inputPwd);
    if (!query.exec()) {
        QMessageBox::critical(this, "查询失败", "执行 user 表查询时出错：\n" + query.lastError().text());
        ui->btn_ServerLogin->setText("连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
        return;
    }
    // 鉴权结果判断：匹配成功则执行配置缓存与主进程交接，否则实施安全拦截
    if (query.next()) {
        // 登录成功，将有效的主机与账号配置下沉至本地注册表或配置文件缓存
        QSettings cfg("AttendanceServer", "DBConfig");
        cfg.setValue("host", host);
        cfg.setValue("port", portStr);
        cfg.setValue("user", inputUser);
        QMessageBox::information(this, "认证成功", "超级管理员身份核验通过！");
        // 触发内部通讯信号，调度系统主循环拉起主控工作台窗口
        emit loginSuccessful();
    }
    else {
        QMessageBox::warning(this, "认证失败", "账号或密码错误，请检查！");
        ui->btn_ServerLogin->setText("连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
    }
}
// 鼠标点击事件拦截：计算并记录无边框窗口拖拽的初始相对坐标
void ServerLogin::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
#else
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
#endif
        event->accept();
    }
}
// 鼠标移动事件拦截：动态计算绝对坐标并实时更新主窗口屏幕物理位置
void ServerLogin::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        move(event->globalPosition().toPoint() - m_dragPosition);
#else
        move(event->globalPos() - m_dragPosition);
#endif
        event->accept();
    }
}