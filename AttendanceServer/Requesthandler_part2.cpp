#include "RequestHandler.h"
#include "AttendanceServer.h"
#include "CryptoHelper.h"
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
// SQL 字符串安全化
static QString S(const QString& val)
{
    QString safe = val;
    safe.replace("'", "''");
    return safe;
}
// 查询指定用户在数据库中的权限角色
static QString queryUserRole(QSqlDatabase& db, const QString& userName)
{
    if (userName.isEmpty()) return QString();
    QSqlQuery rq(db);
    rq.prepare("SELECT role FROM users WHERE name = :name");
    rq.bindValue(":name", userName);
    if (rq.exec() && rq.next()) return rq.value(0).toString().trimmed();
    return QString();
}
// 将 JSON 串写入 socket（紧凑格式并追加换行），用于跨线程安全发送
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
// 解码聊天内容（兼容 AES256 / B64 / 明文，保持与 part1 一致）
static QString decodeContent(const QString& raw)
{
    return CryptoHelper::safeDecrypt(raw);
}
// 对要入库的内容做编码处理（使用服务端统一的加密方法）
static QString encodeContent(const QString& content, const QString& /*type*/)
{
    return CryptoHelper::encryptContent(content);
}
// 处理并转发客户端发来的聊天消息（支持普通消息、图片及大文件分片的审计与转发）
void RequestHandler::handleChatMessage(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, const QByteArray& /*rawData*/, AttendanceServer* server)
{
    QString type = json["type"].toString();
    QString fromUser = json["from"].toString();
    // 若为大文件分片则走分片接收与审计逻辑
    if (type == "file_chunk" || type == "group_file_chunk") {
        int chunkIndex = json["chunk_index"].toInt();
        int totalChunks = json["total_chunks"].toInt();
        QString filename = json["filename"].toString();
        QByteArray chunkData = QByteArray::fromBase64(json["file_data"].toString().toUtf8());
        // 将分片写入服务端审计目录以便还原与留证
        QString serverDirPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/OfflineFiles/" + fromUser + "_audits";
        QDir dir; dir.mkpath(serverDirPath);
        QString localFilePath = serverDirPath + "/" + filename;
        QFile file(localFilePath);
        if (chunkIndex == 0 && file.exists()) file.remove();
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            file.write(chunkData);
            file.close();
        }
        if (type == "file_chunk") { // ── 单聊分片 ──
            // 单聊分片：实时转发在线用户或在最后一片时入离线队列
            QString toUser = json["to"].toString();
            QTcpSocket* targetSocket = server->getSocketByName(toUser);
            if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                sendJson(targetSocket, json); // 对方在线，实时穿透转发
                // 在线接收完成时记录审计日志
                if (chunkIndex == totalChunks - 1 && server) {
                    QMetaObject::invokeMethod(server, [server, filename]() {
                        server->logMessage(QString("<font color='#E6A23C'>[文件审计] 截获单聊在线大文件并归档: %1</font>").arg(filename));
                        }, Qt::QueuedConnection);
                }
            }
            else {
                // 若接收方离线，在最后一片时将元信息写入离线表并记录审计
                if (chunkIndex == totalChunks - 1) {
                    QSqlQuery offQ(db);
                    offQ.prepare("INSERT INTO offline_messages (sender, receiver, department, msg_type, content, filename, send_time) VALUES (:s, :r, '', 'offline_file_task', :c, :f, NOW())");
                    offQ.bindValue(":s", fromUser);
                    offQ.bindValue(":r", toUser);
                    offQ.bindValue(":c", localFilePath);
                    offQ.bindValue(":f", filename);
                    offQ.exec();
                    if (server) {
                        QMetaObject::invokeMethod(server, [server, filename]() {
                            server->logMessage(QString("<font color='#E6A23C'>[文件审计] 截获单聊离线大文件并暂存: %1</font>").arg(filename));
                            }, Qt::QueuedConnection);
                    }
                }
            }
            if (chunkIndex == 0) {
                QSqlQuery iq(db);
                iq.prepare("INSERT INTO chat_history (sender, receiver, msg_type, content, filename, send_time, is_group) VALUES (:s, :r, 'file_meta', :c, :f, NOW(), 0)");
                iq.bindValue(":s", fromUser); iq.bindValue(":r", toUser);
                iq.bindValue(":c", "[系统大文件记录]: " + filename);
                iq.bindValue(":f", filename);
                iq.exec();
            }
        }
        else if (type == "group_file_chunk") { 
            // 群聊分片：遍历部门成员进行在线穿透并为离线成员入离线表
            QString targetDept = json["department"].toString();
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
            bool hasOffline = false;
            QList<QString> offlineMembers;
            while (qMembers.next()) {
                QString member = qMembers.value(0).toString();
                QTcpSocket* targetSocket = server->getSocketByName(member);
                if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
                    sendJson(targetSocket, json);
                }
                else {
                    hasOffline = true;
                    offlineMembers.append(member);
                }
            }
            // 在最后一片时为离线成员写入离线任务并记录审计日志
            if (chunkIndex == totalChunks - 1) {
                if (hasOffline) {
                    QSqlQuery offQ(db);
                    offQ.prepare("INSERT INTO offline_messages (sender, receiver, department, msg_type, content, filename, send_time) VALUES (:s, :r, :d, 'group_offline_file_task', :c, :f, NOW())");
                    for (const QString& offMem : offlineMembers) {
                        offQ.bindValue(":s", fromUser);
                        offQ.bindValue(":r", offMem);
                        offQ.bindValue(":d", targetDept);
                        offQ.bindValue(":c", localFilePath);
                        offQ.bindValue(":f", filename);
                        offQ.exec();
                    }
                }
                if (server) {
                    QMetaObject::invokeMethod(server, [server, filename]() {
                        server->logMessage(QString("<font color='#E6A23C'>[文件审计] 截获群组大文件并归档: %1</font>").arg(filename));
                        }, Qt::QueuedConnection);
                }
            }
            // 文件元信息写入历史记录表（仅第一次分片时写入）
            if (chunkIndex == 0) {
                QSqlQuery iq(db);
                iq.prepare("INSERT INTO chat_history (sender, receiver, msg_type, content, filename, send_time, is_group) VALUES (:s, :r, 'file_meta', :c, :f, NOW(), 1)");
                iq.bindValue(":s", fromUser); iq.bindValue(":r", targetDept);
                iq.bindValue(":c", "[系统群大文件记录]: " + filename);
                iq.bindValue(":f", filename);
                iq.exec();
            }
        }
        return; 
    }
    //审计与入库逻辑（仅处理纯文本、表情、小图）
    QString content = json["msg"].toString();
    QString filename = json["filename"].toString();
    QString msgId = json["msg_id"].toString();
    if (msgId.isEmpty()) msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // 将文本与附件按照账号目录写入审计目录，便于审计与回溯
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
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/server/ChatFiles";
    QString baseDir = QDir::cleanPath(rawPath) + "/";
    QString folderName = QString("%1_%2_%3_%4").arg(account, fromUser, senderDept, title);
    QDir dir;
    if (!dir.exists(baseDir + folderName)) {
        dir.mkpath(baseDir + folderName);
    }
    // 审计分流：附件保存为文件，文本追加到聊天记录文本文件
    if (type == "image" || type == "group_image" || type == "file" || type == "group_file") {
        if (filename.isEmpty()) filename = type.contains("image") ? "pasted_image.png" : "unknown_file.dat";
        QString saveFileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_") + filename;
        QString filePath = baseDir + folderName + "/" + saveFileName;
        QByteArray fileData = QByteArray::fromBase64(content.toUtf8());
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(fileData);
            file.close();
            if (server) {
                QMetaObject::invokeMethod(server, [server, saveFileName]() {
                    server->logMessage(QString("<font color='#E6A23C'>[文件审计] 截获聊天附件: %1</font>").arg(saveFileName));
                    }, Qt::QueuedConnection);
            }
        }
    }
    else if (type == "chat" || type == "group_chat") {
        QString logFilePath = baseDir + folderName + "/chat_records.txt";
        QFile logFile(logFilePath);
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&logFile);
            QString target = (type == "group_chat") ? json["department"].toString() : json["to"].toString();
            QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            out << "[" << timeStr << "] 发送给 [" << target << "]: " << content << "\n";
            logFile.close();
        }
    }
    // 数据库存档与消息转发：文本/媒体入历史表并实时或离线转发给目标
    QString historyContent = content;
    if (type.contains("image") || type.contains("file")) {
        historyContent = "[媒体文件已被系统归档]"; // 存入极简文本，彻底释放数据库压力！
    }
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
        // 历史记录表：只存极简占位符
        insertQ.bindValue(":c", encodeContent(historyContent, type));
        insertQ.bindValue(":f", filename);
        if (!insertQ.exec() && server) {
            QMetaObject::invokeMethod(server, [server, err = insertQ.lastError().text()]() {
                server->logMessage(QString("<font color='red'>[数据库异常] 单聊入库失败: %1</font>").arg(err));
                }, Qt::QueuedConnection);
        }
        QTcpSocket* targetSocket = server->getSocketByName(toUser);
        if (targetSocket && targetSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject forwardJson = json;
            forwardJson["is_offline"] = false;
            sendJson(targetSocket, forwardJson); // 实时转发依然包含真实 content
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
            // 离线信箱表：必须存真实的 content，保证对方上线能收到文件！
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
        // 群聊历史记录表：同样只存极简占位符
        insertQ.bindValue(":c", encodeContent(historyContent, type));
        insertQ.bindValue(":f", filename);
        insertQ.exec();
        QSqlQuery qMembers(db);
        if (targetDept == "公司总群") qMembers.prepare("SELECT name FROM users WHERE name != :n AND status != '离线'");
        else { qMembers.prepare("SELECT name FROM users WHERE department = :d AND name != :n"); qMembers.bindValue(":d", targetDept); }
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
                offQ.prepare("INSERT INTO offline_messages (sender, receiver, department, msg_type, content, filename, send_time) VALUES (:s, :r, :d, :mt, :c, :f, NOW())");
                offQ.bindValue(":s", fromUser); offQ.bindValue(":r", member); offQ.bindValue(":d", targetDept);
                offQ.bindValue(":mt", type);
                // 离线信箱表：存真实的 content
                offQ.bindValue(":c", encodeContent(content, type));
                offQ.bindValue(":f", filename);
                offQ.exec();
            }
        }
        if (server) QMetaObject::invokeMethod(server, [server, pushCount]() { server->logMessage(QString("   └─ 成功推送到 %1 名在线员工。").arg(pushCount)); }, Qt::QueuedConnection);
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
            QString msgType = q.value(1).toString();
            o["msg_type"] = msgType;
            // 防止 JSON 数组累加超过 4MB 导致截断和客户端死锁！
            if (msgType == "file" || msgType == "group_file") {
                o["content"] = "[系统附件]"; 
            }
            else {
                // 图片经过压缩很小，或者纯文字，允许正常返回
                o["content"] = decodeContent(q.value(2).toString());
            }
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
    // 查询请求者自身的身份信息
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
    // 根据身份查询部门列表
    QJsonArray deptArr;
    QString deptSql = (myDept == "总经办")
        ? "SELECT DISTINCT department FROM users WHERE department != '' AND department IS NOT NULL"
        : QString("SELECT DISTINCT department FROM users WHERE department = '%1'").arg(myDept);
    QSqlQuery dQ(db);
    dQ.exec(deptSql);
    while (dQ.next()) deptArr.append(dQ.value(0).toString());
    dQ.finish();
    res["departments"] = deptArr;
    //查询除自己和管理员之外的所有用户列表
    QJsonArray userArr;
    QSqlQuery uQ(db);
    uQ.exec(QString("SELECT id, name, department, role, job_title, avatar FROM users "
        "WHERE name != '%1' AND account NOT LIKE '%%admin%%' AND name NOT LIKE '%%超级管理员%%'").arg(safeName));
    while (uQ.next()) {
        QJsonObject u;
        u["id"] = uQ.value(0).toInt();
        u["name"] = uQ.value(1).toString().trimmed();
        u["department"] = uQ.value(2).toString().trimmed();
        u["role"] = uQ.value(3).toString().trimmed();
        u["job_title"] = uQ.value(4).toString().trimmed();
        // 智能读取硬盘物理头像，并转换为 Base64 传输
        QString avPath = uQ.value(5).toString();
        QString avatarBase64 = "";
        if (!avPath.isEmpty()) {
            if (avPath.startsWith("/9j/") || avPath.length() > 500) {
                // 如果数据库里存的已经是老版本的 Base64，直接使用
                avatarBase64 = avPath;
            }
            else {
                // 去服务端的硬盘里读图片
                QString fullPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceServer/" + avPath);
                QFile f(fullPath);
                if (f.open(QIODevice::ReadOnly)) {
                    avatarBase64 = QString(f.readAll().toBase64());
                    f.close();
                }
            }
        }
        u["avatar"] = avatarBase64; // 传给客户端的Base64 图片数据
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
    q.prepare("UPDATE chat_history SET is_read = 1 WHERE sender = :s AND receiver = :r AND (is_read = 0 OR is_read IS NULL)");
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
// [Issue #3 修复] 在服务端的系统公告板上发布持久化公告（需管理员权限）
void RequestHandler::handlePublishAnnouncement(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server)
{
    // 服务端二次鉴权：仅管理员可发布系统广播
    QString callerName = server->getClientName(socket);
    QString callerRole = queryUserRole(db, callerName);
    if (!callerRole.contains("管理员")) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "权限不足：仅管理员可发布系统广播";
        sendJson(socket, res); return;
    }
    QString publisher = json["publisher"].toString();
    QString content = json["content"].toString();
    QSqlQuery q(db);
    q.prepare("INSERT INTO system_announcements (publisher, content, publish_time) VALUES (?, ?, NOW())");
    q.addBindValue(publisher);
    q.addBindValue(content);
    q.exec();
}
// [Issue #17] 删除聊天消息（服务端同步删除数据库记录）
void RequestHandler::handleChatDeleteMessage(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString msgId = json["msg_id"].toString().trimmed();
    QString caller = json["name"].toString().trimmed();
    QJsonObject res;
    if (msgId.isEmpty() || caller.isEmpty()) {
        res["status"] = "fail";
        res["msg"] = "参数缺失";
        sendJson(socket, res);
        return;
    }
    // 仅允许删除自己发送的消息
    QSqlQuery q(db);
    q.prepare("DELETE FROM chat_messages WHERE msg_id = :mid AND sender = :s");
    q.bindValue(":mid", msgId);
    q.bindValue(":s", caller);
    if (q.exec() && q.numRowsAffected() > 0) {
        res["status"] = "success";
    }
    else {
        res["status"] = "fail";
        res["msg"] = "消息不存在或无权删除";
    }
    sendJson(socket, res);
}