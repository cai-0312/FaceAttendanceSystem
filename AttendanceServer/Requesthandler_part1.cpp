#include "RequestHandler.h"
#include "AttendanceServer.h"
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
    catch (...) {
    }
}
// 保护聊天内容
static QString decodeContent(const QString& raw)
{
    if (raw.startsWith("B64:"))
        return QString::fromUtf8(QByteArray::fromBase64(raw.mid(4).toUtf8()));
    return raw;
}
// 防止 ODBC 吞表情包或特殊字符）
static QString encodeContent(const QString& content)
{
    return "B64:" + QString(content.toUtf8().toBase64());
}
// 查询系统中所有已注册的人脸特征
void RequestHandler::handleQueryFaceFeatures(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& /*json*/)
{
    QJsonArray arr;
    QSqlQuery query(db);
    // 仅查询录入了人脸特征 (feature IS NOT NULL) 的用户
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
// 注册（更新）用户的人脸特征
void RequestHandler::handleRegisterFace(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QByteArray featureData = QByteArray::fromBase64(json["feature"].toString().toUtf8());
    QSqlQuery q(db);
    q.prepare("UPDATE users SET feature = :f WHERE name = :n");
    q.bindValue(":f", featureData);
    q.bindValue(":n", name);
    q.exec();
}
// 客户端账号密码角色登录验证
void RequestHandler::handleClientLoginAuth(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString account = json["account"].toString();
    QString pwd = json["pwd"].toString();         // 客户端传来的SHA-256哈希值
    QString role = json["role"].toString();
    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    // 兼容旧明文密码：数据库中如果存的是明文，用MySQL的SHA2()函数算出哈希后比对
    // 如果数据库中已经存了哈希，则直接比对
    QString sql = "SELECT name, feature FROM users WHERE account = '" + account + "' AND role = '" + role + "'"
        " AND (password = '" + pwd + "' OR SHA2(password, 256) = '" + pwd + "')";
    qDebug() << "[LoginAuth] account=" << account << "role=" << role;
    if (q.exec(sql) && q.next()) {
        res["status"] = "success";
        res["real_name"] = q.value("name").toString();
        // 问题四：检查人脸特征是否存在
        QByteArray feat = q.value("feature").toByteArray();
        res["has_face"] = !feat.isNull() && !feat.isEmpty();
        qDebug() << "[LoginAuth] SUCCESS, name=" << res["real_name"].toString() << "has_face=" << res["has_face"].toBool();
    }
    else {
        qDebug() << "[LoginAuth] FAILED, sql error:" << q.lastError().text();
    }
    sendJson(socket, res);
}
// 客户端注册新账号
void RequestHandler::handleClientRegisterAccount(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString account = json["account"].toString();
    QString pwd = json["pwd"].toString();           // 已经是SHA-256哈希
    QString name = json["name"].toString();
    QString dept = json["dept"].toString();
    QString jobTitle = json["job_title"].toString();
    QString phone = json["phone"].toString();
    QString gender = json["gender"].toString();
    // 问题二：服务端强制所有新注册用户为"普通登录"，无视客户端传来的role
    QString role = "普通登录";
    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    q.prepare("INSERT INTO users (account, password, name, role, department, job_title, phone, gender) "
        "VALUES (:acc, :pwd, :name, :role, :dept, :job, :phone, :gender)");
    q.bindValue(":acc", account);
    q.bindValue(":pwd", pwd);
    q.bindValue(":name", name);
    q.bindValue(":role", role);
    q.bindValue(":dept", dept);
    q.bindValue(":job", jobTitle);
    q.bindValue(":phone", phone);
    q.bindValue(":gender", gender);
    if (q.exec()) {
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
    QString sql = QString("SELECT id FROM users WHERE name = '%1' AND department = '%2'").arg(name, dept);
    // 如果能查到记录，说明验证通过
    if (q.exec(sql) && q.next()) {
        res["status"] = "success";
    }
    sendJson(socket, res);
}
// 处理客户端登录后的初始化工作（维护在线列表、下发离线消息）
void RequestHandler::handleLogin(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    QString name = json["name"].toString().trimmed();
    QString ip = socket->peerAddress().toString().remove("::ffff:");
    QString dept = "未知部门", jobTitle = "未分配";
    // 1. 查询该登录用户的部门和职位信息
    QSqlQuery query(db);
    query.prepare("SELECT department, job_title FROM users WHERE name = :name");
    query.bindValue(":name", name);
    if (query.exec() && query.next()) {
        dept = query.value(0).toString();
        jobTitle = query.value(1).toString();
        // 处理空字段容错
        if (dept.isEmpty())     dept = "未分配部门";
        if (jobTitle.isEmpty()) jobTitle = "未分配";
    }
    // 2. 通知服务端主类注册该客户端，加入到在线客户端的管理集合中
    QMetaObject::invokeMethod(server, [server, socket, name, dept, jobTitle, ip]() {
        server->registerClient(socket, name, dept, jobTitle, ip);
        }, Qt::QueuedConnection);
    // 3. 检查并补发属于该用户的离线消息或文件
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
            // 将离线消息发送给客户端
            QByteArray outData = QJsonDocument(offMsg).toJson(QJsonDocument::Compact) + "\n";
            QMetaObject::invokeMethod(socket, [socket, outData]() { socket->write(outData); },
                Qt::QueuedConnection);
            offlineCount++;
        }
    }
    // 4. 如果有补发动作，在服务端打印日志
    if (offlineCount > 0) {
        QMetaObject::invokeMethod(server, [server, name, offlineCount]() {
            server->logMessage(QString("<font color='#E6A23C'>已向 [%1] 补发 %2 条离线消息/文件。</font>")
                .arg(name).arg(offlineCount));
            }, Qt::QueuedConnection);
    }
    // 5.线消息表中删除这些已读记录
    QSqlQuery deleteOffQ(db);
    deleteOffQ.prepare("DELETE FROM offline_messages WHERE receiver = :n");
    deleteOffQ.bindValue(":n", name);
    deleteOffQ.exec();
}
// 更新用户的当前状态（例如：在线、离开、忙碌等图标状态）
void RequestHandler::handleStatusUpdate(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery q(db);
    q.prepare("UPDATE users SET status_icon = :s WHERE name = :n");
    q.bindValue(":s", json["status"].toString());
    q.bindValue(":n", json["name"].toString());
    q.exec();
}
// 查询用户个人资料
void RequestHandler::handleQueryUserProfile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString safeName = name;
    safeName.replace("'", "''");
    QJsonObject res;
    res["status"] = "fail";
    QSqlQuery q(db);
    QString sql = QString("SELECT id, job_title, role, department, gender, phone, name, avatar "
        "FROM users WHERE name = '%1' OR account = '%1'").arg(safeName);
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
        // 头像处理：区分文件路径和旧Base64
        QString av = q.value(7).toString();
        if (av.contains("/") && !av.startsWith("/9j/")) {
            // 文件路径模式：读取文件转Base64返回
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
    if (field == "gender") {
        dbColumn = "gender";
    }
    else if (field == "phone") {
        dbColumn = "phone";
    }
    else if (field == "avatar") {
        dbColumn = "avatar";
    }
    else if (field == "real_name") {
        dbColumn = "name";
    }
    else {
        res["msg"] = "不支持修改该字段: " + field;
        sendJson(socket, res);
        return;
    }
    QSqlQuery q(db);
    // 使用 %1 动态替换列名，使用 ? 占位符绑定值
    QString sql = QString("UPDATE users SET %1 = ? WHERE name = ? OR account = ?").arg(dbColumn);
    q.prepare(sql);
    q.addBindValue(value);
    q.addBindValue(name);
    q.addBindValue(name);
    if (q.exec()) {
        if (q.numRowsAffected() > 0) {
            res["status"] = "success";
        }
        else {
            res["msg"] = "未找到该账号，无法更新";
        }
    }
    else {
        res["msg"] = "数据库更新失败: " + q.lastError().text();
    }
    sendJson(socket, res);
}
// 按部门和关键字查询花名册/用户轻量列表
void RequestHandler::handleQueryUserList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString dept = json["dept"].toString();
    QString keyword = json["keyword"].toString();
    // 从视图 (view_users_lite) 查询数据，屏蔽掉超管账号，不外泄管理员信息
    QString sql =
        "SELECT id, account, name, gender, department, job_title, phone "
        "FROM view_users_lite "
        "WHERE account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'";
    // 动态追加筛选条件
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
// 根据用户名查询其归属的部门
void RequestHandler::handleQueryUserDept(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QString dept, jobTitle, realName = name;
    QSqlQuery dq(db);
    dq.prepare("SELECT department, job_title, name FROM users WHERE name = :n OR account = :n");
    dq.bindValue(":n", name);
    if (dq.exec() && dq.next()) {
        dept = dq.value("department").toString();
        jobTitle = dq.value("job_title").toString();
        realName = dq.value("name").toString();
    }
    qDebug() << "[QueryUserDept] input=" << name << "dept=" << dept << "jobTitle=" << jobTitle << "realName=" << realName;
    QJsonObject res;
    res["type"] = "user_dept_reply";
    res["department"] = dept;
    res["job_title"] = jobTitle;
    res["real_name"] = realName;
    sendJson(socket, res);
}
// 超级管理员强制重置指定用户的密码（默认重置为 123456）
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
        // 记录后台管理日志
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
    QSqlQuery q(db);
    q.prepare("DELETE FROM users WHERE account = :acc");
    q.bindValue(":acc", account);
    QJsonObject res;
    if (q.exec()) {
        res["status"] = "success";
        // 记录高危操作日志，并刷新后台权限列表
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
// 超级管理员手动修改打卡记录的状态（例如将“迟到”改为“正常”）
void RequestHandler::handleAdminModifyStatus(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    int recordId = json["record_id"].toInt();
    QString newStatus = json["new_status"].toString();
    QSqlQuery q(db);
    q.prepare("UPDATE attendance_records SET status = :s WHERE id = :id");
    q.bindValue(":s", newStatus);
    q.bindValue(":id", recordId);
    if (q.exec()) {
        // 操作成功后，仅在服务端发出告警日志并重新加载全局考勤记录到 UI 上
        QMetaObject::invokeMethod(server, [server]() {
            server->logMessage("<font color='#E6A23C'>⚠️ 管理员后台强行修改了某条考勤记录状态。</font>");
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}

// ===== 问题3：修改密码（服务端校验+复杂度检查+MD5哈希存储）=====
void RequestHandler::handleVerifyAndUpdatePassword(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString oldPwd = json["old_pwd"].toString();
    QString newPwd = json["new_pwd"].toString();
    QJsonObject res; res["status"] = "fail";
    if (name.isEmpty() || oldPwd.isEmpty() || newPwd.isEmpty()) { res["msg"] = "参数不完整"; sendJson(socket, res); return; }
    if (newPwd.length() < 8) { res["msg"] = "新密码长度必须至少8位"; sendJson(socket, res); return; }
    bool hasL = false, hasD = false;
    for (const QChar& ch : newPwd) { if (ch.isLetter()) hasL = true; if (ch.isDigit()) hasD = true; }
    if (!hasL || !hasD) { res["msg"] = "新密码必须同时包含字母和数字"; sendJson(socket, res); return; }
    QSqlQuery q(db); q.prepare("SELECT password FROM users WHERE name = :n"); q.bindValue(":n", name);
    if (!q.exec() || !q.next()) { res["msg"] = "用户不存在"; sendJson(socket, res); return; }
    QString stored = q.value(0).toString();
    QString oldHash = QString(QCryptographicHash::hash(oldPwd.toUtf8(), QCryptographicHash::Md5).toHex());
    if (stored != oldPwd && stored != oldHash) { res["msg"] = "旧密码错误"; sendJson(socket, res); return; }
    if (newPwd == oldPwd) { res["msg"] = "新密码不能与旧密码相同"; sendJson(socket, res); return; }
    QString newHash = QString(QCryptographicHash::hash(newPwd.toUtf8(), QCryptographicHash::Md5).toHex());
    QSqlQuery uq(db); uq.prepare("UPDATE users SET password = :p WHERE name = :n");
    uq.bindValue(":p", newHash); uq.bindValue(":n", name);
    if (uq.exec()) { res["status"] = "success"; res["msg"] = "密码修改成功"; }
    else { res["msg"] = "数据库更新失败"; }
    sendJson(socket, res);
}

// ===== 问题2：人脸重录审批申请 =====
void RequestHandler::handleFaceReregisterRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString applicant = json["applicant"].toString().trimmed();
    QString reason = json["reason"].toString().trimmed();
    QString approver = json["approver"].toString().trimmed();
    QJsonObject res; res["status"] = "fail";
    if (applicant.isEmpty() || reason.isEmpty() || approver.isEmpty()) { res["msg"] = "参数不完整"; sendJson(socket, res); return; }
    QSqlQuery q(db);
    q.prepare("INSERT INTO appeals (applicant, abnormal_time, original_status, reason, approver, status) VALUES (?, NOW(), '人脸重录', ?, ?, '待审批')");
    q.addBindValue(applicant); q.addBindValue(reason); q.addBindValue(approver);
    if (q.exec()) { res["status"] = "success"; res["msg"] = "人脸重录申请已提交"; }
    else { res["msg"] = "数据库写入失败: " + q.lastError().text(); }
    sendJson(socket, res);
}

// ===== 问题4：头像上传到文件系统（数据库只存路径）=====
void RequestHandler::handleUploadAvatarFile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QString avatarData = json["avatar_data"].toString();
    QJsonObject res; res["status"] = "fail";
    if (name.isEmpty() || avatarData.isEmpty()) { res["msg"] = "参数不完整"; sendJson(socket, res); return; }

    // 查询工号(account)和id
    QSqlQuery q(db);
    q.prepare("SELECT id, account FROM users WHERE name = :n");
    q.bindValue(":n", name);
    QString account = "unknown";
    int userId = 0;
    if (q.exec() && q.next()) {
        userId = q.value(0).toInt();
        account = q.value(1).toString();
    }

    // 构建路径: avatars/姓名/工号_姓名.jpg（服务端exe同级目录下）
    QString baseDir = QCoreApplication::applicationDirPath() + "/avatars/" + name + "/";
    QDir dir;
    if (!dir.exists(baseDir)) {
        bool ok = dir.mkpath(baseDir);
        qDebug() << "[Avatar] mkpath:" << baseDir << "result:" << ok;
    }

    QString fileName = QString("%1_%2.jpg").arg(account, name);
    QString fullPath = baseDir + fileName;
    QString relativePath = QString("avatars/%1/%2").arg(name, fileName);

    // 解码Base64并写入文件
    QByteArray imgBytes = QByteArray::fromBase64(avatarData.toUtf8());
    qDebug() << "[Avatar] Writing" << imgBytes.size() << "bytes to:" << fullPath;

    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(imgBytes);
        file.close();

        // 数据库只存相对路径
        QSqlQuery uq(db);
        uq.prepare("UPDATE users SET avatar = :p WHERE name = :n");
        uq.bindValue(":p", relativePath);
        uq.bindValue(":n", name);
        if (uq.exec() && uq.numRowsAffected() > 0) {
            res["status"] = "success";
            res["msg"] = "头像已保存: " + relativePath;
        }
        else {
            res["msg"] = "数据库路径更新失败: " + uq.lastError().text();
        }
    }
    else {
        res["msg"] = "文件写入失败: " + file.errorString() + " 路径: " + fullPath;
    }
    sendJson(socket, res);
}

// ===== 问题4：根据路径读取头像文件返回Base64 =====
void RequestHandler::handleQueryAvatarFile(QSqlDatabase& /*db*/, QTcpSocket* socket, const QJsonObject& json)
{
    QString avatarPath = json["avatar_path"].toString();
    QJsonObject res; res["status"] = "fail";
    if (avatarPath.isEmpty()) { res["msg"] = "路径为空"; sendJson(socket, res); return; }

    QString fullPath = QCoreApplication::applicationDirPath() + "/" + avatarPath;
    qDebug() << "[Avatar] Reading from:" << fullPath;

    QFile file(fullPath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();
        res["status"] = "success";
        res["avatar_data"] = QString(data.toBase64());
        qDebug() << "[Avatar] Read OK," << data.size() << "bytes";
    }
    else {
        res["msg"] = "头像文件不存在: " + fullPath;
        qDebug() << "[Avatar] File not found:" << fullPath;
    }
    sendJson(socket, res);
}

// ===== 首页大屏：部门列表查询 =====
void RequestHandler::handleQueryDeptList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& /*json*/)
{
    QJsonArray arr;
    QSqlQuery q(db);
    q.exec("SELECT DISTINCT department FROM users WHERE department IS NOT NULL AND department != '' AND role != '超级管理员' ORDER BY department");
    while (q.next()) arr.append(q.value(0).toString());
    QJsonObject res;
    res["status"] = "success";
    res["departments"] = arr;
    sendJson(socket, res);
}