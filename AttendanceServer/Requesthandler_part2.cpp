#include "RequestHandler.h"
#include "AttendanceServer.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>
#include <QUuid>
#include <QThread>
// 将 JSON 转为紧凑格式，加上换行符后跨线程发送，处理 TCP 粘包
static void sendJson(QTcpSocket* socket, const QJsonObject& obj)
{
    if (!socket) return;
    if (!socket->isValid()) return;
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    try {
        QByteArray outData = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
        socket->write(outData);
        socket->flush();
    }
    catch (...) {
    }
}
// 解码 Base64 文本
static QString decodeContent(const QString& raw)
{
    return raw;
}
// 防止特殊字符导致数据库写入截断
static QString encodeContent(const QString& content, const QString& type)
{
    if (type == "chat" || type == "group_chat") {
        return "B64:" + QString(content.toUtf8().toBase64());
    }
    return content;
}
// 处理并转发客户端发来的聊天消息（单聊/群聊）
void RequestHandler::handleChatMessage(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, const QByteArray& /*rawData*/, AttendanceServer* server)
{
    QString type = json["type"].toString();
    QString fromUser = json["from"].toString();
    QString content = json["msg"].toString();
    QString filename = json["filename"].toString();
    QString msgId = json["msg_id"].toString();
    if (msgId.isEmpty()) msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // ---------------------------------------------------------
    // ⭐️ 安全升级：全局拦截聊天数据（文本+附件），写入专属审计目录
    // ---------------------------------------------------------
    // 1. 获取员工信息，构建分类目录（重命名SQL变量为 qSender 防止冲突）
    QString account = "Unk", senderDept = "Unk", title = "Unk";
    QSqlQuery qSender(db);
    qSender.prepare("SELECT account, department, job_title FROM users WHERE name = :n");
    qSender.bindValue(":n", fromUser);
    if (qSender.exec() && qSender.next()) {
        account = qSender.value(0).toString();
        senderDept = qSender.value(1).toString();
        title = qSender.value(2).toString();
    }
    qSender.finish();

    // 2. 使用相对路径跨级定位到 AttendanceServer/server/ChatFiles 目录
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/ChatFiles";
    QString baseDir = QDir::cleanPath(rawPath) + "/";
    QString folderName = QString("%1_%2_%3_%4").arg(account, fromUser, senderDept, title);

    QDir dir;
    if (!dir.exists(baseDir + folderName)) {
        dir.mkpath(baseDir + folderName);
    }

    // 3. 审计分流保存：附件存实体文件，文本追加到 txt 记录本中
    if (type == "image" || type == "group_image" || type == "file" || type == "group_file") {
        if (filename.isEmpty()) {
            filename = type.contains("image") ? "pasted_image.png" : "unknown_file.dat";
        }
        QString saveFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + filename;
        QString filePath = baseDir + folderName + "/" + saveFileName;

        QByteArray fileData = QByteArray::fromBase64(content.toUtf8());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(fileData);
            file.close();
            if (server) {
                QMetaObject::invokeMethod(server, [server, fromUser, saveFileName]() {
                    server->logMessage(QString("<font color='#E6A23C'>[文件审计] 截获聊天附件: %1</font>").arg(saveFileName));
                    }, Qt::QueuedConnection);
            }
        }
    }
    else if (type == "chat" || type == "group_chat") {
        QString logFilePath = baseDir + folderName + "/chat_records.txt";
        QFile logFile(logFilePath);
        // 使用 Append 模式，不断在文件末尾追加新行
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&logFile);
            // 兼容防乱码处理
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out.setCodec("UTF-8");
#endif

            QString target = (type == "group_chat") ? json["department"].toString() : json["to"].toString();
            QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

            out << "[" << timeStr << "] 发送给 [" << target << "]: " << content << "\n";
            logFile.close();
        }
    }
    // ---------------------------------------------------------

    // ====== 以下保持原有的数据库写入和消息转发逻辑完全不变 ======
    if (type == "chat" || type == "image" || type == "file") {
        QString toUser = json["to"].toString();
        QSqlQuery insertQ(db);
        insertQ.prepare(
            "INSERT INTO chat_history "
            "(sender, receiver, msg_type, content, filename, send_time, is_group) " 
            "VALUES (:s, :r, :mt, :c, :f, NOW(), 0)" 
        );
        insertQ.bindValue(":s", fromUser);
        insertQ.bindValue(":r", toUser);
        insertQ.bindValue(":mt", type);
        insertQ.bindValue(":c", encodeContent(content, type));
        insertQ.bindValue(":f", filename);
        if (!insertQ.exec()) {
            if (server) {
                QMetaObject::invokeMethod(server, [server, err = insertQ.lastError().text()]() {
                    server->logMessage(QString("<font color='red'>[数据库异常] 单聊记录入库失败: %1</font>").arg(err));
                    }, Qt::QueuedConnection);
            }
        }

        QTcpSocket* targetSocket = server->getSocketByName(toUser);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            // ⭐️ 核心防崩溃修复：拷贝一份 JSON 再修改，绝对不要修改只读的引用
            QJsonObject forwardJson = json;
            forwardJson["is_offline"] = false;
            sendJson(targetSocket, forwardJson);
        }
        else {
            QSqlQuery offQ(db);
            offQ.prepare(
                "INSERT INTO offline_messages "
                "(sender, receiver, department, msg_type, content, filename, send_time) "
                "VALUES (:s, :r, '', :mt, :c, :f, NOW())"
            );
            offQ.bindValue(":s", fromUser);
            offQ.bindValue(":r", toUser);
            offQ.bindValue(":mt", type);
            offQ.bindValue(":c", encodeContent(content, type));
            offQ.bindValue(":f", filename);
            offQ.exec();
        }
    }
    else if (type.startsWith("group_")) {
        QString targetDept = json["department"].toString();

        QSqlQuery insertQ(db);
        insertQ.prepare(
            "INSERT INTO chat_history "
            "(sender, receiver, msg_type, content, filename, send_time, is_group) " 
            "VALUES (:s, :r, :mt, :c, :f, NOW(), 1)" 
        );
        insertQ.bindValue(":s", fromUser);
        insertQ.bindValue(":r", targetDept);
        insertQ.bindValue(":mt", type);
        insertQ.bindValue(":c", encodeContent(content, type));
        insertQ.bindValue(":f", filename);
        if (!insertQ.exec()) {
            if (server) {
                QMetaObject::invokeMethod(server, [server, err = insertQ.lastError().text()]() {
                    server->logMessage(QString("<font color='red'>[数据库异常] 群聊记录入库失败: %1</font>").arg(err));
                    }, Qt::QueuedConnection);
            }
        }

        // 检索群成员并分发（重命名SQL变量为 qMembers 防止冲突）
        QSqlQuery qMembers(db);
        if (targetDept == "公司总群") {
            qMembers.prepare("SELECT name FROM users WHERE name != :n AND status != '离线'");
        }
        else {
            qMembers.prepare("SELECT name FROM users WHERE department = :d AND name != :n");
            qMembers.bindValue(":d", targetDept);
        }
        qMembers.bindValue(":n", fromUser);
        qMembers.exec();

        QByteArray forwardData = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
        int pushCount = 0;
        while (qMembers.next()) {
            QString member = qMembers.value(0).toString();
            QTcpSocket* targetSocket = server->getSocketByName(member);
            if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                QMetaObject::invokeMethod(server, [targetSocket, forwardData]() {
                    if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                        targetSocket->write(forwardData);
                        targetSocket->flush();
                    }
                    }, Qt::QueuedConnection);
                pushCount++;
            }
            else {
                QSqlQuery offQ(db);
                offQ.prepare(
                    "INSERT INTO offline_messages "
                    "(sender, receiver, department, msg_type, content, filename, send_time) "
                    "VALUES (:s, :r, :d, :mt, :c, :f, NOW())"
                );
                offQ.bindValue(":s", fromUser);
                offQ.bindValue(":r", member);
                offQ.bindValue(":d", targetDept);
                offQ.bindValue(":mt", type);
                offQ.bindValue(":c", encodeContent(content, type));
                offQ.bindValue(":f", filename);
                offQ.exec();
            }
        }
        if (server) {
            QMetaObject::invokeMethod(server, [server, pushCount]() {
                server->logMessage(QString("   └─ 成功即时推送到 %1 名在线员工。").arg(pushCount));
                }, Qt::QueuedConnection);
        }
    }
}
// 查询聊天历史记录（单聊或群聊），限制返回最近 100 条
void RequestHandler::handleQueryChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString me = json["me"].toString();
    QString target = json["target"].toString();
    bool    isGroup = json["is_group"].toBool();
    QString sql = isGroup
        ? QString("SELECT sender, msg_type, content, filename, DATE_FORMAT(send_time, '%m-%d %H:%i'), is_read "
            "FROM chat_history WHERE receiver = '%1' AND is_group = 1 ORDER BY send_time ASC LIMIT 100").arg(target)
        : QString("SELECT sender, msg_type, content, filename, DATE_FORMAT(send_time, '%m-%d %H:%i'), is_read "
            "FROM chat_history WHERE ((sender='%1' AND receiver='%2') OR (sender='%2' AND receiver='%1')) "
            "AND is_group = 0 ORDER BY send_time ASC LIMIT 100").arg(me, target);
    QJsonArray arr;
    QSqlQuery q(db);
    if (q.exec(sql)) {
        while (q.next()) {
            QJsonObject o;
            o["sender"] = q.value(0).toString();
            o["msg_type"] = q.value(1).toString();
            o["content"] = decodeContent(q.value(2).toString());
            o["filename"] = q.value(3).toString();
            o["time"] = q.value(4).toString();
            o["is_read"] = q.value(5).toInt();
            arr.append(o);
        }
    }
    QJsonObject res; res["status"] = "success"; res["data"] = arr; sendJson(socket, res);
}
// 查询聊天联系人列表及组织架构
void RequestHandler::handleQueryChatContacts(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QString safeName = name;
    safeName.replace("'", "''");

    QJsonObject res;
    res["status"] = "success";

    // 1. 查询请求者自身的身份信息（规避 ODBC bindValue 中文 Bug）
    QString myEmpId, myDept, myJob;
    QSqlQuery myQ(db);
    myQ.exec(QString("SELECT id, department, job_title FROM users WHERE name = '%1'").arg(safeName));
    if (myQ.next()) {
        myEmpId = myQ.value(0).toString();
        myDept = myQ.value(1).toString().isEmpty() ? "未分配部门" : myQ.value(1).toString();
        myJob = (myQ.value(2).toString().isEmpty() || myQ.value(2).toString() == "未分配") ? "员工" : myQ.value(2).toString();
    }
    myQ.finish();

    res["my_dept"] = myDept;
    res["my_folder"] = QString("%1_%2_%3_%4").arg(myEmpId, name, myDept, myJob);

    // 2. 根据身份查询部门列表
    QJsonArray deptArr;
    QString deptSql = (myDept == "总经办")
        ? "SELECT DISTINCT department FROM users WHERE department != '' AND department IS NOT NULL"
        : QString("SELECT DISTINCT department FROM users WHERE department = '%1'").arg(myDept);
    QSqlQuery dQ(db);
    dQ.exec(deptSql);
    while (dQ.next()) deptArr.append(dQ.value(0).toString());
    dQ.finish();
    res["departments"] = deptArr;

    // 3. 查询除自己和管理员之外的所有用户列表（含 job_title）
    QJsonArray userArr;
    QSqlQuery uQ(db);
    uQ.exec(QString("SELECT id, name, department, role, job_title FROM users "
        "WHERE name != '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(safeName));
    while (uQ.next()) {
        QJsonObject u;
        u["id"] = uQ.value(0).toInt();
        u["name"] = uQ.value(1).toString().trimmed();
        u["department"] = uQ.value(2).toString().trimmed();
        u["role"] = uQ.value(3).toString().trimmed();
        u["job_title"] = uQ.value(4).toString().trimmed();
        userArr.append(u);
    }
    uQ.finish();
    res["users"] = userArr;

    sendJson(socket, res);
}
// 查询指定群组/部门内的所有成员
void RequestHandler::handleQueryGroupMembers(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString dept = json["department"].toString();
    // 区分总群与部门群的查询条件，统一屏蔽管理员账号
    QString sql = (dept == "公司总群")
        ? "SELECT name, department, job_title FROM users WHERE account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'"
        : QString("SELECT name, department, job_title FROM users WHERE department = '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(dept);
    QJsonArray arr;
    QSqlQuery q(db);
    q.exec(sql);
    while (q.next()) {
        QJsonObject u;
        u["name"] = q.value(0).toString();
        u["dept"] = q.value(1).toString();
        QString j = q.value(2).toString();
        // 补充空值默认设定
        u["job"] = (j.isEmpty() || j == "未分配") ? "员工" : j;
        arr.append(u);
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}
// 处理客户端发回的已读回执
void RequestHandler::handleReadReceipt(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QString reader = json["from"].toString(); // 当前正在看聊天界面的用户
    QString senderUser = json["to"].toString(); // 消息的发送方
    QSqlQuery q(db);
    q.prepare("UPDATE chat_history SET is_read = 1 WHERE sender = :s AND receiver = :r AND is_read = 0");
    q.bindValue(":s", senderUser);
    q.bindValue(":r", reader);
    q.exec();
    bool isOnline = server->isClientOnline(senderUser);
    if (isOnline) {
        QTcpSocket* targetSocket = server->getSocketByName(senderUser);
        if (targetSocket) {
            QByteArray outData = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
            QPointer<QTcpSocket> safeSocket(targetSocket);
            QMetaObject::invokeMethod(targetSocket, [safeSocket, outData]() {
                if (safeSocket && safeSocket->state() == QAbstractSocket::ConnectedState) {
                    safeSocket->write(outData);
                    safeSocket->flush();
                }
                }, Qt::QueuedConnection);
        }
    }
}
// 处理系统强制全员广播消息
void RequestHandler::handleBroadcast(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, const QByteArray& rawData,
    AttendanceServer* server)
{
    QString fromUser = json["from"].toString();
    QString content = json["msg"].toString();
    // 权限校验
    QString senderRole = "普通登录";
    QSqlQuery roleQ(db);
    roleQ.prepare("SELECT role FROM users WHERE name = :n OR account = :n");
    roleQ.bindValue(":n", fromUser);
    if (roleQ.exec() && roleQ.next()) {
        senderRole = roleQ.value(0).toString();
    }
    // 只有管理层允许发送系统广播，其他角色拦截并告警
    if (senderRole != "超级管理员" && senderRole != "管理员登录" && senderRole != "经理") {
        QMetaObject::invokeMethod(server, [server, fromUser, senderRole]() {
            server->logMessage(QString(
                "<font color='red'>越权拦截: 员工 [%1](%2) 尝试发送系统广播，已被零信任网关阻断！</font>")
                .arg(fromUser, senderRole));
            }, Qt::QueuedConnection);
        return;
    }
    // 记录日志
    QMetaObject::invokeMethod(server, [server, fromUser]() {
        server->logMessage(QString("<font color='#F56C6C'>授权通过: 管理层 [%1] 已下发全员广播。</font>").arg(fromUser));
        }, Qt::QueuedConnection);
    // 查询所有接收者（排除管理员）
    QSqlQuery allUsersQ(db);
    allUsersQ.exec("SELECT name FROM users WHERE role != '超级管理员' AND account != 'admin'");
    int pushCount = 0;
    while (allUsersQ.next()) {
        QString member = allUsersQ.value(0).toString().trimmed();
        if (member == fromUser) continue;
        bool isOnline = server->isClientOnline(member);
        if (isOnline) {
            QByteArray forwardData = rawData + "\n";
            QMetaObject::invokeMethod(server, [server, member, forwardData]() {
                QTcpSocket* targetSocket = server->getSocketByName(member);
                if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                    targetSocket->write(forwardData);
                    targetSocket->flush();
                }
                }, Qt::QueuedConnection);
            pushCount++; 
        }
        else {
            // 离线则存入离线信箱
            QSqlQuery insertQ(db);
            insertQ.prepare(
                "INSERT INTO offline_messages "
                "(sender, receiver, department, msg_type, content, filename, send_time) "
                "VALUES (:s, :r, '', 'broadcast', :c, '', NOW())"
            );
            insertQ.bindValue(":s", fromUser);
            insertQ.bindValue(":r", member);
            insertQ.bindValue(":c", "B64:" + QString(content.toUtf8().toBase64()));
            insertQ.exec();
        }
    }
    // 统计并打印送达人数
    QMetaObject::invokeMethod(server, [server, pushCount]() {
        server->logMessage(QString("   └─ 成功即时推送到 %1 名在线员工。").arg(pushCount));
        }, Qt::QueuedConnection);
}
// 在服务端的系统公告板上发布持久化公告
void RequestHandler::handlePublishAnnouncement(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString publisher = json["publisher"].toString();
    QString content = json["content"].toString();
    QSqlQuery q(db);
    q.prepare("INSERT INTO system_announcements (publisher, content, publish_time) VALUES (?, ?, NOW())");
    q.addBindValue(publisher);
    q.addBindValue(content);
    q.exec();
}