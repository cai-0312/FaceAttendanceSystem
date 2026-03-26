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
#include <QDateTime>
#include <QtConcurrent>
#include <QFuture>
#include <QThread>

// 构造函数：初始化网络路由引擎、建立底层数据库连接池并构建控制台界面
AttendanceServer::AttendanceServer(QWidget* parent)
    : QWidget(parent), ui(new Ui::AttendanceServerClass)
{
    ui->setupUi(this);

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
            if (db.isOpen()) {
                RequestHandler::executeDailyAbsentCheck(db, this);
            }
        }
        });
    dailyCheckTimer->start(60000);
}

AttendanceServer::~AttendanceServer()
{
    delete ui;
}

// 执行控制台各级子视图模块及权限模型的深度绑定与配置
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
    m_permModel->setHeaderData(m_permModel->fieldIndex("role"), Qt::Horizontal, "系统权限角色(双击修改)");

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

    // 挂载全局操控类按钮的事件调度回调槽
    connect(ui->btn_StartServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StartServer_clicked);
    connect(ui->btn_StopServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StopServer_clicked);
    connect(ui->btn_RefreshData, &QPushButton::clicked, this, &AttendanceServer::on_btn_RefreshData_clicked);
    connect(ui->btn_ExportGlobal, &QPushButton::clicked, this, &AttendanceServer::on_btn_ExportGlobal_clicked);
}

// 基于HTML富文本格式向服务端内嵌的实时监控终端推送运行日志
void AttendanceServer::logMessage(const QString& msg)
{
    QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    ui->textBrowser_ServerLog->append(timeStr + msg);
}

// 唤醒内核监听线程并对外暴露TCP连接服务
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

// 剥离并终止TCP底层监听，并安全踢出全网范围内已建立长连接的终端节点
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

// 拦截新到来的套接字请求实例，配置通信钩子
void AttendanceServer::onNewConnection()
{
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    connect(socket, &QTcpSocket::readyRead, this, &AttendanceServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &AttendanceServer::onClientDisconnected);
    logMessage(QString("收到新的物理连接请求: %1").arg(socket->peerAddress().toString()));
}

// 清洗并回收离线终端套接字的内存级路由占用
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

// 利用内部缓冲区机制修复TCP粘包半包现象，解析JSON报文并向任务池派发网络负荷指令
void AttendanceServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    //if (!socket || !socket->isValid()) return;

    m_buffers[socket].append(socket->readAll());

    while (m_buffers.contains(socket) && m_buffers[socket].contains('\n')) {
        int pos = m_buffers[socket].indexOf('\n');
        QByteArray data = m_buffers[socket].left(pos).trimmed();
        m_buffers[socket].remove(0, pos + 1);

        if (data.isEmpty()) continue;

        //// 每轮循环重新检查socket状态
        //if (!socket->isValid() || socket->state() != QAbstractSocket::ConnectedState) {
        //    m_buffers.remove(socket);
        //    return;
        //}

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isObject()) continue;
        QJsonObject json = doc.object();

        if (json["type"].toString() == "update_profile_field" && json["name"].toString().isEmpty()) {
            continue;
        }

        QString type = json["type"].toString();
        QString lookupKey = type.startsWith("group_") ? "chat" : type;

        auto it = m_dispatchTable.constFind(lookupKey);
        if (it == m_dispatchTable.constEnd()) continue;

        QSqlDatabase db = QSqlDatabase::database("server_db_connection");
        if (db.isOpen()) {
            it.value()(db, socket, json, data);
        }
    }
}
// 通过授权逻辑拦截的业务终端进行中心化连接池写入及状态通报
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

// 检索指定实体在中心服务器长连接池中的存活状态
bool AttendanceServer::isClientOnline(const QString& name) const
{
    return m_nameToSocket.contains(name);
}

// 基于业务标识提取关联的底层套接字内存指针
QTcpSocket* AttendanceServer::getSocketByName(const QString& name) const
{
    return m_nameToSocket.value(name, nullptr);
}

// 触发展开RBAC权控表层的数据全量同步刷新
void AttendanceServer::refreshPermModel()
{
    m_permModel->select();
}

// 动态提取缓存表中的客户连接流，利用标准项模型重新渲染至前端总控监控台
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

// 连接基础SQL引擎将底层记录流水映射呈现在控制台全览面板中
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

// 读取内存缓存模型状态，将整个体系内的历史归档流转化为脱机可读的本地CSV文件
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

// 调度及挂接各类不同分片层面的处理业务操作器集合，以驱动系统功能网络解析执行
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
    for (const QString& t : { "chat", "image", "file" }) {
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
    // 问题3：修改密码
    m_dispatchTable["verify_and_update_password"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleVerifyAndUpdatePassword(db, s, j);
        };
    // 问题2：人脸重录审批
    m_dispatchTable["face_reregister_request"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleFaceReregisterRequest(db, s, j);
        };
    // 问题4：头像文件上传/读取
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
    // 首页大屏：部门列表查询（供筛选下拉框用）
    m_dispatchTable["query_dept_list"] = [](auto& db, auto* s, auto& j, auto&) {
        RequestHandler::handleQueryDeptList(db, s, j);
        };
}