#include "ServerLogin.h"
#include "ui_ServerLogin.h" 
#include "ConfigEncryptor.h"
#include <QMouseEvent>
#include <QSqlDatabase>
#include <QSqlError>
#include <QMessageBox>
#include <QSettings>
#include <QSqlQuery>
#include <QCryptographicHash>
#include <QGraphicsDropShadowEffect>
// 构造函数：初始化登录界面并配置窗口样式
ServerLogin::ServerLogin(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ServerLoginClass){
    ui->setupUi(this);
    // 隐藏系统边框并启用透明背景以支持自定义窗口样式
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
    // 绑定最小化与关闭按钮的窗口行为
    connect(ui->btn_Min, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(ui->btn_Close, &QPushButton::clicked, this, &QWidget::close);
    QString iconBase = "../../AttendanceServer/icon_library/";
    ui->lineEdit_DBUser->setPlaceholderText("超级管理员账号");
    ui->lineEdit_DBPwd->setPlaceholderText("超级管理员密码");
    QAction* actUser = ui->lineEdit_DBUser->addAction(QIcon(iconBase + "icon_account.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_DBUser, &QLineEdit::textChanged, this, [=](const QString& text) { actUser->setVisible(text.isEmpty()); });
    QAction* actPwd = ui->lineEdit_DBPwd->addAction(QIcon(iconBase + "icon_password.svg"), QLineEdit::LeadingPosition);
    connect(ui->lineEdit_DBPwd, &QLineEdit::textChanged, this, [=](const QString& text) { actPwd->setVisible(text.isEmpty()); });
    // 配置登录按钮文本与图标
    ui->btn_ServerLogin->setText(" 连 接 并 启 动 服 务 器");
    ui->btn_ServerLogin->setIcon(QIcon(iconBase + "btn_login.svg"));
    ui->btn_ServerLogin->setIconSize(QSize(18, 18));
}
// 析构函数：释放 UI 资源
ServerLogin::~ServerLogin()
{
    delete ui;
}
void ServerLogin::on_btn_ServerLogin_clicked()
{
    // 处理登录按钮点击：使用加密配置连接数据库并验证管理员身份
    QString inputUser = ui->lineEdit_DBUser->text().trimmed();
    QString inputPwd = ui->lineEdit_DBPwd->text().trimmed();

    if (inputUser.isEmpty() || inputPwd.isEmpty()) {
        QMessageBox::warning(this, "输入不完整", "请填写管理员账号和密码！");
        return;
    }
    // 从加密配置文件加载数据库连接参数
    QString configPath = QCoreApplication::applicationDirPath() + "/server_config.enc";
    QJsonObject config = ConfigEncryptor::loadEncryptedConfig(configPath);
    if (config.isEmpty()) {
        QMessageBox::critical(this, "配置错误",
            "无法加载数据库配置文件！\n"
            "请确认：\n"
            "1. server_config.enc 存在于程序目录\n"
            "2. 环境变量 ATTENDANCE_CONFIG_KEY 已正确设置");
        return;
    }
    QString host = config["db_host"].toString();
    QString port = config["db_port"].toString();
    QString dbUser = config["db_user"].toString();  
    QString dbPwd = config["db_pwd"].toString();
    // 使用配置中的降级账号建立数据库连接
    QSqlDatabase db;
    if (QSqlDatabase::contains("DB_CONN_MAIN")) {
        db = QSqlDatabase::database("DB_CONN_MAIN");
    }
    else {
        db = QSqlDatabase::addDatabase("QODBC", "DB_CONN_MAIN");
    }
    QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
        "SERVER=%1;PORT=%2;DATABASE=attendance_db;"
        "UID=%3;PWD=%4;CHARSET=utf8mb4;")
        .arg(host, port, dbUser, dbPwd);
    db.setDatabaseName(dsn);
    // 更新 UI 提示为正在验证并禁用按钮以防重复提交
    ui->btn_ServerLogin->setText("正在验证...");
    ui->btn_ServerLogin->setEnabled(false);
    if (!db.open()) {
        QMessageBox::critical(this, "连接失败",
            "数据库连接失败，请联系系统管理员检查配置！\n" + db.lastError().text());
        ui->btn_ServerLogin->setText(" 连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
        return;
    }
    // 验证管理员身份：通过 SHA256 哈希比对密码
    QSqlQuery query(db);
    QString pwdHash = QString(QCryptographicHash::hash(
        inputPwd.toUtf8(), QCryptographicHash::Sha256).toHex());
    query.prepare("SELECT * FROM users WHERE account = :user "
        "AND (password = :hash OR SHA2(password, 256) = :hash) "
        "AND role = '超级管理员'");
    query.bindValue(":user", inputUser);
    query.bindValue(":hash", pwdHash);
    if (!query.exec() || !query.next()) {
        QMessageBox::warning(this, "认证失败", "管理员账号或密码错误！");
        ui->btn_ServerLogin->setText(" 连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
        return;
    }
    // 登录成功，仅缓存非敏感的主机信息（不存密码）
    QSettings cfg("AttendanceServer", "DBConfig");
    cfg.setValue("host", host);
    QMessageBox::information(this, "认证成功", "超级管理员身份核验通过！");
    emit loginSuccessful();
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