#include "AttendanceServer.h"
#include "DatabaseManager.h"
#include "RequestHandler.h"
#include "CenterAndComboDelegate.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QProcess>
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QtConcurrent>
#include <QFuture>
#include <QThread>
// 构造函数，初始化界面、数据库和网络路由，并设置定时任务
AttendanceServer::AttendanceServer(QWidget* parent)
    : QWidget(parent), ui(new Ui::AttendanceServerClass)
{
    ui->setupUi(this);
    if (ui->tabWidget_Main) {
        QString iconBase = "../../AttendanceServer/icon_library/";
        ui->tabWidget_Main->setTabText(0, "运行总览");
        ui->tabWidget_Main->setTabText(1, "考勤流水账");
        ui->tabWidget_Main->setTabText(2, "权限控制中心");
        ui->tabWidget_Main->setTabIcon(0, QIcon(iconBase + "icon_dashboard.svg"));
        ui->tabWidget_Main->setTabIcon(1, QIcon(iconBase + "icon_records.svg"));
        ui->tabWidget_Main->setTabIcon(2, QIcon(iconBase + "icon_permissions.svg"));
        ui->tabWidget_Main->setIconSize(QSize(18, 18));
        ui->tabWidget_Main->setStyleSheet(
            "QTabWidget::pane { border-top: 1px dashed #DCDFE6; } " 
            "QTabBar::tab { padding: 8px 15px; font-weight: bold; color: #4E5969; background: transparent; }"
            "QTabBar::tab:selected { color: #165DFF; border-bottom: 2px solid #165DFF; }"
            "QTabBar::tab:hover { color: #165DFF; }"
        );
        ui->label_OnlineUsers->setText(
            "<img src='" + iconBase + "dot_green.svg' width='16' height='16' align='middle'>&nbsp;"
            "<span style='color: #1F2329;'>当前在线员工节点</span>"
        );
        ui->label_ServerLog->setText(
            "<img src='" + iconBase + "icon_log.svg' width='16' height='16' align='middle'>&nbsp;"
            "<span style='color: #1F2329;'>消息路由与系统底层日志</span>"
        );
        ui->label_ServerStatus->setText(
            "<img src='" + iconBase + "dot_red.svg' width='12' height='12' align='middle'>&nbsp;"
            "<span style='color: #F53F3F; font-weight: bold;'>当前状态：未启动</span>"
        );
    }
    // 执行核心存储初始化：挂载数据库连接实例并完成基础数据表结构的建表工作
    DatabaseManager::initDatabase();
    // 构建视图树、绑定底层交互模型及指令分发表
    initUI();
    initDispatchTable();
    QTimer* dailyCheckTimer = new QTimer(this);
    connect(dailyCheckTimer, &QTimer::timeout, this, [this]() {
        QTime currentTime = QTime::currentTime();
        if (currentTime.hour() == 23 && currentTime.minute() == 58) {
            // 获取当前线程的数据库句柄，交由 RequestHandler 处理业务
            QSqlDatabase db = QSqlDatabase::database("server_db_connection");
            if (!db.isOpen()) {
                qDebug() << "[onReadyRead] 连接已断开，正在尝试重连...";
                db.open();
            }
            {
                QSqlQuery ping(db);
                if (!ping.exec("SELECT 1")) {
                    qDebug() << "检测失败，执行重连:" << ping.lastError().text();
                    db.close();
                    db.open();
                }
            }
            if (db.isOpen()) {
                RequestHandler::executeDailyAbsentCheck(db, this);
            }
        }
        });
    dailyCheckTimer->start(60000);
    QTimer* dbKeepAlive = new QTimer(this);
    connect(dbKeepAlive, &QTimer::timeout, this, []() {
        QSqlDatabase db = QSqlDatabase::database("server_db_connection");
        if (db.isOpen()) {
            QSqlQuery ping(db);
            if (!ping.exec("SELECT 1")) {
                qDebug() << "心跳失败，尝试重连...";
                db.close();
                db.open();
            }
        }
        else {
            qDebug() << "连接已关闭，尝试重连...";
            db.open();
        }
        });
    dbKeepAlive->start(180000);
}
AttendanceServer::~AttendanceServer()
{
    delete ui;
}
// 初始化 UI 子视图与数据模型，绑定模型与视图。
void AttendanceServer::initUI()
{
    // 配置全局考勤记录只读视图与数据模型
    m_globalRecordModel = new QSqlQueryModel(this);
    ui->tableView_GlobalRecords->setModel(m_globalRecordModel);
    ui->tableView_GlobalRecords->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableView_GlobalRecords->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView_GlobalRecords->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView_GlobalRecords->verticalHeader()->setVisible(false);
    ui->tableView_GlobalRecords->setItemDelegate(new CenterAndComboDelegate(-1, QStringList(), this));
    loadGlobalRecords();
    // 配置RBAC级用户权限管控可编辑模型
    m_permModel = new QSqlTableModel(this, QSqlDatabase::database("server_db_connection"));
    m_permModel->setTable("view_users_lite");
    m_permModel->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_permModel->select();
    ui->tableView_Permissions->setModel(m_permModel);
    ui->tableView_Permissions->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableView_Permissions->setEditTriggers(QAbstractItemView::DoubleClicked);
    // 按需规避不需要干预的字段，实施视图级数据脱敏
    for (int i = 0; i < m_permModel->columnCount(); ++i) {
        QString colName = m_permModel->record().fieldName(i);
        if (colName != "id" && colName != "name" && colName != "department"
            && colName != "job_title" && colName != "role") {
            ui->tableView_Permissions->setColumnHidden(i, true);
        }
    }
    m_permModel->setHeaderData(m_permModel->fieldIndex("id"), Qt::Horizontal, "工号");
    m_permModel->setHeaderData(m_permModel->fieldIndex("name"), Qt::Horizontal, "员工姓名");
    m_permModel->setHeaderData(m_permModel->fieldIndex("department"), Qt::Horizontal, "所属部门");
    m_permModel->setHeaderData(m_permModel->fieldIndex("job_title"), Qt::Horizontal, "企业职务");
    m_permModel->setHeaderData(m_permModel->fieldIndex("role"), Qt::Horizontal, "系统权限角色");
    // 绑定下拉选择代理，约束权限配置的操作范围
    int roleColIdx = m_permModel->fieldIndex("role");
    QStringList roleOptions = { "普通登录", "管理员登录", "超级管理员" };
    ui->tableView_Permissions->setItemDelegate(
        new CenterAndComboDelegate(roleColIdx, roleOptions, this));
    // 实例化底层的TCP端口监听引擎句柄
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &AttendanceServer::onNewConnection);
    // 配置实时在线设备接入表视图模型
    ui->tableWidget_OnlineUsers->setColumnCount(5);
    ui->tableWidget_OnlineUsers->setHorizontalHeaderLabels({ "姓名", "工作部门", "企业职务", "IP地址", "登录时间" });
    ui->tableWidget_OnlineUsers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_OnlineUsers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QString iconBase = "../../AttendanceServer/icon_library/";
    if (ui->btn_RefreshData) {
        ui->btn_RefreshData->setText(" 手动刷新全局考勤"); 
        ui->btn_RefreshData->setIcon(QIcon(iconBase + "icon_refresh.svg"));
        ui->btn_RefreshData->setIconSize(QSize(16, 16));
    }
    if (ui->btn_ExportGlobal) {
        ui->btn_ExportGlobal->setText(" 导出全局报表");
        ui->btn_ExportGlobal->setIcon(QIcon(iconBase + "icon_export.svg"));
        ui->btn_ExportGlobal->setIconSize(QSize(16, 16));
    }
    if (ui->btn_LogoutServer) {
        connect(ui->btn_LogoutServer, &QPushButton::clicked, this, [=]() {
            // 服务器还在运行，先安全踢掉所有人并解绑端口
            if (m_tcpServer->isListening()) {
                on_btn_StopServer_clicked();
            }
            QMessageBox::information(this, "退出", "已安全断开服务器与数据库连接，即将返回登录界面。");
            QProcess::startDetached(qApp->applicationFilePath(), QStringList());
            qApp->quit();
            });
    }
}
// 将 HTML 格式的日志追加到监控终端显示区。
void AttendanceServer::logMessage(const QString& msg)
{
    QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    ui->textBrowser_ServerLog->append(timeStr + msg);
}
// 启动 TCP 监听并更新 UI 状态。
void AttendanceServer::on_btn_StartServer_clicked()
{
    quint16 port = ui->lineEdit_Port->text().toUShort();
    if (m_tcpServer->isListening()) m_tcpServer->close();

    if (m_tcpServer->listen(QHostAddress::Any, port)) {
        ui->label_ServerStatus->setText(
            "<img src='../../AttendanceServer/icon_library/dot_green.svg' width='12' height='12' align='middle'>&nbsp;"
            "<span style='color: #67C23A;'>当前状态：运行中 (端口: " + QString::number(port) + ")</span>"
        );
        ui->btn_StartServer->setEnabled(false);
        ui->btn_StopServer->setEnabled(true);
        ui->lineEdit_Port->setEnabled(false);
        logMessage(QString("<font color='#67C23A'>TCP 核心路由引擎已启动，正在监听端口 %1...</font>").arg(port));
    }
    else {
        QMessageBox::critical(this, "启动失败", "端口被占用或权限不足！\n" + m_tcpServer->errorString());
        logMessage("<font color='red'>启动失败：" + m_tcpServer->errorString() + "</font>");
    }
}
// 停止 TCP 服务并断开所有客户端连接。
void AttendanceServer::on_btn_StopServer_clicked()
{
    m_tcpServer->close();
    for (QTcpSocket* socket : m_clients.keys()) socket->disconnectFromHost();
    m_clients.clear();
    m_nameToSocket.clear();
    m_authenticatedSockets.clear();
    updateOnlineUsersTable();
    ui->label_ServerStatus->setText(
        "<img src='../../AttendanceServer/icon_library/dot_red.svg' width='12' height='12' align='middle'>&nbsp;"
        "<span style='color: #F53F3F; font-weight: bold;'>当前状态：未启动</span>"
    );
    ui->btn_StartServer->setEnabled(true);
    ui->btn_StopServer->setEnabled(false);
    ui->lineEdit_Port->setEnabled(true);
    logMessage("<font color='#E6A23C'>TCP 核心路由引擎已安全关闭。</font>");
}
// 处理新连接：设置 socket 选项并绑定读取与断开回调。
void AttendanceServer::onNewConnection()
{
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    connect(socket, &QTcpSocket::readyRead, this, &AttendanceServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &AttendanceServer::onClientDisconnected);
    //logMessage(QString("收到新的物理连接请求: %1").arg(socket->peerAddress().toString()));
}
// 处理客户端断开：清理缓冲区、传输状态与在线列表。
void AttendanceServer::onClientDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    // 立即断开所有信号连接，防止后续排队的信号触发
    socket->disconnect();
    // 强制中止底层连接（比disconnectFromHost更彻底）
    socket->abort();
    if (m_buffers.contains(socket)) {
        m_buffers.remove(socket);
    }
    // 清除该 socket 的认证状态
    m_authenticatedSockets.remove(socket);
    if (m_clients.contains(socket)) {
        QString name = m_clients[socket].name;
        logMessage(QString("<font color='#F56C6C'>节点掉线: [%1] 已断开连接。</font>").arg(name));
        m_nameToSocket.remove(name);
        if (m_nameToSocket.value(name) == socket) {
            m_nameToSocket.remove(name);
        }
        m_clients.remove(socket);
        updateOnlineUsersTable();
    }
    socket->deleteLater();
}
// 读取 socket 数据并按协议解析为文件分片或 JSON 请求进行派发。
void AttendanceServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    m_buffers[socket].append(socket->readAll());
    // 定义二进制魔数（4字节）
    static const char MAGIC[] = { '\xF1', '\xE2', '\xD3', '\xC4' };
    static const int HEADER_SIZE = 8; // 4字节魔数 + 4字节长度
    while (m_buffers.contains(socket) && !m_buffers[socket].isEmpty()) {
        QByteArray& buf = m_buffers[socket];
        // ── 模式A：当前 socket 正在进行文件传输 ──
        if (m_fileTransfers.contains(socket)) {
            FileTransferState& state = m_fileTransfers[socket];
            // 检查是否有完整的二进制分块包头
            if (buf.size() < HEADER_SIZE) return; // 数据不够，等下一波
            // 校验魔数
            if (buf[0] == MAGIC[0] && buf[1] == MAGIC[1] &&
                buf[2] == MAGIC[2] && buf[3] == MAGIC[3]) {
                // 读取 payload 长度（小端 uint32）
                quint32 payloadLen = 0;
                memcpy(&payloadLen, buf.constData() + 4, 4);
                // 特殊值 0 表示传输结束
                if (payloadLen == 0) {
                    buf.remove(0, HEADER_SIZE);
                    onFileTransferComplete(socket);
                    continue;
                }
                // 检查 payload 是否完整到达
                if (buf.size() < HEADER_SIZE + (int)payloadLen) return;
                // 提取 payload 并写入磁盘
                QByteArray payload = buf.mid(HEADER_SIZE, payloadLen);
                buf.remove(0, HEADER_SIZE + payloadLen);
                if (state.file && state.file->isOpen()) {
                    state.file->write(payload);
                    state.receivedSize += payload.size();
                    // 进度日志（每 5MB 打印一次）
                    if (state.receivedSize % (5 * 1024 * 1024) < 65536) {
                        double pct = 100.0 * state.receivedSize / qMax(state.totalSize, (qint64)1);
                        qDebug() << QString("[FileTransfer] %1 -> %2%  (%3/%4 MB)")
                            .arg(state.fileName)
                            .arg(pct, 0, 'f', 1)
                            .arg(state.receivedSize / 1048576)
                            .arg(state.totalSize / 1048576);
                    }
                }
                continue;
            }
            else {
                // 魔数不匹配，可能是传输异常，中止
                qWarning() << "[FileTransfer] 二进制魔数校验失败，中止传输";
                if (state.file) { state.file->close(); delete state.file; }
                m_fileTransfers.remove(socket);
                // 继续作为 JSON 处理
            }
        }
        // ── 模式B：正常 JSON 文本协议（以 \n 分隔）──
        if (!buf.contains('\n')) return;
        int pos = buf.indexOf('\n');
        QByteArray data = buf.left(pos).trimmed();
        buf.remove(0, pos + 1);
        if (data.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;
        QJsonObject json = doc.object();
        if (json.contains("__internal_verified")) { json.remove("__internal_verified"); }
        QString type = json["type"].toString();

        // 认证拦截：未认证的 socket 只允许执行登录和注册相关操作
        static const QSet<QString> allowedBeforeAuth = {
            "client_login_auth", "client_register_account", "verify_user_for_registration"
        };
        if (!m_authenticatedSockets.contains(socket) && !allowedBeforeAuth.contains(type)) {
            // client_login_auth 成功后由 handleLogin 标记认证状态
            // 这里标记已通过 client_login_auth 的 socket（login 是认证后的第二步）
            if (type == "login") {
                // login 请求需要先通过 client_login_auth，此处放行但在 handleLogin 中校验用户存在性
                // 放行后由 registerClient 完成最终认证标记
            }
            else {
                logMessage(QString("<font color='#F53F3F'>安全拦截: 未认证连接 %1 尝试执行 [%2]，已拒绝。</font>")
                    .arg(socket->peerAddress().toString(), type));
                continue;
            }
        }

        // ── 拦截文件传输启动指令 ──
        if (type == "file_transfer_start") {
            handleFileTransferStart(socket, json);
            continue;
        }
        // ── 拦截文件下载请求 ──
        if (type == "file_download_request") {
            handleFileDownloadRequest(socket, json);
            continue;
        }
        if (json["type"].toString() == "update_profile_field" && json["name"].toString().isEmpty()) continue;
        QString lookupKey = type.startsWith("group_") ? "chat" : type;
        auto it = m_dispatchTable.constFind(lookupKey);
        if (it == m_dispatchTable.constEnd()) continue;
        QSqlDatabase db = QSqlDatabase::database("server_db_connection");
        if (!db.isOpen()) { db.open(); }
        { QSqlQuery ping(db); if (!ping.exec("SELECT 1")) { db.close(); db.open(); } }
        if (db.isOpen()) {
            it.value()(db, socket, json, data);
        }
    }
}
// 初始化文件接收并创建本地存储与传输状态记录。
void AttendanceServer::handleFileTransferStart(QTcpSocket* socket, const QJsonObject& json)
{
    QString sender = json["from"].toString();
    QString receiver = json["to"].toString();
    QString fileName = json["filename"].toString();
    qint64  fileSize = json["file_size"].toVariant().toLongLong();
    bool    isGroup = json["is_group"].toBool(false);
    QString dept = json["department"].toString();
    // 构建服务端存储路径
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/ChatFiles";
    QString baseDir = QDir::cleanPath(rawPath) + "/transfers/";
    QDir().mkpath(baseDir);
    // 生成唯一文件名
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString savedName = QString("%1_%2_%3").arg(timestamp, sender, fileName);
    QString fullPath = baseDir + savedName;
    QFile* file = new QFile(fullPath);
    if (!file->open(QIODevice::WriteOnly)) {
        qWarning() << "[FileTransfer] 无法创建文件:" << fullPath;
        delete file;
        QJsonObject res;
        res["type"] = "file_transfer_error";
        res["msg"] = "服务端无法创建文件";
        QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
        socket->write(out);
        return;
    }
    // 记录传输状态
    FileTransferState state;
    state.fileName = fileName;
    state.savedPath = fullPath;
    state.sender = sender;
    state.receiver = receiver;
    state.totalSize = fileSize;
    state.receivedSize = 0;
    state.file = file;
    state.isGroup = isGroup;
    state.department = dept;
    m_fileTransfers[socket] = state;
    // 告知客户端可以开始发送二进制数据了
    QJsonObject ack;
    ack["type"] = "file_transfer_ready";
    ack["msg_id"] = json["msg_id"].toString();
    QByteArray out = QJsonDocument(ack).toJson(QJsonDocument::Compact) + "\n";
    socket->write(out);
    socket->flush();
    logMessage(QString("<font color='#3370FF'>[文件传输] 开始接收: %1 (%2 MB) 来自 [%3]</font>")
        .arg(fileName).arg(fileSize / 1048576.0, 0, 'f', 1).arg(sender));
}
//  文件传输完成处理
void AttendanceServer::onFileTransferComplete(QTcpSocket* socket)
{
    if (!m_fileTransfers.contains(socket)) return;
    FileTransferState state = m_fileTransfers.take(socket);
    if (state.file) {
        state.file->close();
        delete state.file;
    }
    logMessage(QString("<font color='#00B42A'>[文件传输] 接收完成: %1 (%2 MB)</font>")
        .arg(state.fileName).arg(state.receivedSize / 1048576.0, 0, 'f', 1));
    // 通知发送方：传输成功
    QJsonObject ack;
    ack["type"] = "file_transfer_complete";
    ack["filename"] = state.fileName;
    ack["saved_path"] = state.savedPath;
    ack["status"] = "success";
    QByteArray ackData = QJsonDocument(ack).toJson(QJsonDocument::Compact) + "\n";
    socket->write(ackData);
    socket->flush();
    // 通知接收方：有文件可下载（走 JSON 消息通道）
    QJsonObject notify;
    notify["type"] = state.isGroup ? "group_file_notify" : "file_notify";
    notify["from"] = state.sender;
    notify["to"] = state.receiver;
    notify["filename"] = state.fileName;
    notify["file_size"] = state.totalSize;
    notify["saved_path"] = state.savedPath;
    notify["department"] = state.department;
    notify["time"] = QDateTime::currentDateTime().toString("HH:mm:ss");
    if (state.isGroup) {
        // 群发通知给部门所有在线成员
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.value().dept == state.department && it.value().name != state.sender) {
                QByteArray out = QJsonDocument(notify).toJson(QJsonDocument::Compact) + "\n";
                it.key()->write(out);
            }
        }
    }
    else {
        QTcpSocket* targetSocket = getSocketByName(state.receiver);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            QByteArray out = QJsonDocument(notify).toJson(QJsonDocument::Compact) + "\n";
            targetSocket->write(out);
        }
        // TODO: 接收方离线时，可将通知存入 offline_messages 表
    }
    // 数据库记录（只存元数据，不存文件内容）
    QSqlDatabase db = QSqlDatabase::database("server_db_connection");
    if (db.isOpen()) {
        QSqlQuery q(db);
        QString safeSender = state.sender;   safeSender.replace("'", "''");
        QString safeReceiver = state.receiver;  safeReceiver.replace("'", "''");
        QString safeFileName = state.fileName;  safeFileName.replace("'", "''");
        QString safePath = state.savedPath; safePath.replace("'", "''");
        q.exec(QString(
            "INSERT INTO chat_history (sender, receiver, msg_type, content, filename, send_time, is_group) "
            "VALUES ('%1', '%2', 'file', '%3', '%4', NOW(), %5)")
            .arg(safeSender, safeReceiver, safePath, safeFileName)
            .arg(state.isGroup ? 1 : 0));
    }
}
// 处理文件下载请求并以分片二进制方式发送文件给客户端。
void AttendanceServer::handleFileDownloadRequest(QTcpSocket* socket, const QJsonObject& json)
{
    QString filePath = json["saved_path"].toString();
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        QJsonObject err;
        err["type"] = "file_download_error";
        err["msg"] = "文件不存在或无法读取";
        socket->write(QJsonDocument(err).toJson(QJsonDocument::Compact) + "\n");
        return;
    }
    qint64 totalSize = file.size();
    // 发送元数据头
    QJsonObject header;
    header["type"] = "file_download_start";
    header["filename"] = json["filename"].toString();
    header["file_size"] = totalSize;
    socket->write(QJsonDocument(header).toJson(QJsonDocument::Compact) + "\n");
    socket->flush();
    // 分块发送二进制数据
    static const char MAGIC[] = { '\xF1', '\xE2', '\xD3', '\xC4' };
    const int CHUNK_SIZE = 65536;
    while (!file.atEnd()) {
        QByteArray chunk = file.read(CHUNK_SIZE);
        quint32 len = chunk.size();
        // 构造包头：魔数(4) + 长度(4)
        QByteArray packet;
        packet.append(MAGIC, 4);
        packet.append(reinterpret_cast<const char*>(&len), 4);
        packet.append(chunk);
        socket->write(packet);
        // 每写 1MB 刷一次缓冲区，防止内存堆积
        if (socket->bytesToWrite() > 1024 * 1024) {
            socket->waitForBytesWritten(3000);
        }
    }
    // 发送结束标记（长度=0）
    quint32 zero = 0;
    QByteArray endPacket;
    endPacket.append(MAGIC, 4);
    endPacket.append(reinterpret_cast<const char*>(&zero), 4);
    socket->write(endPacket);
    socket->flush();
    file.close();
    logMessage(QString("[文件下载] 向客户端推送文件: %1 (%2 MB)")
        .arg(json["filename"].toString()).arg(totalSize / 1048576.0, 0, 'f', 1));
}
// 注册客户端到在线连接池并更新 UI 与日志。
void AttendanceServer::registerClient(QTcpSocket* socket, const QString& name,
    const QString& dept, const QString& jobTitle,
    const QString& ip)
{
    ClientInfo info{ name, dept, jobTitle, ip, QDateTime::currentDateTime().toString("HH:mm:ss") };
    m_clients[socket] = info;
    m_nameToSocket[name] = socket;
    // 标记该 socket 为已认证，后续请求将被放行
    m_authenticatedSockets.insert(socket);
    updateOnlineUsersTable();
    logMessage(QString("<font color='#409EFF'>认证成功: 员工 [%1] (%2 - %3) 已接入总控网关。</font>")
        .arg(name, dept, jobTitle));
}
// 检查指定用户名是否在线。
bool AttendanceServer::isClientOnline(const QString& name) const
{
    return m_nameToSocket.contains(name);
}
// 根据用户名返回对应的套接字指针或 nullptr。
QTcpSocket* AttendanceServer::getSocketByName(const QString& name) const
{
    return m_nameToSocket.value(name, nullptr);
}
// 刷新权限模型数据以同步前端视图。
void AttendanceServer::refreshPermModel()
{
    m_permModel->select();
}
// 刷新在线用户表以展示当前连接池状态。
void AttendanceServer::updateOnlineUsersTable()
{
    ui->tableWidget_OnlineUsers->setRowCount(0);
    for (const ClientInfo& info : m_clients.values()) {
        int row = ui->tableWidget_OnlineUsers->rowCount();
        ui->tableWidget_OnlineUsers->insertRow(row);
        auto makeItem = [](const QString& text) {
            QTableWidgetItem* item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            return item;
            };
        ui->tableWidget_OnlineUsers->setItem(row, 0, makeItem(info.name));
        ui->tableWidget_OnlineUsers->setItem(row, 1, makeItem(info.dept));
        ui->tableWidget_OnlineUsers->setItem(row, 2, makeItem(info.jobTitle));
        ui->tableWidget_OnlineUsers->setItem(row, 3, makeItem(info.ip));
        ui->tableWidget_OnlineUsers->setItem(row, 4, makeItem(info.loginTime));
    }
}
// 加载并展示全局考勤流水记录到模型。
void AttendanceServer::loadGlobalRecords()
{
    QString sql =
        "SELECT ROW_NUMBER() OVER (ORDER BY r.punch_time DESC) AS '序号', "
        "r.name AS '姓名', u.department AS '部门', u.job_title AS '职务', "
        "DATE_FORMAT(r.punch_time, '%Y-%m-%d %H:%i:%s') AS '打卡时间', "
        "r.status AS '状态' "
        "FROM attendance_records r "
        "LEFT JOIN users u ON r.name = u.name "
        "ORDER BY r.punch_time DESC";
    QSqlDatabase db = QSqlDatabase::database("server_db_connection");
    m_globalRecordModel->setQuery(sql, db);
}
void AttendanceServer::on_btn_RefreshData_clicked()
{
    loadGlobalRecords();
}
// 将全局考勤数据导出为本地 CSV 文件。
void AttendanceServer::on_btn_ExportGlobal_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this, "导出全公司考勤报表",
        "全公司考勤大表_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm") + ".csv",
        "CSV (*.csv)");
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << QString("\xEF\xBB\xBF") << "序号,姓名,部门,职务,打卡时间,状态\n";
        int rowCount = m_globalRecordModel->rowCount();
        int colCount = m_globalRecordModel->columnCount();
        for (int r = 0; r < rowCount; ++r) {
            QStringList rowData;
            for (int c = 0; c < colCount; ++c)
                rowData << m_globalRecordModel->data(m_globalRecordModel->index(r, c)).toString();
            out << rowData.join(",") << "\n";
        }
        file.close();
        QMessageBox::information(this, "成功",
            QString("成功导出 %1 条全局考勤记录！").arg(rowCount));
    }
}
// 初始化请求分发表，将消息类型映射到相应的处理函数。
void AttendanceServer::initDispatchTable()
{
    // 配置人脸图像及基础账号管理路由通道
    m_dispatchTable["query_face_features"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryFaceFeatures(db, s, j);
        };
    m_dispatchTable["register_face"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleRegisterFace(db, s, j);
        };
    m_dispatchTable["client_login_auth"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleClientLoginAuth(db, s, j);
        };
    m_dispatchTable["verify_user_for_registration"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleVerifyUserForRegistration(db, s, j);
        };
    // 配置须持有主干应用上下文的扩展级业务挂载路由
    m_dispatchTable["client_register_account"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleClientRegisterAccount(db, s, j, this);
        };
    m_dispatchTable["login"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleLogin(db, s, j, this);
        };
    // 挂载动态状态追踪模块流节点
    m_dispatchTable["status_update"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleStatusUpdate(db, s, j);
        };
    // 绑定多类型即时通讯底层协议路由机制
    for (const QString& t : { "chat", "image", "file", "file_chunk" }) {
        m_dispatchTable[t] = [this](auto& db, auto* s, auto& j, auto& raw) {
            RequestHandler::handleChatMessage(db, s, j, raw, this);
            };
    }
    m_dispatchTable["broadcast"] = [this](auto& db, auto* s, auto& j, auto& raw) {
        RequestHandler::handleBroadcast(db, s, j, raw, this);
        };
    // 绑定数据持久化信息及回执业务拉取逻辑
    m_dispatchTable["query_chat_history"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryChatHistory(db, s, j);
        };
    m_dispatchTable["query_chat_contacts"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryChatContacts(db, s, j);
        };
    m_dispatchTable["query_group_members"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryGroupMembers(db, s, j);
        };
    m_dispatchTable["read_receipt"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleReadReceipt(db, s, j, this);
        };
    m_dispatchTable["publish_announcement"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handlePublishAnnouncement(db, s, j);
        };
    // 绑定企业级用户及花名册管控操作路由
    m_dispatchTable["query_user_profile"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryUserProfile(db, s, j);
        };
    m_dispatchTable["update_profile_field"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleUpdateProfileField(db, s, j);
        };
    m_dispatchTable["query_user_list"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryUserList(db, s, j);
        };
    m_dispatchTable["query_user_dept"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryUserDept(db, s, j);
        };
    // 配置高危级管理拦截功能通道挂载点
    m_dispatchTable["admin_reset_password"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAdminResetPassword(db, s, j, this);
        };
    m_dispatchTable["admin_delete_user"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAdminDeleteUser(db, s, j, this);
        };
    m_dispatchTable["admin_modify_status"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAdminModifyStatus(db, s, j, this);
        };
    // 修改密码
    m_dispatchTable["verify_and_update_password"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleVerifyAndUpdatePassword(db, s, j);
        };
    // 人脸重录审批
    m_dispatchTable["face_reregister_request"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleFaceReregisterRequest(db, s, j);
        };
    // 头像文件上传/读取
    m_dispatchTable["upload_avatar_file"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleUploadAvatarFile(db, s, j);
        };
    m_dispatchTable["query_avatar_file"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryAvatarFile(db, s, j);
        };
    // 构建核心签到判活业务的执行入口分发
    m_dispatchTable["punch_request"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handlePunchRequest(db, s, j, this);
        };
    m_dispatchTable["secure_punch_request"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleSecurePunchRequest(db, s, j, this);
        };
    m_dispatchTable["punch_cheat"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handlePunchCheat(db, s, j, this);
        };
    m_dispatchTable["query_today_status"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryTodayStatus(db, s, j);
        };
    m_dispatchTable["query_monthly_status"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryMonthlyStatus(db, s, j);
        };
    m_dispatchTable["query_monthly_summary_all"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryMonthlySummaryAll(db, s, j);
        };
    // 部门报表路由
    m_dispatchTable["query_dept_summary"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryDeptSummary(db, s, j);
        };
    m_dispatchTable["query_attendance_detail"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryAttendanceDetail(db, s, j);
        };
    m_dispatchTable["query_today_attendance_for_ai"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryTodayAttendanceForAi(db, s, j);
        };
    m_dispatchTable["query_home_dashboard"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryHomeDashboard(db, s, j);
        };
    // 设置企业级规则调配和动态计算模型入口
    m_dispatchTable["query_shift_rule"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryShiftRule(db, s, j);
        };
    m_dispatchTable["rule_settings"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleRuleSettings(db, s, j, this);
        };
    // 绑定OA级请假申报与日常行为纠正体系的路由
    m_dispatchTable["leave_request"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleLeaveRequest(db, s, j);
        };
    m_dispatchTable["leave_approve"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleLeaveApprove(db, s, j, this);
        };
    m_dispatchTable["appeal_request"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAppealRequest(db, s, j);
        };
    m_dispatchTable["appeal_approve"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAppealApprove(db, s, j, this);
        };
    m_dispatchTable["query_pending_leaves"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryPendingLeaves(db, s, j);
        };
    m_dispatchTable["query_pending_appeals"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryPendingAppeals(db, s, j);
        };
    m_dispatchTable["query_my_requests"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryMyRequests(db, s, j);
        };
    m_dispatchTable["query_approval_candidates"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryApprovalCandidates(db, s, j);
        };
    // 挂载人工智能对话上下文以及智能诊断相关的日志上报分支
    m_dispatchTable["ai_save_message"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAiSaveMessage(db, s, j);
        };
    m_dispatchTable["create_ai_session"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleCreateAiSession(db, s, j);
        };
    m_dispatchTable["query_ai_sessions"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryAiSessions(db, s, j);
        };
    m_dispatchTable["rename_ai_session"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleRenameAiSession(db, s, j);
        };
    m_dispatchTable["delete_ai_session"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleDeleteAiSession(db, s, j);
        };
    m_dispatchTable["search_ai_history"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleSearchAiHistory(db, s, j);
        };
    m_dispatchTable["query_ai_chat_history"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryAiChatHistory(db, s, j);
        };
    m_dispatchTable["ai_audit_file"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAiAuditFile(db, s, j, this);
        };
    m_dispatchTable["ai_chat_request"] = [this](QSqlDatabase& db, QTcpSocket* s,
      const QJsonObject& j, const QByteArray&) {
      RequestHandler::handleAiChatRequest(db, s, j, this);
          };
    // 首页大屏：部门列表查询（供筛选下拉框用）
    m_dispatchTable["query_dept_list"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryDeptList(db, s, j);
        };
}