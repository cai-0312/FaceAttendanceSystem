#include "RequestHandler.h"
#include "AttendanceServer.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDebug>

// ============================================================
// 内部工具函数：发送 JSON 回包（线程安全）
// ============================================================
static void sendJson(QTcpSocket* socket, const QJsonObject& obj)
{
    QByteArray outData = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    QMetaObject::invokeMethod(socket,
        [socket, outData]() { socket->write(outData); },
        Qt::QueuedConnection);
}

// ============================================================
// 内部工具：Base64 解码可能被保护的聊天内容
// ============================================================
static QString decodeContent(const QString& raw)
{
    if (raw.startsWith("B64:"))
        return QString::fromUtf8(QByteArray::fromBase64(raw.mid(4).toUtf8()));
    return raw;
}

// ============================================================
// 内部工具：Base64 编码聊天内容（防止 ODBC 吞表情包）
// ============================================================
static QString encodeContent(const QString& content)
{
    return "B64:" + QString(content.toUtf8().toBase64());
}


// ============================================================
// ── 人脸 & 账号 ──────────────────────────────────────────────
// ============================================================

void RequestHandler::handleQueryFaceFeatures(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& /*json*/)
{
    QJsonArray arr;
    QSqlQuery query(db);
    if (query.exec("SELECT name, feature FROM users WHERE feature IS NOT NULL")) {
        while (query.next()) {
            QJsonObject o;
            o["name"] = query.value(0).toString();
            o["feature"] = QString(query.value(1).toByteArray().toBase64());
            arr.append(o);
        }
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleRegisterFace(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString   name = json["name"].toString();
    QByteArray featureData = QByteArray::fromBase64(json["feature"].toString().toUtf8());

    QSqlQuery q(db);
    q.prepare("UPDATE users SET feature = :f WHERE name = :n");
    q.bindValue(":f", featureData);
    q.bindValue(":n", name);
    q.exec();
}

void RequestHandler::handleClientLoginAuth(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString account = json["account"].toString();
    QString pwd = json["pwd"].toString();
    QString role = json["role"].toString();

    QJsonObject res;
    res["status"] = "fail";

    QSqlQuery q(db);
    QString sql = QString("SELECT name FROM users WHERE account = '%1' AND password = '%2' AND role = '%3'")
        .arg(account, pwd, role);
    if (q.exec(sql) && q.next()) {
        res["status"] = "success";
        res["real_name"] = q.value(0).toString();
    }
    sendJson(socket, res);
}

void RequestHandler::handleClientRegisterAccount(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString pwd = json["pwd"].toString();
    QString name = json["name"].toString();
    QString role = json["role"].toString();
    QString dept = json["dept"].toString();
    QString jobTitle = json["job_title"].toString();
    QString phone = json["phone"].toString();
    QString gender = json["gender"].toString();

    QJsonObject res;
    res["status"] = "fail";

    QString sql = QString(
        "INSERT INTO users (account, password, name, role, department, job_title, phone, gender) "
        "VALUES ('%1', '%2', '%3', '%4', '%5', '%6', '%7', '%8')"
    ).arg(account, pwd, name, role, dept, jobTitle, phone, gender);

    QSqlQuery q(db);
    if (q.exec(sql)) {
        res["status"] = "success";
        QMetaObject::invokeMethod(server, [server, name, role]() {
            server->logMessage(QString("<font color='#E6A23C'>新兵入职: [%1] 注册了账号，权限为 [%2]。</font>").arg(name, role));
            server->refreshPermModel();
            }, Qt::QueuedConnection);
    }
    else {
        res["msg"] = q.lastError().text();
    }
    sendJson(socket, res);
}

void RequestHandler::handleVerifyUserForRegistration(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString dept = json["dept"].toString().trimmed();

    QJsonObject res;
    res["status"] = "fail";

    QSqlQuery q(db);
    QString sql = QString("SELECT id FROM users WHERE name = '%1' AND department = '%2'").arg(name, dept);
    if (q.exec(sql) && q.next()) {
        res["status"] = "success";
    }
    sendJson(socket, res);
}


// ============================================================
// ── 登录 / 在线状态 ───────────────────────────────────────────
// ============================================================

void RequestHandler::handleLogin(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString name = json["name"].toString().trimmed();
    QString ip = socket->peerAddress().toString().remove("::ffff:");
    QString dept = "未知部门", jobTitle = "未分配";

    QSqlQuery query(db);
    query.prepare("SELECT department, job_title FROM users WHERE name = :name");
    query.bindValue(":name", name);
    if (query.exec() && query.next()) {
        dept = query.value(0).toString();
        jobTitle = query.value(1).toString();
        if (dept.isEmpty())     dept = "未分配部门";
        if (jobTitle.isEmpty()) jobTitle = "未分配";
    }

    QMetaObject::invokeMethod(server, [server, socket, name, dept, jobTitle, ip]() {
        server->registerClient(socket, name, dept, jobTitle, ip);
        }, Qt::QueuedConnection);

    // 补发离线消息
    QSqlQuery offlineQ(db);
    offlineQ.prepare(
        "SELECT sender, msg_type, content, filename, send_time, department "
        "FROM offline_messages WHERE receiver = :n ORDER BY send_time ASC"
    );
    offlineQ.bindValue(":n", name);

    int offlineCount = 0;
    if (offlineQ.exec()) {
        while (offlineQ.next()) {
            QJsonObject offMsg;
            offMsg["from"] = offlineQ.value(0).toString();
            QString mType = offlineQ.value(1).toString();
            offMsg["type"] = mType;
            offMsg["msg"] = decodeContent(offlineQ.value(2).toString());
            offMsg["filename"] = offlineQ.value(3).toString();
            offMsg["time"] = offlineQ.value(4).toDateTime().toString("HH:mm:ss");
            offMsg["department"] = offlineQ.value(5).toString();
            offMsg["is_offline"] = true;

            QByteArray outData = QJsonDocument(offMsg).toJson(QJsonDocument::Compact) + "\n";
            QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); },
                Qt::QueuedConnection);
            offlineCount++;
        }
    }

    if (offlineCount > 0) {
        QMetaObject::invokeMethod(server, [server, name, offlineCount]() {
            server->logMessage(QString("<font color='#E6A23C'>已向 [%1] 补发 %2 条离线消息/文件。</font>")
                .arg(name).arg(offlineCount));
            }, Qt::QueuedConnection);
    }

    // 清除已补发的离线消息
    QSqlQuery deleteOffQ(db);
    deleteOffQ.prepare("DELETE FROM offline_messages WHERE receiver = :n");
    deleteOffQ.bindValue(":n", name);
    deleteOffQ.exec();
}

void RequestHandler::handleStatusUpdate(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.prepare("UPDATE users SET status_icon = :s WHERE name = :n");
    q.bindValue(":s", json["status"].toString());
    q.bindValue(":n", json["name"].toString());
    q.exec();
}


// ============================================================
// ── 用户档案 ─────────────────────────────────────────────────
// ============================================================

void RequestHandler::handleQueryUserProfile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QJsonObject res;
    res["status"] = "fail";

    QSqlQuery q(db);
    q.prepare("SELECT id, job_title, role, department, gender, phone, name, avatar "
        "FROM users WHERE name = :n OR account = :n");
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
    sendJson(socket, res);
}

void RequestHandler::handleUpdateProfileField(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QString field = json["field"].toString();
    QString value = json["value"].toString();

    QSqlQuery q(db);
    QString sql = QString("UPDATE users SET %1 = :v WHERE name = :n OR account = :n").arg(field);
    q.prepare(sql);
    q.bindValue(":v", value);
    q.bindValue(":n", name);
    q.exec();
}

void RequestHandler::handleQueryUserList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString dept = json["dept"].toString();
    QString keyword = json["keyword"].toString();

    QString sql =
        "SELECT id, account, name, gender, department, job_title, phone "
        "FROM view_users_lite "
        "WHERE account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'";

    if (dept != "全部" && !dept.isEmpty())
        sql += QString(" AND department = '%1'").arg(dept);
    if (!keyword.isEmpty())
        sql += QString(" AND (name LIKE '%%%1%%' OR id LIKE '%%%1%%')").arg(keyword);

    QJsonArray arr;
    QSqlQuery q(db);
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
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleQueryUserDept(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QString dept = "全部";

    QSqlQuery dq(db);
    dq.prepare("SELECT department FROM users WHERE name = :n");
    dq.bindValue(":n", name);
    if (dq.exec() && dq.next()) dept = dq.value(0).toString();

    QJsonObject res;
    res["type"] = "user_dept_reply";
    res["department"] = dept;
    sendJson(socket, res);
}


// ============================================================
// ── 管理员操作 ───────────────────────────────────────────────
// ============================================================

void RequestHandler::handleAdminResetPassword(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString empName = json["name"].toString();

    QSqlQuery q(db);
    q.prepare("UPDATE users SET password = '123456' WHERE account = :acc");
    q.bindValue(":acc", account);

    QJsonObject res;
    if (q.exec()) {
        res["status"] = "success";
        QMetaObject::invokeMethod(server, [server, empName]() {
            server->logMessage(QString("<font color='#E6A23C'>权限操作: 管理员已重置 [%1] 的登录密码。</font>").arg(empName));
            }, Qt::QueuedConnection);
    }
    else {
        res["status"] = "fail";
    }
    sendJson(socket, res);
}

void RequestHandler::handleAdminDeleteUser(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString empName = json["name"].toString();

    QSqlQuery q(db);
    q.prepare("DELETE FROM users WHERE account = :acc");
    q.bindValue(":acc", account);

    QJsonObject res;
    if (q.exec()) {
        res["status"] = "success";
        QMetaObject::invokeMethod(server, [server, empName]() {
            server->logMessage(QString("<font color='red'>高危操作: 管理员已将员工 [%1] 彻底踢出系统！</font>").arg(empName));
            server->refreshPermModel();
            }, Qt::QueuedConnection);
    }
    else {
        res["status"] = "fail";
    }
    sendJson(socket, res);
}

void RequestHandler::handleAdminModifyStatus(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    int     recordId = json["record_id"].toInt();
    QString newStatus = json["new_status"].toString();

    QSqlQuery q(db);
    q.prepare("UPDATE attendance_records SET status = :s WHERE id = :id");
    q.bindValue(":s", newStatus);
    q.bindValue(":id", recordId);
    if (q.exec()) {
        QMetaObject::invokeMethod(server, [server]() {
            server->logMessage("<font color='#E6A23C'>⚠️ 管理员后台强行修改了某条考勤记录状态。</font>");
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}