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
#include <QCryptographicHash>
#include <QRandomGenerator>
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

static QString S(const QString& val)
{
    QString safe = val;
    safe.replace("'", "''");
    return safe;
}

// 聊天内容解码：自动兼容 AES256 / B64 / 明文
static QString decodeContent(const QString& raw)
{
    return CryptoHelper::safeDecrypt(raw);
}

// 聊天内容编码：AES-256 加密
static QString encodeContent(const QString& content)
{
    return CryptoHelper::encryptContent(content);
}
// 查询系统中所有已注册的人脸特征
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
    query.finish();
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

// 注册（更新）用户的人脸特征
// 注意：feature 是二进制 BLOB，必须用 bindValue，但 name 改用拼接
void RequestHandler::handleRegisterFace(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QByteArray featureData = QByteArray::fromBase64(json["feature"].toString().toUtf8());

    // feature 是 BLOB 二进制数据，只能用 bindValue；name 用 WHERE 拼接
    QSqlQuery q(db);
    q.prepare(QString("UPDATE users SET feature = :f WHERE name = '%1'").arg(S(name)));
    q.bindValue(":f", featureData);
    q.exec();
}

// 客户端账号密码角色登录验证
void RequestHandler::handleClientLoginAuth(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString account = json["account"].toString();
    QString pwd = json["pwd"].toString();
    QString role = json["role"].toString();
    QJsonObject res;
    res["status"] = "fail";

    qDebug() << "[LoginAuth] account=" << account << "role=" << role;

    QSqlQuery q(db);
    bool ok = q.exec(QString("SELECT name, feature, password, role FROM users WHERE account = '%1'").arg(S(account)));

    if (!ok) {
        qDebug() << "[LoginAuth] SQL执行失败:" << q.lastError().text();
        sendJson(socket, res);
        return;
    }
    if (!q.next()) {
        qDebug() << "[LoginAuth] FAILED, account not found:" << account;
        sendJson(socket, res);
        return;
    }

    QString dbRole = q.value("role").toString().trimmed();
    QString storedHash = q.value("password").toString();
    QString dbName = q.value("name").toString();
    QByteArray feat = q.value("feature").toByteArray();
    q.finish();

    // 应用层比对角色
    if (dbRole != role.trimmed()) {
        qDebug() << "[LoginAuth] FAILED, role mismatch: db=" << dbRole << "client=" << role;
        sendJson(socket, res);
        return;
    }

    // 密码验证
    if (CryptoHelper::verifyPassword(pwd, storedHash)) {
        res["status"] = "success";
        res["real_name"] = dbName;
        res["has_face"] = !feat.isNull() && !feat.isEmpty();
        qDebug() << "[LoginAuth] SUCCESS, name=" << dbName;

        // 旧密码自动升级为 PBKDF2
        if (!storedHash.startsWith("PBKDF2:")) {
            QString upgradedHash = CryptoHelper::hashPassword(pwd);
            QSqlQuery upgradeQ(db);
            upgradeQ.exec(QString("UPDATE users SET password = '%1' WHERE account = '%2'")
                .arg(S(upgradedHash), S(account)));
            if (upgradeQ.numRowsAffected() > 0) {
                qDebug() << "[LoginAuth] 密码已自动升级为 PBKDF2 格式";
            }
        }
    }
    else {
        qDebug() << "[LoginAuth] FAILED, password mismatch for account=" << account;
    }
    sendJson(socket, res);
}

// 客户端注册新账号
void RequestHandler::handleClientRegisterAccount(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString pwd = json["pwd"].toString();
    QString name = json["name"].toString();
    QString dept = json["dept"].toString();
    QString jobTitle = json["job_title"].toString();
    QString phone = json["phone"].toString();
    QString gender = json["gender"].toString();
    QString role = "普通登录";

    // 密码用 PBKDF2 哈希存储
    QString hashedPwd = CryptoHelper::hashPassword(pwd);

    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    bool ok = q.exec(QString(
        "INSERT INTO users (account, password, name, role, department, job_title, phone, gender) "
        "VALUES ('%1', '%2', '%3', '%4', '%5', '%6', '%7', '%8')")
        .arg(S(account), S(hashedPwd), S(name), S(role), S(dept), S(jobTitle), S(phone), S(gender)));

    if (ok) {
        res["status"] = "success";
        QMetaObject::invokeMethod(server, [server, name, role]() {
            server->logMessage(QString("<font color='#E6A23C'>新入职: [%1] 注册了账号，权限为 [%2]。</font>").arg(name, role));
            server->refreshPermModel();
            }, Qt::QueuedConnection);
    }
    else {
        res["msg"] = q.lastError().text();
    }
    sendJson(socket, res);
}

// 在注册前验证用户是否已存在指定部门中
void RequestHandler::handleVerifyUserForRegistration(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString dept = json["dept"].toString().trimmed();
    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    if (q.exec(QString("SELECT id FROM users WHERE name = '%1' AND department = '%2'").arg(S(name), S(dept))) && q.next()) {
        res["status"] = "success";
    }
    sendJson(socket, res);
}

// ═══════════════════════════════════════════════════════════════════
//  登录 / 在线状态
// ═══════════════════════════════════════════════════════════════════

// 处理客户端登录后的初始化工作（维护在线列表、下发离线消息）
void RequestHandler::handleLogin(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString name = json["name"].toString().trimmed();
    QString ip = socket->peerAddress().toString().remove("::ffff:");
    QString dept = "未知部门", jobTitle = "未分配";

    QSqlQuery query(db);
    query.exec(QString("SELECT department, job_title FROM users WHERE name = '%1'").arg(S(name)));
    if (query.next()) {
        dept = query.value(0).toString();
        jobTitle = query.value(1).toString();
        if (dept.isEmpty()) dept = "未分配部门";
        if (jobTitle.isEmpty()) jobTitle = "未分配";
    }
    query.finish();

    QMetaObject::invokeMethod(server, [server, socket, name, dept, jobTitle, ip]() {
        server->registerClient(socket, name, dept, jobTitle, ip);
        }, Qt::QueuedConnection);

    // 检查并补发离线消息
    QSqlQuery offlineQ(db);
    offlineQ.exec(QString("SELECT sender, msg_type, content, filename, send_time, department "
        "FROM offline_messages WHERE receiver = '%1' ORDER BY send_time ASC").arg(S(name)));
    int offlineCount = 0;
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
    offlineQ.finish();

    if (offlineCount > 0) {
        QMetaObject::invokeMethod(server, [server, name, offlineCount]() {
            server->logMessage(QString("<font color='#E6A23C'>已向 [%1] 补发 %2 条离线消息/文件。</font>")
                .arg(name).arg(offlineCount));
            }, Qt::QueuedConnection);
    }

    QSqlQuery deleteOffQ(db);
    deleteOffQ.exec(QString("DELETE FROM offline_messages WHERE receiver = '%1'").arg(S(name)));
}

// 更新用户的当前状态
void RequestHandler::handleStatusUpdate(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString status = json["status"].toString();
    QString name = json["name"].toString();
    QSqlQuery q(db);
    q.exec(QString("UPDATE users SET status_icon = '%1' WHERE name = '%2'").arg(S(status), S(name)));
}

// ═══════════════════════════════════════════════════════════════════
//  用户档案
// ═══════════════════════════════════════════════════════════════════

// 查询用户个人资料
void RequestHandler::handleQueryUserProfile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    QString sql = QString("SELECT id, job_title, role, department, gender, phone, name, avatar "
        "FROM users WHERE name = '%1' OR account = '%1'").arg(S(name));
    if (!q.exec(sql)) {
        res["msg"] = "数据库查询异常: " + q.lastError().text();
    }
    else if (q.next()) {
        res["status"] = "success";
        res["id"] = q.value(0).toInt();
        res["job_title"] = q.value(1).toString();
        res["role"] = q.value(2).toString();
        res["department"] = q.value(3).toString();
        res["gender"] = q.value(4).toString();
        res["phone"] = q.value(5).toString();
        res["real_name"] = q.value(6).toString();
        QString av = q.value(7).toString();
        if (av.contains("/") && !av.startsWith("/9j/")) {
            QString fullPath = QCoreApplication::applicationDirPath() + "/" + av;
            QFile avatarFile(fullPath);
            if (avatarFile.open(QIODevice::ReadOnly)) {
                QByteArray fileData = avatarFile.readAll();
                avatarFile.close();
                res["avatar_base64"] = QString(fileData.toBase64());
            }
            else {
                res["avatar_base64"] = "";
            }
            res["avatar_path"] = av;
        }
        else {
            res["avatar_base64"] = av;
            res["avatar_path"] = "";
        }
    }
    else {
        res["msg"] = "花名册中找不到该用户信息";
    }
    q.finish();
    sendJson(socket, res);
}

// 修改用户的个人资料
void RequestHandler::handleUpdateProfileField(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString field = json["field"].toString().trimmed();
    QString value = json["value"].toString();
    QJsonObject res;
    res["status"] = "fail";
    QString dbColumn;
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

// 按部门和关键字查询花名册
void RequestHandler::handleQueryUserList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString dept = json["dept"].toString();
    QString keyword = json["keyword"].toString();
    QString sql = "SELECT id, account, name, gender, department, job_title, phone "
        "FROM view_users_lite "
        "WHERE account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'";
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

// 根据用户名查询其归属的部门
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

// ═══════════════════════════════════════════════════════════════════
//  管理员操作
// ═══════════════════════════════════════════════════════════════════

// 超级管理员强制重置密码
void RequestHandler::handleAdminResetPassword(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString empName = json["name"].toString();
    // 重置密码链路：与登录一致，登录时客户端发 SHA256("123456")，所以存储 PBKDF2(SHA256("123456"))
    QString sha256Of123456 = QString(QCryptographicHash::hash(
        QByteArray("123456"), QCryptographicHash::Sha256).toHex());
    QString hashedPwd = CryptoHelper::hashPassword(sha256Of123456);

    QSqlQuery q(db);
    QJsonObject res;
    if (q.exec(QString("UPDATE users SET password = '%1' WHERE account = '%2'").arg(S(hashedPwd), S(account)))) {
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

// 超级管理员强制删除用户/员工
void RequestHandler::handleAdminDeleteUser(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString empName = json["name"].toString();

    TransactionGuard txn(db);
    if (!txn.isActive()) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "事务开启失败";
        sendJson(socket, res); return;
    }

    QSqlQuery delRecords(db);
    if (!delRecords.exec(QString("DELETE FROM attendance_records WHERE name = '%1'").arg(S(empName)))) {
        QJsonObject res; res["status"] = "fail";
        res["msg"] = "删除考勤记录失败，事务已回滚：" + delRecords.lastError().text();
        sendJson(socket, res); return;
    }

    QSqlQuery delUser(db);
    if (!delUser.exec(QString("DELETE FROM users WHERE account = '%1'").arg(S(account)))) {
        QJsonObject res; res["status"] = "fail";
        res["msg"] = "删除用户失败，事务已回滚：" + delUser.lastError().text();
        sendJson(socket, res); return;
    }

    if (!txn.commit()) {
        QJsonObject res; res["status"] = "fail"; res["msg"] = "事务提交失败，已回滚。";
        sendJson(socket, res); return;
    }

    QJsonObject res; res["status"] = "success";
    sendJson(socket, res);
    QMetaObject::invokeMethod(server, [server, empName]() {
        server->logMessage(QString("<font color='red'>高危操作: 管理员已将员工 [%1] 彻底删除！</font>").arg(empName));
        server->refreshPermModel();
        }, Qt::QueuedConnection);
}

// 超级管理员手动修改打卡记录的状态
void RequestHandler::handleAdminModifyStatus(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    int recordId = json["record_id"].toInt();
    QString newStatus = json["new_status"].toString();
    QSqlQuery q(db);
    // id 是整数，不受中文 bindValue 影响，但为了统一风格也用拼接
    if (q.exec(QString("UPDATE attendance_records SET status = '%1' WHERE id = %2").arg(S(newStatus)).arg(recordId))) {
        QMetaObject::invokeMethod(server, [server]() {
            server->logMessage("<font color='#E6A23C'>管理员后台修改了某条考勤记录状态。</font>");
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  修改密码（密码链路统一修复）
// ═══════════════════════════════════════════════════════════════════
void RequestHandler::handleVerifyAndUpdatePassword(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString oldPwd = json["old_pwd"].toString();   // 客户端发来的是明文
    QString newPwd = json["new_pwd"].toString();   // 客户端发来的是明文
    QJsonObject res;
    res["status"] = "fail";

    if (name.isEmpty() || oldPwd.isEmpty() || newPwd.isEmpty()) {
        res["msg"] = "参数不完整"; sendJson(socket, res); return;
    }
    if (newPwd.length() < 8) {
        res["msg"] = "新密码长度必须至少8位"; sendJson(socket, res); return;
    }
    bool hasLetter = false, hasDigit = false;
    for (const QChar& ch : newPwd) {
        if (ch.isLetter()) hasLetter = true;
        if (ch.isDigit()) hasDigit = true;
    }
    if (!hasLetter || !hasDigit) {
        res["msg"] = "新密码必须同时包含字母和数字"; sendJson(socket, res); return;
    }

    // 从数据库查出当前密码哈希
    QSqlQuery q(db);
    q.exec(QString("SELECT password FROM users WHERE name = '%1'").arg(S(name)));
    if (!q.next()) {
        res["msg"] = "用户不存在"; sendJson(socket, res); return;
    }
    QString stored = q.value(0).toString();
    q.finish();

    // 密码验证：客户端发的是明文，但数据库可能存的是 PBKDF2(SHA256(明文))
    // 所以同时尝试明文和 SHA256(明文)
    QString oldPwdSha256 = QString(QCryptographicHash::hash(oldPwd.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!CryptoHelper::verifyPassword(oldPwd, stored) && !CryptoHelper::verifyPassword(oldPwdSha256, stored)) {
        res["msg"] = "旧密码错误"; sendJson(socket, res); return;
    }

    if (newPwd == oldPwd) {
        res["msg"] = "新密码不能与旧密码相同"; sendJson(socket, res); return;
    }

    // 新密码存储：与登录链路一致 → PBKDF2(SHA256(明文))
    QString newPwdSha256 = QString(QCryptographicHash::hash(newPwd.toUtf8(), QCryptographicHash::Sha256).toHex());
    QString newHash = CryptoHelper::hashPassword(newPwdSha256);

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

// 人脸重录审批申请
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

    // 3. 将旧头像重命名备份，后缀依然保持为 .jpg
    if (!oldAvatarPath.isEmpty()) {
        QString oldFullPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../../AttendanceServer/" + oldAvatarPath);
        QFile oldFile(oldFullPath);
        if (oldFile.exists()) {
            // 生成随机码
            QString randomCode = QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + QString::number(QRandomGenerator::global()->bounded(1000, 9999));
            // 把原路径中的 .jpg 替换为 _随机码.jpg
            QString backupPath = oldFullPath;
            backupPath.replace(".jpg", "_" + randomCode + ".jpg");
            oldFile.rename(backupPath);
        }
    }
    // 4. 新头像依然使用干净的 %1_%2.jpg 格式
    QString fileName = QString("%1_%2.jpg").arg(account, name);
    QString fullPath = baseDir + fileName;
    QString relativePath = QString("avatars/%1/%2").arg(name, fileName);

    // 5. 写入文件并更新数据库
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
// 根据路径读取头像文件返回Base64
void RequestHandler::handleQueryAvatarFile(QSqlDatabase& /*db*/, QTcpSocket* socket, const QJsonObject& json)
{
    QString avatarPath = json["avatar_path"].toString();
    QJsonObject res; res["status"] = "fail";
    if (avatarPath.isEmpty()) { res["msg"] = "路径为空"; sendJson(socket, res); return; }

    // 读取时也使用相对路径拼凑，确保能精准找到图片
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

// 部门列表查询
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