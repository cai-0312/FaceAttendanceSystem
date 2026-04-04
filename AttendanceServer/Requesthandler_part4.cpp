#include "RequestHandler.h"
#include "AttendanceServer.h"
#include "DatabaseManager.h"
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
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QtConcurrent>
#include <QSslError>
#include <QSettings>
// SQL 安全转义
static QString S(const QString& val) { QString s = val; s.replace("'", "''"); return s; }
// 线程安全 JSON 发送
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
    catch (...) {}
}
//  AI 模型配置（密钥从 server.ini 读取，不再硬编码到源码中）
struct AiModelConfig {
    QString apiUrl;
    QString apiKey;
    QString modelName;
};
static AiModelConfig getModelConfig(const QString& model)
{
    QSettings cfg(QCoreApplication::applicationDirPath() + "/server.ini", QSettings::IniFormat);
    // 从配置文件读取 API 密钥，回退到空串（需管理员在 server.ini 中配置）
    QString deepseekKey = cfg.value("AI/deepseek_key", "").toString();
    QString doubaoKey   = cfg.value("AI/doubao_key", "").toString();
    AiModelConfig c;
    if (model == "deepseek-chat" || model == "deepseek-v3") {
        c.apiUrl = "https://api.deepseek.com/chat/completions";
        c.apiKey = deepseekKey;
        c.modelName = "deepseek-chat";
    }
    else if (model == "deepseek-reasoner" || model == "deepseek-r1") {
        c.apiUrl = "https://api.deepseek.com/chat/completions";
        c.apiKey = deepseekKey;
        c.modelName = "deepseek-reasoner";
    }
    else if (model == "doubao-lite") {
        c.apiUrl = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
        c.apiKey = doubaoKey;
        c.modelName = cfg.value("AI/doubao_lite_ep", "ep-20260404162018-n225c").toString();
    }
    else if (model == "doubao-seedream") {
        c.apiUrl = "https://ark.cn-beijing.volces.com/api/v3/images/generations";
        c.apiKey = doubaoKey;
        c.modelName = cfg.value("AI/doubao_seedream_ep", "ep-20260306195042-l95bj").toString();
    }
    else {
        c.apiUrl = "https://api.deepseek.com/chat/completions";
        c.apiKey = deepseekKey;
        c.modelName = "deepseek-chat";
    }
    return c;
}
//  子线程异步调用大模型 API，不阻塞 TCP 主线程
void RequestHandler::handleAiChatRequest(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString sessionId = json["session_id"].toString();
    QString name = json["name"].toString();
    QString content = json["content"].toString();
    QString model = json["model"].toString();
    QJsonArray msgHistory = json["message_history"].toArray();
    bool isImageAPI = json["is_image_api"].toBool(false);
    // 1. 保存用户提问到数据库
    {
        QString dbContent = "B64:" + QString(content.toUtf8().toBase64());
        QSqlQuery q(db);
        q.exec(QString("INSERT INTO ai_chat_logs (session_id, role, content, create_time) "
            "VALUES ('%1', 'user', '%2', NOW())").arg(S(sessionId), S(dbContent)));
        q.finish();

        QString snippet = content;
        snippet.remove(QRegularExpression("<[^>]*>"));
        snippet.replace("\n", " ");
        if (snippet.length() > 15) snippet = snippet.left(15) + "...";
        QSqlQuery uq(db);
        uq.exec(QString("UPDATE ai_sessions SET last_message = '%1' WHERE session_id = '%2'")
            .arg(S(snippet), S(sessionId)));
        uq.finish();
    }
    // 2. 本地审计文件
    {
        QString account = "Unk", dept2 = "Unk", title2 = "Unk";
        QSqlQuery userQ(db);
        userQ.exec(QString("SELECT account, department, job_title FROM users WHERE name = '%1'").arg(S(name)));
        if (userQ.next()) {
            account = userQ.value(0).toString();
            dept2 = userQ.value(1).toString();
            title2 = userQ.value(2).toString();
        }
        userQ.finish();
        QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/AiChat";
        QString baseDir = QDir::cleanPath(rawPath) + "/";
        QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept2, title2);
        QDir().mkpath(baseDir + folderName);
        QString filePath = baseDir + folderName + "/Session_" + sessionId + ".doc";
        QFile file(filePath);
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out.setEncoding(QStringConverter::Utf8);
            QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            out << QString("【%1】 员工本人 (%2):\n%3\n\n----------------------------------------------------\n\n")
                .arg(timeStr, name, content);
            file.close();
        }
    }
    // 3. 获取模型配置，校验密钥是否有效
    AiModelConfig cfg = getModelConfig(model);
    if (cfg.apiKey.isEmpty()) {
        QJsonObject errResp;
        errResp["type"] = "ai_chat_response";
        errResp["session_id"] = sessionId;
        errResp["status"] = "fail";
        errResp["msg"] = "AI服务未配置：server.ini 中缺少 [AI] 段的 API 密钥，请联系管理员配置后重启服务端。";
        sendJson(socket, errResp);
        if (server) {
            QMetaObject::invokeMethod(server, [server, name, model]() {
                server->logMessage(QString("<font color='#F53F3F'>[AI代理] [%1] 请求模型 %2 失败：server.ini 中未配置 API 密钥</font>")
                    .arg(name, model));
                }, Qt::QueuedConnection);
        }
        return;
    }
    // 4. 服务端日志
    if (server) {
        QMetaObject::invokeMethod(server, [server, name, model]() {
            server->logMessage(QString("<font color='#165DFF'>[AI代理] 收到 [%1] 的请求，模型: %2，正在调用云端API...</font>")
                .arg(name, model));
            }, Qt::QueuedConnection);
    }
    // 5. 子线程异步调用大模型 API
    QPointer<QTcpSocket> safeSocket = socket;
    QPointer<AttendanceServer> safeServer = server;
    QString safeName = name;
    QString safeSessionId = sessionId;
    QtConcurrent::run([=]() {
        QJsonObject requestBody;
        requestBody["model"] = cfg.modelName;

        if (isImageAPI) {
            requestBody["prompt"] = content;
        }
        else {
            requestBody["messages"] = msgHistory;
            requestBody["temperature"] = 0.7;
        }
        // 子线程同步 HTTP 请求
        QNetworkAccessManager manager;
        QNetworkRequest request(QUrl(cfg.apiUrl));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + cfg.apiKey).toUtf8());
        request.setTransferTimeout(60000);
        QNetworkReply* reply = manager.post(request, QJsonDocument(requestBody).toJson());
        // 忽略 SSL 错误
        QObject::connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError>& errors) { reply->ignoreSslErrors(errors); });
        // 带超时的同步等待
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(60000);
        loop.exec();
        // 解析响应
        QString aiReply;
        QString status = "fail";
        QString errMsg;

        if (timer.isActive()) {
            timer.stop();
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonObject respJson = doc.object();

                if (isImageAPI) {
                    if (respJson.contains("data") && respJson["data"].isArray()) {
                        QString imgUrl = respJson["data"].toArray()[0].toObject()["url"].toString();
                        aiReply = "[AI_IMAGE]" + imgUrl;
                        status = "success";
                    }
                    else {
                        errMsg = "画图API返回异常";
                    }
                }
                else {
                    if (respJson.contains("choices") && respJson["choices"].isArray()) {
                        QJsonObject msgObj = respJson["choices"].toArray()[0].toObject()["message"].toObject();
                        aiReply = msgObj["content"].toString();
                        status = "success";
                    }
                    else {
                        errMsg = "API返回格式异常";
                    }
                }
            }
            else {
                // 读取 API 返回的错误响应体，提取详细诊断信息（如密钥过期、余额不足等）
                QByteArray errBody = reply->readAll();
                int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString errDetail;
                QJsonDocument errDoc = QJsonDocument::fromJson(errBody);
                if (!errDoc.isNull()) {
                    QJsonObject errObj = errDoc.object();
                    // 火山引擎/DeepSeek 标准错误格式: {"error": {"message": "...", "code": "..."}}
                    if (errObj.contains("error")) {
                        QJsonObject apiErr = errObj["error"].toObject();
                        errDetail = apiErr["message"].toString();
                        QString errCode = apiErr["code"].toString();
                        if (!errCode.isEmpty() && !errDetail.contains(errCode))
                            errDetail = QString("[%1] %2").arg(errCode, errDetail);
                    }
                }
                if (errDetail.isEmpty()) errDetail = reply->errorString();
                errMsg = QString("API错误 (HTTP %1): %2").arg(httpStatus).arg(errDetail);
            }
        }
        else {
            reply->abort();
            errMsg = "AI接口响应超时(60秒)";
        }
        reply->deleteLater();
        // 6. 保存 AI 回复到数据库（子线程使用独立数据库连接）
        if (status == "success" && !aiReply.isEmpty()) {
            QString connName = DatabaseManager::makeThreadConnName();
            if (DatabaseManager::openThreadConnection(connName)) {
                QSqlDatabase threadDb = QSqlDatabase::database(connName);
                QString dbAiContent = "B64:" + QString(aiReply.toUtf8().toBase64());
                QSqlQuery q(threadDb);
                q.exec(QString("INSERT INTO ai_chat_logs (session_id, role, content, create_time) "
                    "VALUES ('%1', 'ai', '%2', NOW())").arg(S(safeSessionId), S(dbAiContent)));
                q.finish();
                // 更新会话摘要
                QString aiSnippet = aiReply;
                aiSnippet.remove(QRegularExpression("<[^>]*>"));
                aiSnippet.replace("\n", " ");
                if (aiSnippet.length() > 15) aiSnippet = aiSnippet.left(15) + "...";
                QSqlQuery uq(threadDb);
                uq.exec(QString("UPDATE ai_sessions SET last_message = '%1' WHERE session_id = '%2'")
                    .arg(S(aiSnippet), S(safeSessionId)));
                uq.finish();
                threadDb.close();
            }
            QSqlDatabase::removeDatabase(connName);
            // 审计文件追加 AI 回复
            QString rawPath2 = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/AiChat";
            QString baseDir2 = QDir::cleanPath(rawPath2) + "/";
            // 简化审计路径（使用 session_id 定位即可）
            QDir auditDir(baseDir2);
            QStringList dirs = auditDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& d : dirs) {
                QString docPath = baseDir2 + d + "/Session_" + safeSessionId + ".doc";
                QFile docFile(docPath);
                if (docFile.exists() && docFile.open(QIODevice::Append | QIODevice::Text)) {
                    QTextStream out(&docFile);
                    out.setEncoding(QStringConverter::Utf8);
                    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                    out << QString("【%1】 AI 管家回复:\n%2\n\n----------------------------------------------------\n\n")
                        .arg(timeStr, aiReply);
                    docFile.close();
                    break;
                }
            }
        }
        // 7. TCP 回传客户端（必须回主线程操作 socket）
        QJsonObject response;
        response["type"] = "ai_chat_response";
        response["session_id"] = safeSessionId;
        response["status"] = status;
        response["content"] = aiReply;
        response["msg"] = errMsg;
        response["is_image"] = isImageAPI && status == "success";
        QByteArray responseData = QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n";
        if (safeSocket && safeSocket->isValid()) {
            QMetaObject::invokeMethod(safeSocket, [safeSocket, responseData]() {
                if (safeSocket && safeSocket->isValid()) {
                    safeSocket->write(responseData);
                    safeSocket->flush();
                }
                }, Qt::QueuedConnection);
        }
        // 8. 服务端日志
        if (safeServer) {
            QMetaObject::invokeMethod(safeServer.data(), [safeServer, safeName, status, errMsg]() {
                if (safeServer) {
                    if (status == "success") {
                        safeServer->logMessage(QString("<font color='#00B42A'>[AI代理] [%1] 的AI请求已成功响应。</font>").arg(safeName));
                    }
                    else {
                        safeServer->logMessage(QString("<font color='red'>[AI代理] [%1] 的AI请求失败: %2</font>").arg(safeName, errMsg));
                    }
                }
                }, Qt::QueuedConnection);
        }
        });
}
// 保存 AI 聊天记录（保留兼容，客户端仍可能用于本地意图拦截后的保存）
void RequestHandler::handleAiSaveMessage(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString sessionId = json["session_id"].toString();
    QString role = json["role"].toString();
    QString content = json["content"].toString();
    QString name = json["name"].toString();
    QString dbContent = "B64:" + QString(content.toUtf8().toBase64());
    QSqlQuery q(db);
    q.exec(QString("INSERT INTO ai_chat_logs (session_id, role, content, create_time) "
        "VALUES ('%1', '%2', '%3', NOW())").arg(S(sessionId), S(role), S(dbContent)));
    q.finish();
    QString snippet = content;
    snippet.remove(QRegularExpression("<[^>]*>"));
    snippet.replace("\n", " ");
    if (snippet.length() > 15) snippet = snippet.left(15) + "...";
    QSqlQuery uq(db);
    uq.exec(QString("UPDATE ai_sessions SET last_message = '%1' WHERE session_id = '%2'")
        .arg(S(snippet), S(sessionId)));
    uq.finish();
    // 审计文件
    QString account = "Unk", dept = "Unk", title = "Unk";
    QSqlQuery userQ(db);
    userQ.exec(QString("SELECT account, department, job_title FROM users WHERE name = '%1'").arg(S(name)));
    if (userQ.next()) {
        account = userQ.value(0).toString();
        dept = userQ.value(1).toString();
        title = userQ.value(2).toString();
    }
    userQ.finish();
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/AiChat";
    QString baseDir = QDir::cleanPath(rawPath) + "/";
    QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
    QDir().mkpath(baseDir + folderName);
    QString filePath = baseDir + folderName + "/Session_" + sessionId + ".doc";
    QFile file(filePath);
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
// 创建新 AI 会话
void RequestHandler::handleCreateAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString sid = json["session_id"].toString();
    QString name = json["name"].toString();
    QString title = json["title"].toString();
    QSqlQuery q(db);
    q.exec(QString("INSERT INTO ai_sessions (session_id, user_name, title, create_time, is_visible, last_message) "
        "VALUES ('%1', '%2', '%3', NOW(), 1, '暂无聊天记录...')").arg(S(sid), S(name), S(title)));
    q.finish();
}
// 查询用户可见的 AI 历史会话列表
void RequestHandler::handleQueryAiSessions(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QJsonArray arr;
    QSqlQuery q(db);
    q.exec(QString("SELECT session_id, title, last_message FROM ai_sessions "
        "WHERE user_name='%1' AND is_visible=1 ORDER BY create_time DESC").arg(S(json["name"].toString())));
    while (q.next()) {
        QJsonObject o;
        o["session_id"] = q.value(0).toString();
        o["title"] = q.value(1).toString();
        o["last_message"] = q.value(2).toString();
        arr.append(o);
    }
    q.finish();
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}
// 重命名 AI 会话
void RequestHandler::handleRenameAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.exec(QString("UPDATE ai_sessions SET title = '%1' WHERE session_id = '%2'")
        .arg(S(json["title"].toString()), S(json["session_id"].toString())));
    q.finish();
}
// 逻辑删除 AI 会话
void RequestHandler::handleDeleteAiSession(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.exec(QString("UPDATE ai_sessions SET is_visible = 0 WHERE session_id = '%1'")
        .arg(S(json["session_id"].toString())));
    q.finish();
}
// 模糊搜索 AI 会话
void RequestHandler::handleSearchAiHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString userName = json["name"].toString();
    QString keyword = json["keyword"].toString();
    QJsonArray arr;
    QSqlQuery q(db);
    q.exec(QString("SELECT session_id, title, last_message FROM ai_sessions "
        "WHERE user_name = '%1' AND is_visible = 1 "
        "AND (title LIKE '%%%2%%' OR last_message LIKE '%%%2%%') "
        "ORDER BY create_time DESC").arg(S(userName), S(keyword)));
    while (q.next()) {
        QJsonObject o;
        o["session_id"] = q.value(0).toString();
        o["title"] = q.value(1).toString();
        o["last_message"] = q.value(2).toString();
        arr.append(o);
    }
    q.finish();
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}
// 查询 AI 会话完整聊天历史
void RequestHandler::handleQueryAiChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString sid = json["session_id"].toString();
    QJsonArray arr;
    QSqlQuery q(db);
    q.exec(QString("SELECT role, content FROM ai_chat_logs WHERE session_id = '%1' ORDER BY create_time ASC").arg(S(sid)));
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
    q.finish();
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}
// AI 审计文件备份
void RequestHandler::handleAiAuditFile(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QString name = json["name"].toString();
    QString fileName = json["filename"].toString();
    QString fileDataBase64 = json["filedata"].toString();
    QString account = "Unk", dept = "Unk", title = "Unk";
    QSqlQuery q(db);
    q.exec(QString("SELECT account, department, job_title FROM users WHERE name = '%1'").arg(S(name)));
    if (q.next()) {
        account = q.value(0).toString();
        dept = q.value(1).toString();
        title = q.value(2).toString();
    }
    q.finish();
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/AiChat";
    QString baseDir = QDir::cleanPath(rawPath) + "/";
    QString folderName = QString("%1_%2_%3_%4").arg(account, name, dept, title);
    QDir().mkpath(baseDir + folderName);
    QByteArray fileData = QByteArray::fromBase64(fileDataBase64.toUtf8());
    QString filePath = baseDir + folderName + "/" + fileName;
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(fileData);
        file.close();
    }
    QMetaObject::invokeMethod(server, [server, name, fileName]() {
        server->logMessage(QString("<font color='#00B42A'>AI 附件审计: 拦截并备份了 [%1] 上传的 AI 附件 '%2'。</font>")
            .arg(name, fileName));
        }, Qt::QueuedConnection);
}