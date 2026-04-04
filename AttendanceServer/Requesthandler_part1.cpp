#include "RequestHandler.h"
#include "AttendanceServer.h"
#include "TransactionGuard.h"
#include "CryptoHelper.h"
#include <QSqlRecord>
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
#include <QPointer>
#include <QThread>
#include <QUuid>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSettings>
// 发送 JSON 回包（线程安全）
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
// SQL 字符串安全化：将单引号转义为两个单引号，防止简单的 SQL 语法错误
static QString S(const QString& val)
{
    QString safe = val;
    safe.replace("'", "''");
    return safe;
}
// 查询指定用户在数据库中的权限角色，用于服务端二次鉴权
static QString queryUserRole(QSqlDatabase& db, const QString& userName)
{
    if (userName.isEmpty()) return QString();
    QSqlQuery rq(db);
    rq.prepare("SELECT role FROM users WHERE name = :name");
    rq.bindValue(":name", userName);
    if (rq.exec() && rq.next()) return rq.value(0).toString().trimmed();
    return QString();
}
// 聊天内容解码：自动兼容 AES256 / B64 / 明文，返回明文字符串
static QString decodeContent(const QString& raw)
{
    return CryptoHelper::safeDecrypt(raw);
}
// 聊天内容编码：使用服务端统一的加密方法（AES-256）
static QString encodeContent(const QString& content)
{
    return CryptoHelper::encryptContent(content);
}
// [Issue #2 修复] 仅返回当前用户自己的人脸特征，不再下发全量特征库
void RequestHandler::handleQueryFaceFeatures(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QJsonArray arr;
    QSqlQuery query(db);
    QString reqName = json["name"].toString().trimmed();
    if (reqName.isEmpty()) {
        // 未指定用户时返回空结果
        QJsonObject res;
        res["status"] = "success";
        res["data"] = arr;
        sendJson(socket, res);
        return;
    }
    // 仅查询请求用户自己的特征
    query.prepare("SELECT name, feature FROM users WHERE name = :n AND feature IS NOT NULL");
    query.bindValue(":n", reqName);
    if (query.exec()) {
        while (query.next()) {
            QJsonObject o;
            o["name"] = query.value(0).toString();
            // 将 BLOB 转为 Base64 便于 JSON 传输
            o["feature"] = QString(query.value(1).toByteArray().toBase64());
            arr.append(o);
        }
    }
    query.finish();
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}
// 注册（或更新）用户的人脸特征
void RequestHandler::handleRegisterFace(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QByteArray featureData = QByteArray::fromBase64(json["feature"].toString().toUtf8());
    // feature 是 BLOB 二进制数据，只能用 bindValue；name 用 WHERE 拼接以保持简单
    QSqlQuery q(db);
    q.prepare(QString("UPDATE users SET feature = :f WHERE name = '%1'").arg(S(name)));
    q.bindValue(":f", featureData);
    q.exec();
}
// 客户端账号/密码登录验证（服务端校验客户端选择的角色是否与数据库匹配，超级管理员禁止客户端登录）
void RequestHandler::handleClientLoginAuth(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString pwd = json["pwd"].toString();
    QString clientRole = json["role"].toString().trimmed();
    QJsonObject res;
    res["status"] = "fail";
    qDebug() << "[LoginAuth] account=" << account << "clientRole=" << clientRole;
    QSqlQuery q(db);
    // 查询账户的基本信息与密码哈希
    bool ok = q.exec(QString("SELECT name, feature, password, role FROM users WHERE account = '%1'").arg(S(account)));
    if (!ok) {
        qDebug() << "[LoginAuth] SQL执行失败:" << q.lastError().text();
        res["msg"] = "服务器内部错误";
        sendJson(socket, res);
        return;
    }
    if (!q.next()) {
        qDebug() << "[LoginAuth] FAILED, account not found:" << account;
        res["msg"] = "工号或密码错误";
        sendJson(socket, res);
        return;
    }
    QString dbRole = q.value("role").toString().trimmed();
    QString storedHash = q.value("password").toString();
    QString dbName = q.value("name").toString();
    QByteArray feat = q.value("feature").toByteArray();
    q.finish();
    // 超级管理员禁止通过客户端登录，只能使用服务端控制台
    if (dbRole == QStringLiteral("超级管理员")) {
        res["msg"] = QStringLiteral("超级管理员账号不允许通过客户端登录，请使用服务端控制台登录！");
        qDebug() << "[LoginAuth] BLOCKED, super admin cannot login via client:" << account;
        sendJson(socket, res);
        return;
    }
    // 验证密码哈希（支持旧版与新版哈希验证）
    if (!CryptoHelper::verifyPassword(pwd, storedHash)) {
        res["msg"] = QStringLiteral("工号或密码错误");
        qDebug() << "[LoginAuth] FAILED, password mismatch for account=" << account;
        sendJson(socket, res);
        return;
    }
    // 校验客户端选择的角色是否与数据库中的角色一致
    if (!clientRole.isEmpty() && clientRole != dbRole) {
        res["msg"] = QStringLiteral("您的账号角色为「") + dbRole +
            QStringLiteral("」，请选择正确的登录方式！");
        qDebug() << "[LoginAuth] FAILED, role mismatch: client=" << clientRole << "db=" << dbRole;
        sendJson(socket, res);
        return;
    }
    // 登录成功，返回服务端确认的角色
    res["status"] = "success";
    res["real_name"] = dbName;
    res["role"] = dbRole;
    res["has_face"] = !feat.isNull() && !feat.isEmpty();
    // 生成会话令牌，客户端后续请求需携带此令牌通过认证门
    QString sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    res["session_token"] = sessionToken;
    if (server) {
        server->addSessionToken(sessionToken, dbName);
    }
    qDebug() << "[LoginAuth] SUCCESS, name=" << dbName << "role=" << dbRole;
    // 若发现数据库中仍为旧格式哈希，则升级为 PBKDF2 存储
    if (!storedHash.startsWith("PBKDF2:")) {
        QString upgradedHash = CryptoHelper::hashPassword(pwd);
        QSqlQuery upgradeQ(db);
        upgradeQ.exec(QString("UPDATE users SET password = '%1' WHERE account = '%2'")
            .arg(S(upgradedHash), S(account)));
        if (upgradeQ.numRowsAffected() > 0) {
            qDebug() << "[LoginAuth] 密码已自动升级为 PBKDF2 格式";
        }
    }
    sendJson(socket, res);
}
// 客户端注册新账号：服务端校验 + 事务保护 + 参数化写入
void RequestHandler::handleClientRegisterAccount(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString().trimmed();
    QString pwd = json["pwd"].toString().trimmed();
    QString name = json["name"].toString().trimmed();
    QString dept = json["dept"].toString().trimmed();
    QString jobTitle = json["job_title"].toString().trimmed();
    QString phone = json["phone"].toString().trimmed();
    QString gender = json["gender"].toString().trimmed();

    QJsonObject res;
    res["status"] = "fail";

    // 服务端输入校验：防止绕过客户端直接提交非法数据
    if (account.isEmpty() || name.isEmpty() || pwd.isEmpty()) {
        res["msg"] = "工号、姓名、密码为必填项";
        sendJson(socket, res);
        return;
    }
    QRegularExpression reChinese("[\\x{4e00}-\\x{9fa5}]");
    if (reChinese.match(account).hasMatch()) {
        res["msg"] = "工号不允许包含中文字符";
        sendJson(socket, res);
        return;
    }
    if (account.length() > 50 || name.length() > 50) {
        res["msg"] = "工号或姓名长度超限";
        sendJson(socket, res);
        return;
    }
    // 密码哈希长度校验：客户端发来的应为64位SHA-256十六进制
    if (pwd.length() != 64) {
        res["msg"] = "密码格式异常";
        sendJson(socket, res);
        return;
    }
    if (!phone.isEmpty() && phone != "未设置") {
        QRegularExpression rePhone("^\\d{11}$");
        if (!rePhone.match(phone).hasMatch()) {
            res["msg"] = "手机号码格式不正确";
            sendJson(socket, res);
            return;
        }
    }
    // 性别白名单校验
    if (gender != "男" && gender != "女") {
        res["msg"] = "性别参数非法";
        sendJson(socket, res);
        return;
    }
    // 部门白名单校验
    static const QStringList validDepts = {
        "总经办", "人力资源部", "财务部", "销售部", "研发部", "市场部", "客户服务部"
    };
    if (!validDepts.contains(dept)) {
        res["msg"] = "部门参数非法";
        sendJson(socket, res);
        return;
    }
    // 部门-职务合法性校验：防止客户端伪造非法职务组合
    static const QMap<QString, QStringList> validJobTitles = {
        {"总经办", {"总经理", "行政助理", "财务总监"}},
        {"人力资源部", {"部门经理", "招聘专员", "员工关系专员", "培训与发展专员"}},
        {"财务部", {"部门经理", "财务分析师", "会计", "出纳"}},
        {"销售部", {"部门经理", "区域销售经理", "销售代表", "客户服务代表"}},
        {"研发部", {"部门经理", "软件工程师", "测试工程师", "产品经理"}},
        {"市场部", {"部门经理", "市场营销专员", "品牌管理专员", "公关专员"}},
        {"客户服务部", {"部门经理", "客户服务代表", "技术支持"}}
    };
    if (validJobTitles.contains(dept) && !validJobTitles[dept].contains(jobTitle)) {
        res["msg"] = "该部门下不存在此职务";
        sendJson(socket, res);
        return;
    }

    // 强制使用普通权限，忽略客户端传入的 role 字段
    QString role = "普通登录";
    // 使用 PBKDF2 对客户端哈希值再次加固存储
    QString hashedPwd = CryptoHelper::hashPassword(pwd);

    // 事务保护：先查重再插入，防止并发注册导致重复数据
    TransactionGuard txn(db);
    if (!txn.isActive()) {
        res["msg"] = "服务器繁忙，请稍后重试";
        sendJson(socket, res);
        return;
    }

    // 使用参数化查询检查工号是否已存在
    QSqlQuery checkQ(db);
    checkQ.prepare("SELECT COUNT(*) FROM users WHERE account = :acc");
    checkQ.bindValue(":acc", account);
    if (!checkQ.exec() || !checkQ.next() || checkQ.value(0).toInt() > 0) {
        res["msg"] = "该工号已被注册";
        sendJson(socket, res);
        return;
    }
    checkQ.finish();

    // 使用参数化查询插入新用户，杜绝 SQL 注入
    QSqlQuery q(db);
    q.prepare("INSERT INTO users (account, password, name, role, department, job_title, phone, gender) "
              "VALUES (:account, :pwd, :name, :role, :dept, :job_title, :phone, :gender)");
    q.bindValue(":account", account);
    q.bindValue(":pwd", hashedPwd);
    q.bindValue(":name", name);
    q.bindValue(":role", role);
    q.bindValue(":dept", dept);
    q.bindValue(":job_title", jobTitle);
    q.bindValue(":phone", phone);
    q.bindValue(":gender", gender);

    if (q.exec() && txn.commit()) {
        res["status"] = "success";
        QMetaObject::invokeMethod(server, [server, name, role]() {
            server->logMessage(QString("<font color='#E6A23C'>新入职: [%1] 注册了账号，权限为 [%2]。</font>").arg(name, role));
            server->refreshPermModel();
            }, Qt::QueuedConnection);
    }
    else {
        res["msg"] = "注册失败，请稍后重试";
    }
    sendJson(socket, res);
}
// 注册前验证用户是否在指定部门已存在（避免重复录入）
void RequestHandler::handleVerifyUserForRegistration(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString dept = json["dept"].toString().trimmed();
    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    // 若查询到记录则返回 success
    if (q.exec(QString("SELECT id FROM users WHERE name = '%1' AND department = '%2'").arg(S(name), S(dept))) && q.next()) {
        res["status"] = "success";
    }
    sendJson(socket, res);
}
//  登录 / 在线状态
// 处理客户端登录后的初始化工作，验证用户存在后注册在线
void RequestHandler::handleLogin(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString name = json["name"].toString().trimmed();
    if (name.isEmpty()) {
        QJsonObject res;
        res["type"] = "login_reject";
        res["msg"] = "用户名不能为空";
        sendJson(socket, res);
        return;
    }
    QString ip = socket->peerAddress().toString().remove("::ffff:");
    QString dept = "未知部门", jobTitle = "未分配";
    QSqlQuery query(db);
    // 查询用户是否存在于数据库
    query.prepare("SELECT department, job_title FROM users WHERE name = :name");
    query.bindValue(":name", name);
    query.exec();
    if (query.next()) {
        dept = query.value(0).toString();
        jobTitle = query.value(1).toString();
        if (dept.isEmpty()) dept = "未分配部门";
        if (jobTitle.isEmpty()) jobTitle = "未分配";
    }
    else {
        // 数据库中无此用户，拒绝接入
        query.finish();
        QJsonObject res;
        res["type"] = "login_reject";
        res["msg"] = "用户不存在，拒绝接入";
        sendJson(socket, res);
        QMetaObject::invokeMethod(server, [server, name, ip]() {
            server->logMessage(QString("<font color='#F53F3F'>拒绝接入: [%1] (IP: %2) 不存在于数据库。</font>").arg(name, ip));
            }, Qt::QueuedConnection);
        return;
    }
    query.finish();
    // 在主线程注册客户端到在线管理
    QMetaObject::invokeMethod(server, [server, socket, name, dept, jobTitle, ip]() {
        server->registerClient(socket, name, dept, jobTitle, ip);
        }, Qt::QueuedConnection);
    // 检查并补发离线消息，从 offline_messages 表按时间升序读取
    QSqlQuery offlineQ(db);
    offlineQ.exec(QString("SELECT sender, msg_type, content, filename, send_time, department "
        "FROM offline_messages WHERE receiver = '%1' ORDER BY send_time ASC").arg(S(name)));
    int offlineCount = 0;
    while (offlineQ.next()) {
        QString sender = offlineQ.value(0).toString();
        QString mType = offlineQ.value(1).toString();
        QString content = decodeContent(offlineQ.value(2).toString());
        QString filename = offlineQ.value(3).toString();
        QString timeStr = offlineQ.value(4).toDateTime().toString("HH:mm:ss");
        QString msgDept = offlineQ.value(5).toString();
        // 处理离线大文件：读取服务器暂存文件并分片推送给客户端
        if (mType == "offline_file_task" || mType == "group_offline_file_task") {
            QString filePath = offlineQ.value(2).toString();
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly)) {
                const qint64 CHUNK_SIZE = 1024 * 256; // 每片 256KB
                int totalChunks = (file.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
                int chunkIndex = 0;
                while (!file.atEnd()) {
                    QByteArray chunk = file.read(CHUNK_SIZE);
                    QJsonObject pushJson;
                    // 根据类型决定是群文件还是点对点文件分片
                    pushJson["type"] = (mType == "group_offline_file_task") ? "group_file_chunk" : "file_chunk";
                    pushJson["from"] = sender;
                    if (mType == "group_offline_file_task") pushJson["department"] = msgDept;
                    else pushJson["to"] = name;
                    pushJson["filename"] = filename;
                    pushJson["chunk_index"] = chunkIndex++;
                    pushJson["total_chunks"] = totalChunks;
                    pushJson["file_data"] = QString(chunk.toBase64());
                    QByteArray outData = QJsonDocument(pushJson).toJson(QJsonDocument::Compact) + "\n";
                    // 同步写入 socket，必要时短等待以避免缓冲区过大
                    socket->write(outData);
                    if (socket->bytesToWrite() > 1024 * 1024) socket->waitForBytesWritten(100);
                }
                file.close();
            }
            offlineCount++;
            continue;
        }
        // 普通消息 构造回传的 JSON 并写入 socket
        QJsonObject offMsg;
        offMsg["from"] = sender;
        offMsg["type"] = mType;
        offMsg["msg"] = content;
        offMsg["filename"] = filename;
        offMsg["time"] = timeStr;
        offMsg["department"] = msgDept;
        offMsg["is_offline"] = true;
        QByteArray outData = QJsonDocument(offMsg).toJson(QJsonDocument::Compact) + "\n";
        socket->write(outData);
        if (socket->bytesToWrite() > 1024 * 1024) socket->waitForBytesWritten(100);
        offlineCount++;
    }
    offlineQ.finish();
    // 如果有补发记录则在服务端日志中记录
    if (offlineCount > 0) {
        QMetaObject::invokeMethod(server, [server, name, offlineCount]() {
            server->logMessage(QString("<font color='#E6A23C'>已向 [%1] 补发 %2 条离线消息/文件。</font>")
                .arg(name).arg(offlineCount));
            }, Qt::QueuedConnection);
    }
    // 补发完成后删除离线消息
    QSqlQuery deleteOffQ(db);
    deleteOffQ.exec(QString("DELETE FROM offline_messages WHERE receiver = '%1'").arg(S(name)));
}
// 更新用户的当前在线状态图标/文字 将客户端上报的 status 写入 users 表的 status_icon 字段
void RequestHandler::handleStatusUpdate(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString status = json["status"].toString();
    QString name = json["name"].toString();
    QSqlQuery q(db);
    // 直接更新数据库中的状态显示字段
    q.exec(QString("UPDATE users SET status_icon = '%1' WHERE name = '%2'").arg(S(status), S(name)));
}
//  用户档案
// 查询用户个人资料并返回给客户端 支持返回基础字段以及将服务器端存储的头像文件读取为 Base64
void RequestHandler::handleQueryUserProfile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    // 查询用户详细信息（允许通过 name 或 account 查询）
    QString sql = QString("SELECT id, job_title, role, department, gender, phone, name, avatar "
        "FROM users WHERE name = '%1' OR account = '%1'").arg(S(name));
    if (!q.exec(sql)) {
        // 查询异常返回错误信息
        res["msg"] = "数据库查询异常: " + q.lastError().text();
    }
    else if (q.next()) {
        // 组装成功响应
        res["status"] = "success";
        res["id"] = q.value(0).toInt();
        res["job_title"] = q.value(1).toString();
        res["role"] = q.value(2).toString();
        res["department"] = q.value(3).toString();
        res["gender"] = q.value(4).toString();
        res["phone"] = q.value(5).toString();
        res["real_name"] = q.value(6).toString();
        QString av = q.value(7).toString();
        // 如果 avatar 字段为相对路径则读取文件并返回 Base64，否则直接返回字段内容
        if (av.contains("/") && !av.startsWith("/9j/")) {
            QString fullPath = QCoreApplication::applicationDirPath() + "/" + av;
            QFile avatarFile(fullPath);
            if (avatarFile.open(QIODevice::ReadOnly)) {
                QByteArray fileData = avatarFile.readAll();
                avatarFile.close();
                res["avatar_base64"] = QString(fileData.toBase64());
            }
            else {
                res["avatar_base64"] = ""; // 无法读取则返回空字符串
            }
            res["avatar_path"] = av; // 返回原始路径给客户端
        }
        else {
            // avatar 字段已是 Base64 或空，直接透传
            res["avatar_base64"] = av;
            res["avatar_path"] = "";
        }
    }
    else {
        // 未找到用户
        res["msg"] = "花名册中找不到该用户信息";
    }
    q.finish();
    sendJson(socket, res);
}
// 修改用户单个资料字段（如性别、电话、头像） 仅允许预定义字段修改，防止任意列被改写
void RequestHandler::handleUpdateProfileField(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString field = json["field"].toString().trimmed();
    QString value = json["value"].toString();
    QJsonObject res;
    res["status"] = "fail";
    QString dbColumn;
    // 白名单字段映射
    if (field == "gender") dbColumn = "gender";
    else if (field == "phone") dbColumn = "phone";
    else if (field == "avatar") dbColumn = "avatar";
    else if (field == "real_name") dbColumn = "name";
    else {
        res["msg"] = "不支持修改该字段: " + field;
        sendJson(socket, res);
        return;
    }
    QSqlQuery q(db);
    // 执行更新并根据受影响行数判断是否成功
    QString sql = QString("UPDATE users SET %1 = '%2' WHERE name = '%3' OR account = '%3'")
        .arg(dbColumn, S(value), S(name));
    if (q.exec(sql)) {
        if (q.numRowsAffected() > 0) res["status"] = "success";
        else res["msg"] = "未找到该账号，无法更新";
    }
    else {
        res["msg"] = "数据库更新失败: " + q.lastError().text();
    }
    sendJson(socket, res);
}
// 按部门和关键字查询花名册（需服务端权限校验）
void RequestHandler::handleQueryUserList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server)
{
    // 服务端二次鉴权：仅管理员或人资部门经理可查询花名册
    QString callerName = server->getClientName(socket);
    QString callerRole = queryUserRole(db, callerName);
    if (!callerRole.contains("管理员")) {
        // 进一步检查是否为人资部门经理
        QSqlQuery dq(db);
        dq.prepare("SELECT department, job_title FROM users WHERE name = :n");
        dq.bindValue(":n", callerName);
        bool isHRManager = false;
        if (dq.exec() && dq.next()) {
            isHRManager = (dq.value(0).toString() == "人力资源部" && dq.value(1).toString() == "部门经理");
        }
        if (!isHRManager) {
            QJsonObject res; res["status"] = "fail"; res["msg"] = "权限不足：仅管理员或人资经理可查看员工列表";
            sendJson(socket, res); return;
        }
    }
    QString dept = json["dept"].toString();
    QString keyword = json["keyword"].toString();
    QString sql = "SELECT id, account, name, gender, department, job_title, phone "
        "FROM view_users_lite "
        "WHERE account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'";
    // 根据部门和关键字追加过滤条件
    if (dept != "全部" && !dept.isEmpty())
        sql += QString(" AND department = '%1'").arg(S(dept));
    if (!keyword.isEmpty())
        sql += QString(" AND (name LIKE '%%%1%%' OR id LIKE '%%%1%%')").arg(S(keyword));
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
    q.finish();
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}
// 根据用户名或账号返回用户所属的部门与职务信息 用于客户端在 UI 上显示或做权限判断
void RequestHandler::handleQueryUserDept(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QString dept, jobTitle, realName = name;
    QSqlQuery dq(db);
    dq.exec(QString("SELECT department, job_title, name FROM users WHERE name = '%1' OR account = '%1'").arg(S(name)));
    if (dq.next()) {
        dept = dq.value("department").toString();
        jobTitle = dq.value("job_title").toString();
        realName = dq.value("name").toString();
    }
    dq.finish();
    qDebug() << "[QueryUserDept] input=" << name << "dept=" << dept << "jobTitle=" << jobTitle;
    QJsonObject res;
    res["type"] = "user_dept_reply";
    res["department"] = dept;
    res["job_title"] = jobTitle;
    res["real_name"] = realName;
    sendJson(socket, res);
}
//  管理员操作
// 超级管理员强制重置用户密码为默认值（安全链路与客户端登录一致）
// 将默认密码 "123456" 的 SHA256 再做 PBKDF2 哈希后存储
void RequestHandler::handleAdminResetPassword(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    // 服务端二次鉴权：校验操作者是否具有管理员权限
    QString callerName = server->getClientName(socket);
    QString callerRole = queryUserRole(db, callerName);
    if (!callerRole.contains("管理员")) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "权限不足：仅管理员可执行此操作";
        sendJson(socket, res); return;
    }
    QString account = json["account"].toString();
    QString empName = json["name"].toString();
    // [Issue #6 修复] 从配置文件读取默认重置密码，避免硬编码
    QSettings serverCfg(QCoreApplication::applicationDirPath() + "/server.ini", QSettings::IniFormat);
    QString defaultPwd = serverCfg.value("Security/default_reset_password", "Abc@1234").toString();
    QString sha256OfDefault = QString(QCryptographicHash::hash(
        defaultPwd.toUtf8(), QCryptographicHash::Sha256).toHex());
    QString hashedPwd = CryptoHelper::hashPassword(sha256OfDefault);
    QSqlQuery q(db);
    QJsonObject res;
    if (q.exec(QString("UPDATE users SET password = '%1' WHERE account = '%2'").arg(S(hashedPwd), S(account)))) {
        res["status"] = "success";
        // 记录管理员操作日志并异步刷新权限模型
        QMetaObject::invokeMethod(server, [server, empName]() {
            server->logMessage(QString("<font color='#E6A23C'>权限操作: 管理员已重置 [%1] 的登录密码。</font>").arg(empName));
            }, Qt::QueuedConnection);
    }
    else {
        res["status"] = "fail";
    }
    sendJson(socket, res);
}
// 超级管理员强制删除用户（含其考勤记录），在事务中执行以保证原子性
void RequestHandler::handleAdminDeleteUser(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    // 服务端二次鉴权：校验操作者是否具有管理员权限
    QString callerName = server->getClientName(socket);
    QString callerRole = queryUserRole(db, callerName);
    if (!callerRole.contains("管理员")) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "权限不足：仅管理员可执行此操作";
        sendJson(socket, res); return;
    }
    QString account = json["account"].toString();
    QString empName = json["name"].toString();
    // 使用事务守卫，确保中间任意失败时自动回滚
    TransactionGuard txn(db);
    if (!txn.isActive()) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "事务开启失败";
        sendJson(socket, res); return;
    }
    // 删除用户的考勤记录
    QSqlQuery delRecords(db);
    if (!delRecords.exec(QString("DELETE FROM attendance_records WHERE name = '%1'").arg(S(empName)))) {
        QJsonObject res; res["status"] = "fail";
        res["msg"] = "删除考勤记录失败，事务已回滚：" + delRecords.lastError().text();
        sendJson(socket, res); return;
    }
    // 删除用户主表记录
    QSqlQuery delUser(db);
    if (!delUser.exec(QString("DELETE FROM users WHERE account = '%1'").arg(S(account)))) {
        QJsonObject res; res["status"] = "fail";
        res["msg"] = "删除用户失败，事务已回滚：" + delUser.lastError().text();
        sendJson(socket, res); return;
    }
    // 提交事务
    if (!txn.commit()) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "事务提交失败，已回滚。";
        sendJson(socket, res); return;
    }
    QJsonObject res; res["status"] = "success";
    sendJson(socket, res);
    // 记录高危操作日志并刷新权限模型
    QMetaObject::invokeMethod(server, [server, empName]() {
        server->logMessage(QString("<font color='red'>高危操作: 管理员已将员工 [%1] 彻底删除！</font>").arg(empName));
        server->refreshPermModel();
        }, Qt::QueuedConnection);
}
// 超级管理员手动修改某条考勤记录的状态 变更后在服务端记录日志并重新加载全局考勤记录缓存
void RequestHandler::handleAdminModifyStatus(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    // 服务端二次鉴权：校验操作者是否具有管理员权限
    QString callerName = server->getClientName(socket);
    QString callerRole = queryUserRole(db, callerName);
    if (!callerRole.contains("管理员")) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "权限不足：仅管理员可执行此操作";
        sendJson(socket, res); return;
    }
    int recordId = json["record_id"].toInt();
    QString newStatus = json["new_status"].toString();
    QSqlQuery q(db);
    // 直接更新记录状态，成功后异步记录日志和刷新缓存
    if (q.exec(QString("UPDATE attendance_records SET status = '%1' WHERE id = %2").arg(S(newStatus)).arg(recordId))) {
        QMetaObject::invokeMethod(server, [server]() {
            server->logMessage("<font color='#E6A23C'>管理员后台修改了某条考勤记录状态。</font>");
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}
//  修改密码（密码链路统一修复 — 客户端已发送 SHA256 哈希）
void RequestHandler::handleVerifyAndUpdatePassword(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString oldPwdHash = json["old_pwd"].toString();   // 客户端发来的是 SHA256 哈希
    QString newPwdHash = json["new_pwd"].toString();   // 客户端发来的是 SHA256 哈希
    QJsonObject res;
    res["status"] = "fail";

    if (name.isEmpty() || oldPwdHash.isEmpty() || newPwdHash.isEmpty()) {
        res["msg"] = "参数不完整"; sendJson(socket, res); return;
    }
    // 校验哈希格式：SHA256 应为64位十六进制
    if (oldPwdHash.length() != 64 || newPwdHash.length() != 64) {
        res["msg"] = "密码格式异常"; sendJson(socket, res); return;
    }
    if (newPwdHash == oldPwdHash) {
        res["msg"] = "新密码不能与旧密码相同"; sendJson(socket, res); return;
    }
    // 从数据库查出当前密码哈希
    QSqlQuery q(db);
    q.exec(QString("SELECT password FROM users WHERE name = '%1'").arg(S(name)));
    if (!q.next()) {
        res["msg"] = "用户不存在"; sendJson(socket, res); return;
    }
    QString stored = q.value(0).toString();
    q.finish();
    // 验证旧密码：客户端发的是 SHA256(明文)，数据库存储的是 PBKDF2(SHA256(明文))
    if (!CryptoHelper::verifyPassword(oldPwdHash, stored)) {
        res["msg"] = "旧密码错误"; sendJson(socket, res); return;
    }
    // 新密码存储：PBKDF2(SHA256(明文))
    QString newHash = CryptoHelper::hashPassword(newPwdHash);

    QSqlQuery uq(db);
    if (uq.exec(QString("UPDATE users SET password = '%1' WHERE name = '%2'").arg(S(newHash), S(name)))) {
        res["status"] = "success";
        res["msg"] = "密码修改成功";
    }
    else {
        res["msg"] = "数据库更新失败";
    }
    sendJson(socket, res);
}
// 提交人脸重录审批申请（写入 appeals 表）
void RequestHandler::handleFaceReregisterRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString applicant = json["applicant"].toString().trimmed();
    QString reason = json["reason"].toString().trimmed();
    QString approver = json["approver"].toString().trimmed();
    QJsonObject res; res["status"] = "fail";
    if (applicant.isEmpty() || reason.isEmpty() || approver.isEmpty()) {
        res["msg"] = "参数不完整"; sendJson(socket, res); return;
    }
    QSqlQuery q(db);
    bool ok = q.exec(QString("INSERT INTO appeals (applicant, abnormal_time, original_status, reason, approver, status) "
        "VALUES ('%1', NOW(), '人脸重录', '%2', '%3', '待审批')")
        .arg(S(applicant), S(reason), S(approver)));
    if (ok) { res["status"] = "success"; res["msg"] = "人脸重录申请已提交"; }
    else { res["msg"] = "数据库写入失败: " + q.lastError().text(); }
    sendJson(socket, res);
}
// 接收 Base64 字符串，写入服务器 avatars 目录并更新 users.avatar 字段为相对路径
void RequestHandler::handleUploadAvatarFile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString avatarData = json["avatar_data"].toString();
    QJsonObject res; res["status"] = "fail";
    if (name.isEmpty() || avatarData.isEmpty()) { res["msg"] = "参数不完整"; sendJson(socket, res); return; }
    // 1. 查询当前用户的 account 以及旧的头像路径
    QSqlQuery q(db);
    q.exec(QString("SELECT id, account, avatar FROM users WHERE name = '%1'").arg(S(name)));
    QString account = "unknown";
    QString oldAvatarPath = "";
    if (q.next()) {
        account = q.value(1).toString();
        oldAvatarPath = q.value(2).toString();
    }
    q.finish();
    // 2. 使用相对路径定位到服务端的 avatars 目录
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/avatars/" + name;
    QString baseDir = QDir::cleanPath(rawPath) + "/";
    QDir dir;
    if (!dir.exists(baseDir)) dir.mkpath(baseDir);
    // 3. 将旧头像重命名备份，避免覆盖历史文件
    if (!oldAvatarPath.isEmpty()) {
        QString oldFullPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceServer/" + oldAvatarPath);
        QFile oldFile(oldFullPath);
        if (oldFile.exists()) {
            // 生成随机码并在文件名中加入，用作备份
            QString randomCode = QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + QString::number(QRandomGenerator::global()->bounded(1000, 9999));
            QString backupPath = oldFullPath;
            backupPath.replace(".jpg", "_" + randomCode + ".jpg");
            oldFile.rename(backupPath);
        }
    }
    // 4. 新头像使用统一命名规则 account_name.jpg
    QString fileName = QString("%1_%2.jpg").arg(account, name);
    QString fullPath = baseDir + fileName;
    QString relativePath = QString("avatars/%1/%2").arg(name, fileName);
    // 5. 写入文件并更新数据库记录
    QByteArray imgBytes = QByteArray::fromBase64(avatarData.toUtf8());
    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(imgBytes);
        file.close();
        QSqlQuery uq(db);
        if (uq.exec(QString("UPDATE users SET avatar = '%1' WHERE name = '%2'").arg(S(relativePath), S(name)))) {
            res["status"] = "success";
            res["msg"] = "头像已保存: " + relativePath;
        }
        else {
            res["msg"] = "数据库路径更新失败: " + uq.lastError().text();
        }
    }
    else {
        res["msg"] = "文件写入失败: " + file.errorString();
    }
    sendJson(socket, res);
}
// 根据相对路径读取头像文件并返回 Base64 编码,不修改数据库，仅作为文件读取辅助接口
void RequestHandler::handleQueryAvatarFile(QSqlDatabase& /*db*/, QTcpSocket* socket, const QJsonObject& json)
{
    QString avatarPath = json["avatar_path"].toString();
    QJsonObject res; res["status"] = "fail";
    if (avatarPath.isEmpty()) { res["msg"] = "路径为空"; sendJson(socket, res); return; }
    // 使用应用程序目录与相对路径拼接得到绝对路径
    QString rawPath = QCoreApplication::applicationDirPath() + "/../../AttendanceServer/" + avatarPath;
    QString fullPath = QDir::cleanPath(rawPath);
    QFile file(fullPath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();
        res["status"] = "success";
        res["avatar_data"] = QString(data.toBase64());
    }
    else {
        res["msg"] = "头像文件不存在: " + fullPath;
    }
    sendJson(socket, res);
}
// 查询系统中所有部门名称不包含空部门和超级管理员
void RequestHandler::handleQueryDeptList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& /*json*/)
{
    QJsonArray arr;
    QSqlQuery q(db);
    q.exec("SELECT DISTINCT department FROM users WHERE department IS NOT NULL AND department != '' AND role != '超级管理员' ORDER BY department");
    while (q.next()) arr.append(q.value(0).toString());
    q.finish();
    QJsonObject res;
    res["status"] = "success";
    res["departments"] = arr;
    sendJson(socket, res);
}
// 查询部门-职务映射表，供客户端动态填充注册/录入等下拉框
void RequestHandler::handleQueryDeptJobList(QSqlDatabase& /*db*/, QTcpSocket* socket, const QJsonObject& /*json*/)
{
    // 与服务端注册校验白名单保持一致的权威数据源
    static const QMap<QString, QStringList> deptJobMap = {
        {"总经办", {"总经理", "行政助理", "财务总监"}},
        {"人力资源部", {"部门经理", "招聘专员", "员工关系专员", "培训与发展专员"}},
        {"财务部", {"部门经理", "财务分析师", "会计", "出纳"}},
        {"销售部", {"部门经理", "区域销售经理", "销售代表", "客户服务代表"}},
        {"研发部", {"部门经理", "软件工程师", "测试工程师", "产品经理"}},
        {"市场部", {"部门经理", "市场营销专员", "品牌管理专员", "公关专员"}},
        {"客户服务部", {"部门经理", "客户服务代表", "技术支持"}}
    };
    QJsonObject mapping;
    for (auto it = deptJobMap.constBegin(); it != deptJobMap.constEnd(); ++it) {
        QJsonArray jobs;
        for (const QString& j : it.value()) jobs.append(j);
        mapping[it.key()] = jobs;
    }
    QJsonObject res;
    res["status"] = "success";
    res["dept_job_map"] = mapping;
    sendJson(socket, res);
}