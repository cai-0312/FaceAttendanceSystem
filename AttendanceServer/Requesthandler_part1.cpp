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
// 发送 JSON 回包（线程安全）
static void sendJson(QTcpSocket* socket, const QJsonObject& obj)
{
    QByteArray outData = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    QMetaObject::invokeMethod(socket, [socket, outData]() {
        if (socket->state() == QAbstractSocket::ConnectedState) {
            socket->write(outData);
            socket->flush(); 
        }
        }, Qt::QueuedConnection);
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
    QString pwd = json["pwd"].toString();
    QString role = json["role"].toString();
    QJsonObject res;
    res["status"] = "fail"; // 默认状态为失败
    QSqlQuery q(db);
    // 直接使用 QString::arg 拼接 SQL 进行查询验证匹配
    QString sql = QString("SELECT name FROM users WHERE account = '%1' AND password = '%2' AND role = '%3'")
        .arg(account, pwd, role);
    if (q.exec(sql) && q.next()) {
        res["status"] = "success";
        res["real_name"] = q.value(0).toString(); // 取出真实姓名返回给客户端使用
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
            server->logMessage(QString("<font color='#E6A23C'>新入职: [%1] 注册了账号，权限为 [%2]。</font>").arg(name, role));
            server->refreshPermModel(); 
            }, Qt::QueuedConnection);
    }
    else {
        // 如果注册失败（例如账号冲突等），将数据库错误信息返回给客户端
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
        res["avatar_base64"] = q.value(7).toString();
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
    QString dept = "全部"; // 默认返回“全部”
    QSqlQuery dq(db);
    dq.prepare("SELECT department FROM users WHERE name = :n");
    dq.bindValue(":n", name);
    // 若查到结果，覆盖默认值
    if (dq.exec() && dq.next()) dept = dq.value(0).toString();
    QJsonObject res;
    res["type"] = "user_dept_reply";
    res["department"] = dept;
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