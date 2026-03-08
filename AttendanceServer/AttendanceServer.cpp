#include "AttendanceServer.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QSqlRecord>
#include <QTime>
#include <QtConcurrent>
#include <QFuture>
#include <QThread>
#include <QStyledItemDelegate>
#include <QComboBox>

class CenterAndComboDelegate : public QStyledItemDelegate {
public:
    int comboColumn;
    QStringList comboItems;

    CenterAndComboDelegate(int col = -1, QStringList items = QStringList(), QObject* parent = nullptr)
        : QStyledItemDelegate(parent), comboColumn(col), comboItems(items) {
    }

    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        option->displayAlignment = Qt::AlignCenter;

        if (index.column() == 0) {
            int idVal = index.data().toInt();
            if (idVal > 0) {
                option->text = QString("%1").arg(idVal, 3, 10, QChar('0'));
            }
        }
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (index.column() == comboColumn && !comboItems.isEmpty()) {
            QComboBox* cb = new QComboBox(parent);
            cb->addItems(comboItems);
            cb->setStyleSheet("QComboBox { padding-left: 10px; }");
            return cb;
        }
        return nullptr;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        if (index.column() == comboColumn && !comboItems.isEmpty()) {
            QString val = index.model()->data(index, Qt::EditRole).toString();
            QComboBox* cb = qobject_cast<QComboBox*>(editor);
            if (cb) cb->setCurrentText(val);
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        if (index.column() == comboColumn && !comboItems.isEmpty()) {
            QComboBox* cb = qobject_cast<QComboBox*>(editor);
            if (cb) {
                QString newRole = cb->currentText();
                model->setData(index, newRole, Qt::EditRole);
                int userId = model->index(index.row(), 0).data().toInt();

                if (userId > 0) {
                    QSqlQuery query(QSqlDatabase::database("server_db_connection"));
                    query.prepare("UPDATE users SET role = :r WHERE id = :id");
                    query.bindValue(":r", newRole);
                    query.bindValue(":id", userId);
                    if (!query.exec()) {
                        qDebug() << "严重错误：修改底层数据库失败！" << query.lastError().text();
                    }
                }
            }
        }
    }
};

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

    ui->tableView_GlobalRecords->setItemDelegate(new CenterAndComboDelegate(-1, QStringList(), this));
    loadGlobalRecords();

    m_permModel = new QSqlTableModel(this, QSqlDatabase::database("server_db_connection"));
    m_permModel->setTable("view_users_lite");
    m_permModel->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_permModel->select();

    ui->tableView_Permissions->setModel(m_permModel);
    ui->tableView_Permissions->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableView_Permissions->setEditTriggers(QAbstractItemView::DoubleClicked);

    for (int i = 0; i < m_permModel->columnCount(); ++i) {
        QString colName = m_permModel->record().fieldName(i);
        if (colName != "id" && colName != "name" && colName != "department" && colName != "job_title" && colName != "role") {
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
    ui->tableView_Permissions->setItemDelegate(new CenterAndComboDelegate(roleColIdx, roleOptions, this));

    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &AttendanceServer::onNewConnection);

    ui->tableWidget_OnlineUsers->setColumnCount(5);
    ui->tableWidget_OnlineUsers->setHorizontalHeaderLabels({ "姓名", "工作部门", "企业职务", "IP地址", "登录时间" });
    ui->tableWidget_OnlineUsers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_OnlineUsers->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->btn_StartServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StartServer_clicked);
    connect(ui->btn_StopServer, &QPushButton::clicked, this, &AttendanceServer::on_btn_StopServer_clicked);
    connect(ui->btn_RefreshData, &QPushButton::clicked, this, &AttendanceServer::on_btn_RefreshData_clicked);
    connect(ui->btn_ExportGlobal, &QPushButton::clicked, this, &AttendanceServer::on_btn_ExportGlobal_clicked);
}

AttendanceServer::~AttendanceServer() {
    delete ui;
}

void AttendanceServer::logMessage(const QString& msg) {
    QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    ui->textBrowser_ServerLog->append(timeStr + msg);
}

void AttendanceServer::on_btn_StartServer_clicked() {
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

void AttendanceServer::on_btn_StopServer_clicked() {
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

void AttendanceServer::onNewConnection() {
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &AttendanceServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &AttendanceServer::onClientDisconnected);
    logMessage(QString("收到新的物理连接请求: %1").arg(socket->peerAddress().toString()));
}

void AttendanceServer::onClientDisconnected() {
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

void AttendanceServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    while (socket->canReadLine()) {
        QByteArray data = socket->readLine();

        QtConcurrent::run([this, socket, data]() {
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isNull() || !doc.isObject()) return;

            QJsonObject json = doc.object();
            QString type = json["type"].toString();

            QString threadConnName = QString("ServerConn_%1_%2").arg(quintptr(QThread::currentThreadId())).arg(QDateTime::currentMSecsSinceEpoch());
            {
                QSqlDatabase threadDb = QSqlDatabase::addDatabase("QODBC", threadConnName);
                threadDb.setDatabaseName("DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3305;DATABASE=attendance_db;UID=root;PWD=root;");

                if (threadDb.open()) {

                    if (type == "query_face_features") {
                        QJsonArray arr;
                        QSqlQuery query(threadDb);
                        if (query.exec("SELECT name, feature FROM users WHERE feature IS NOT NULL")) {
                            while (query.next()) {
                                QJsonObject o;
                                o["name"] = query.value(0).toString();
                                o["feature"] = QString(query.value(1).toByteArray().toBase64());
                                arr.append(o);
                            }
                        }
                        QJsonObject res; res["status"] = "success"; res["data"] = arr;
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "publish_announcement") {
                        QString publisher = json["publisher"].toString();
                        QString content = json["content"].toString();
                        QSqlQuery q(threadDb);
                        q.prepare("INSERT INTO system_announcements (publisher, content, publish_time) VALUES (?, ?, NOW())");
                        q.addBindValue(publisher); q.addBindValue(content);
                        q.exec();
                    }

                    if (type == "client_login_auth") {
                        QString account = json["account"].toString();
                        QString pwd = json["pwd"].toString();
                        QString role = json["role"].toString();

                        QJsonObject res; res["status"] = "fail";
                        QSqlQuery q(threadDb);
                        QString sql = QString("SELECT name FROM users WHERE account = '%1' AND password = '%2' AND role = '%3'").arg(account, pwd, role);
                        if (q.exec(sql) && q.next()) {
                            res["status"] = "success";
                            res["real_name"] = q.value(0).toString();
                        }
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "client_register_account") {
                        QString account = json["account"].toString();
                        QString pwd = json["pwd"].toString();
                        QString name = json["name"].toString();
                        QString role = json["role"].toString();
                        QString dept = json["dept"].toString();
                        QString jobTitle = json["job_title"].toString();
                        QString phone = json["phone"].toString();
                        QString gender = json["gender"].toString();

                        QJsonObject res; res["status"] = "fail";
                        QString sql = QString("INSERT INTO users (account, password, name, role, department, job_title, phone, gender) VALUES ('%1', '%2', '%3', '%4', '%5', '%6', '%7', '%8')")
                            .arg(account, pwd, name, role, dept, jobTitle, phone, gender);
                        QSqlQuery q(threadDb);
                        if (q.exec(sql)) {
                            res["status"] = "success";
                            QMetaObject::invokeMethod(this, [this, name, role]() {
                                logMessage(QString("<font color='#E6A23C'>新兵入职: [%1] 注册了账号，权限为 [%2]。</font>").arg(name, role));
                                m_permModel->select(); // 刷新权限表格
                                }, Qt::QueuedConnection);
                        }
                        else {
                            res["msg"] = q.lastError().text();
                        }
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }

                    if (type == "login") {
                        QString name = json["name"].toString().trimmed();
                        QString ip = socket->peerAddress().toString().remove("::ffff:");
                        QString dept = "未知部门"; QString jobTitle = "未分配";

                        QSqlQuery query(threadDb);
                        query.prepare("SELECT department, job_title FROM users WHERE name = :name");
                        query.bindValue(":name", name);
                        if (query.exec() && query.next()) {
                            dept = query.value(0).toString(); jobTitle = query.value(1).toString();
                            if (dept.isEmpty()) dept = "未分配部门";
                            if (jobTitle.isEmpty()) jobTitle = "未分配";
                        }

                        QMetaObject::invokeMethod(this, [this, socket, name, dept, jobTitle, ip]() {
                            ClientInfo info{ name, dept, jobTitle, ip, QDateTime::currentDateTime().toString("HH:mm:ss") };
                            m_clients[socket] = info; m_nameToSocket[name] = socket;
                            updateOnlineUsersTable();
                            logMessage(QString("<font color='#409EFF'>认证成功: 员工 [%1] (%2 - %3) 已接入总控网关。</font>").arg(name, dept, jobTitle));
                            }, Qt::QueuedConnection);

                        QSqlQuery offlineQ(threadDb);
                        offlineQ.prepare("SELECT sender, msg_type, content, filename, send_time, department FROM offline_messages WHERE receiver = :n ORDER BY send_time ASC");
                        offlineQ.bindValue(":n", name);

                        int offlineCount = 0;
                        if (offlineQ.exec()) {
                            while (offlineQ.next()) {
                                QJsonObject offMsg;
                                offMsg["from"] = offlineQ.value(0).toString(); offMsg["type"] = offlineQ.value(1).toString();
                                offMsg["msg"] = offlineQ.value(2).toString(); offMsg["filename"] = offlineQ.value(3).toString();
                                offMsg["time"] = offlineQ.value(4).toDateTime().toString("HH:mm:ss");
                                offMsg["department"] = offlineQ.value(5).toString(); offMsg["is_offline"] = true;

                                QByteArray outData = QJsonDocument(offMsg).toJson(QJsonDocument::Compact) + "\n";
                                QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                                offlineCount++;
                            }
                        }
                        if (offlineCount > 0) {
                            QMetaObject::invokeMethod(this, [this, name, offlineCount]() {
                                logMessage(QString("<font color='#E6A23C'>已向 [%1] 补发 %2 条离线消息/文件。</font>").arg(name).arg(offlineCount));
                                }, Qt::QueuedConnection);
                        }

                        QSqlQuery deleteOffQ(threadDb);
                        deleteOffQ.prepare("DELETE FROM offline_messages WHERE receiver = :n");
                        deleteOffQ.bindValue(":n", name); deleteOffQ.exec();
                    }

                    else if (type == "query_user_profile") {
                        QString name = json["name"].toString();
                        QJsonObject res; res["status"] = "fail";
                        QSqlQuery q(threadDb);
                        q.prepare("SELECT id, job_title, role, department, gender, phone, name, avatar FROM users WHERE name = :n OR account = :n");
                        q.bindValue(":n", name);
                        if (q.exec() && q.next()) {
                            res["status"] = "success";
                            res["id"] = q.value(0).toInt();
                            res["job_title"] = q.value(1).toString();
                            res["role"] = q.value(2).toString();
                            res["department"] = q.value(3).toString();
                            res["gender"] = q.value(4).toString();
                            res["phone"] = q.value(5).toString();
                            res["real_name"] = q.value(6).toString();
                            res["avatar_base64"] = q.value(7).toString();
                        }
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "update_profile_field") {
                        QString name = json["name"].toString();
                        QString field = json["field"].toString(); // "gender", "phone", "avatar"
                        QString value = json["value"].toString();
                        QSqlQuery q(threadDb);
                        QString sql = QString("UPDATE users SET %1 = :v WHERE name = :n OR account = :n").arg(field);
                        q.prepare(sql);
                        q.bindValue(":v", value);
                        q.bindValue(":n", name);
                        q.exec();
                    }
                    else if (type == "verify_and_update_password") {
                        QString name = json["name"].toString();
                        QString oldPwd = json["old_pwd"].toString();
                        QString newPwd = json["new_pwd"].toString();
                        QJsonObject res; res["status"] = "fail";

                        QSqlQuery q(threadDb);
                        q.prepare("SELECT password FROM users WHERE name = :n OR account = :n");
                        q.bindValue(":n", name);
                        if (q.exec() && q.next()) {
                            if (q.value(0).toString() == oldPwd) {
                                QSqlQuery uq(threadDb);
                                uq.prepare("UPDATE users SET password = :newp WHERE name = :n OR account = :n");
                                uq.bindValue(":newp", newPwd);
                                uq.bindValue(":n", name);
                                if (uq.exec()) res["status"] = "success";
                            }
                            else {
                                res["msg"] = "旧密码错误！";
                            }
                        }
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }

                    else if (type == "verify_user_for_registration") {
                        QString name = json["name"].toString().trimmed();
                        QString dept = json["dept"].toString().trimmed();
                        QJsonObject res;
                        res["status"] = "fail";

                        QSqlQuery q(threadDb);
                        // 防 ODBC 中文绑定 Bug，采用 arg 强行拼接
                        QString sql = QString("SELECT id FROM users WHERE name = '%1' AND department = '%2'").arg(name, dept);
                        if (q.exec(sql) && q.next()) {
                            res["status"] = "success";
                        }

                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }

                    else if (type == "query_chat_contacts") {
                        QString name = json["name"].toString();
                        QJsonObject res; res["status"] = "success";

                        QString myEmpId, myDept, myJob;
                        QSqlQuery myQ(threadDb); myQ.prepare("SELECT id, department, job_title FROM users WHERE name = :n"); myQ.bindValue(":n", name);
                        if (myQ.exec() && myQ.next()) {
                            myEmpId = myQ.value(0).toString();
                            myDept = myQ.value(1).toString().isEmpty() ? "未分配部门" : myQ.value(1).toString();
                            myJob = myQ.value(2).toString().isEmpty() || myQ.value(2).toString() == "未分配" ? "员工" : myQ.value(2).toString();
                        }
                        res["my_dept"] = myDept;
                        res["my_folder"] = QString("%1_%2_%3_%4").arg(myEmpId, name, myDept, myJob);

                        QJsonArray deptArr;
                        QString deptSql = (myDept == "总经办") ? "SELECT DISTINCT department FROM users WHERE department != '' AND department IS NOT NULL"
                            : QString("SELECT DISTINCT department FROM users WHERE department = '%1'").arg(myDept);
                        QSqlQuery dQ(threadDb); dQ.exec(deptSql);
                        while (dQ.next()) deptArr.append(dQ.value(0).toString());
                        res["departments"] = deptArr;

                        QJsonArray userArr;
                        QSqlQuery uQ(threadDb); uQ.exec(QString("SELECT id, name, department, role FROM users WHERE name != '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(name));
                        while (uQ.next()) {
                            QJsonObject u; u["id"] = uQ.value(0).toInt(); u["name"] = uQ.value(1).toString().trimmed();
                            u["department"] = uQ.value(2).toString().trimmed(); u["role"] = uQ.value(3).toString().trimmed();
                            userArr.append(u);
                        }
                        res["users"] = userArr;

                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                        }
                    else if (type == "query_group_members") {
                            QString dept = json["department"].toString();
                            QJsonObject res; res["status"] = "success";
                            QJsonArray arr;

                            QString sql = (dept == "公司总群") ? "SELECT name, department, job_title FROM users WHERE account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'"
                                : QString("SELECT name, department, job_title FROM users WHERE department = '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(dept);
                            QSqlQuery q(threadDb); q.exec(sql);
                            while (q.next()) {
                                QJsonObject u; u["name"] = q.value(0).toString(); u["dept"] = q.value(1).toString();
                                QString j = q.value(2).toString(); u["job"] = (j.isEmpty() || j == "未分配") ? "员工" : j;
                                arr.append(u);
                            }
                            res["data"] = arr;
                            QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                            QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                            }

                    else if (type == "ai_save_message") {
                        QString sessionId = json["session_id"].toString();
                        QString role = json["role"].toString();
                        QString content = json["content"].toString();
                        QString name = json["name"].toString();

                        QSqlQuery q(threadDb);
                        q.prepare("INSERT INTO ai_chat_logs (session_id, role, content, create_time) VALUES (?, ?, ?, NOW())");
                        q.addBindValue(sessionId); q.addBindValue(role); q.addBindValue(content);
                        q.exec();

                        QSqlQuery uq(threadDb);
                        uq.prepare("UPDATE ai_sessions SET last_message = ? WHERE session_id = ?");
                        QString snippet = content; snippet.remove(QRegularExpression("<[^>]*>")); snippet.replace("\n", " ");
                        if (snippet.length() > 15) snippet = snippet.left(15) + "...";
                        uq.addBindValue(snippet); uq.addBindValue(sessionId); uq.exec();

                        // 触发服务器审计系统保存聊天记录文本
                        QString account = "Unk", dept = "Unk", title = "Unk";
                        QSqlQuery userQ(threadDb); userQ.prepare("SELECT account, department, job_title FROM users WHERE name = :n"); userQ.bindValue(":n", name);
                        if (userQ.exec() && userQ.next()) { account = userQ.value(0).toString(); dept = userQ.value(1).toString(); title = userQ.value(2).toString(); }

                        QString baseDir = QCoreApplication::applicationDirPath() + "/server/AiChat/";
                        QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
                        QDir dir; dir.mkpath(baseDir + folderName);

                        QString filePath = baseDir + folderName + "/Session_" + sessionId + ".doc";
                        QFile file(filePath);
                        if (file.open(QIODevice::Append | QIODevice::Text)) {
                            QTextStream out(&file); out.setEncoding(QStringConverter::Utf8);
                            QString roleName = (role == "user") ? QString("员工本人 (%1)").arg(name) : "AI 管家回复";
                            QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                            out << QString("【%1】 %2:\n%3\n\n----------------------------------------------------\n\n").arg(timeStr, roleName, content);
                            file.close();
                        }
                        }
                    else if (type == "create_ai_session") {
                            QSqlQuery q(threadDb);
                            q.prepare("INSERT INTO ai_sessions (session_id, user_name, title, create_time, is_visible, last_message) VALUES (?, ?, ?, NOW(), 1, '暂无聊天记录...')");
                            q.addBindValue(json["session_id"].toString());
                            q.addBindValue(json["name"].toString());
                            q.addBindValue(json["title"].toString());
                            q.exec();
                            }
                    else if (type == "query_ai_sessions") {
                                QJsonArray arr;
                                QSqlQuery q(threadDb);
                                q.exec(QString("SELECT session_id, title, last_message FROM ai_sessions WHERE user_name='%1' AND is_visible=1 ORDER BY create_time DESC").arg(json["name"].toString()));
                                while (q.next()) {
                                    QJsonObject o;
                                    o["session_id"] = q.value(0).toString();
                                    o["title"] = q.value(1).toString();
                                    o["last_message"] = q.value(2).toString();
                                    arr.append(o);
                                }
                                QJsonObject res; res["status"] = "success"; res["data"] = arr;
                                QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                                QMetaObject::invokeMethod(socket, [socket, out]() { socket->write(out); });
                                }
                    else if (type == "rename_ai_session") {
                                    QSqlQuery q(threadDb); q.prepare("UPDATE ai_sessions SET title=? WHERE session_id=?");
                                    q.addBindValue(json["title"].toString()); q.addBindValue(json["session_id"].toString()); q.exec();
                                    }
                    else if (type == "delete_ai_session") {
                                        QSqlQuery q(threadDb); q.prepare("UPDATE ai_sessions SET is_visible=0 WHERE session_id=?");
                                        q.addBindValue(json["session_id"].toString()); q.exec();
                                        }
                    else if (type == "search_ai_history") {
                                            QJsonArray arr;
                                            QSqlQuery q(threadDb);
                                            q.prepare("SELECT DISTINCT s.session_id, s.title, s.last_message FROM ai_sessions s JOIN ai_chat_logs l ON s.session_id = l.session_id WHERE s.user_name = ? AND s.is_visible=1 AND l.content LIKE ? ORDER BY s.create_time DESC");
                                            q.addBindValue(json["name"].toString()); q.addBindValue("%" + json["keyword"].toString() + "%");
                                            if (q.exec()) {
                                                while (q.next()) {
                                                    QJsonObject o; o["session_id"] = q.value(0).toString(); o["title"] = q.value(1).toString(); o["last_message"] = q.value(2).toString();
                                                    arr.append(o);
                                                }
                                            }
                                            QJsonObject res; res["status"] = "success"; res["data"] = arr;
                                            QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                                            QMetaObject::invokeMethod(socket, [socket, out]() { socket->write(out); });
                                            }
                    else if (type == "query_ai_chat_history") {
                                                QJsonArray arr;
                                                QSqlQuery q(threadDb); q.prepare("SELECT role, content FROM ai_chat_logs WHERE session_id = ? ORDER BY create_time ASC");
                                                q.addBindValue(json["session_id"].toString());
                                                if (q.exec()) {
                                                    while (q.next()) {
                                                        QJsonObject o; o["role"] = q.value(0).toString(); o["content"] = q.value(1).toString();
                                                        arr.append(o);
                                                    }
                                                }
                                                QJsonObject res; res["status"] = "success"; res["data"] = arr;
                                                QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                                                QMetaObject::invokeMethod(socket, [socket, out]() { socket->write(out); });
                                                }
                    else if (type == "query_today_attendance_for_ai") {
                                                    QJsonArray arr;
                                                    QSqlQuery q(threadDb); q.prepare("SELECT punch_time, status FROM attendance_records WHERE name = ? AND DATE(punch_time) = CURDATE() ORDER BY punch_time ASC");
                                                    q.addBindValue(json["name"].toString());
                                                    if (q.exec()) {
                                                        while (q.next()) {
                                                            QJsonObject o; o["time"] = q.value(0).toDateTime().toString("HH:mm:ss"); o["status"] = q.value(1).toString();
                                                            arr.append(o);
                                                        }
                                                    }
                                                    QJsonObject res; res["status"] = "success"; res["data"] = arr;
                                                    QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                                                    QMetaObject::invokeMethod(socket, [socket, out]() { socket->write(out); });
                                                    }

                    else if (type == "query_home_dashboard") {
                        QString role = json["role"].toString();
                        QString name = json["name"].toString();
                        QJsonObject res; res["status"] = "success";

                        QJsonObject topCards;
                        QSqlQuery q(threadDb);
                        q.exec("SELECT COUNT(*) FROM users WHERE role != '超级管理员'");
                        if (q.next()) topCards["total_expected"] = q.value(0).toInt();

                        q.exec("SELECT COUNT(DISTINCT name) FROM attendance_records WHERE DATE(punch_time) = CURDATE()");
                        if (q.next()) topCards["actual_punched"] = q.value(0).toInt();

                        q.exec("SELECT COUNT(DISTINCT name) FROM attendance_records WHERE DATE(punch_time) = CURDATE() AND status NOT LIKE '%正常%'");
                        if (q.next()) topCards["abnormal_count"] = q.value(0).toInt();

                        if (role.contains("管理员") || role == "经理") {
                            q.exec(QString("SELECT COUNT(*) FROM leave_requests WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(name));
                            if (q.next()) topCards["pending_leaves"] = q.value(0).toInt();
                            q.exec(QString("SELECT COUNT(*) FROM appeals WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(name));
                            if (q.next()) topCards["pending_appeals"] = q.value(0).toInt();
                        }
                        else {
                            q.exec(QString("SELECT COUNT(*) FROM attendance_records WHERE name='%1' AND DATE(punch_time) >= DATE_FORMAT(CURDATE() ,'%Y-%m-01') AND status NOT LIKE '%%正常%%'").arg(name));
                            if (q.next()) topCards["my_abnormal"] = q.value(0).toInt();
                        }
                        res["top_cards"] = topCards;

                        QJsonArray pieArr;
                        q.exec("SELECT status, COUNT(*) FROM attendance_records WHERE DATE(punch_time) = CURDATE() GROUP BY status");
                        while (q.next()) {
                            QJsonObject o; o["status"] = q.value(0).toString(); o["count"] = q.value(1).toInt();
                            pieArr.append(o);
                        }
                        res["pie_chart"] = pieArr;

                        QJsonArray barArr;
                        q.exec("SELECT u.department, COUNT(a.id) FROM users u LEFT JOIN attendance_records a ON u.name = a.name AND a.status NOT LIKE '%正常%' WHERE DATE(a.punch_time) >= DATE_SUB(CURDATE(), INTERVAL 30 DAY) GROUP BY u.department");
                        while (q.next()) {
                            QJsonObject o; o["dept"] = q.value(0).toString().isEmpty() ? "未分组" : q.value(0).toString(); o["count"] = q.value(1).toInt();
                            barArr.append(o);
                        }
                        res["bar_chart"] = barArr;

                        QJsonArray lineArr;
                        q.exec("SELECT DATE_FORMAT(punch_time, '%m-%d'), COUNT(DISTINCT name) FROM attendance_records WHERE punch_time >= DATE_SUB(CURDATE(), INTERVAL 6 DAY) GROUP BY DATE(punch_time) ORDER BY DATE(punch_time) ASC");
                        while (q.next()) {
                            QJsonObject o; o["date"] = q.value(0).toString(); o["count"] = q.value(1).toInt();
                            lineArr.append(o);
                        }
                        res["line_chart"] = lineArr;

                        QJsonArray feedArr;
                        q.exec("SELECT DATE_FORMAT(punch_time, '%H:%i:%s'), name, status FROM attendance_records WHERE DATE(punch_time) = CURDATE() ORDER BY punch_time DESC LIMIT 10");
                        while (q.next()) {
                            QJsonObject o; o["time"] = q.value(0).toString(); o["name"] = q.value(1).toString(); o["status"] = q.value(2).toString();
                            feedArr.append(o);
                        }
                        res["feed_list"] = feedArr;

                        QJsonArray noticeArr;
                        q.exec("SELECT content, DATE_FORMAT(publish_time, '%m-%d') FROM system_announcements ORDER BY publish_time DESC LIMIT 3");
                        while (q.next()) {
                            QJsonObject o; o["content"] = q.value(0).toString(); o["date"] = q.value(1).toString();
                            noticeArr.append(o);
                        }
                        res["notice_list"] = noticeArr;

                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                        }

                    else if (type == "query_user_list") {
                        QString dept = json["dept"].toString();
                        QString keyword = json["keyword"].toString();

                        QString sql = "SELECT id, account, name, gender, department, job_title, phone FROM view_users_lite WHERE account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'";
                        if (dept != "全部" && !dept.isEmpty()) {
                            sql += QString(" AND department = '%1'").arg(dept);
                        }
                        if (!keyword.isEmpty()) {
                            sql += QString(" AND (name LIKE '%%%1%%' OR id LIKE '%%%1%%')").arg(keyword);
                        }

                        QJsonArray arr;
                        QSqlQuery q(threadDb);
                        if (q.exec(sql)) {
                            while (q.next()) {
                                QJsonObject o;
                                o["id"] = q.value(0).toString();
                                o["account"] = q.value(1).toString();
                                o["name"] = q.value(2).toString();
                                o["gender"] = q.value(3).toString();
                                o["department"] = q.value(4).toString();
                                o["job_title"] = q.value(5).toString();
                                o["phone"] = q.value(6).toString();
                                arr.append(o);
                            }
                        }
                        QJsonObject res; res["status"] = "success"; res["data"] = arr;
                        QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, out]() { socket->write(out); });
                    }
                    else if (type == "admin_reset_password") {
                        QString account = json["account"].toString();
                        QString empName = json["name"].toString();
                        QSqlQuery q(threadDb);
                        q.prepare("UPDATE users SET password = '123456' WHERE account = :acc");
                        q.bindValue(":acc", account);
                        QJsonObject res;
                        if (q.exec()) {
                            res["status"] = "success";
                            QMetaObject::invokeMethod(this, [this, empName]() {
                                logMessage(QString("<font color='#E6A23C'>权限操作: 管理员已重置 [%1] 的登录密码。</font>").arg(empName));
                                }, Qt::QueuedConnection);
                        }
                        else {
                            res["status"] = "fail";
                        }
                        QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, out]() { socket->write(out); });
                    }
                    else if (type == "admin_delete_user") {
                        QString account = json["account"].toString();
                        QString empName = json["name"].toString();
                        QSqlQuery q(threadDb);
                        q.prepare("DELETE FROM users WHERE account = :acc");
                        q.bindValue(":acc", account);
                        QJsonObject res;
                        if (q.exec()) {
                            res["status"] = "success";
                            QMetaObject::invokeMethod(this, [this, empName]() {
                                logMessage(QString("<font color='red'>高危操作: 管理员已将员工 [%1] 彻底踢出系统及人脸库！</font>").arg(empName));
                                // 刷新一下服务端的权限表格
                                m_permModel->select();
                                }, Qt::QueuedConnection);
                        }
                        else {
                            res["status"] = "fail";
                        }
                        QByteArray out = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, out]() { socket->write(out); });
                    }
                    else if (type == "query_user_dept") {
                        QString name = json["name"].toString();
                        QString dept = "全部";
                        QSqlQuery dq(threadDb);
                        dq.prepare("SELECT department FROM users WHERE name = :n");
                        dq.bindValue(":n", name);
                        if (dq.exec() && dq.next()) dept = dq.value(0).toString();

                        QJsonObject res; res["type"] = "user_dept_reply"; res["department"] = dept;
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "query_shift_rule") {
                        QString dept = json["dept"].toString();
                        QJsonObject res; res["status"] = "fail";
                        QSqlQuery sq(threadDb);
                        sq.prepare("SELECT rule_name, start_time, end_time, late_mins, absent_mins FROM shift_rules WHERE dept = :d OR dept = '全部' ORDER BY (dept = :d) DESC LIMIT 1");
                        sq.bindValue(":d", dept);
                        if (sq.exec() && sq.next()) {
                            res["status"] = "success";
                            res["rule_name"] = sq.value(0).toString();
                            res["start_time"] = sq.value(1).toTime().toString("HH:mm:ss");
                            res["end_time"] = sq.value(2).toTime().toString("HH:mm:ss");
                            res["late_mins"] = sq.value(3).toInt();
                            res["absent_mins"] = sq.value(4).toInt();
                        }
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "query_today_status") {
                        QString name = json["name"].toString();
                        QJsonObject res; res["status"] = "success";

                        QSqlQuery leaveQ(threadDb);
                        leaveQ.prepare("SELECT id FROM leave_requests WHERE applicant=:n AND status='已批准' AND CURDATE() BETWEEN DATE(start_time) AND DATE(end_time)");
                        leaveQ.bindValue(":n", name);
                        res["is_on_leave"] = (leaveQ.exec() && leaveQ.next());

                        QJsonArray punches;
                        QSqlQuery query(threadDb);
                        query.prepare("SELECT punch_time, status FROM attendance_records WHERE name = :n AND DATE(punch_time) = CURDATE() ORDER BY punch_time ASC");
                        query.bindValue(":n", name);
                        if (query.exec()) {
                            while (query.next()) {
                                QJsonObject p;
                                p["time"] = query.value(0).toDateTime().toString("HH:mm:ss");
                                p["status"] = query.value(1).toString();
                                punches.append(p);
                            }
                        }
                        res["punches"] = punches;
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "query_approval_candidates") {
                        QString name = json["name"].toString();
                        QJsonObject res;

                        QSqlQuery infoQ(threadDb);
                        infoQ.prepare("SELECT role, department, job_title FROM users WHERE name=:n");
                        infoQ.bindValue(":n", name);
                        if (infoQ.exec() && infoQ.next()) {
                            res["my_role"] = infoQ.value(0).toString();
                            res["my_dept"] = infoQ.value(1).toString();
                            res["my_job"] = infoQ.value(2).toString();
                        }

                        QJsonArray abnArr;
                        QSqlQuery rq(threadDb);
                        rq.prepare("SELECT punch_time, status FROM attendance_records WHERE name=:n AND (status NOT LIKE '%正常%') ORDER BY punch_time DESC LIMIT 10");
                        rq.bindValue(":n", name);
                        if (rq.exec()) {
                            while (rq.next()) {
                                QJsonObject rec;
                                rec["time"] = rq.value(0).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
                                rec["display"] = rq.value(0).toDateTime().toString("MM-dd HH:mm") + " [" + rq.value(1).toString() + "]";
                                abnArr.append(rec);
                            }
                        }
                        res["abnormal_records"] = abnArr;

                        QJsonArray hrArr, gmArr, mgrArr;
                        QSqlQuery hq(threadDb); hq.exec("SELECT name FROM users WHERE department = '人力资源部' AND job_title = '部门经理' AND name NOT LIKE '%超级管理员%'");
                        while (hq.next()) hrArr.append(hq.value(0).toString());

                        QSqlQuery gq(threadDb); gq.exec("SELECT name FROM users WHERE job_title IN ('总经理', '总裁', '董事长') LIMIT 1");
                        while (gq.next()) gmArr.append(gq.value(0).toString());

                        QSqlQuery mq(threadDb); mq.prepare("SELECT name FROM users WHERE department=:d AND role='经理' AND name != :me");
                        mq.bindValue(":d", res["my_dept"].toString()); mq.bindValue(":me", name); mq.exec();
                        while (mq.next()) mgrArr.append(mq.value(0).toString());

                        res["hr_list"] = hrArr; res["gm_list"] = gmArr; res["mgr_list"] = mgrArr;

                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "query_pending_leaves") {
                        QString approver = json["approver"].toString();
                        QJsonObject res; res["status"] = "success";
                        QJsonArray arr;
                        QSqlQuery q(threadDb);
                        QString sql = QString("SELECT id, applicant, leave_type, start_time, end_time, reason FROM leave_requests WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(approver);
                        if (q.exec(sql)) {
                            while (q.next()) {
                                QJsonObject row;
                                row["id"] = q.value(0).toInt();
                                row["applicant"] = q.value(1).toString();
                                row["type"] = q.value(2).toString();
                                row["start"] = q.value(3).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
                                row["end"] = q.value(4).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
                                row["reason"] = q.value(5).toString();
                                arr.append(row);
                            }
                        }
                        res["data"] = arr;
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "query_pending_appeals") {
                        QString approver = json["approver"].toString();
                        QJsonObject res; res["status"] = "success";
                        QJsonArray arr;
                        QSqlQuery q(threadDb);
                        QString sql = QString("SELECT id, applicant, abnormal_time, original_status, reason FROM appeals WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(approver);
                        if (q.exec(sql)) {
                            while (q.next()) {
                                QJsonObject row;
                                row["id"] = q.value(0).toInt();
                                row["applicant"] = q.value(1).toString();
                                row["time"] = q.value(2).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
                                row["type"] = q.value(3).toString();
                                row["reason"] = q.value(4).toString();
                                arr.append(row);
                            }
                        }
                        res["data"] = arr;
                        QByteArray outData = QJsonDocument(res).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    // ==========================================

                    else if (type == "punch_request") {
                        QString name = json["name"].toString();
                        QDateTime serverNow = QDateTime::currentDateTime();
                        QString timeStr = serverNow.toString("yyyy-MM-dd HH:mm:ss");
                        QTime currentTime = serverNow.time();

                        QString dept = "全部";
                        QSqlQuery dq(threadDb);
                        dq.prepare("SELECT department FROM users WHERE name = :n");
                        dq.bindValue(":n", name);
                        if (dq.exec() && dq.next()) dept = dq.value(0).toString();

                        QTime startTime(9, 0), endTime(18, 0);
                        int absentMins = 120;
                        QSqlQuery sq(threadDb);
                        sq.prepare("SELECT start_time, end_time, absent_mins FROM shift_rules WHERE dept = :d OR dept = '全部' ORDER BY (dept = :d) DESC LIMIT 1");
                        sq.bindValue(":d", dept);
                        if (sq.exec() && sq.next()) {
                            startTime = sq.value(0).toTime();
                            endTime = sq.value(1).toTime();
                            absentMins = sq.value(2).toInt();
                        }

                        QString status = "正常打卡";
                        if (currentTime < QTime(12, 0)) {
                            int secsLate = startTime.secsTo(currentTime);
                            if (secsLate > 0) {
                                status = (secsLate > absentMins * 60) ? "旷工" : "迟到";
                            }
                        }
                        else {
                            status = (currentTime < endTime) ? "早退" : "正常下班";
                        }

                        QSqlQuery insertQuery(threadDb);
                        insertQuery.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (:n, :t, :s)");
                        insertQuery.bindValue(":n", name);
                        insertQuery.bindValue(":t", timeStr);
                        insertQuery.bindValue(":s", status);

                        if (insertQuery.exec()) {
                            QMetaObject::invokeMethod(this, [this, name, status]() {
                                logMessage(QString("<font color='#00B42A'>考勤中心: 成功记录 [%1] 的考勤: %2</font>").arg(name, status));
                                loadGlobalRecords();
                                }, Qt::QueuedConnection);
                        }
                        else {
                            QString err = insertQuery.lastError().text();
                            QMetaObject::invokeMethod(this, [this, err]() {
                                logMessage(QString("<font color='red'>数据库写入失败(考勤表): %1</font>").arg(err));
                                }, Qt::QueuedConnection);
                        }
                    }
                    else if (type == "rule_settings") {
                        QSqlQuery q(threadDb);
                        q.prepare("REPLACE INTO shift_rules (dept, rule_name, start_time, end_time, late_mins, absent_mins) VALUES (?, ?, ?, ?, ?, ?)");
                        q.addBindValue(json["dept"].toString());
                        q.addBindValue(json["rule_name"].toString());
                        q.addBindValue(json["start_time"].toString());
                        q.addBindValue(json["end_time"].toString());
                        q.addBindValue(json["late_mins"].toInt());
                        q.addBindValue(json["absent_mins"].toInt());
                        q.exec();
                        QMetaObject::invokeMethod(this, [this]() { logMessage("<font color='#E6A23C'>规则中心: 管理层更新了企业排班规则。</font>"); }, Qt::QueuedConnection);
                    }
                    else if (type == "status_update") {
                        QSqlQuery q(threadDb);
                        q.prepare("UPDATE users SET status_icon = :s WHERE name = :n");
                        q.bindValue(":s", json["status"].toString());
                        q.bindValue(":n", json["name"].toString());
                        q.exec();
                    }
                    else if (type == "punch_cheat") {
                        QSqlQuery q(threadDb);
                        q.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (?, NOW(), '作弊打卡')");
                        q.addBindValue(json["name"].toString());
                        q.exec();
                        QMetaObject::invokeMethod(this, [this, json]() {
                            logMessage(QString("<font color='red'>安全警报: 员工 [%1] 多次人脸核验失败，强制记为作弊！</font>").arg(json["name"].toString()));
                            loadGlobalRecords();
                            }, Qt::QueuedConnection);
                    }
                    else if (type == "appeal_request") {
                        QSqlQuery ins(threadDb);
                        ins.prepare("INSERT INTO appeals (applicant, abnormal_time, original_status, reason, approver, status) VALUES (?, ?, ?, ?, ?, '待审批')");
                        ins.addBindValue(json["applicant"].toString());
                        ins.addBindValue(json["abnormal_time"].toString());
                        ins.addBindValue(json["original_status"].toString());
                        ins.addBindValue(json["reason"].toString());
                        ins.addBindValue(json["approver"].toString());

                        if (!ins.exec()) {
                            QString err = ins.lastError().text();
                            QMetaObject::invokeMethod(this, [this, err]() { logMessage(QString("<font color='red'>数据库写入失败(申诉表): %1</font>").arg(err)); }, Qt::QueuedConnection);
                        }
                        else {
                            QMetaObject::invokeMethod(this, [this, json]() { logMessage(QString("<font color='#409EFF'>流程中心: 收到并入库 [%1] 的异常考勤申诉。</font>").arg(json["applicant"].toString())); }, Qt::QueuedConnection);
                        }
                    }
                    else if (type == "leave_request") {
                        QSqlQuery ins(threadDb);
                        ins.prepare("INSERT INTO leave_requests (applicant, leave_type, start_time, end_time, reason, approver, status) VALUES (?, ?, ?, ?, ?, ?, '待审批')");
                        ins.addBindValue(json["applicant"].toString());
                        ins.addBindValue(json["leave_type"].toString());
                        ins.addBindValue(json["start_time"].toString());
                        ins.addBindValue(json["end_time"].toString());
                        ins.addBindValue(json["reason"].toString());
                        ins.addBindValue(json["approver"].toString());

                        if (!ins.exec()) {
                            QString err = ins.lastError().text();
                            QMetaObject::invokeMethod(this, [this, err]() { logMessage(QString("<font color='red'>数据库写入失败(请假表): %1</font>").arg(err)); }, Qt::QueuedConnection);
                        }
                        else {
                            QMetaObject::invokeMethod(this, [this, json]() { logMessage(QString("<font color='#409EFF'>流程中心: 成功收到并入库 [%1] 的请假申请。</font>").arg(json["applicant"].toString())); }, Qt::QueuedConnection);
                        }
                    }
                    else if (type == "leave_approve") {
                        int reqId = json["reqId"].toInt();
                        QString applicant = json["applicant"].toString();
                        QString sTimeStr = json["start_time"].toString();
                        QString eTimeStr = json["end_time"].toString();
                        QString lType = json["leave_type"].toString();

                        QSqlQuery upd(threadDb);
                        upd.exec(QString("UPDATE leave_requests SET status='已批准' WHERE id=%1").arg(reqId));

                        QString finalStatus = "假-" + lType;

                        QDate sDate = QDate::fromString(sTimeStr.left(10), "yyyy-MM-dd");
                        QDate eDate = QDate::fromString(eTimeStr.left(10), "yyyy-MM-dd");

                        if (sDate.isValid() && eDate.isValid() && sDate <= eDate) {
                            for (QDate d = sDate; d <= eDate; d = d.addDays(1)) {
                                QString punchTime = d.toString("yyyy-MM-dd") + " 09:00:00";
                                QSqlQuery insertQ(threadDb);
                                insertQ.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (?, ?, ?)");
                                insertQ.addBindValue(applicant);
                                insertQ.addBindValue(punchTime);
                                insertQ.addBindValue(finalStatus);
                                insertQ.exec();
                            }
                        }
                        else {
                            QSqlQuery insertQ(threadDb);
                            insertQ.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (?, ?, ?)");
                            insertQ.addBindValue(applicant);
                            insertQ.addBindValue(sTimeStr);
                            insertQ.addBindValue(finalStatus);
                            insertQ.exec();
                        }

                        QMetaObject::invokeMethod(this, [this, applicant]() {
                            logMessage(QString("<font color='#67C23A'>流程审批: [%1] 的请假已获批准，多天流水已连片入库。</font>").arg(applicant));
                            loadGlobalRecords();
                            }, Qt::QueuedConnection);
                    }
                    else if (type == "appeal_approve") {
                        int reqId = json["reqId"].toInt();
                        QString applicant = json["applicant"].toString();
                        QString aTime = json["abnormal_time"].toString();
                        QString aType = json["appeal_type"].toString();

                        QSqlQuery upd(threadDb);
                        upd.exec(QString("UPDATE appeals SET status='已批准' WHERE id=%1").arg(reqId));
                        if (aType == "整天申诉") {
                            upd.exec(QString("UPDATE attendance_records SET status='正常(修正)' WHERE name='%1' AND DATE(punch_time)=DATE('%2')").arg(applicant, aTime));
                        }
                        else {
                            upd.exec(QString("UPDATE attendance_records SET status='正常(修正)' WHERE name='%1' AND punch_time='%2'").arg(applicant, aTime));
                        }
                        QMetaObject::invokeMethod(this, [this, applicant]() {
                            logMessage(QString("<font color='#67C23A'>流程审批: [%1] 的异常申诉已通过，后台自动修正。</font>").arg(applicant));
                            loadGlobalRecords();
                            }, Qt::QueuedConnection);
                    }
                    else if (type == "register_face") {
                        QString name = json["name"].toString();
                        QByteArray featureData = QByteArray::fromBase64(json["feature"].toString().toUtf8());

                        QSqlQuery q(threadDb);
                        q.prepare("UPDATE users SET feature = :f WHERE name = :n");
                        q.bindValue(":f", featureData);
                        q.bindValue(":n", name);
                        if (q.exec()) {
                            QMetaObject::invokeMethod(this, [this, name]() {
                                logMessage(QString("<font color='#00B42A'>人脸中心: 成功接收并落盘员工 [%1] 的人脸特征生物模型。</font>").arg(name));
                                }, Qt::QueuedConnection);
                        }
                        else {
                            QString err = q.lastError().text();
                            QMetaObject::invokeMethod(this, [this, err]() {
                                logMessage(QString("<font color='red'>人脸入库致命失败: %1</font>").arg(err));
                                }, Qt::QueuedConnection);
                        }
                    }

                    // ========================================================
                    // 🚀 终极架构大脑：强制使用 arg() 拼接 SQL，解决 ODBC 中文绑定失效的史诗级 BUG！
                    // ========================================================
                    else if (type == "query_my_requests") {
                        QString name = json["name"].toString().trimmed();
                        QJsonObject response;
                        response["status"] = "success";

                        QJsonArray leaveArr;
                        QSqlQuery lq(threadDb);
                        // 强制字符串拼接，绕开 bindValue 的中文编码过滤陷阱
                        QString lqStr = QString("SELECT leave_type, start_time, end_time, reason, approver, status FROM leave_requests WHERE applicant LIKE '%%%1%%' ORDER BY id DESC").arg(name);
                        if (lq.exec(lqStr)) {
                            while (lq.next()) {
                                QJsonObject row;
                                row["type"] = lq.value(0).toString();
                                row["start"] = lq.value(1).toDateTime().toString("MM-dd HH:mm");
                                row["end"] = lq.value(2).toDateTime().toString("MM-dd HH:mm");
                                row["reason"] = lq.value(3).toString();
                                row["approver"] = lq.value(4).toString();
                                row["status"] = lq.value(5).toString();
                                leaveArr.append(row);
                            }
                        }
                        response["leave_data"] = leaveArr;

                        QJsonArray appealArr;
                        QSqlQuery aq(threadDb);
                        QString aqStr = QString("SELECT abnormal_time, original_status, reason, approver, status FROM appeals WHERE applicant LIKE '%%%1%%' ORDER BY id DESC").arg(name);
                        if (aq.exec(aqStr)) {
                            while (aq.next()) {
                                QJsonObject row;
                                row["time"] = aq.value(0).toDateTime().toString("MM-dd HH:mm");
                                row["type"] = aq.value(1).toString();
                                row["reason"] = aq.value(2).toString();
                                row["approver"] = aq.value(3).toString();
                                row["status"] = aq.value(4).toString();
                                appealArr.append(row);
                            }
                        }
                        response["appeal_data"] = appealArr;

                        QByteArray outData = QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "query_monthly_status") {
                        QString name = json["name"].toString().trimmed();
                        int year = json["year"].toInt();
                        int month = json["month"].toInt();

                        QMap<QString, QStringList> dailyStatus;

                        QSqlQuery query(threadDb);
                        QString qStr = QString("SELECT DATE(punch_time), status FROM attendance_records WHERE name LIKE '%%%1%%' AND YEAR(punch_time) = %2 AND MONTH(punch_time) = %3").arg(name).arg(year).arg(month);
                        if (query.exec(qStr)) {
                            while (query.next()) {
                                dailyStatus[query.value(0).toDate().toString("yyyy-MM-dd")].append(query.value(1).toString());
                            }
                        }

                        int normalDays = 0, lateDays = 0, absentDays = 0, leaveDays = 0;
                        QJsonObject colorMap;

                        for (auto it = dailyStatus.constBegin(); it != dailyStatus.constEnd(); ++it) {
                            bool hasLeave = false, hasAbsent = false, hasLate = false, hasNormal = false;
                            for (const QString& s : it.value()) {
                                if (s.contains("假") || s.contains("调休")) hasLeave = true;
                                else if (s.contains("旷工") || s.contains("缺")) hasAbsent = true;
                                else if (s.contains("迟到") || s.contains("早退")) hasLate = true;
                                else if (s.contains("正常")) hasNormal = true;
                            }

                            if (hasLeave) { colorMap[it.key()] = "leave"; leaveDays++; }
                            else if (hasAbsent) { colorMap[it.key()] = "absent"; absentDays++; }
                            else if (hasLate) { colorMap[it.key()] = "late"; lateDays++; }
                            else if (hasNormal) { colorMap[it.key()] = "normal"; normalDays++; }
                        }

                        QJsonObject response;
                        response["type"] = "monthly_status_reply";
                        response["normal_days"] = normalDays;
                        response["late_days"] = lateDays;
                        response["leave_days"] = leaveDays;
                        response["absent_days"] = absentDays;
                        response["color_map"] = colorMap;

                        QByteArray outData = QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "query_attendance_detail") {
                        QString nameFilter = json["name_filter"].toString().trimmed();
                        QString sDate = json["start_date"].toString();
                        QString eDate = json["end_date"].toString();
                        QString statusFilter = json["status_filter"].toString();

                        QJsonArray recordsArr;

                        QString attSql = QString("SELECT id, name, punch_time, status FROM attendance_records WHERE DATE(punch_time) BETWEEN '%1' AND '%2'").arg(sDate, eDate);
                        if (!nameFilter.isEmpty()) attSql += QString(" AND name LIKE '%%%1%%'").arg(nameFilter); // 强行拼接

                        if (statusFilter == "正常") attSql += " AND status LIKE '%正常%' AND status NOT LIKE '%补卡%'";
                        else if (statusFilter == "请假") attSql += " AND (status LIKE '%假%' OR status LIKE '%调休%')";
                        else if (statusFilter != "全部状态") attSql += QString(" AND status LIKE '%%%1%%'").arg(statusFilter);

                        attSql += " ORDER BY punch_time DESC";

                        QSqlQuery attQ(threadDb);
                        if (attQ.exec(attSql)) {
                            while (attQ.next()) {
                                QJsonObject row;
                                row["id"] = attQ.value(0).toString();
                                row["name"] = attQ.value(1).toString();
                                row["time"] = attQ.value(2).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
                                row["status"] = attQ.value(3).toString();
                                row["source"] = "A";
                                recordsArr.append(row);
                            }
                        }

                        QJsonObject response;
                        response["status"] = "success";
                        response["records"] = recordsArr;
                        QByteArray outData = QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n";
                        QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); }, Qt::QueuedConnection);
                    }
                    else if (type == "admin_modify_status") {
                        int recordId = json["record_id"].toInt();
                        QString newStatus = json["new_status"].toString();
                        QSqlQuery q(threadDb);
                        q.prepare("UPDATE attendance_records SET status = :s WHERE id = :id");
                        q.bindValue(":s", newStatus);
                        q.bindValue(":id", recordId);
                        if (q.exec()) {
                            QMetaObject::invokeMethod(this, [this]() {
                                logMessage("<font color='#E6A23C'>⚠️ 管理员后台强行修改了某条考勤记录状态。</font>");
                                loadGlobalRecords();
                                }, Qt::QueuedConnection);
                        }
                    }

                    else if (type == "broadcast") {
                        QString fromUser = json["from"].toString();
                        QString content = json["msg"].toString();

                        QString senderRole = "普通登录";
                        QSqlQuery roleQ(threadDb);
                        roleQ.prepare("SELECT role FROM users WHERE name = :n OR account = :n");
                        roleQ.bindValue(":n", fromUser);
                        if (roleQ.exec() && roleQ.next()) {
                            senderRole = roleQ.value(0).toString();
                        }

                        if (senderRole != "超级管理员" && senderRole != "管理员登录" && senderRole != "经理") {
                            QMetaObject::invokeMethod(this, [this, fromUser, senderRole]() {
                                logMessage(QString("<font color='red'>越权拦截: 员工 [%1](%2) 尝试发送系统广播，已被零信任网关阻断！</font>").arg(fromUser, senderRole));
                                }, Qt::QueuedConnection);
                            return;
                        }

                        QMetaObject::invokeMethod(this, [this, fromUser]() {
                            logMessage(QString("<font color='#F56C6C'>授权通过: 管理层 [%1] 已下发全员广播。</font>").arg(fromUser));
                            }, Qt::QueuedConnection);

                        QSqlQuery allUsersQ(threadDb);
                        allUsersQ.exec("SELECT name FROM users WHERE role != '超级管理员' AND account != 'admin'");
                        int pushCount = 0;
                        while (allUsersQ.next()) {
                            QString member = allUsersQ.value(0).toString().trimmed();
                            if (member == fromUser) continue;

                            bool isOnline = false;
                            QMetaObject::invokeMethod(this, [this, member, &isOnline]() { isOnline = m_nameToSocket.contains(member); }, Qt::BlockingQueuedConnection);

                            if (isOnline) {
                                QTcpSocket* targetSocket = nullptr;
                                QMetaObject::invokeMethod(this, [this, member, &targetSocket]() { targetSocket = m_nameToSocket[member]; }, Qt::BlockingQueuedConnection);
                                if (targetSocket) {
                                    QMetaObject::invokeMethod(targetSocket, [targetSocket, data]() { targetSocket->write(data); }, Qt::QueuedConnection);
                                    pushCount++;
                                }
                            }
                            else {
                                QSqlQuery insertQ(threadDb);
                                insertQ.prepare("INSERT INTO offline_messages (sender, receiver, department, msg_type, content, filename, send_time) VALUES (:s, :r, '', 'broadcast', :c, '', NOW())");
                                insertQ.bindValue(":s", fromUser); insertQ.bindValue(":r", member); insertQ.bindValue(":c", content); insertQ.exec();
                            }
                        }
                        QMetaObject::invokeMethod(this, [this, pushCount]() { logMessage(QString("   └─ 成功即时推送到 %1 名在线员工。").arg(pushCount)); }, Qt::QueuedConnection);
                    }
                    else if (type == "read_receipt") {
                        QString toUser = json["to"].toString();
                        QTcpSocket* targetSocket = nullptr;
                        QMetaObject::invokeMethod(this, [this, toUser, &targetSocket]() {
                            if (m_nameToSocket.contains(toUser)) targetSocket = m_nameToSocket[toUser];
                            }, Qt::BlockingQueuedConnection);
                        if (targetSocket) {
                            QMetaObject::invokeMethod(targetSocket, [targetSocket, data]() { targetSocket->write(data); }, Qt::QueuedConnection);
                        }
                    }
                    else if (type == "ai_audit") {
                        QString name = json["name"].toString();
                        QString role = json["role"].toString();
                        QString content = json["content"].toString();
                        QString sessionId = json["session_id"].toString();

                        QString account = "Unk", dept = "Unk", title = "Unk";
                        QSqlQuery q(threadDb); q.prepare("SELECT account, department, job_title FROM users WHERE name = :n"); q.bindValue(":n", name);
                        if (q.exec() && q.next()) { account = q.value(0).toString(); dept = q.value(1).toString(); title = q.value(2).toString(); }

                        QString baseDir = QCoreApplication::applicationDirPath() + "/server/AiChat/";
                        QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
                        QDir dir; dir.mkpath(baseDir + folderName);

                        QString filePath = baseDir + folderName + "/Session_" + sessionId + ".doc";
                        QFile file(filePath);
                        if (file.open(QIODevice::Append | QIODevice::Text)) {
                            QTextStream out(&file); out.setEncoding(QStringConverter::Utf8);
                            QString roleName = (role == "user") ? QString("员工本人 (%1)").arg(name) : "AI 管家回复";
                            QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                            out << QString("【%1】 %2:\n%3\n\n----------------------------------------------------\n\n").arg(timeStr, roleName, content);
                            file.close();
                        }
                    }
                    else if (type == "ai_audit_file") {
                        QString name = json["name"].toString();
                        QString sessionId = json["session_id"].toString();
                        QString fileName = json["filename"].toString();
                        QString fileDataBase64 = json["filedata"].toString();

                        QString account = "Unk", dept = "Unk", title = "Unk";
                        QSqlQuery q(threadDb); q.prepare("SELECT account, department, job_title FROM users WHERE name = :n"); q.bindValue(":n", name);
                        if (q.exec() && q.next()) { account = q.value(0).toString(); dept = q.value(1).toString(); title = q.value(2).toString(); }

                        QString baseDir = QCoreApplication::applicationDirPath() + "/server/AiChat/";
                        QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
                        QDir dir; dir.mkpath(baseDir + folderName);

                        QByteArray fileData = QByteArray::fromBase64(fileDataBase64.toUtf8());
                        QString filePath = baseDir + folderName + "/" + fileName;
                        QFile file(filePath);
                        if (file.open(QIODevice::WriteOnly)) {
                            file.write(fileData);
                            file.close();
                        }

                        QMetaObject::invokeMethod(this, [this, name, fileName]() {
                            logMessage(QString("<font color='#00B42A'>AI 附件审计: 拦截并备份了 [%1] 上传的 AI 附件 '%2'。</font>").arg(name, fileName));
                            }, Qt::QueuedConnection);
                    }
                    else if (type == "chat" || type == "image" || type == "file") {
                        QString fromUser = json["from"].toString(); QString toUser = json["to"].toString(); QString content = json["msg"].toString();

                        if (type == "chat") {
                            QMetaObject::invokeMethod(this, [this, fromUser, toUser, content]() {
                                logMessage(QString("<font color='#E6A23C'>单聊审计: [%1] -> [%2]: %3</font>").arg(fromUser, toUser, content));
                                }, Qt::QueuedConnection);
                        }

                        if (type == "file" || type == "image") {
                            QString fileName = json["filename"].toString();
                            if (fileName.isEmpty()) fileName = "image_audit_" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + ".png";

                            QString folderName = "Unknown";
                            QSqlQuery query(threadDb); query.prepare("SELECT id, name, department, job_title FROM users WHERE name = :n"); query.bindValue(":n", fromUser);
                            if (query.exec() && query.next()) {
                                QString id = query.value(0).toString(); QString n = query.value(1).toString();
                                QString d = query.value(2).toString(); QString j = query.value(3).toString();
                                if (d.isEmpty()) d = "未分配部门"; if (j.isEmpty() || j == "未分配") j = "员工";
                                folderName = QString("%1_%2_%3_%4").arg(id, n, d, j);
                            }

                            QByteArray fileData = QByteArray::fromBase64(content.toUtf8());
                            QString serverDirPath = QCoreApplication::applicationDirPath() + "/ChatFiles/server/" + folderName;
                            QDir().mkpath(serverDirPath);
                            QFile file(serverDirPath + "/" + fileName);
                            if (file.open(QIODevice::WriteOnly)) {
                                file.write(fileData); file.close();
                                QMetaObject::invokeMethod(this, [this, fromUser, fileName]() {
                                    logMessage(QString("<font color='#67C23A'>文件审计: 拦截到单聊 [%1] 发送的文件 '%2'。</font>").arg(fromUser, fileName));
                                    }, Qt::QueuedConnection);
                            }
                        }

                        bool isOnline = false;
                        QMetaObject::invokeMethod(this, [this, toUser, &isOnline]() { isOnline = m_nameToSocket.contains(toUser); }, Qt::BlockingQueuedConnection);

                        if (isOnline) {
                            QTcpSocket* targetSocket = nullptr;
                            QMetaObject::invokeMethod(this, [this, toUser, &targetSocket]() { targetSocket = m_nameToSocket[toUser]; }, Qt::BlockingQueuedConnection);
                            if (targetSocket) QMetaObject::invokeMethod(targetSocket, [targetSocket, data]() { targetSocket->write(data); }, Qt::QueuedConnection);
                        }
                        else {
                            QSqlQuery insertQ(threadDb);
                            insertQ.prepare("INSERT INTO offline_messages (sender, receiver, department, msg_type, content, filename, send_time) VALUES (:s, :r, '', :t, :c, :f, NOW())");
                            insertQ.bindValue(":s", fromUser); insertQ.bindValue(":r", toUser);
                            insertQ.bindValue(":t", type); insertQ.bindValue(":c", content);
                            insertQ.bindValue(":f", json["filename"].toString()); insertQ.exec();
                            QMetaObject::invokeMethod(this, [this, toUser]() { logMessage(QString("不在线，文件/消息转入离线信箱。").arg(toUser)); }, Qt::QueuedConnection);
                        }
                    }
                    else if (type.startsWith("group_")) {
                        QString fromUser = json["from"].toString(); QString dept = json["department"].toString(); QString content = json["msg"].toString();

                        QMetaObject::invokeMethod(this, [this, fromUser, dept]() {
                            logMessage(QString("<font color='#9C27B0'>群发路由: [%1] 向 [%2] 发送了群消息。</font>").arg(fromUser, dept));
                            }, Qt::QueuedConnection);

                        if (type == "group_file" || type == "group_image") {
                            QString fileName = json["filename"].toString();
                            if (fileName.isEmpty()) fileName = "group_audit_" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + ".png";

                            QString folderName = "Unknown";
                            QSqlQuery query(threadDb); query.prepare("SELECT id, name, department, job_title FROM users WHERE name = :n"); query.bindValue(":n", fromUser);
                            if (query.exec() && query.next()) {
                                QString id = query.value(0).toString(); QString n = query.value(1).toString();
                                QString d = query.value(2).toString(); QString j = query.value(3).toString();
                                if (d.isEmpty()) d = "未分配部门"; if (j.isEmpty() || j == "未分配") j = "员工";
                                folderName = QString("%1_%2_%3_%4").arg(id, n, d, j);
                            }

                            QByteArray fileData = QByteArray::fromBase64(content.toUtf8());
                            QString serverDirPath = QCoreApplication::applicationDirPath() + "/ChatFiles/server/" + folderName;
                            QDir().mkpath(serverDirPath);
                            QFile file(serverDirPath + "/" + fileName);
                            if (file.open(QIODevice::WriteOnly)) {
                                file.write(fileData); file.close();
                                QMetaObject::invokeMethod(this, [this, fromUser, dept, fileName]() {
                                    logMessage(QString("<font color='#67C23A'>群文件审计: 拦截到 [%1] 发送到群 [%2] 的文件 '%3'。</font>").arg(fromUser, dept, fileName));
                                    }, Qt::QueuedConnection);
                            }
                        }

                        QSqlQuery groupQ(threadDb);
                        if (dept == "公司总群") groupQ.exec("SELECT name FROM users WHERE role != '超级管理员' AND account != 'admin'");
                        else { groupQ.prepare("SELECT name FROM users WHERE department = :d AND role != '超级管理员'"); groupQ.bindValue(":d", dept); groupQ.exec(); }

                        while (groupQ.next()) {
                            QString member = groupQ.value(0).toString().trimmed();
                            if (member == fromUser) continue;

                            bool isOnline = false;
                            QMetaObject::invokeMethod(this, [this, member, &isOnline]() { isOnline = m_nameToSocket.contains(member); }, Qt::BlockingQueuedConnection);

                            if (isOnline) {
                                QTcpSocket* targetSocket = nullptr;
                                QMetaObject::invokeMethod(this, [this, member, &targetSocket]() { targetSocket = m_nameToSocket[member]; }, Qt::BlockingQueuedConnection);
                                if (targetSocket) QMetaObject::invokeMethod(targetSocket, [targetSocket, data]() { targetSocket->write(data); }, Qt::QueuedConnection);
                            }
                            else {
                                QSqlQuery insertQ(threadDb);
                                insertQ.prepare("INSERT INTO offline_messages (sender, receiver, department, msg_type, content, filename, send_time) VALUES (:s, :r, :d, :t, :c, :f, NOW())");
                                insertQ.bindValue(":s", fromUser); insertQ.bindValue(":r", member); insertQ.bindValue(":d", dept);
                                insertQ.bindValue(":t", type); insertQ.bindValue(":c", content); insertQ.bindValue(":f", json["filename"].toString()); insertQ.exec();
                            }
                        }
                    }
                }
            }
            QSqlDatabase::removeDatabase(threadConnName);
            });
    }
}

void AttendanceServer::updateOnlineUsersTable() {
    ui->tableWidget_OnlineUsers->setRowCount(0);
    for (const ClientInfo& info : m_clients.values()) {
        int row = ui->tableWidget_OnlineUsers->rowCount();
        ui->tableWidget_OnlineUsers->insertRow(row);

        QTableWidgetItem* item0 = new QTableWidgetItem(info.name); item0->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem* item1 = new QTableWidgetItem(info.dept); item1->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem* item2 = new QTableWidgetItem(info.jobTitle); item2->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem* item3 = new QTableWidgetItem(info.ip); item3->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem* item4 = new QTableWidgetItem(info.loginTime); item4->setTextAlignment(Qt::AlignCenter);

        ui->tableWidget_OnlineUsers->setItem(row, 0, item0);
        ui->tableWidget_OnlineUsers->setItem(row, 1, item1);
        ui->tableWidget_OnlineUsers->setItem(row, 2, item2);
        ui->tableWidget_OnlineUsers->setItem(row, 3, item3);
        ui->tableWidget_OnlineUsers->setItem(row, 4, item4);
    }
}

void AttendanceServer::initDatabase() {
    if (!QSqlDatabase::contains("server_db_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", "server_db_connection");
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3305;DATABASE=attendance_db;UID=root;PWD=root;");
        db.setDatabaseName(dsn);
        if (!db.open()) return;
    }
    QSqlDatabase db = QSqlDatabase::database("server_db_connection"); QSqlQuery query(db);
    query.exec("SET NAMES utf8mb4"); query.exec("SET GLOBAL max_allowed_packet = 67108864");

    query.exec("CREATE TABLE IF NOT EXISTS offline_messages (id INT AUTO_INCREMENT PRIMARY KEY, sender VARCHAR(50), receiver VARCHAR(50), department VARCHAR(50), msg_type VARCHAR(20), content LONGTEXT, filename VARCHAR(255), send_time DATETIME)");
    query.exec("ALTER TABLE offline_messages ADD COLUMN department VARCHAR(50) DEFAULT ''");

    query.exec("CREATE TABLE IF NOT EXISTS leave_requests ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "applicant VARCHAR(50), "
        "leave_type VARCHAR(50), "
        "start_time DATETIME, "
        "end_time DATETIME, "
        "duration VARCHAR(50), "
        "reason TEXT, "
        "approver VARCHAR(255), "
        "cc VARCHAR(255), "
        "status VARCHAR(50))");

    query.exec("CREATE TABLE IF NOT EXISTS appeals ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "applicant VARCHAR(50), "
        "abnormal_time DATETIME, "
        "original_status VARCHAR(50), "
        "reason TEXT, "
        "approver VARCHAR(255), "
        "status VARCHAR(50))");

    query.exec("CREATE TABLE IF NOT EXISTS shift_rules ("
        "dept VARCHAR(50) PRIMARY KEY, "
        "rule_name VARCHAR(50), "
        "start_time TIME, "
        "end_time TIME, "
        "late_mins INT, "
        "absent_mins INT)");
    query.exec("INSERT IGNORE INTO shift_rules (dept, rule_name, start_time, end_time, late_mins, absent_mins) VALUES ('全部', '常规班', '09:00:00', '18:00:00', 30, 120)");
}

void AttendanceServer::loadGlobalRecords() {
    QString sql = "SELECT ROW_NUMBER() OVER (ORDER BY r.punch_time DESC) AS '序号', r.name AS '姓名', u.department AS '部门', u.job_title AS '职务', DATE_FORMAT(r.punch_time, '%Y-%m-%d %H:%i:%s') AS '打卡时间', r.status AS '状态' FROM attendance_records r LEFT JOIN users u ON r.name = u.name ORDER BY r.punch_time DESC";
    QSqlDatabase db = QSqlDatabase::database("server_db_connection"); m_globalRecordModel->setQuery(sql, db);
}

void AttendanceServer::on_btn_RefreshData_clicked() { loadGlobalRecords(); }

void AttendanceServer::on_btn_ExportGlobal_clicked() {
    QString fileName = QFileDialog::getSaveFileName(this, "导出全公司考勤报表", "全公司考勤大表_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm") + ".csv", "CSV (*.csv)");
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file); out.setEncoding(QStringConverter::Utf8); out << QString("\xEF\xBB\xBF") << "序号,姓名,部门,职务,打卡时间,状态\n";
        int rowCount = m_globalRecordModel->rowCount(); int colCount = m_globalRecordModel->columnCount();
        for (int r = 0; r < rowCount; ++r) {
            QStringList rowData; for (int c = 0; c < colCount; ++c) rowData << m_globalRecordModel->data(m_globalRecordModel->index(r, c)).toString();
            out << rowData.join(",") << "\n";
        }
        file.close(); QMessageBox::information(this, "成功", QString("成功导出 %1 条全局考勤记录！").arg(rowCount));
    }
}