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
#include <QDebug>

static void sendJson(QTcpSocket* socket, const QJsonObject& obj)
{
    QByteArray outData = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    QMetaObject::invokeMethod(socket,
        [socket, outData]() { socket->write(outData); },
        Qt::QueuedConnection);
}


// ============================================================
// ── 考勤核心 ─────────────────────────────────────────────────
// ============================================================

void RequestHandler::handlePunchRequest(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QString   name = json["name"].toString();
    QDateTime serverNow = QDateTime::currentDateTime();
    QString   timeStr = serverNow.toString("yyyy-MM-dd HH:mm:ss");
    QTime     currentTime = serverNow.time();

    // 查询所属部门
    QString dept = "全部";
    QSqlQuery dq(db);
    dq.prepare("SELECT department FROM users WHERE name = :n");
    dq.bindValue(":n", name);
    if (dq.exec() && dq.next()) dept = dq.value(0).toString();

    // 读取排班规则
    QTime startTime(9, 0), endTime(18, 0);
    int   absentMins = 120;
    QSqlQuery sq(db);
    sq.prepare("SELECT start_time, end_time, absent_mins FROM shift_rules "
        "WHERE dept = :d OR dept = '全部' ORDER BY (dept = :d) DESC LIMIT 1");
    sq.bindValue(":d", dept);
    if (sq.exec() && sq.next()) {
        startTime = sq.value(0).toTime();
        endTime = sq.value(1).toTime();
        absentMins = sq.value(2).toInt();
    }

    // 判断状态
    QString status = "正常打卡";
    if (currentTime < QTime(12, 0)) {
        int secsLate = startTime.secsTo(currentTime);
        if (secsLate > 0)
            status = (secsLate > absentMins * 60) ? "旷工" : "迟到";
    }
    else {
        status = (currentTime < endTime) ? "早退" : "正常下班";
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (:n, :t, :s)");
    insertQuery.bindValue(":n", name);
    insertQuery.bindValue(":t", timeStr);
    insertQuery.bindValue(":s", status);

    if (insertQuery.exec()) {
        QMetaObject::invokeMethod(server, [server, name, status]() {
            server->logMessage(QString("<font color='#00B42A'>考勤中心: 成功记录 [%1] 的考勤: %2</font>")
                .arg(name, status));
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}

void RequestHandler::handlePunchCheat(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (?, NOW(), '作弊打卡')");
    q.addBindValue(json["name"].toString());
    q.exec();

    QString name = json["name"].toString();
    QMetaObject::invokeMethod(server, [server, name]() {
        server->logMessage(QString("<font color='red'>安全警报: 员工 [%1] 多次人脸核验失败，强制记为作弊！</font>").arg(name));
        server->loadGlobalRecords();
        }, Qt::QueuedConnection);
}

void RequestHandler::handleQueryTodayStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QJsonObject res;
    res["status"] = "success";

    QSqlQuery leaveQ(db);
    leaveQ.prepare("SELECT id FROM leave_requests WHERE applicant=:n AND status='已批准' "
        "AND CURDATE() BETWEEN DATE(start_time) AND DATE(end_time)");
    leaveQ.bindValue(":n", name);
    res["is_on_leave"] = (leaveQ.exec() && leaveQ.next());

    QJsonArray punches;
    QSqlQuery query(db);
    query.prepare("SELECT punch_time, status FROM attendance_records "
        "WHERE name = :n AND DATE(punch_time) = CURDATE() ORDER BY punch_time ASC");
    query.bindValue(":n", name);
    if (query.exec()) {
        while (query.next()) {
            QJsonObject p;
            p["time"] = query.value(0).toDateTime().toString("HH:mm:ss");
            p["status"] = query.value(1).toString();
            punches.append(p);
        }
    }
    res["punches"] = punches;
    sendJson(socket, res);
}

void RequestHandler::handleQueryMonthlyStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    int     year = json["year"].toInt();
    int     month = json["month"].toInt();

    QString startDate = QString("%1-%2-01").arg(year).arg(month, 2, 10, QChar('0'));
    QDate   lastDay(year, month, 1);
    lastDay = lastDay.addMonths(1).addDays(-1);
    QString endDate = lastDay.toString("yyyy-MM-dd");

    QSqlQuery q(db);
    QString sql = QString(
        "SELECT DATE(punch_time) as d, status FROM attendance_records "
        "WHERE name = '%1' AND DATE(punch_time) BETWEEN '%2' AND '%3' "
        "ORDER BY punch_time ASC"
    ).arg(name, startDate, endDate);

    QMap<QString, QString> dayStatus;
    if (q.exec(sql)) {
        while (q.next()) {
            QString dateStr = q.value(0).toDate().toString("yyyy-MM-dd");
            QString status = q.value(1).toString();
            QString existing = dayStatus.value(dateStr, "");

            if (status.contains("旷工")) {
                dayStatus[dateStr] = "absent";
            }
            else if (status.contains("迟到") || status.contains("早退")) {
                if (existing != "absent") dayStatus[dateStr] = "late";
            }
            else if (status.contains("假")) {
                if (existing != "absent" && existing != "late") dayStatus[dateStr] = "leave";
            }
            else if (status.contains("正常") || status.contains("下班")) {
                if (existing.isEmpty()) dayStatus[dateStr] = "normal";
            }
        }
    }

    int normalDays = 0, lateDays = 0, leaveDays = 0, absentDays = 0;
    QJsonObject colorMap;
    for (auto it = dayStatus.begin(); it != dayStatus.end(); ++it) {
        colorMap[it.key()] = it.value();
        if (it.value() == "normal") normalDays++;
        else if (it.value() == "late")   lateDays++;
        else if (it.value() == "leave")  leaveDays++;
        else if (it.value() == "absent") absentDays++;
    }

    QJsonObject res;
    res["status"] = "success";
    res["normal_days"] = normalDays;
    res["late_days"] = lateDays;
    res["leave_days"] = leaveDays;
    res["absent_days"] = absentDays;
    res["color_map"] = colorMap;
    sendJson(socket, res);
}

void RequestHandler::handleQueryAttendanceDetail(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString nameFilter = json["name_filter"].toString().trimmed();
    QString sDate = json["start_date"].toString();
    QString eDate = json["end_date"].toString();
    QString statusFilter = json["status_filter"].toString();

    QString attSql = QString(
        "SELECT id, name, punch_time, status FROM attendance_records "
        "WHERE DATE(punch_time) BETWEEN '%1' AND '%2'"
    ).arg(sDate, eDate);

    if (!nameFilter.isEmpty())
        attSql += QString(" AND name LIKE '%%%1%%'").arg(nameFilter);

    if (statusFilter == "正常")  attSql += " AND status LIKE '%正常%' AND status NOT LIKE '%补卡%'";
    else if (statusFilter == "请假")  attSql += " AND (status LIKE '%假%' OR status LIKE '%调休%')";
    else if (statusFilter != "全部状态") attSql += QString(" AND status LIKE '%%%1%%'").arg(statusFilter);

    attSql += " ORDER BY punch_time DESC";

    QJsonArray recordsArr;
    QSqlQuery  attQ(db);
    if (attQ.exec(attSql)) {
        while (attQ.next()) {
            QJsonObject row;
            row["id"] = attQ.value(0).toString();
            row["name"] = attQ.value(1).toString();
            row["time"] = attQ.value(2).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
            row["status"] = attQ.value(3).toString();
            row["source"] = "A";
            recordsArr.append(row);
        }
    }

    QJsonObject response;
    response["status"] = "success";
    response["records"] = recordsArr;
    sendJson(socket, response);
}

void RequestHandler::handleQueryTodayAttendanceForAi(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QJsonArray arr;
    QSqlQuery  q(db);
    q.prepare("SELECT punch_time, status FROM attendance_records "
        "WHERE name = ? AND DATE(punch_time) = CURDATE() ORDER BY punch_time ASC");
    q.addBindValue(json["name"].toString());
    if (q.exec()) {
        while (q.next()) {
            QJsonObject o;
            o["time"] = q.value(0).toDateTime().toString("HH:mm:ss");
            o["status"] = q.value(1).toString();
            arr.append(o);
        }
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleQueryHomeDashboard(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString role = json["role"].toString();
    QString name = json["name"].toString();
    QJsonObject res;
    res["status"] = "success";

    QJsonObject topCards;
    QSqlQuery   q(db);

    q.exec("SELECT COUNT(*) FROM users WHERE role != '超级管理员'");
    if (q.next()) topCards["total_expected"] = q.value(0).toInt();

    q.exec("SELECT COUNT(DISTINCT name) FROM attendance_records WHERE DATE(punch_time) = CURDATE()");
    if (q.next()) topCards["actual_punched"] = q.value(0).toInt();

    q.exec("SELECT COUNT(DISTINCT name) FROM attendance_records WHERE DATE(punch_time) = CURDATE() AND status NOT LIKE '%正常%'");
    if (q.next()) topCards["abnormal_count"] = q.value(0).toInt();

    if (role.contains("管理员") || role == "经理") {
        q.exec(QString("SELECT COUNT(*) FROM leave_requests WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(name));
        if (q.next()) topCards["pending_leaves"] = q.value(0).toInt();
        q.exec(QString("SELECT COUNT(*) FROM appeals WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(name));
        if (q.next()) topCards["pending_appeals"] = q.value(0).toInt();
    }
    else {
        q.exec(QString("SELECT COUNT(*) FROM attendance_records WHERE name='%1' AND DATE(punch_time) >= DATE_FORMAT(CURDATE() ,'%%Y-%%m-01') AND status NOT LIKE '%%正常%%'").arg(name));
        if (q.next()) topCards["my_abnormal"] = q.value(0).toInt();
    }
    res["top_cards"] = topCards;

    QJsonArray pieArr;
    q.exec("SELECT status, COUNT(*) FROM attendance_records WHERE DATE(punch_time) = CURDATE() GROUP BY status");
    while (q.next()) {
        QJsonObject o; o["status"] = q.value(0).toString(); o["count"] = q.value(1).toInt();
        pieArr.append(o);
    }
    res["pie_chart"] = pieArr;

    QJsonArray barArr;
    q.exec("SELECT u.department, COUNT(a.id) FROM users u LEFT JOIN attendance_records a ON u.name = a.name AND a.status NOT LIKE '%正常%' WHERE DATE(a.punch_time) >= DATE_SUB(CURDATE(), INTERVAL 30 DAY) GROUP BY u.department");
    while (q.next()) {
        QJsonObject o;
        o["dept"] = q.value(0).toString().isEmpty() ? "未分组" : q.value(0).toString();
        o["count"] = q.value(1).toInt();
        barArr.append(o);
    }
    res["bar_chart"] = barArr;

    QJsonArray lineArr;
    q.exec("SELECT DATE_FORMAT(punch_time, '%m-%d'), COUNT(DISTINCT name) FROM attendance_records WHERE punch_time >= DATE_SUB(CURDATE(), INTERVAL 6 DAY) GROUP BY DATE(punch_time) ORDER BY DATE(punch_time) ASC");
    while (q.next()) {
        QJsonObject o; o["date"] = q.value(0).toString(); o["count"] = q.value(1).toInt();
        lineArr.append(o);
    }
    res["line_chart"] = lineArr;

    QJsonArray feedArr;
    q.exec("SELECT DATE_FORMAT(punch_time, '%H:%i:%s'), name, status FROM attendance_records WHERE DATE(punch_time) = CURDATE() ORDER BY punch_time DESC LIMIT 10");
    while (q.next()) {
        QJsonObject o; o["time"] = q.value(0).toString(); o["name"] = q.value(1).toString(); o["status"] = q.value(2).toString();
        feedArr.append(o);
    }
    res["feed_list"] = feedArr;

    QJsonArray noticeArr;
    q.exec("SELECT content, DATE_FORMAT(publish_time, '%m-%d') FROM system_announcements ORDER BY publish_time DESC LIMIT 3");
    while (q.next()) {
        QJsonObject o; o["content"] = q.value(0).toString(); o["date"] = q.value(1).toString();
        noticeArr.append(o);
    }
    res["notice_list"] = noticeArr;

    sendJson(socket, res);
}


// ============================================================
// ── 排班规则 ─────────────────────────────────────────────────
// ============================================================

void RequestHandler::handleQueryShiftRule(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString dept = json["dept"].toString();
    QJsonObject res;
    res["status"] = "fail";

    QSqlQuery sq(db);
    sq.prepare("SELECT rule_name, start_time, end_time, late_mins, absent_mins FROM shift_rules "
        "WHERE dept = :d OR dept = '全部' ORDER BY (dept = :d) DESC LIMIT 1");
    sq.bindValue(":d", dept);
    if (sq.exec() && sq.next()) {
        res["status"] = "success";
        res["rule_name"] = sq.value(0).toString();
        res["start_time"] = sq.value(1).toTime().toString("HH:mm:ss");
        res["end_time"] = sq.value(2).toTime().toString("HH:mm:ss");
        res["late_mins"] = sq.value(3).toInt();
        res["absent_mins"] = sq.value(4).toInt();
    }
    sendJson(socket, res);
}

void RequestHandler::handleRuleSettings(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QSqlQuery q(db);
    q.prepare("REPLACE INTO shift_rules (dept, rule_name, start_time, end_time, late_mins, absent_mins) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    q.addBindValue(json["dept"].toString());
    q.addBindValue(json["rule_name"].toString());
    q.addBindValue(json["start_time"].toString());
    q.addBindValue(json["end_time"].toString());
    q.addBindValue(json["late_mins"].toInt());
    q.addBindValue(json["absent_mins"].toInt());
    q.exec();

    QMetaObject::invokeMethod(server, [server]() {
        server->logMessage("<font color='#E6A23C'>规则中心: 管理层更新了企业排班规则。</font>");
        }, Qt::QueuedConnection);
}


// ============================================================
// ── 请假 & 申诉 ──────────────────────────────────────────────
// ============================================================

void RequestHandler::handleLeaveRequest(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery ins(db);
    ins.prepare("INSERT INTO leave_requests (applicant, leave_type, start_time, end_time, reason, approver, status) "
        "VALUES (?, ?, ?, ?, ?, ?, '待审批')");
    ins.addBindValue(json["applicant"].toString());
    ins.addBindValue(json["leave_type"].toString());
    ins.addBindValue(json["start_time"].toString());
    ins.addBindValue(json["end_time"].toString());
    ins.addBindValue(json["reason"].toString());
    ins.addBindValue(json["approver"].toString());
    ins.exec();
}

void RequestHandler::handleLeaveApprove(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    int     reqId = json["reqId"].toInt();
    QString applicant = json["applicant"].toString();
    QString sTimeStr = json["start_time"].toString();
    QString eTimeStr = json["end_time"].toString();
    QString lType = json["leave_type"].toString();

    QSqlQuery upd(db);
    upd.exec(QString("UPDATE leave_requests SET status='已批准' WHERE id=%1").arg(reqId));

    QString finalStatus = "假-" + lType;
    QDate   sDate = QDate::fromString(sTimeStr.left(10), "yyyy-MM-dd");
    QDate   eDate = QDate::fromString(eTimeStr.left(10), "yyyy-MM-dd");

    if (sDate.isValid() && eDate.isValid() && sDate <= eDate) {
        for (QDate d = sDate; d <= eDate; d = d.addDays(1)) {
            QString    punchTime = d.toString("yyyy-MM-dd") + " 09:00:00";
            QSqlQuery  insertQ(db);
            insertQ.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (?, ?, ?)");
            insertQ.addBindValue(applicant);
            insertQ.addBindValue(punchTime);
            insertQ.addBindValue(finalStatus);
            insertQ.exec();
        }
    }

    QMetaObject::invokeMethod(server, [server, applicant]() {
        server->logMessage(QString("<font color='#67C23A'>流程审批: [%1] 的请假已获批准，多天流水已连片入库。</font>").arg(applicant));
        server->loadGlobalRecords();
        }, Qt::QueuedConnection);
}

void RequestHandler::handleAppealRequest(QSqlDatabase& db, QTcpSocket* /*socket*/, const QJsonObject& json)
{
    QSqlQuery ins(db);
    ins.prepare("INSERT INTO appeals (applicant, abnormal_time, original_status, reason, approver, status) "
        "VALUES (?, ?, ?, ?, ?, '待审批')");
    ins.addBindValue(json["applicant"].toString());
    ins.addBindValue(json["abnormal_time"].toString());
    ins.addBindValue(json["original_status"].toString());
    ins.addBindValue(json["reason"].toString());
    ins.addBindValue(json["approver"].toString());
    ins.exec();
}

void RequestHandler::handleAppealApprove(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    int     reqId = json["reqId"].toInt();
    QString applicant = json["applicant"].toString();
    QString aTime = json["abnormal_time"].toString();
    QString aType = json["appeal_type"].toString();

    QSqlQuery upd(db);
    upd.exec(QString("UPDATE appeals SET status='已批准' WHERE id=%1").arg(reqId));

    if (aType == "整天申诉") {
        upd.exec(QString("UPDATE attendance_records SET status='正常(修正)' WHERE name='%1' AND DATE(punch_time)=DATE('%2')").arg(applicant, aTime));
    }
    else {
        upd.exec(QString("UPDATE attendance_records SET status='正常(修正)' WHERE name='%1' AND punch_time='%2'").arg(applicant, aTime));
    }

    QMetaObject::invokeMethod(server, [server]() {
        server->loadGlobalRecords();
        }, Qt::QueuedConnection);
}

void RequestHandler::handleQueryPendingLeaves(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString approver = json["approver"].toString();
    QJsonArray arr;
    QSqlQuery q(db);
    QString sql = QString("SELECT id, applicant, leave_type, start_time, end_time, reason "
        "FROM leave_requests WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(approver);
    if (q.exec(sql)) {
        while (q.next()) {
            QJsonObject row;
            row["id"] = q.value(0).toInt();
            row["applicant"] = q.value(1).toString();
            row["type"] = q.value(2).toString();
            row["start"] = q.value(3).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
            row["end"] = q.value(4).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
            row["reason"] = q.value(5).toString();
            arr.append(row);
        }
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleQueryPendingAppeals(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString approver = json["approver"].toString();
    QJsonArray arr;
    QSqlQuery q(db);
    QString sql = QString("SELECT id, applicant, abnormal_time, original_status, reason "
        "FROM appeals WHERE approver LIKE '%%%1%%' AND status='待审批'").arg(approver);
    if (q.exec(sql)) {
        while (q.next()) {
            QJsonObject row;
            row["id"] = q.value(0).toInt();
            row["applicant"] = q.value(1).toString();
            row["time"] = q.value(2).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
            row["type"] = q.value(3).toString();
            row["reason"] = q.value(4).toString();
            arr.append(row);
        }
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = arr;
    sendJson(socket, res);
}

void RequestHandler::handleQueryMyRequests(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString().trimmed();
    QJsonObject response;
    response["status"] = "success";

    QJsonArray leaveArr;
    QSqlQuery lq(db);
    if (lq.exec(QString("SELECT leave_type, start_time, end_time, reason, approver, status "
        "FROM leave_requests WHERE applicant LIKE '%%%1%%' ORDER BY id DESC").arg(name))) {
        while (lq.next()) {
            QJsonObject row;
            row["type"] = lq.value(0).toString();
            row["start"] = lq.value(1).toDateTime().toString("MM-dd HH:mm");
            row["end"] = lq.value(2).toDateTime().toString("MM-dd HH:mm");
            row["reason"] = lq.value(3).toString();
            row["approver"] = lq.value(4).toString();
            row["status"] = lq.value(5).toString();
            leaveArr.append(row);
        }
    }
    response["leave_data"] = leaveArr;

    QJsonArray appealArr;
    QSqlQuery aq(db);
    if (aq.exec(QString("SELECT abnormal_time, original_status, reason, approver, status "
        "FROM appeals WHERE applicant LIKE '%%%1%%' ORDER BY id DESC").arg(name))) {
        while (aq.next()) {
            QJsonObject row;
            row["time"] = aq.value(0).toDateTime().toString("MM-dd HH:mm");
            row["type"] = aq.value(1).toString();
            row["reason"] = aq.value(2).toString();
            row["approver"] = aq.value(3).toString();
            row["status"] = aq.value(4).toString();
            appealArr.append(row);
        }
    }
    response["appeal_data"] = appealArr;
    sendJson(socket, response);
}

void RequestHandler::handleQueryApprovalCandidates(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QJsonObject res;

    QSqlQuery infoQ(db);
    infoQ.prepare("SELECT role, department, job_title FROM users WHERE name=:n");
    infoQ.bindValue(":n", name);
    if (infoQ.exec() && infoQ.next()) {
        res["my_role"] = infoQ.value(0).toString();
        res["my_dept"] = infoQ.value(1).toString();
        res["my_job"] = infoQ.value(2).toString();
    }

    QJsonArray abnArr;
    QSqlQuery rq(db);
    rq.prepare("SELECT punch_time, status FROM attendance_records "
        "WHERE name=:n AND (status NOT LIKE '%正常%') ORDER BY punch_time DESC LIMIT 10");
    rq.bindValue(":n", name);
    if (rq.exec()) {
        while (rq.next()) {
            QJsonObject rec;
            rec["time"] = rq.value(0).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
            rec["display"] = rq.value(0).toDateTime().toString("MM-dd HH:mm") + " [" + rq.value(1).toString() + "]";
            abnArr.append(rec);
        }
    }
    res["abnormal_records"] = abnArr;

    QJsonArray hrArr, gmArr, mgrArr;
    QSqlQuery hq(db);
    hq.exec("SELECT name FROM users WHERE department='人力资源部' AND role IN ('管理员登录','经理') AND name NOT LIKE '%超级管理员%'");
    while (hq.next()) hrArr.append(hq.value(0).toString());

    QSqlQuery gq(db);
    gq.exec("SELECT name FROM users WHERE job_title IN ('总经理', '总裁', '董事长') OR name='总经理' LIMIT 1");
    while (gq.next()) gmArr.append(gq.value(0).toString());

    QSqlQuery mq(db);
    mq.prepare("SELECT name FROM users WHERE department=:d AND role='经理' AND name != :me");
    mq.bindValue(":d", res["my_dept"].toString());
    mq.bindValue(":me", name);
    mq.exec();
    while (mq.next()) mgrArr.append(mq.value(0).toString());

    res["hr_list"] = hrArr;
    res["gm_list"] = gmArr;
    res["mgr_list"] = mgrArr;
    sendJson(socket, res);
}