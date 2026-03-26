#include "RequestHandler.h"
#include "AttendanceServer.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
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
    if (raw.startsWith("B64:"))
        return QString::fromUtf8(QByteArray::fromBase64(raw.mid(4).toUtf8()));
    return raw;
}
// 防止特殊字符导致数据库写入截断
static QString encodeContent(const QString& content)
{
    return "B64:" + QString(content.toUtf8().toBase64());
}
// 处理并转发客户端发来的聊天消息（单聊/群聊）
void RequestHandler::handleChatMessage(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, const QByteArray& rawData,
    AttendanceServer* server)
{
    // 提取消息元数据
    QString type = json["type"].toString();
    QString fromUser = json["from"].toString();
    QString content = json["msg"].toString();
    QString fileName = json["filename"].toString();
    bool    isGroup = type.startsWith("group_");
    QString target = isGroup ? json["department"].toString() : json["to"].toString();

    QString dbContent = content;
    if (type == "chat" || type == "group_chat") {
        dbContent = encodeContent(content);
    }
    QSqlQuery histQ(db);
    histQ.prepare(
        "INSERT INTO chat_history (sender, receiver, msg_type, content, filename, send_time, is_group, is_read) "
        "VALUES (?, ?, ?, ?, ?, NOW(), ?, 0)"
    );
    histQ.addBindValue(fromUser);
    histQ.addBindValue(target);
    histQ.addBindValue(type);
    histQ.addBindValue(dbContent);
    histQ.addBindValue(fileName);
    histQ.addBindValue(isGroup ? 1 : 0);
    histQ.exec();
    // ── 私聊路由 ──────────────────────────────────────────────
    if (!isGroup) {
        bool isOnline = server->isClientOnline(target);
        if (isOnline) {
            // 🚩 核心修复：彻底抛弃 BlockingQueuedConnection 防止死锁！直接获取 Socket！
            QTcpSocket* targetSocket = server->getSocketByName(target);
            if (targetSocket) {
                QMetaObject::invokeMethod(targetSocket, [targetSocket, rawData]() {
                    targetSocket->write(rawData);
                    }, Qt::QueuedConnection);

                QMetaObject::invokeMethod(server, [server, fromUser, target, type]() {
                    server->logMessage(QString("<font color='#E6A23C'>消息转发: [%1] -> [%2] (%3)</font>")
                        .arg(fromUser, target, type));
                    }, Qt::QueuedConnection);
            }
        }
        else {
            // 目标不在线，拦截存入离线信箱
            QSqlQuery offQ(db);
            offQ.prepare(
                "INSERT INTO offline_messages "
                "(sender, receiver, department, msg_type, content, filename, send_time) "
                "VALUES (:s, :r, '', :t, :c, :f, NOW())"
            );
            offQ.bindValue(":s", fromUser);
            offQ.bindValue(":r", target);
            offQ.bindValue(":t", type);
            offQ.bindValue(":c", dbContent);
            offQ.bindValue(":f", fileName);
            if (offQ.exec()) {
                QMetaObject::invokeMethod(server, [server, target, type]() {
                    server->logMessage(QString("拦截成功: 目标 [%1] 不在线，%2 已转入离线信箱。").arg(target, type));
                    }, Qt::QueuedConnection);
            }
        }
        return;
    }
    // ── 群发路由 ──────────────────────────────────────────────
    QMetaObject::invokeMethod(server, [server, fromUser, target]() {
        server->logMessage(QString("<font color='#9C27B0'>群发路由: [%1] 向 [%2] 发送了群消息。</font>").arg(fromUser, target));
        }, Qt::QueuedConnection);

    QSqlQuery groupQ(db);
    if (target == "公司总群") {
        groupQ.exec("SELECT name FROM users WHERE role != '超级管理员' AND account != 'admin'");
    }
    else {
        groupQ.prepare("SELECT name FROM users WHERE department = :d AND role != '超级管理员'");
        groupQ.bindValue(":d", target);
        groupQ.exec();
    }

    while (groupQ.next()) {
        QString member = groupQ.value(0).toString().trimmed();
        if (member == fromUser) continue;

        bool isOnline = server->isClientOnline(member);
        if (isOnline) {
            QTcpSocket* targetSocket = server->getSocketByName(member);
            if (targetSocket) {
                QMetaObject::invokeMethod(targetSocket, [targetSocket, rawData]() {
                    targetSocket->write(rawData);
                    }, Qt::QueuedConnection);
            }
        }
        else {
            QSqlQuery insertQ(db);
            insertQ.prepare(
                "INSERT INTO offline_messages "
                "(sender, receiver, department, msg_type, content, filename, send_time) "
                "VALUES (:s, :r, :d, :t, :c, :f, NOW())"
            );
            insertQ.bindValue(":s", fromUser); insertQ.bindValue(":r", member); insertQ.bindValue(":d", target);
            insertQ.bindValue(":t", type); insertQ.bindValue(":c", dbContent); insertQ.bindValue(":f", fileName);
            insertQ.exec();
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
    QJsonObject res;
    res["status"] = "success";
    // 1. 查询请求者自身的身份信息
    QString myEmpId, myDept, myJob;
    QSqlQuery myQ(db);
    myQ.prepare("SELECT id, department, job_title FROM users WHERE name = :n");
    myQ.bindValue(":n", name);
    if (myQ.exec() && myQ.next()) {
        myEmpId = myQ.value(0).toString();
        myDept = myQ.value(1).toString().isEmpty() ? "未分配部门" : myQ.value(1).toString();
        myJob = (myQ.value(2).toString().isEmpty() || myQ.value(2).toString() == "未分配") ? "员工" : myQ.value(2).toString();
    }
    // 组装返回给客户端，用于生成本地缓存或文件管理的专属目录名
    res["my_dept"] = myDept;
    res["my_folder"] = QString("%1_%2_%3_%4").arg(myEmpId, name, myDept, myJob);
    // 2. 根据身份查询部门列表（总经办能看到所有部门，普通人只能看到自己部门）
    QJsonArray deptArr;
    QString deptSql = (myDept == "总经办")
        ? "SELECT DISTINCT department FROM users WHERE department != '' AND department IS NOT NULL"
        : QString("SELECT DISTINCT department FROM users WHERE department = '%1'").arg(myDept);
    QSqlQuery dQ(db);
    dQ.exec(deptSql);
    while (dQ.next()) deptArr.append(dQ.value(0).toString());
    res["departments"] = deptArr;
    // 3. 查询除自己和管理员之外的所有用户列表
    QJsonArray userArr;
    QSqlQuery uQ(db);
    uQ.exec(QString("SELECT id, name, department, role FROM users "
        "WHERE name != '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(name));
    while (uQ.next()) {
        QJsonObject u;
        u["id"] = uQ.value(0).toInt();
        u["name"] = uQ.value(1).toString().trimmed();
        u["department"] = uQ.value(2).toString().trimmed();
        u["role"] = uQ.value(3).toString().trimmed();
        userArr.append(u);
    }
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
            QMetaObject::invokeMethod(targetSocket, [targetSocket, outData]() {
                targetSocket->write(outData);
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
            // 在线则立刻转发
            QTcpSocket* targetSocket = nullptr;
            QMetaObject::invokeMethod(server, [server, member, &targetSocket]() {
                targetSocket = server->getSocketByName(member);
                }, Qt::BlockingQueuedConnection);
            if (targetSocket) {
                QMetaObject::invokeMethod(targetSocket, [targetSocket, rawData]() {
                    targetSocket->write(rawData);
                    }, Qt::QueuedConnection);
                pushCount++;
            }
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