#include "AttendanceServer.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

AttendanceServer::AttendanceServer(QWidget* parent)
    : QWidget(parent), ui(new Ui::AttendanceServerClass)
{
    ui->setupUi(this);

    initDatabase();

    m_globalRecordModel = new QSqlQueryModel(this);
    ui->tableView_GlobalRecords->setModel(m_globalRecordModel);
    ui->tableView_GlobalRecords->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableView_GlobalRecords->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView_GlobalRecords->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView_GlobalRecords->verticalHeader()->setVisible(false);

    loadGlobalRecords();

    // 🚀 初始化 TCP 服务器
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &AttendanceServer::onNewConnection);

    // 美化在线用户表格
    ui->tableWidget_OnlineUsers->setColumnCount(4);
    ui->tableWidget_OnlineUsers->setHorizontalHeaderLabels({ "账号/姓名", "工作部门", "IP地址", "登录时间" });
    ui->tableWidget_OnlineUsers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // 绑定启动/停止按钮
    connect(ui->btn_StartServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StartServer_clicked);
    connect(ui->btn_StopServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StopServer_clicked);
}

AttendanceServer::~AttendanceServer() {
    delete ui;
}

// === 辅助函数：打印彩色日志 ===
void AttendanceServer::logMessage(const QString& msg) {
    QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    ui->textBrowser_ServerLog->append(timeStr + msg);
}

// === 🚀 1. 服务器启停逻辑 ===
void AttendanceServer::on_btn_StartServer_clicked() {
    quint16 port = ui->lineEdit_Port->text().toUShort();
    if (m_tcpServer->listen(QHostAddress::Any, port)) {
        ui->label_ServerStatus->setText("🟢 当前状态：运行中 (端口: " + QString::number(port) + ")");
        ui->label_ServerStatus->setStyleSheet("color: #67C23A;");
        ui->btn_StartServer->setEnabled(false);
        ui->btn_StopServer->setEnabled(true);
        ui->lineEdit_Port->setEnabled(false);
        logMessage(QString("<font color='#67C23A'>TCP 核心路由引擎已启动，正在监听端口 %1...</font>").arg(port));
    }
    else {
        QMessageBox::critical(this, "启动失败", "端口被占用或权限不足！");
        logMessage("<font color='red'>启动失败：" + m_tcpServer->errorString() + "</font>");
    }
}

void AttendanceServer::on_btn_StopServer_clicked() {
    m_tcpServer->close();
    // 断开所有现有连接
    for (QTcpSocket* socket : m_clients.keys()) {
        socket->disconnectFromHost();
    }
    m_clients.clear();
    m_nameToSocket.clear();
    updateOnlineUsersTable();

    ui->label_ServerStatus->setText("🔴 当前状态：未启动");
    ui->label_ServerStatus->setStyleSheet("color: black;");
    ui->btn_StartServer->setEnabled(true);
    ui->btn_StopServer->setEnabled(false);
    ui->lineEdit_Port->setEnabled(true);
    logMessage("<font color='#E6A23C'>TCP 核心路由引擎已安全关闭。</font>");
}

// === 🚀 2. 客户端连接与断开 ===
void AttendanceServer::onNewConnection() {
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &AttendanceServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &AttendanceServer::onClientDisconnected);
    logMessage(QString("🔗 收到新的物理连接请求: %1").arg(socket->peerAddress().toString()));
}

void AttendanceServer::onClientDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    if (m_clients.contains(socket)) {
        QString name = m_clients[socket].name;
        logMessage(QString("<font color='#F56C6C'>❌ 节点掉线: [%1] 已断开连接。</font>").arg(name));
        m_nameToSocket.remove(name);
        m_clients.remove(socket);
        updateOnlineUsersTable(); // 刷新在线雷达表格
    }
    socket->deleteLater();
}

// === 🚀 3. 核心路由器：拆解 JSON 并转发 ===
void AttendanceServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    while (socket->canReadLine()) {
        QByteArray data = socket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject json = doc.object();
        QString type = json["type"].toString();

        // 🌟 场景 A：客户端刚连接上，发送了身份认证 (Login)
        if (type == "login") {
            QString name = json["name"].toString();
            QString ip = socket->peerAddress().toString().remove("::ffff:");

            // 顺手去 MySQL 里查一下这个人的部门！让表格显得更高级
            QString dept = "未知部门";
            QSqlDatabase db = QSqlDatabase::database("server_db_connection");
            QSqlQuery query(db);
            query.prepare("SELECT department FROM users WHERE name = :name");
            query.bindValue(":name", name);
            if (query.exec() && query.next()) {
                dept = query.value(0).toString();
            }

            ClientInfo info{ name, dept, ip, QDateTime::currentDateTime().toString("HH:mm:ss") };
            m_clients[socket] = info;
            m_nameToSocket[name] = socket; // 登记路由表

            logMessage(QString("<font color='#409EFF'>🟢 认证成功: 员工 [%1] (%2) 已接入总控网关。</font>").arg(name, dept));
            updateOnlineUsersTable();
        }
        // 🌟 场景 B：客户端发聊天消息
        else if (type == "chat") {
            // ... 保持原有 ...
            QString fromUser = json["from"].toString();
            QString toUser = json["to"].toString();
            logMessage(QString("<font color='#E6A23C'>✉️ 消息审计:</font> [%1] -> [%2]: %3").arg(fromUser, toUser, json["msg"].toString()));
            if (m_nameToSocket.contains(toUser)) m_nameToSocket[toUser]->write(data);
        }
        // 🌟 场景 C：客户端发送文件 (拦截并备份到服务端工号目录)
        else if (type == "file") {
            QString fromUser = json["from"].toString();
            QString toUser = json["to"].toString();
            QString fileName = json["filename"].toString();

            // 查出发送人的工号
            QString empId = "Unknown";
            QSqlQuery query(QSqlDatabase::database("server_db_connection"));
            query.prepare("SELECT id FROM users WHERE name = :n");
            query.bindValue(":n", fromUser);
            if (query.exec() && query.next()) empId = query.value(0).toString();

            // [需求实现]：服务端备份到 /server/工号/ 目录
            QByteArray fileData = QByteArray::fromBase64(json["filedata"].toString().toUtf8());
            QString serverDirPath = QCoreApplication::applicationDirPath() + "/server/" + empId;
            QDir().mkpath(serverDirPath);
            QFile file(serverDirPath + "/" + fileName);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(fileData);
                file.close();
                logMessage(QString("<font color='#67C23A'>📁 文件审计:</font> 拦截到 [%1] 发送的文件 '%2'，已云端备份至工号 %3 目录。").arg(fromUser, fileName, empId));
            }

            // 存完之后，继续路由原数据包给接收方！
            if (m_nameToSocket.contains(toUser)) {
                m_nameToSocket[toUser]->write(data);
            }
        
        }
    }
}

// === 辅助函数：实时刷新在线用户大表 ===
void AttendanceServer::updateOnlineUsersTable() {
    ui->tableWidget_OnlineUsers->setRowCount(0);
    for (const ClientInfo& info : m_clients.values()) {
        int row = ui->tableWidget_OnlineUsers->rowCount();
        ui->tableWidget_OnlineUsers->insertRow(row);
        ui->tableWidget_OnlineUsers->setItem(row, 0, new QTableWidgetItem("👤 " + info.name));
        ui->tableWidget_OnlineUsers->setItem(row, 1, new QTableWidgetItem("🏢 " + info.dept));
        ui->tableWidget_OnlineUsers->setItem(row, 2, new QTableWidgetItem("🌐 " + info.ip));
        ui->tableWidget_OnlineUsers->setItem(row, 3, new QTableWidgetItem("🕒 " + info.loginTime));
    }
}

// ---------------- 以下为数据库部分 ----------------
void AttendanceServer::initDatabase() {
    if (!QSqlDatabase::contains("server_db_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", "server_db_connection");
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3305;DATABASE=attendance_db;UID=root;PWD=root;");
        db.setDatabaseName(dsn);
        if (!db.open()) QMessageBox::critical(this, "错误", "无法连接到中心数据库！");
    }
}

void AttendanceServer::loadGlobalRecords() {
    QString sql = "SELECT ROW_NUMBER() OVER (ORDER BY r.punch_time DESC) AS '序号', "
        "r.name AS '姓名', u.department AS '部门', "
        "DATE_FORMAT(r.punch_time, '%Y-%m-%d %H:%i:%s') AS '打卡时间', r.status AS '状态' "
        "FROM attendance_records r LEFT JOIN users u ON r.name = u.name ORDER BY r.punch_time DESC";
    QSqlDatabase db = QSqlDatabase::database("server_db_connection");
    m_globalRecordModel->setQuery(sql, db);
}

void AttendanceServer::on_btn_RefreshData_clicked() {
    loadGlobalRecords();
}

void AttendanceServer::on_btn_ExportGlobal_clicked() {
    QString fileName = QFileDialog::getSaveFileName(this, "导出全公司考勤报表",
        "全公司考勤大表_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm") + ".csv", "CSV (*.csv)");

    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);

        // Qt 6 写法
        out.setEncoding(QStringConverter::Utf8);
        out << QString("\xEF\xBB\xBF") << "序号,姓名,部门,打卡时间,状态\n";

        int rowCount = m_globalRecordModel->rowCount();
        int colCount = m_globalRecordModel->columnCount();

        for (int r = 0; r < rowCount; ++r) {
            QStringList rowData;
            for (int c = 0; c < colCount; ++c) {
                rowData << m_globalRecordModel->data(m_globalRecordModel->index(r, c)).toString();
            }
            out << rowData.join(",") << "\n";
        }
        file.close();
        QMessageBox::information(this, "成功", QString("成功导出 %1 条全局考勤记录！").arg(rowCount));
    }
}