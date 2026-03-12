#include "RequestHandler.h"
#include "AttendanceServer.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDebug>

static void sendJson(QTcpSocket* socket, const QJsonObject& obj)
{
    QByteArray outData = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    QMetaObject::invokeMethod(socket,
        [socket, outData]() { socket->write(outData); },
        Qt::QueuedConnection);
}


// ============================================================
// ── AI 助手 ──────────────────────────────────────────────────
// ============================================================

void RequestHandler::handleAiSaveMessage(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString sessionId = json["session_id"].toString();
    QString role = json["role"].toString();
    QString content = json["content"].toString();
    QString name = json["name"].toString();

    // Base64 保护防止 ODBC 吞特殊字符
    QString dbContent = "B64:" + QString(content.toUtf8().toBase64());

    QSqlQuery q(db);
    QString sql = QString("INSERT INTO ai_chat_logs (session_id, role, content, create_time) VALUES ('%1', '%2', '%3', NOW())")
        .arg(sessionId, role, dbContent);
    q.exec(sql);

    // 更新会话最后消息摘要
    QSqlQuery uq(db);
    QString snippet = content;
    snippet.remove(QRegularExpression("<[^>]*>"));
    snippet.replace("\n", " ");
    if (snippet.length() > 15) snippet = snippet.left(15) + "...";
    snippet.replace("'", "''");
    uq.exec(QString("UPDATE ai_sessions SET last_message = '%1' WHERE session_id = '%2'").arg(snippet, sessionId));

    // 查询用户信息，写本地审计文件
    QString account = "Unk", dept = "Unk", title = "Unk";
    QSqlQuery userQ(db);
    userQ.exec(QString("SELECT account, department, job_title FROM users WHERE name = '%1'").arg(name));
    if (userQ.next()) {
        account = userQ.value(0).toString();
        dept = userQ.value(1).toString();
        title = userQ.value(2).toString();
    }

    QString baseDir = QCoreApplication::applicationDirPath() + "/server/AiChat/";
    QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
    QDir    dir;
    dir.mkpath(baseDir + folderName);

    QString filePath = baseDir + folderName + "/Session_" + sessionId + ".doc";
    QFile   file(filePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        QString roleName = (role == "user") ? QString("员工本人 (%1)").arg(name) : "AI 管家回复";
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        out << QString("【%1】 %2:\n%3\n\n----------------------------------------------------\n\n")
            .arg(timeStr, roleName, content);
        file.close();
    }
}

void RequestHandler::handleCreateAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO ai_sessions (session_id, user_name, title, create_time, is_visible, last_message) "
        "VALUES (?, ?, ?, NOW(), 1, '暂无聊天记录...')");
    q.addBindValue(json["session_id"].toString());
    q.addBindValue(json["name"].toString());
    q.addBindValue(json["title"].toString());
    q.exec();
}

void RequestHandler::handleQueryAiSessions(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QJsonArray arr;
    QSqlQuery  q(db);
    q.exec(QString("SELECT session_id, title, last_message FROM ai_sessions "
        "WHERE user_name='%1' AND is_visible=1 ORDER BY create_time DESC").arg(json["name"].toString()));
    while (q.next()) {
        QJsonObject o;
        o["session_id"] = q.value(0).toString();
        o["title"] = q.value(1).toString();
        o["last_message"] = q.value(2).toString();
        arr.append(o);
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleRenameAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.prepare("UPDATE ai_sessions SET title=? WHERE session_id=?");
    q.addBindValue(json["title"].toString());
    q.addBindValue(json["session_id"].toString());
    q.exec();
}

void RequestHandler::handleDeleteAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.prepare("UPDATE ai_sessions SET is_visible=0 WHERE session_id=?");
    q.addBindValue(json["session_id"].toString());
    q.exec();
}

void RequestHandler::handleSearchAiHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString userName = json["name"].toString();
    QString keyword = json["keyword"].toString();
    userName.replace("'", "''");
    keyword.replace("'", "''");

    QJsonArray arr;
    QSqlQuery  q(db);
    QString sql = QString(
        "SELECT session_id, title, last_message FROM ai_sessions "
        "WHERE user_name = '%1' AND is_visible = 1 "
        "AND (title LIKE '%%%2%%' OR last_message LIKE '%%%2%%') "
        "ORDER BY create_time DESC"
    ).arg(userName, keyword);

    if (q.exec(sql)) {
        while (q.next()) {
            QJsonObject o;
            o["session_id"] = q.value(0).toString();
            o["title"] = q.value(1).toString();
            o["last_message"] = q.value(2).toString();
            arr.append(o);
        }
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleQueryAiChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString   sid = json["session_id"].toString();
    QJsonArray arr;
    QSqlQuery  q(db);
    QString sql = QString("SELECT role, content FROM ai_chat_logs WHERE session_id = '%1' ORDER BY create_time ASC").arg(sid);

    if (q.exec(sql)) {
        while (q.next()) {
            QJsonObject o;
            o["role"] = q.value(0).toString();
            QString rawContent = q.value(1).toString();
            if (rawContent.startsWith("B64:"))
                o["content"] = QString::fromUtf8(QByteArray::fromBase64(rawContent.mid(4).toUtf8()));
            else
                o["content"] = rawContent;
            arr.append(o);
        }
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleAiAuditFile(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QString name = json["name"].toString();
    QString fileName = json["filename"].toString();
    QString fileDataBase64 = json["filedata"].toString();

    QString account = "Unk", dept = "Unk", title = "Unk";
    QSqlQuery q(db);
    q.prepare("SELECT account, department, job_title FROM users WHERE name = :n");
    q.bindValue(":n", name);
    if (q.exec() && q.next()) {
        account = q.value(0).toString();
        dept = q.value(1).toString();
        title = q.value(2).toString();
    }

    QString baseDir = QCoreApplication::applicationDirPath() + "/server/AiChat/";
    QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
    QDir    dir;
    dir.mkpath(baseDir + folderName);

    QByteArray fileData = QByteArray::fromBase64(fileDataBase64.toUtf8());
    QString    filePath = baseDir + folderName + "/" + fileName;
    QFile      file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(fileData);
        file.close();
    }

    QMetaObject::invokeMethod(server, [server, name, fileName]() {
        server->logMessage(QString("<font color='#00B42A'>AI 附件审计: 拦截并备份了 [%1] 上传的 AI 附件 '%2'。</font>")
            .arg(name, fileName));
        }, Qt::QueuedConnection);
}