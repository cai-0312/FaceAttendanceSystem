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
    QString iconBase = "../../AttendanceServer/icon_library/";

    // 1. 强行清除 .ui 文件里可能存在的 Emoji 提示词
    ui->lineEdit_DBUser->setPlaceholderText("超级管理员账号");
    ui->lineEdit_DBPwd->setPlaceholderText("超级管理员密码");

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
void ServerLogin::on_btn_ServerLogin_clicked()
{
    // 问题四：UI 仅收集管理员账号密码，数据库连接信息从加密配置读取
    QString inputUser = ui->lineEdit_DBUser->text().trimmed();
    QString inputPwd = ui->lineEdit_DBPwd->text().trimmed();

    if (inputUser.isEmpty() || inputPwd.isEmpty()) {
        QMessageBox::warning(this, "输入不完整", "请填写管理员账号和密码！");
        return;
    }

    // 1. 从加密配置文件读取数据库连接参数
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

    // 2. 使用降级账号建立数据库连接（不再使用 root）
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

    ui->btn_ServerLogin->setText("正在验证...");
    ui->btn_ServerLogin->setEnabled(false);

    if (!db.open()) {
        QMessageBox::critical(this, "连接失败",
            "数据库连接失败，请联系系统管理员检查配置！\n" + db.lastError().text());
        ui->btn_ServerLogin->setText(" 连 接 并 启 动 服 务 器");
        ui->btn_ServerLogin->setEnabled(true);
        return;
    }

    // 3. 验证管理员身份（使用哈希比对，不再明文查询）
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

    // 4. 登录成功，仅缓存非敏感的主机信息（不存密码）
    QSettings cfg("AttendanceServer", "DBConfig");
    cfg.setValue("host", host);
    // 注意：不再保存 user、pwd 等敏感字段

    QMessageBox::information(this, "认证成功", "超级管理员身份核验通过！");
    emit loginSuccessful();
}// 鼠标点击事件拦截：计算并记录无边框窗口拖拽的初始相对坐标
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