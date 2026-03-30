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
#include <QPointer>
#include <QThread>
// 将 JSON 转换为紧凑格式并加上换行符，通过跨线程安全发送
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
// 接收并持久化保存 AI 会话的聊天记录（同时落库并生成本地审计文件）
void RequestHandler::handleAiSaveMessage(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString sessionId = json["session_id"].toString();
    QString role = json["role"].toString();
    QString content = json["content"].toString();
    QString name = json["name"].toString();
    // 1. Base64 保护：防止 AI 生成的复杂代码、Markdown 或特殊字符被 ODBC 截断
    QString dbContent = "B64:" + QString(content.toUtf8().toBase64());
    // 2. 将原始对话存入数据库聊天日志表
    QSqlQuery q(db);
    QString sql = QString("INSERT INTO ai_chat_logs (session_id, role, content, create_time) VALUES ('%1', '%2', '%3', NOW())")
        .arg(sessionId, role, dbContent);
    q.exec(sql);
    // 3. 提取对话摘要，更新到对应会话的 last_message 字段用于列表展示
    QSqlQuery uq(db);
    QString snippet = content;
    snippet.remove(QRegularExpression("<[^>]*>")); // 移除富文本 HTML 标签
    snippet.replace("\n", " "); // 将换行替换为空格
    if (snippet.length() > 15) snippet = snippet.left(15) + "..."; // 截断长文本
    snippet.replace("'", "''"); // SQL 防注入转义
    uq.exec(QString("UPDATE ai_sessions SET last_message = '%1' WHERE session_id = '%2'").arg(snippet, sessionId));
    // 4. 企业合规审计：在服务端本地生成明文文本备查
    QString account = "Unk", dept = "Unk", title = "Unk";
    QSqlQuery userQ(db);
    userQ.exec(QString("SELECT account, department, job_title FROM users WHERE name = '%1'").arg(name));
    if (userQ.next()) {
        account = userQ.value(0).toString();
        dept = userQ.value(1).toString();
        title = userQ.value(2).toString();
    }
    // ⭐️ 核心修复 1：先拼接带 ../ 的相对路径，再用 cleanPath 净化为绝对路径，最后补上斜杠
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/AiChat";
    QString baseDir = QDir::cleanPath(rawPath) + "/";
    QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
    QDir    dir;
    dir.mkpath(baseDir + folderName);
    // 将对话追加写入本地 .doc 文件（纯文本格式）
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
// 创建一个新的 AI 聊天会话
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
// 查询用户可见的 AI 历史会话列表
void RequestHandler::handleQueryAiSessions(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QJsonArray arr;
    QSqlQuery  q(db);
    // 仅查询 is_visible=1 的会话（未被用户逻辑删除的）
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
// 重命名指定的 AI 会话标题
void RequestHandler::handleRenameAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.prepare("UPDATE ai_sessions SET title=? WHERE session_id=?");
    q.addBindValue(json["title"].toString());
    q.addBindValue(json["session_id"].toString());
    q.exec();
}
// 逻辑删除 AI 会话（对用户隐藏，但后台审计库仍保留）
void RequestHandler::handleDeleteAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.prepare("UPDATE ai_sessions SET is_visible=0 WHERE session_id=?");
    q.addBindValue(json["session_id"].toString());
    q.exec();
}
// 模糊搜索用户的 AI 会话记录（匹配标题或最后一条消息）
void RequestHandler::handleSearchAiHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString userName = json["name"].toString();
    QString keyword = json["keyword"].toString();
    // 转义单引号防注入
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
// 获取某个具体 AI 会话的完整上下文对话内容
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
            // 兼容性处理：若是加密存储的 Base64 数据则解码，否则直接输出明文
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
// 拦截并备份用户发给 AI 的文件，用于企业数据防泄露审计
void RequestHandler::handleAiAuditFile(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QString name = json["name"].toString();
    QString fileName = json["filename"].toString();
    QString fileDataBase64 = json["filedata"].toString();
    // 1. 获取员工信息构建审计目录
    QString account = "Unk", dept = "Unk", title = "Unk";
    QSqlQuery q(db);
    q.prepare("SELECT account, department, job_title FROM users WHERE name = :n");
    q.bindValue(":n", name);
    if (q.exec() && q.next()) {
        account = q.value(0).toString();
        dept = q.value(1).toString();
        title = q.value(2).toString();
    }
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/AiChat";
    QString baseDir = QDir::cleanPath(rawPath) + "/";
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
    // 3. 在服务器端 UI 打印文件审计拦截日志
    QMetaObject::invokeMethod(server, [server, name, fileName]() {
        server->logMessage(QString("<font color='#00B42A'>AI 附件审计: 拦截并备份了 [%1] 上传的 AI 附件 '%2'。</font>")
            .arg(name, fileName));
        }, Qt::QueuedConnection);
}