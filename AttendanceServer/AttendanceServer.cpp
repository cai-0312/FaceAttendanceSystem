#include "AttendanceServer.h"
#include "DatabaseManager.h"
#include "RequestHandler.h"
#include "CenterAndComboDelegate.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QDateTime>
#include <QtConcurrent>
#include <QFuture>
#include <QThread>

// ============================================================
// 构造 / 析构
// ============================================================

AttendanceServer::AttendanceServer(QWidget* parent)
    : QWidget(parent), ui(new Ui::AttendanceServerClass)
{
    ui->setupUi(this);

    // 1. 初始化数据库（建连接 + DDL）
    DatabaseManager::initDatabase();

    // 2. 初始化 UI 控件、模型、信号槽
    initUI();
    initDispatchTable();
}

AttendanceServer::~AttendanceServer()
{
    delete ui;
}

// ============================================================
// UI 初始化
// ============================================================

void AttendanceServer::initUI()
{
    // ── 全局考勤记录表 ────────────────────────────────────────
    m_globalRecordModel = new QSqlQueryModel(this);
    ui->tableView_GlobalRecords->setModel(m_globalRecordModel);
    ui->tableView_GlobalRecords->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableView_GlobalRecords->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView_GlobalRecords->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView_GlobalRecords->verticalHeader()->setVisible(false);
    ui->tableView_GlobalRecords->setItemDelegate(new CenterAndComboDelegate(-1, QStringList(), this));
    loadGlobalRecords();

    // ── 权限管理表 ────────────────────────────────────────────
    m_permModel = new QSqlTableModel(this, QSqlDatabase::database("server_db_connection"));
    m_permModel->setTable("view_users_lite");
    m_permModel->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_permModel->select();

    ui->tableView_Permissions->setModel(m_permModel);
    ui->tableView_Permissions->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableView_Permissions->setEditTriggers(QAbstractItemView::DoubleClicked);

    // 只显示指定列
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
    m_permModel->setHeaderData(m_permModel->fieldIndex("role"), Qt::Horizontal, "系统权限角色(双击修改)");

    int roleColIdx = m_permModel->fieldIndex("role");
    QStringList roleOptions = { "普通登录", "管理员登录", "超级管理员" };
    ui->tableView_Permissions->setItemDelegate(
        new CenterAndComboDelegate(roleColIdx, roleOptions, this));

    // ── TCP 服务器 ────────────────────────────────────────────
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &AttendanceServer::onNewConnection);

    // ── 在线用户表 ────────────────────────────────────────────
    ui->tableWidget_OnlineUsers->setColumnCount(5);
    ui->tableWidget_OnlineUsers->setHorizontalHeaderLabels({ "姓名", "工作部门", "企业职务", "IP地址", "登录时间" });
    ui->tableWidget_OnlineUsers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_OnlineUsers->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // ── 按钮信号槽 ────────────────────────────────────────────
    connect(ui->btn_StartServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StartServer_clicked);
    connect(ui->btn_StopServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StopServer_clicked);
    connect(ui->btn_RefreshData, &QPushButton::clicked, this, &AttendanceServer::on_btn_RefreshData_clicked);
    connect(ui->btn_ExportGlobal, &QPushButton::clicked, this, &AttendanceServer::on_btn_ExportGlobal_clicked);
}

// ============================================================
// 日志
// ============================================================

void AttendanceServer::logMessage(const QString& msg)
{
    QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    ui->textBrowser_ServerLog->append(timeStr + msg);
}

// ============================================================
// TCP 服务器控制
// ============================================================

void AttendanceServer::on_btn_StartServer_clicked()
{
    quint16 port = ui->lineEdit_Port->text().toUShort();
    if (m_tcpServer->isListening()) m_tcpServer->close();

    if (m_tcpServer->listen(QHostAddress::Any, port)) {
        ui->label_ServerStatus->setText("当前状态：运行中 (端口: " + QString::number(port) + ")");
        ui->label_ServerStatus->setStyleSheet("color: #67C23A; font-weight:bold;");
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

void AttendanceServer::on_btn_StopServer_clicked()
{
    m_tcpServer->close();
    for (QTcpSocket* socket : m_clients.keys()) socket->disconnectFromHost();
    m_clients.clear();
    m_nameToSocket.clear();
    updateOnlineUsersTable();

    ui->label_ServerStatus->setText("当前状态：未启动");
    ui->label_ServerStatus->setStyleSheet("color: black; font-weight:bold;");
    ui->btn_StartServer->setEnabled(true);
    ui->btn_StopServer->setEnabled(false);
    ui->lineEdit_Port->setEnabled(true);
    logMessage("<font color='#E6A23C'>TCP 核心路由引擎已安全关闭。</font>");
}

// ============================================================
// TCP 连接管理
// ============================================================

void AttendanceServer::onNewConnection()
{
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &AttendanceServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &AttendanceServer::onClientDisconnected);
    logMessage(QString("收到新的物理连接请求: %1").arg(socket->peerAddress().toString()));
}

void AttendanceServer::onClientDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    if (m_clients.contains(socket)) {
        QString name = m_clients[socket].name;
        logMessage(QString("<font color='#F56C6C'>节点掉线: [%1] 已断开连接。</font>").arg(name));
        m_nameToSocket.remove(name);
        m_clients.remove(socket);
        updateOnlineUsersTable();
    }
    socket->deleteLater();
}

// ============================================================
// 网络核心路由：将请求分发给 RequestHandler
// ============================================================

void AttendanceServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    while (socket->canReadLine()) {
        QByteArray data = socket->readLine();

        QtConcurrent::run([this, socket, data]() {
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isNull() || !doc.isObject()) return;

            QJsonObject json = doc.object();
            QString     type = json["type"].toString();

            // group_* 前缀类型统一映射到 "chat" 处理器
            QString lookupKey = type.startsWith("group_") ? "chat" : type;

            auto it = m_dispatchTable.constFind(lookupKey);
            if (it == m_dispatchTable.constEnd()) {
                qDebug() << "未知请求类型，已忽略:" << type;
                return;
            }

            QString connName = DatabaseManager::makeThreadConnName();
            if (!DatabaseManager::openThreadConnection(connName)) {
                QSqlDatabase::removeDatabase(connName);
                return;
            }
            {
                QSqlDatabase db = QSqlDatabase::database(connName);
                it.value()(db, socket, json, data);   
            }
            QSqlDatabase::removeDatabase(connName);
            });
    }
}

// ============================================================
// 公开接口：供 RequestHandler 回调
// ============================================================

void AttendanceServer::registerClient(QTcpSocket* socket, const QString& name,
    const QString& dept, const QString& jobTitle,
    const QString& ip)
{
    ClientInfo info{ name, dept, jobTitle, ip, QDateTime::currentDateTime().toString("HH:mm:ss") };
    m_clients[socket] = info;
    m_nameToSocket[name] = socket;
    updateOnlineUsersTable();
    logMessage(QString("<font color='#409EFF'>认证成功: 员工 [%1] (%2 - %3) 已接入总控网关。</font>")
        .arg(name, dept, jobTitle));
}

bool AttendanceServer::isClientOnline(const QString& name) const
{
    return m_nameToSocket.contains(name);
}

QTcpSocket* AttendanceServer::getSocketByName(const QString& name) const
{
    return m_nameToSocket.value(name, nullptr);
}

void AttendanceServer::refreshPermModel()
{
    m_permModel->select();
}

// ============================================================
// 在线用户表刷新
// ============================================================

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

// ============================================================
// 考勤记录刷新
// ============================================================

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

// ============================================================
// 工具栏按钮
// ============================================================

void AttendanceServer::on_btn_RefreshData_clicked()
{
    loadGlobalRecords();
}

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

void AttendanceServer::initDispatchTable()
{
    // ── 人脸 & 账号 ──────────────────────────────────────────
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

    // ── 需要 this 指针的，用 [this] 捕获 ────────────────────
    m_dispatchTable["client_register_account"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleClientRegisterAccount(db, s, j, this);
        };
    m_dispatchTable["login"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleLogin(db, s, j, this);
        };

    // ── 在线状态 ─────────────────────────────────────────────
    m_dispatchTable["status_update"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleStatusUpdate(db, s, j);
        };

    // ── 聊天（需要 rawData 和 this）──────────────────────────
    // chat/image/file/group_* 共用同一个处理器，需特殊处理（见 onReadyRead）
    for (const QString& t : { "chat", "image", "file" }) {
        m_dispatchTable[t] = [this](auto& db, auto* s, auto& j, auto& raw) {
            RequestHandler::handleChatMessage(db, s, j, raw, this);
            };
    }
    m_dispatchTable["broadcast"] = [this](auto& db, auto* s, auto& j, auto& raw) {
        RequestHandler::handleBroadcast(db, s, j, raw, this);
        };

    // ── 聊天查询 ─────────────────────────────────────────────
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

    // ── 用户档案 ─────────────────────────────────────────────
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

    // ── 管理员操作 ───────────────────────────────────────────
    m_dispatchTable["admin_reset_password"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAdminResetPassword(db, s, j, this);
        };
    m_dispatchTable["admin_delete_user"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAdminDeleteUser(db, s, j, this);
        };
    m_dispatchTable["admin_modify_status"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleAdminModifyStatus(db, s, j, this);
        };

    // ── 考勤核心 ─────────────────────────────────────────────
    m_dispatchTable["punch_request"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handlePunchRequest(db, s, j, this);
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
    m_dispatchTable["query_attendance_detail"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryAttendanceDetail(db, s, j);
        };
    m_dispatchTable["query_today_attendance_for_ai"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryTodayAttendanceForAi(db, s, j);
        };
    m_dispatchTable["query_home_dashboard"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryHomeDashboard(db, s, j);
        };

    // ── 排班规则 ─────────────────────────────────────────────
    m_dispatchTable["query_shift_rule"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryShiftRule(db, s, j);
        };
    m_dispatchTable["rule_settings"] = [this](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleRuleSettings(db, s, j, this);
        };

    // ── 请假 & 申诉 ──────────────────────────────────────────
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

    // ── AI 助手 ──────────────────────────────────────────────
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
}