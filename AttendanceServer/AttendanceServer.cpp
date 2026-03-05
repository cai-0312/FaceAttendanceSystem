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
#include <QDir>
#include <QCoreApplication>

AttendanceServer::AttendanceServer(QWidget* parent)
    : QWidget(parent), ui(new Ui::AttendanceServerClass)
{
    ui->setupUi(this);

    initDatabase();

    // 配置全局考勤记录表格模型与显示属性
    m_globalRecordModel = new QSqlQueryModel(this);
    ui->tableView_GlobalRecords->setModel(m_globalRecordModel);
    ui->tableView_GlobalRecords->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableView_GlobalRecords->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView_GlobalRecords->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView_GlobalRecords->verticalHeader()->setVisible(false);

    loadGlobalRecords();

    // 初始化TCP服务器并绑定新连接信号
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &AttendanceServer::onNewConnection);

    // 设置在线用户统计表格的列名与布局
    ui->tableWidget_OnlineUsers->setColumnCount(4);
    ui->tableWidget_OnlineUsers->setHorizontalHeaderLabels({ "账号/姓名", "工作部门", "IP地址", "登录时间" });
    ui->tableWidget_OnlineUsers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // 绑定服务器启停控制按钮
    connect(ui->btn_StartServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StartServer_clicked);
    connect(ui->btn_StopServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StopServer_clicked);
}

AttendanceServer::~AttendanceServer() {
    delete ui;
}

// 日志系统：在服务器日志文本框中追加带时间戳的消息
void AttendanceServer::logMessage(const QString& msg) {
    QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    ui->textBrowser_ServerLog->append(timeStr + msg);
}

// 启动服务器槽函数：根据指定端口开启监听模式
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

// 停止服务器槽函数：关闭监听并断开所有已连接的客户端
void AttendanceServer::on_btn_StopServer_clicked() {
    m_tcpServer->close();
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

// 新连接处理：为每个连接建立的Socket绑定读取与断开信号
void AttendanceServer::onNewConnection() {
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &AttendanceServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &AttendanceServer::onClientDisconnected);
    logMessage(QString("🔗 收到新的物理连接请求: %1").arg(socket->peerAddress().toString()));
}

// 客户端断开处理：从路由映射表中移除对应人员并刷新在线列表
void AttendanceServer::onClientDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    if (m_clients.contains(socket)) {
        QString name = m_clients[socket].name;
        logMessage(QString("<font color='#F56C6C'>❌ 节点掉线: [%1] 已断开连接。</font>").arg(name));
        m_nameToSocket.remove(name);
        m_clients.remove(socket);
        updateOnlineUsersTable();
    }
    socket->deleteLater();
}

// 核心协议处理器：负责身份验证、消息路由转发、文件审计及离线消息暂存
void AttendanceServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QSqlDatabase db = QSqlDatabase::database("server_db_connection");

    while (socket->canReadLine()) {
        QByteArray data = socket->readLine();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject json = doc.object();
        QString type = json["type"].toString();

        // 登录场景：注册Socket映射关系并推送该用户的历史离线消息
        if (type == "login") {
            QString name = json["name"].toString();
            QString ip = socket->peerAddress().toString().remove("::ffff:");

            QString dept = "未知部门";
            QSqlQuery query(db);
            query.prepare("SELECT department FROM users WHERE name = :name");
            query.bindValue(":name", name);
            if (query.exec() && query.next()) dept = query.value(0).toString();

            ClientInfo info{ name, dept, ip, QDateTime::currentDateTime().toString("HH:mm:ss") };
            m_clients[socket] = info;
            m_nameToSocket[name] = socket;

            logMessage(QString("<font color='#409EFF'>🟢 认证成功: 员工 [%1] (%2) 已接入总控网关。</font>").arg(name, dept));
            updateOnlineUsersTable();

            // 执行离线补偿：检索并发送存储在数据库中的离线消息
            QSqlQuery offlineQ(db);
            offlineQ.prepare("SELECT sender, msg_type, content, filename, send_time FROM offline_messages WHERE receiver=:n ORDER BY send_time ASC");
            offlineQ.bindValue(":n", name);
            if (offlineQ.exec()) {
                int offlineCount = 0;
                while (offlineQ.next()) {
                    QJsonObject offMsg;
                    offMsg["type"] = offlineQ.value(1).toString();
                    offMsg["from"] = offlineQ.value(0).toString();
                    offMsg["msg"] = offlineQ.value(2).toString();
                    offMsg["filename"] = offlineQ.value(3).toString();
                    offMsg["time"] = offlineQ.value(4).toDateTime().toString("HH:mm:ss");
                    offMsg["is_offline"] = true;
                    socket->write(QJsonDocument(offMsg).toJson(QJsonDocument::Compact) + "\n");
                    offlineCount++;
                }
                if (offlineCount > 0) logMessage(QString("<font color='#E6A23C'>📥 已向 [%1] 补发 %2 条离线消息。</font>").arg(name).arg(offlineCount));
            }
            offlineQ.exec(QString("DELETE FROM offline_messages WHERE receiver='%1'").arg(name));
        }

        // 回执路由：将已读回执透明转发给原始发送方
        else if (type == "read_receipt") {
            QString toUser = json["to"].toString();
            if (m_nameToSocket.contains(toUser)) {
                m_nameToSocket[toUser]->write(data);
            }
        }

        // 单聊场景：支持文本及多媒体，包含服务器端文件强制审计备份功能
        else if (type == "chat" || type == "image" || type == "file") {
            QString fromUser = json["from"].toString();
            QString toUser = json["to"].toString();
            QString content = json["msg"].toString();

            if (type == "chat") logMessage(QString("<font color='#E6A23C'>✉️ 单聊审计:</font> [%1] -> [%2]: %3").arg(fromUser, toUser, content));

            // 审计逻辑：将客户端发送的二进制文件还原并按工号存储在服务器硬盘
            if (type == "file" || type == "image") {
                QString fileName = json["filename"].toString();
                if (fileName.isEmpty()) fileName = "image_audit_" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + ".png";

                QString empId = "Unknown";
                QSqlQuery query(db);
                query.prepare("SELECT id FROM users WHERE name = :n");
                query.bindValue(":n", fromUser);
                if (query.exec() && query.next()) empId = query.value(0).toString();

                QByteArray fileData = QByteArray::fromBase64(content.toUtf8());
                QString serverDirPath = QCoreApplication::applicationDirPath() + "/server/" + empId;
                QDir().mkpath(serverDirPath);
                QFile file(serverDirPath + "/" + fileName);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(fileData);
                    file.close();
                    logMessage(QString("<font color='#67C23A'>📁 文件审计:</font> 拦截到 [%1] 发送的文件 '%2'，已备份至工号 %3 目录。").arg(fromUser, fileName, empId));
                }
            }

            // 路由分发：对方在线则即时转发，不在线则存入离线表
            if (m_nameToSocket.contains(toUser)) {
                m_nameToSocket[toUser]->write(data);
            }
            else {
                QSqlQuery insertQ(db);
                insertQ.prepare("INSERT INTO offline_messages (sender, receiver, msg_type, content, filename, send_time) VALUES (:s, :r, :t, :c, :f, NOW())");
                insertQ.bindValue(":s", fromUser); insertQ.bindValue(":r", toUser);
                insertQ.bindValue(":t", type); insertQ.bindValue(":c", content);
                insertQ.bindValue(":f", json["filename"].toString());
                insertQ.exec();
                logMessage(QString("📥 [%1] 不在线，消息已转入离线信箱。").arg(toUser));
            }
        }

        // 群聊场景：检索部门全体成员并执行广播分发
        else if (type.startsWith("group_")) {
            QString fromUser = json["from"].toString();
            QString dept = json["department"].toString();

            logMessage(QString("<font color='#9C27B0'>📢 群发路由:</font> [%1] 向全员群 [%2] 广播了消息。").arg(fromUser, dept));

            QSqlQuery groupQ(db);
            groupQ.prepare("SELECT name FROM users WHERE department = :d");
            groupQ.bindValue(":d", dept);
            if (groupQ.exec()) {
                while (groupQ.next()) {
                    QString member = groupQ.value(0).toString();
                    if (member == fromUser) continue;

                    if (m_nameToSocket.contains(member)) {
                        m_nameToSocket[member]->write(data);
                    }
                    else {
                        QSqlQuery insertQ(db);
                        insertQ.prepare("INSERT INTO offline_messages (sender, receiver, msg_type, content, filename, send_time) VALUES (:s, :r, :t, :c, :f, NOW())");
                        insertQ.bindValue(":s", fromUser); insertQ.bindValue(":r", member);
                        insertQ.bindValue(":t", type); insertQ.bindValue(":c", json["msg"].toString());
                        insertQ.bindValue(":f", json["filename"].toString());
                        insertQ.exec();
                    }
                }
            }
        }
    }
}

// UI辅助函数：清空并重新构建在线用户列表
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

// 数据库初始化：配置MySQL驱动并创建离线消息存储表
void AttendanceServer::initDatabase() {
    if (!QSqlDatabase::contains("server_db_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", "server_db_connection");
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3305;DATABASE=attendance_db;UID=root;PWD=root;");
        db.setDatabaseName(dsn);
        if (!db.open()) {
            QMessageBox::critical(this, "错误", "无法连接到中心数据库！");
            return;
        }
    }

    QSqlDatabase db = QSqlDatabase::database("server_db_connection");
    QSqlQuery query(db);
    // 自动建立离线信箱表，用于存储由于接收方不在线而阻塞的JSON报文
    query.exec("CREATE TABLE IF NOT EXISTS offline_messages ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "sender VARCHAR(50), receiver VARCHAR(50), "
        "msg_type VARCHAR(20), content LONGTEXT, filename VARCHAR(255), send_time DATETIME)");
}

// 加载全局报表：通过联合查询获取全体人员的实时打卡流水
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

// 全局导出：将全公司的考勤大表导出为包含BOM头的CSV文件，防止Excel打开乱码
void AttendanceServer::on_btn_ExportGlobal_clicked() {
    QString fileName = QFileDialog::getSaveFileName(this, "导出全公司考勤报表",
        "全公司考勤大表_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm") + ".csv", "CSV (*.csv)");

    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);

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