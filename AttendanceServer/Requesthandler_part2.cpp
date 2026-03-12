#include "RequestHandler.h"
#include "AttendanceServer.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

// ── 内部工具（与 Part1 保持一致，各 .cpp 独立编译） ─────────────
static void sendJson(QTcpSocket* socket, const QJsonObject& obj)
{
    QByteArray outData = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    QMetaObject::invokeMethod(socket,
        [socket, outData]() { socket->write(outData); },
        Qt::QueuedConnection);
}
static QString decodeContent(const QString& raw)
{
    if (raw.startsWith("B64:"))
        return QString::fromUtf8(QByteArray::fromBase64(raw.mid(4).toUtf8()));
    return raw;
}
static QString encodeContent(const QString& content)
{
    return "B64:" + QString(content.toUtf8().toBase64());
}


// ============================================================
// ── 聊天 & 消息路由 ───────────────────────────────────────────
// ============================================================

void RequestHandler::handleChatMessage(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, const QByteArray& rawData,
    AttendanceServer* server)
{
    QString type = json["type"].toString();
    QString fromUser = json["from"].toString();
    QString content = json["msg"].toString();
    QString fileName = json["filename"].toString();
    bool    isGroup = type.startsWith("group_");
    QString target = isGroup ? json["department"].toString() : json["to"].toString();

    // Base64 保护聊天文本防止 ODBC 吞表情包
    QString dbContent = content;
    if (type == "chat" || type == "group_chat") {
        dbContent = encodeContent(content);
    }

    // 落库到历史记录
    QSqlQuery histQ(db);
    histQ.prepare(
        "INSERT INTO chat_history (sender, receiver, msg_type, content, filename, send_time, is_group) "
        "VALUES (?, ?, ?, ?, ?, NOW(), ?)"
    );
    histQ.addBindValue(fromUser);
    histQ.addBindValue(target);
    histQ.addBindValue(type);
    histQ.addBindValue(dbContent);
    histQ.addBindValue(fileName);
    histQ.addBindValue(isGroup ? 1 : 0);
    if (!histQ.exec()) {
        qDebug() << "❌ 历史记录落库失败:" << histQ.lastError().text();
    }

    // ── 私聊路由 ──────────────────────────────────────────────
    if (!isGroup) {
        bool isOnline = false;
        QMetaObject::invokeMethod(server, [server, target, &isOnline]() {
            isOnline = server->isClientOnline(target);
            }, Qt::BlockingQueuedConnection);

        if (isOnline) {
            QTcpSocket* targetSocket = nullptr;
            QMetaObject::invokeMethod(server, [server, target, &targetSocket]() {
                targetSocket = server->getSocketByName(target);
                }, Qt::BlockingQueuedConnection);

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
            // 离线拦截
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
                    server->logMessage(QString("拦截成功: 目标 [%1] 不在线，%2 已转入离线信箱。")
                        .arg(target, type));
                    }, Qt::QueuedConnection);
            }
            else {
                qDebug() << "❌ 离线消息落库失败:" << offQ.lastError().text();
            }
        }
        return;
    }

    // ── 群发路由 ──────────────────────────────────────────────
    QMetaObject::invokeMethod(server, [server, fromUser, target]() {
        server->logMessage(QString("<font color='#9C27B0'>群发路由: [%1] 向 [%2] 发送了群消息。</font>")
            .arg(fromUser, target));
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

        bool isOnline = false;
        QMetaObject::invokeMethod(server, [server, member, &isOnline]() {
            isOnline = server->isClientOnline(member);
            }, Qt::BlockingQueuedConnection);

        if (isOnline) {
            QTcpSocket* targetSocket = nullptr;
            QMetaObject::invokeMethod(server, [server, member, &targetSocket]() {
                targetSocket = server->getSocketByName(member);
                }, Qt::BlockingQueuedConnection);
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
            insertQ.bindValue(":s", fromUser);
            insertQ.bindValue(":r", member);
            insertQ.bindValue(":d", target);
            insertQ.bindValue(":t", type);
            insertQ.bindValue(":c", dbContent);
            insertQ.bindValue(":f", fileName);
            insertQ.exec();
        }
    }
}

void RequestHandler::handleQueryChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString me = json["me"].toString();
    QString target = json["target"].toString();
    bool    isGroup = json["is_group"].toBool();

    QString sql = isGroup
        ? QString("SELECT sender, msg_type, content, filename, DATE_FORMAT(send_time, '%%m-%%d %%H:%%i') "
            "FROM chat_history WHERE receiver = '%1' AND is_group = 1 "
            "ORDER BY send_time ASC LIMIT 100").arg(target)
        : QString("SELECT sender, msg_type, content, filename, DATE_FORMAT(send_time, '%%m-%%d %%H:%%i') "
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
            arr.append(o);
        }
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleQueryChatContacts(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QJsonObject res;
    res["status"] = "success";

    QString myEmpId, myDept, myJob;
    QSqlQuery myQ(db);
    myQ.prepare("SELECT id, department, job_title FROM users WHERE name = :n");
    myQ.bindValue(":n", name);
    if (myQ.exec() && myQ.next()) {
        myEmpId = myQ.value(0).toString();
        myDept = myQ.value(1).toString().isEmpty() ? "未分配部门" : myQ.value(1).toString();
        myJob = (myQ.value(2).toString().isEmpty() || myQ.value(2).toString() == "未分配") ? "员工" : myQ.value(2).toString();
    }
    res["my_dept"] = myDept;
    res["my_folder"] = QString("%1_%2_%3_%4").arg(myEmpId, name, myDept, myJob);

    QJsonArray deptArr;
    QString deptSql = (myDept == "总经办")
        ? "SELECT DISTINCT department FROM users WHERE department != '' AND department IS NOT NULL"
        : QString("SELECT DISTINCT department FROM users WHERE department = '%1'").arg(myDept);
    QSqlQuery dQ(db);
    dQ.exec(deptSql);
    while (dQ.next()) deptArr.append(dQ.value(0).toString());
    res["departments"] = deptArr;

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

void RequestHandler::handleQueryGroupMembers(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString dept = json["department"].toString();
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
        u["job"] = (j.isEmpty() || j == "未分配") ? "员工" : j;
        arr.append(u);
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleReadReceipt(QSqlDatabase& /*db*/, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QString toUser = json["to"].toString();
    QTcpSocket* targetSocket = nullptr;
    QMetaObject::invokeMethod(server, [server, toUser, &targetSocket]() {
        targetSocket = server->getSocketByName(toUser);
        }, Qt::BlockingQueuedConnection);

    if (targetSocket) {
        QByteArray outData = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
        QMetaObject::invokeMethod(targetSocket, [targetSocket, outData]() {
            targetSocket->write(outData);
            }, Qt::QueuedConnection);
    }
}

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

    if (senderRole != "超级管理员" && senderRole != "管理员登录" && senderRole != "经理") {
        QMetaObject::invokeMethod(server, [server, fromUser, senderRole]() {
            server->logMessage(QString(
                "<font color='red'>越权拦截: 员工 [%1](%2) 尝试发送系统广播，已被零信任网关阻断！</font>")
                .arg(fromUser, senderRole));
            }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(server, [server, fromUser]() {
        server->logMessage(QString("<font color='#F56C6C'>授权通过: 管理层 [%1] 已下发全员广播。</font>").arg(fromUser));
        }, Qt::QueuedConnection);

    QSqlQuery allUsersQ(db);
    allUsersQ.exec("SELECT name FROM users WHERE role != '超级管理员' AND account != 'admin'");

    int pushCount = 0;
    while (allUsersQ.next()) {
        QString member = allUsersQ.value(0).toString().trimmed();
        if (member == fromUser) continue;

        bool isOnline = false;
        QMetaObject::invokeMethod(server, [server, member, &isOnline]() {
            isOnline = server->isClientOnline(member);
            }, Qt::BlockingQueuedConnection);

        if (isOnline) {
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
    QMetaObject::invokeMethod(server, [server, pushCount]() {
        server->logMessage(QString("   └─ 成功即时推送到 %1 名在线员工。").arg(pushCount));
        }, Qt::QueuedConnection);
}

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