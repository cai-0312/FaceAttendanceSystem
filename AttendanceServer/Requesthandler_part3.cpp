#include "RequestHandler.h"
#include "AttendanceServer.h"
#include <opencv2/opencv.hpp>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QPointer>
#include <QThread>
// JSON 转换为紧凑格式并加上换行符，通过跨线程安全发送
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
        // 吞掉所有异常，防止因socket底层已被回收导致的崩溃
    }
}
void RequestHandler::handlePunchRequest(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    if (!socket || !server) return;

    QString name = json["name"].toString();
    QDateTime serverNow = QDateTime::currentDateTime();
    QString timeStr = serverNow.toString("yyyy-MM-dd HH:mm:ss");
    QTime currentTime = serverNow.time();

    // 1. 查询部门
    QString dept = "全部";
    QSqlQuery dq(db);
    dq.prepare("SELECT department FROM users WHERE name = :n");
    dq.bindValue(":n", name);
    if (dq.exec() && dq.next()) {
        QString resDept = dq.value(0).toString();
        if (!resDept.isEmpty()) dept = resDept;
    }

    // 2. 查询排班规则
    QTime startTime(9, 0), endTime(18, 0);
    int absentMins = 120;

    QSqlQuery sq(db);
    sq.prepare("SELECT start_time, end_time, absent_mins FROM shift_rules "
        "WHERE dept = :d OR dept = '全部' ORDER BY (dept = :d) DESC LIMIT 1");
    sq.bindValue(":d", dept);

    if (sq.exec() && sq.next()) {
        // 🚩 核心修复 1：强制转为字符串并显式解析，防止 MySQL 驱动返回默认 00:00:00
        QString sStr = sq.value(0).toString();
        QString eStr = sq.value(1).toString();

        QTime pStart = QTime::fromString(sStr, "HH:mm:ss");
        if (!pStart.isValid()) pStart = QTime::fromString(sStr, "HH:mm");
        if (pStart.isValid()) startTime = pStart;

        QTime pEnd = QTime::fromString(eStr, "HH:mm:ss");
        if (!pEnd.isValid()) pEnd = QTime::fromString(eStr, "HH:mm");
        if (pEnd.isValid()) endTime = pEnd;

        absentMins = sq.value(2).toInt();
        if (absentMins <= 0) absentMins = 120;
    }

    // ---------------------------------------------------------
    // 🚩 核心修复 2：将绝对时间改为“距离基准线的偏移秒数”，彻底终结跨午夜 Bug
    // ---------------------------------------------------------

    int secsFromStart = startTime.secsTo(currentTime);
    // 跨天标准化：保证相对秒数在 -12 小时 到 +12 小时内
    if (secsFromStart < -12 * 3600) secsFromStart += 24 * 3600;
    if (secsFromStart > 12 * 3600)  secsFromStart -= 24 * 3600;

    int secsFromEnd = endTime.secsTo(currentTime);
    if (secsFromEnd < -12 * 3600) secsFromEnd += 24 * 3600;
    if (secsFromEnd > 12 * 3600)  secsFromEnd -= 24 * 3600;

    // 定义相对边界 (秒)
    int mWinStartSecs = -4 * 3600;                   // 上班前 4小时内
    int mWinEndSecs = (absentMins + 30) * 60;      // 上班后 旷工时限+30分钟 内

    int eWinStartSecs = -3 * 3600;                   // 下班前 3小时内
    int eWinEndSecs = 6 * 3600;                    // 下班后 6小时内

    QString status = "异常打卡";
    bool validWindow = false;

    // 早班判定区
    if (secsFromStart >= mWinStartSecs && secsFromStart <= mWinEndSecs) {
        validWindow = true;
        if (secsFromStart <= 0) {
            status = "正常(上班)";
        }
        else {
            int minsLate = secsFromStart / 60;
            status = (minsLate > absentMins) ? "旷工(迟到超限)" : "迟到";
        }
    }
    // 晚班判定区
    else if (secsFromEnd >= eWinStartSecs && secsFromEnd <= eWinEndSecs) {
        validWindow = true;
        if (secsFromEnd >= 0) {
            status = "正常(下班)";
        }
        else {
            status = "早退";
        }
    }

    // 时间窗拦截
    if (!validWindow) {
        QJsonObject res;
        res["status"] = "fail";
        res["msg"] = "当前不在有效的上下班打卡时间窗内！";
        sendJson(socket, res);

        QMetaObject::invokeMethod(server, [server, name, timeStr]() {
            if (server) server->logMessage(QString("<font color='#E6A23C'>[拦截] 员工 [%1] 在非打卡时段 (%2) 尝试打卡。</font>").arg(name, timeStr));
            }, Qt::QueuedConnection);
        return;
    }

    // 4. 合法记录落库
    QSqlQuery insertQuery(db);
    insertQuery.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (:n, :t, :s)");
    insertQuery.bindValue(":n", name);
    insertQuery.bindValue(":t", timeStr);
    insertQuery.bindValue(":s", status);

    if (insertQuery.exec()) {
        QJsonObject res;
        res["status"] = "success";
        res["msg"] = "打卡成功：" + status;
        sendJson(socket, res);

        QMetaObject::invokeMethod(server, [server, name, status]() {
            if (server) {
                server->logMessage(QString("<font color='#00B42A'>考勤成功: [%1] - %2</font>").arg(name, status));
                server->loadGlobalRecords();
            }
            }, Qt::QueuedConnection);
    }
}
// 处理客户端上报的作弊打卡（如活体检测失败/非本人特征强制打卡）
void RequestHandler::handlePunchCheat(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (?, NOW(), '作弊打卡')");
    q.addBindValue(json["name"].toString());
    q.exec();
    // 在服务端发出红色安全警报
    QString name = json["name"].toString();
    QMetaObject::invokeMethod(server, [server, name]() {
        server->logMessage(QString("<font color='red'>安全警报: 员工 [%1] 多次人脸核验失败，强制记为作弊！</font>").arg(name));
        server->loadGlobalRecords();
        }, Qt::QueuedConnection);
}
// 查询员工当天的考勤状态及打卡时间轴
void RequestHandler::handleQueryTodayStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QJsonObject res;
    res["status"] = "success";
    // 1. 检查今天是否在已批准的请假日期范围内
    QSqlQuery leaveQ(db);
    leaveQ.prepare("SELECT id FROM leave_requests WHERE applicant=:n AND status='已批准' "
        "AND CURDATE() BETWEEN DATE(start_time) AND DATE(end_time)");
    leaveQ.bindValue(":n", name);
    res["is_on_leave"] = (leaveQ.exec() && leaveQ.next());
    // 2. 查询今天的所有打卡记录流水
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
// 供客户端月度考勤日历视图调用的状态查询（渲染不同颜色的小圆点）
void RequestHandler::handleQueryMonthlyStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    int     year = json["year"].toInt();
    int     month = json["month"].toInt();
    // 计算当月的第一天和最后一天
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
    // 将同一天的多次打卡状态进行合并/覆盖判定
    QMap<QString, QString> dayStatus;
    if (q.exec(sql)) {
        while (q.next()) {
            QString dateStr = q.value(0).toDate().toString("yyyy-MM-dd");
            QString status = q.value(1).toString();
            QString existing = dayStatus.value(dateStr, "");
            // 状态优先级判定逻辑：旷工 > 迟到/早退 > 请假 > 正常
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
    // 统计本月的各类考勤天数，并生成日历颜色映射表
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

// 管理员专用：获取全员当月考勤汇总数据（用于月度汇总报表导出）
void RequestHandler::handleQueryMonthlySummaryAll(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    int year = json["year"].toInt();
    int month = json["month"].toInt();

    QString startDate = QString("%1-%2-01").arg(year).arg(month, 2, 10, QChar('0'));
    QDate lastDay(year, month, 1);
    lastDay = lastDay.addMonths(1).addDays(-1);
    QString endDate = lastDay.toString("yyyy-MM-dd");

    // 计算当月应出勤天数（排除周六日）
    int shouldWork = 0;
    QDate d(year, month, 1);
    while (d <= lastDay) {
        if (d.dayOfWeek() <= 5) shouldWork++;
        d = d.addDays(1);
    }

    // 查询所有员工的基本信息（增加 job_title 字段，并严格屏蔽超管及 admin 账号）
    QSqlQuery userQ(db);
    userQ.exec("SELECT name, department, job_title FROM users WHERE role != '超级管理员' AND account NOT LIKE '%admin%' ORDER BY department, name");

    QJsonArray summary;
    while (userQ.next()) {
        QString empName = userQ.value(0).toString();
        QString empDept = userQ.value(1).toString();
        QString empJob = userQ.value(2).toString();

        // 查询该员工当月的所有打卡记录
        QSqlQuery q(db);
        q.exec(QString(
            "SELECT DATE(punch_time) as d, status FROM attendance_records "
            "WHERE name = '%1' AND DATE(punch_time) BETWEEN '%2' AND '%3' "
            "ORDER BY punch_time ASC"
        ).arg(empName, startDate, endDate));

        // 按天合并状态（同一天多条记录取最高优先级）
        QMap<QString, QString> dayStatus;
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
            else if (status.contains("正常") || status.contains("下班") || status.contains("补卡")) {
                if (existing.isEmpty()) dayStatus[dateStr] = "normal";
            }
        }

        // 统计各项指标
        int actualWork = 0, lateCount = 0, earlyCount = 0, absentDays = 0, leaveDays = 0;
        for (auto it = dayStatus.begin(); it != dayStatus.end(); ++it) {
            if (it.value() == "normal")  actualWork++;
            else if (it.value() == "late") { actualWork++; lateCount++; }
            else if (it.value() == "leave") leaveDays++;
            else if (it.value() == "absent") absentDays++;
        }

        // 统计早退次数（单独查询含"早退"的记录数）
        QSqlQuery earlyQ(db);
        earlyQ.exec(QString(
            "SELECT COUNT(DISTINCT DATE(punch_time)) FROM attendance_records "
            "WHERE name = '%1' AND DATE(punch_time) BETWEEN '%2' AND '%3' AND status LIKE '%%早退%%'"
        ).arg(empName, startDate, endDate));
        if (earlyQ.next()) earlyCount = earlyQ.value(0).toInt();

        QJsonObject row;
        row["name"] = empName;
        row["dept"] = empDept;
        row["job_title"] = empJob;
        row["should_work"] = shouldWork;
        row["actual_work"] = actualWork;
        row["late_count"] = lateCount;
        row["early_count"] = earlyCount;
        row["absent_days"] = absentDays;
        row["leave_days"] = leaveDays;
        summary.append(row);
    }

    QJsonObject res;
    res["status"] = "success";
    res["summary"] = summary;
    sendJson(socket, res);
}

// 获取满足特定过滤条件的考勤明细（包含分页或条件筛选）
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
    // 处理特殊的状态过滤逻辑
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
            row["source"] = "A"; // 预留字段，代表数据来源标识
            recordsArr.append(row);
        }
    }
    QJsonObject response;
    response["status"] = "success";
    response["records"] = recordsArr;
    sendJson(socket, response);
}
// 专供 AI 或大屏展示使用的当天精简考勤数据
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
// 获取客户端首页看板图表所需的所有数据源
void RequestHandler::handleQueryHomeDashboard(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString role = json["role"].toString();
    QString name = json["name"].toString();
    QJsonObject res;
    res["status"] = "success";
    QJsonObject topCards;
    QSqlQuery   q(db);
    // 1. 顶部卡片：统计企业总人数、今日实到打卡人数、异常人数
    q.exec("SELECT COUNT(*) FROM users WHERE role != '超级管理员'");
    if (q.next()) topCards["total_expected"] = q.value(0).toInt();

    q.exec("SELECT COUNT(DISTINCT name) FROM attendance_records WHERE DATE(punch_time) = CURDATE()");
    if (q.next()) topCards["actual_punched"] = q.value(0).toInt();

    q.exec("SELECT COUNT(DISTINCT name) FROM attendance_records WHERE DATE(punch_time) = CURDATE() AND status NOT LIKE '%正常%'");
    if (q.next()) topCards["abnormal_count"] = q.value(0).toInt();
    // 管理层显示待审批数量，普通员工显示自己的异常打卡次数
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
    // 2. 饼图：今日各打卡状态分布比例
    QJsonArray pieArr;
    q.exec("SELECT status, COUNT(*) FROM attendance_records WHERE DATE(punch_time) = CURDATE() GROUP BY status");
    while (q.next()) {
        QJsonObject o; o["status"] = q.value(0).toString(); o["count"] = q.value(1).toInt();
        pieArr.append(o);
    }
    res["pie_chart"] = pieArr;
    // 3. 柱状图：近30天各部门的异常情况对比
    QJsonArray barArr;
    q.exec("SELECT u.department, COUNT(a.id) FROM users u LEFT JOIN attendance_records a ON u.name = a.name AND a.status NOT LIKE '%正常%' WHERE DATE(a.punch_time) >= DATE_SUB(CURDATE(), INTERVAL 30 DAY) GROUP BY u.department");
    while (q.next()) {
        QJsonObject o;
        o["dept"] = q.value(0).toString().isEmpty() ? "未分组" : q.value(0).toString();
        o["count"] = q.value(1).toInt();
        barArr.append(o);
    }
    res["bar_chart"] = barArr;
    // 4. 折线图：近7天的活跃打卡人数趋势
    QJsonArray lineArr;
    q.exec("SELECT DATE_FORMAT(punch_time, '%m-%d'), COUNT(DISTINCT name) FROM attendance_records WHERE punch_time >= DATE_SUB(CURDATE(), INTERVAL 6 DAY) GROUP BY DATE(punch_time) ORDER BY DATE(punch_time) ASC");
    while (q.next()) {
        QJsonObject o; o["date"] = q.value(0).toString(); o["count"] = q.value(1).toInt();
        lineArr.append(o);
    }
    res["line_chart"] = lineArr;
    // 5. 滚动动态流：最近 10 条打卡记录
    QJsonArray feedArr;
    q.exec("SELECT DATE_FORMAT(punch_time, '%H:%i:%s'), name, status FROM attendance_records WHERE DATE(punch_time) = CURDATE() ORDER BY punch_time DESC LIMIT 10");
    while (q.next()) {
        QJsonObject o; o["time"] = q.value(0).toString(); o["name"] = q.value(1).toString(); o["status"] = q.value(2).toString();
        feedArr.append(o);
    }
    res["feed_list"] = feedArr;
    // 6. 系统公告栏：获取最新的3条公告
    QJsonArray noticeArr;
    q.exec("SELECT content, DATE_FORMAT(publish_time, '%m-%d') FROM system_announcements ORDER BY publish_time DESC LIMIT 3");
    while (q.next()) {
        QJsonObject o; o["content"] = q.value(0).toString(); o["date"] = q.value(1).toString();
        noticeArr.append(o);
    }
    res["notice_list"] = noticeArr;
    sendJson(socket, res);
}
// 查询指定部门的排班与考勤惩罚规则
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
// 更新排班规则
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
    // 记录后台管理日志
    QMetaObject::invokeMethod(server, [server]() {
        server->logMessage("<font color='#E6A23C'>规则中心: 管理层更新了企业排班规则。</font>");
        }, Qt::QueuedConnection);
}
// 提交请假申请单
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
// 审批通过请假单，并在对应日期内生成连续的打卡流水
void RequestHandler::handleLeaveApprove(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    int     reqId = json["reqId"].toInt();
    QString applicant = json["applicant"].toString();
    QString sTimeStr = json["start_time"].toString();
    QString eTimeStr = json["end_time"].toString();
    QString lType = json["leave_type"].toString();
    QString currentApprover = json["approver"].toString();
    QString action = json["action"].toString();

    QSqlQuery selQ(db);
    selQ.prepare("SELECT approver FROM leave_requests WHERE id = :id");
    selQ.bindValue(":id", reqId);
    QString fullChain;
    if (selQ.exec() && selQ.next()) fullChain = selQ.value(0).toString();

    QStringList chain = fullChain.split(",", Qt::SkipEmptyParts);
    int idx = -1;
    for (int i = 0; i < chain.size(); i++) {
        if (chain[i] == currentApprover) { idx = i; break; }
    }
    if (idx < 0) {
        for (int i = 0; i < chain.size(); i++) {
            if (!chain[i].startsWith("[✓]") && !chain[i].startsWith("[✗]")) { idx = i; break; }
        }
    }

    // ── 驳回分支 ──
    if (action == "reject") {
        if (idx >= 0) chain[idx] = "[✗]" + currentApprover;
        QString rejectReason = json["reject_reason"].toString();
        QSqlQuery updQ(db);
        updQ.prepare("UPDATE leave_requests SET status = '已驳回', approver = :a, reject_reason = :r WHERE id = :id");
        updQ.bindValue(":a", chain.join(","));
        updQ.bindValue(":r", rejectReason);
        updQ.bindValue(":id", reqId);
        updQ.exec();
        QMetaObject::invokeMethod(server, [server, currentApprover, applicant]() {
            server->logMessage(QString("<font color='#F56C6C'>请假驳回: [%1] 驳回了 [%2] 的请假。</font>").arg(currentApprover, applicant));
            }, Qt::QueuedConnection);
        return;
    }

    // ── 通过分支 ──
    if (idx >= 0 && idx < chain.size() - 1) {
        // 不是最后一个审批人 → 标记已审批，保留完整链
        chain[idx] = "[✓]" + currentApprover;
        QSqlQuery updQ(db);
        updQ.prepare("UPDATE leave_requests SET approver = :a WHERE id = :id");
        updQ.bindValue(":a", chain.join(","));
        updQ.bindValue(":id", reqId);
        updQ.exec();

        QMetaObject::invokeMethod(server, [server, currentApprover, chain, idx]() {
            server->logMessage(QString("<font color='#E6A23C'>请假审批流转: [%1] 已批准，流转至 [%2]</font>")
                .arg(currentApprover, chain[idx + 1]));
            }, Qt::QueuedConnection);
    }
    else {
        // 最后一个审批人 → 终审通过，标记并插入考勤流水
        if (idx >= 0) chain[idx] = "[✓]" + currentApprover;
        QSqlQuery upd(db);
        upd.prepare("UPDATE leave_requests SET status = '已批准', approver = :a WHERE id = :id");
        upd.bindValue(":a", chain.join(","));
        upd.bindValue(":id", reqId);
        upd.exec();

        QString finalStatus = "假-" + lType;
        QDate sDate = QDate::fromString(sTimeStr.left(10), "yyyy-MM-dd");
        QDate eDate = QDate::fromString(eTimeStr.left(10), "yyyy-MM-dd");
        if (sDate.isValid() && eDate.isValid() && sDate <= eDate) {
            for (QDate d = sDate; d <= eDate; d = d.addDays(1)) {
                QString punchTime = d.toString("yyyy-MM-dd") + " 09:00:00";
                QSqlQuery insertQ(db);
                insertQ.prepare("INSERT INTO attendance_records (name, punch_time, status) VALUES (?, ?, ?)");
                insertQ.addBindValue(applicant);
                insertQ.addBindValue(punchTime);
                insertQ.addBindValue(finalStatus);
                insertQ.exec();
            }
        }
        QMetaObject::invokeMethod(server, [server, applicant]() {
            server->logMessage(QString("<font color='#67C23A'>终审通过: [%1] 的请假已全链审批通过。</font>").arg(applicant));
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}
// 提交异常考勤申诉记录
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
// 审批通过申诉，自动修复存在问题的考勤记录
void RequestHandler::handleAppealApprove(QSqlDatabase& db, QTcpSocket* /*socket*/,
    const QJsonObject& json, AttendanceServer* server)
{
    int     reqId = json["reqId"].toInt();
    QString applicant = json["applicant"].toString();
    QString aTime = json["abnormal_time"].toString();
    QString aType = json["appeal_type"].toString();
    QString currentApprover = json["approver"].toString();
    QString action = json["action"].toString();  // "approve" 或 "reject"

    // 读取数据库中完整的审批链
    QSqlQuery selQ(db);
    selQ.prepare("SELECT approver, original_status FROM appeals WHERE id = :id");
    selQ.bindValue(":id", reqId);
    QString fullChain, origStatus;
    if (selQ.exec() && selQ.next()) {
        fullChain = selQ.value(0).toString();
        origStatus = selQ.value(1).toString();
    }

    QStringList chain = fullChain.split(",", Qt::SkipEmptyParts);
    int idx = -1;
    for (int i = 0; i < chain.size(); i++) {
        if (chain[i] == currentApprover) { idx = i; break; }
    }
    if (idx < 0) {
        for (int i = 0; i < chain.size(); i++) {
            if (!chain[i].startsWith("[✓]") && !chain[i].startsWith("[✗]")) { idx = i; break; }
        }
    }

    // ── 驳回分支 ──
    if (action == "reject") {
        if (idx >= 0) chain[idx] = "[✗]" + currentApprover;
        QString rejectReason = json["reject_reason"].toString();
        QSqlQuery updQ(db);
        updQ.prepare("UPDATE appeals SET status = '已驳回', approver = :a, reject_reason = :r WHERE id = :id");
        updQ.bindValue(":a", chain.join(","));
        updQ.bindValue(":r", rejectReason);
        updQ.bindValue(":id", reqId);
        updQ.exec();
        QMetaObject::invokeMethod(server, [server, currentApprover, applicant]() {
            server->logMessage(QString("<font color='#F56C6C'>审批驳回: [%1] 驳回了 [%2] 的申诉。</font>")
                .arg(currentApprover, applicant));
            }, Qt::QueuedConnection);
        return;
    }

    // ── 通过分支 ──
    if (idx >= 0 && idx < chain.size() - 1) {
        chain[idx] = "[✓]" + currentApprover;
        QSqlQuery updQ(db);
        updQ.prepare("UPDATE appeals SET approver = :a WHERE id = :id");
        updQ.bindValue(":a", chain.join(","));
        updQ.bindValue(":id", reqId);
        updQ.exec();
        QMetaObject::invokeMethod(server, [server, currentApprover, chain, idx]() {
            server->logMessage(QString("<font color='#E6A23C'>审批流转: [%1] 已批准，流转至 [%2]</font>")
                .arg(currentApprover, chain[idx + 1]));
            }, Qt::QueuedConnection);
    }
    else {
        if (idx >= 0) chain[idx] = "[✓]" + currentApprover;
        QSqlQuery upd(db);
        upd.prepare("UPDATE appeals SET status = '已批准', approver = :a WHERE id = :id");
        upd.bindValue(":a", chain.join(","));
        upd.bindValue(":id", reqId);
        upd.exec();
        if (origStatus != "人脸重录") {
            QSqlQuery fixQ(db);
            if (aType == "整天申诉") fixQ.exec(QString("UPDATE attendance_records SET status='正常(修正)' WHERE name='%1' AND DATE(punch_time)=DATE('%2')").arg(applicant, aTime));
            else fixQ.exec(QString("UPDATE attendance_records SET status='正常(修正)' WHERE name='%1' AND punch_time='%2'").arg(applicant, aTime));
        }
        QMetaObject::invokeMethod(server, [server, applicant, origStatus]() {
            server->logMessage(QString("<font color='#67C23A'>终审通过: [%1] 的 [%2] 已全链审批通过。</font>").arg(applicant, origStatus));
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}
// 管理层获取分派给自己的待处理请假单
void RequestHandler::handleQueryPendingLeaves(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString approver = json["approver"].toString();
    QJsonArray pendingArr, doneArr;
    QSqlQuery q(db);
    q.exec("SELECT id, applicant, leave_type, start_time, end_time, reason, approver, status, IFNULL(reject_reason,'') FROM leave_requests ORDER BY id DESC");
    while (q.next()) {
        QString approverChain = q.value(6).toString();
        QString status = q.value(7).toString();
        bool isPending = false, isDone = false;
        QStringList parts = approverChain.split(",", Qt::SkipEmptyParts);
        for (int i = 0; i < parts.size(); i++) {
            if (parts[i] == approver && status == "待审批") { isPending = true; break; }
            if (parts[i] == "[✓]" + approver || parts[i] == "[✗]" + approver) { isDone = true; break; }
        }
        QJsonObject row;
        row["id"] = q.value(0).toInt();
        row["applicant"] = q.value(1).toString();
        row["type"] = q.value(2).toString();
        row["start"] = q.value(3).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        row["end"] = q.value(4).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        row["reason"] = q.value(5).toString();
        row["approver_chain"] = approverChain;
        row["status"] = status;
        row["reject_reason"] = q.value(8).toString();
        if (isPending) pendingArr.append(row);
        else if (isDone) doneArr.append(row);
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = pendingArr;
    res["done_data"] = doneArr;
    sendJson(socket, res);
}
// 管理层获取分派给自己的待处理申诉单（含已审批记录）
void RequestHandler::handleQueryPendingAppeals(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString approver = json["approver"].toString();
    QJsonArray pendingArr, doneArr;
    QSqlQuery q(db);
    // 查询所有待审批的申诉单（approver中包含当前用户名）
    q.exec("SELECT id, applicant, abnormal_time, original_status, reason, approver, status, IFNULL(reject_reason,'') FROM appeals ORDER BY id DESC");
    while (q.next()) {
        QString approverChain = q.value(5).toString();
        QString status = q.value(6).toString();
        bool isPending = false;
        bool isDone = false;
        QStringList parts = approverChain.split(",", Qt::SkipEmptyParts);
        for (int i = 0; i < parts.size(); i++) {
            if (parts[i] == approver && status == "待审批") { isPending = true; break; }
            if (parts[i] == "[✓]" + approver || parts[i] == "[✗]" + approver) { isDone = true; break; }
        }
        QJsonObject row;
        row["id"] = q.value(0).toInt();
        row["applicant"] = q.value(1).toString();
        row["time"] = q.value(2).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        row["type"] = q.value(3).toString();
        row["reason"] = q.value(4).toString();
        row["approver_chain"] = approverChain;
        row["status"] = status;
        row["reject_reason"] = q.value(7).toString();
        if (isPending) pendingArr.append(row);
        else if (isDone) doneArr.append(row);
    }
    QJsonObject res;
    res["status"] = "success";
    res["data"] = pendingArr;
    res["done_data"] = doneArr;
    sendJson(socket, res);
}
// 员工获取自己发起的流程审批进度历史（含请假和申诉）
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
// 客户端发起申诉前，获取可以指派的审批人列表及自身的异常打卡流水供选择
void RequestHandler::handleQueryApprovalCandidates(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString name = json["name"].toString();
    QJsonObject res;
    // 1. 获取员工自身岗位信息用于限定审批流层级
    QSqlQuery infoQ(db);
    infoQ.prepare("SELECT role, department, job_title FROM users WHERE name=:n");
    infoQ.bindValue(":n", name);
    if (infoQ.exec() && infoQ.next()) {
        res["my_role"] = infoQ.value(0).toString();
        res["my_dept"] = infoQ.value(1).toString();
        res["my_job"] = infoQ.value(2).toString();
    }
    // 2. 罗列该员工最近 10 条处于异常状态（不是'正常'）的打卡记录
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
    // 3. 构建多级审批人列表：HR、总经理、直属部门经理
    QJsonArray hrArr, gmArr, mgrArr;
    QSqlQuery hq(db);
    hq.exec("SELECT name FROM users WHERE department='人力资源部' AND job_title='部门经理' AND name NOT LIKE '%超级管理员%'");
    while (hq.next()) hrArr.append(hq.value(0).toString());
    QSqlQuery gq(db);
    gq.exec("SELECT name FROM users WHERE job_title IN ('总经理', '总裁', '董事长') OR name='总经理' LIMIT 1");
    while (gq.next()) gmArr.append(gq.value(0).toString());
    QSqlQuery mq(db);
    mq.prepare("SELECT name FROM users WHERE department=:d AND job_title='部门经理' AND name != :me");
    mq.bindValue(":d", res["my_dept"].toString());
    mq.bindValue(":me", name); // 防止部门经理自己向自己发起审批
    mq.exec();
    while (mq.next()) mgrArr.append(mq.value(0).toString());
    res["hr_list"] = hrArr;
    res["gm_list"] = gmArr;
    res["mgr_list"] = mgrArr;
    sendJson(socket, res);
}
void RequestHandler::handleQueryDeptSummary(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json)
{
    QString role = json["role"].toString();
    QString loginName = json["name"].toString();
    QString startDate = json["start_date"].toString();
    QString endDate = json["end_date"].toString();

    QJsonObject res;
    res["status"] = "fail";

    // 需求 3：零信任模型，服务端实施强制鉴权拦截
    QString targetDept = "";
    if (role == "经理") {
        QSqlQuery dq(db);
        dq.prepare("SELECT department FROM users WHERE name = :n");
        dq.bindValue(":n", loginName);
        if (dq.exec() && dq.next()) {
            targetDept = dq.value(0).toString();
        }
        else {
            res["msg"] = "未能识别您的所属部门，拦截越权访问。";
            sendJson(socket, res);
            return;
        }
    }
    else if (role != "管理员登录" && role != "超级管理员") {
        res["msg"] = "非管理层角色，拒绝部门级数据查询。";
        sendJson(socket, res);
        return;
    }

    // 计算统计区间内的标准工作日天数
    int expectedWorkDays = 0;
    QDate sD = QDate::fromString(startDate, "yyyy-MM-dd");
    QDate eD = QDate::fromString(endDate, "yyyy-MM-dd");
    for (QDate d = sD; d <= eD; d = d.addDays(1)) {
        if (d.dayOfWeek() <= 5) expectedWorkDays++; // 假定双休为标准
    }

    // 获取需要统计的部门列表
    QString deptSql = "SELECT department, COUNT(id) FROM users WHERE role != '超级管理员' AND account != 'admin'";
    if (!targetDept.isEmpty()) {
        deptSql += QString(" AND department = '%1'").arg(targetDept);
    }
    deptSql += " GROUP BY department";

    QJsonArray dataArr;
    QSqlQuery qDept(db);
    qDept.exec(deptSql);

    while (qDept.next()) {
        QString deptName = qDept.value(0).toString();
        if (deptName.isEmpty()) deptName = "未分配部门";
        int totalPeople = qDept.value(1).toInt();
        int expectedManDays = totalPeople * expectedWorkDays;

        // 提取该部门在区间内的所有打卡记录明细进行二次聚合
        QString recordSql = QString(
            "SELECT a.status FROM attendance_records a "
            "JOIN users u ON a.name = u.name "
            "WHERE u.department = '%1' AND DATE(a.punch_time) BETWEEN '%2' AND '%3'"
        ).arg(deptName, startDate, endDate);

        QSqlQuery qRec(db);
        qRec.exec(recordSql);

        int totalLate = 0, totalEarly = 0, totalAbsent = 0, totalLeave = 0, actualNormal = 0;
        while (qRec.next()) {
            QString status = qRec.value(0).toString();
            if (status.contains("迟到")) totalLate++;
            else if (status.contains("早退")) totalEarly++;
            else if (status.contains("旷工")) totalAbsent++;
            else if (status.contains("假")) totalLeave++;
            else if (status.contains("正常") || status.contains("补卡")) actualNormal++;
        }

        // 需求 5：严谨计算考核指标，防御除零异常
        double attendanceRate = 0.0;
        double abnormalRate = 0.0;
        double deductHours = 0.0;

        if (expectedManDays > 0) {
            // 出勤率 = (正常人天 + 迟到/早退按出勤算) / 应出勤人天
            // 注意：具体公式可依企业制度调整，这里给出通用模板
            attendanceRate = double(actualNormal + totalLate + totalEarly) / expectedManDays * 100.0;

            // 异常占比 = (迟到 + 早退 + 旷工) / 应出勤人天
            abnormalRate = double(totalLate + totalEarly + totalAbsent) / expectedManDays * 100.0;
        }

        // 扣薪工时规则设定 (示例：迟到早退各扣 0.5H，旷工扣 8H)
        deductHours = (totalLate * 0.5) + (totalEarly * 0.5) + (totalAbsent * 8.0);

        QJsonObject row;
        row["dept_name"] = deptName;
        row["total_people"] = totalPeople;
        row["expected_mandays"] = expectedManDays;
        row["total_late"] = totalLate;
        row["total_early"] = totalEarly;
        row["total_absent"] = totalAbsent;
        row["total_leave"] = totalLeave;
        row["abnormal_rate"] = QString::number(abnormalRate, 'f', 1) + "%";
        row["deduct_hours"] = deductHours;
        row["attendance_rate"] = QString::number(attendanceRate, 'f', 1) + "%";

        dataArr.append(row);
    }

    res["status"] = "success";
    res["data"] = dataArr;
    sendJson(socket, res);
}
// 执行每日隐性旷工盘点批处理 (业务逻辑层)
void RequestHandler::executeDailyAbsentCheck(QSqlDatabase& db, AttendanceServer* server)
{
    QSqlQuery query(db);
    QString sql =
        "INSERT INTO attendance_records (name, punch_time, status) "
        "SELECT u.name, NOW(), '旷工(缺卡)' "
        "FROM users u "
        "WHERE u.role != '超级管理员' AND u.account NOT LIKE '%admin%' "
        "  AND u.name NOT IN (SELECT name FROM attendance_records WHERE DATE(punch_time) = CURDATE()) "
        "  AND u.name NOT IN (SELECT applicant FROM leave_requests WHERE status='已批准' AND CURDATE() BETWEEN DATE(start_time) AND DATE(end_time))";

    if (query.exec(sql)) {
        // 业务盘点成功后，调用服务端实例打印日志并刷新全局视图
        QMetaObject::invokeMethod(server, [server]() {
            server->logMessage("<font color='red'>系统通知: 每日隐性旷工盘点业务已执行完毕，缺卡人员已记为旷工。</font>");
            server->loadGlobalRecords();
            }, Qt::QueuedConnection);
    }
}
void RequestHandler::handleSecurePunchRequest(QSqlDatabase& db, QTcpSocket* socket,
    const QJsonObject& json, AttendanceServer* server)
{
    // 【基础安全检查】
    if (!socket || !socket->isValid() || socket->state() != QAbstractSocket::ConnectedState) return;
    if (!server) return;

    QString base64Feature = json["feature"].toString();
    if (base64Feature.isEmpty()) return;

    // 1. 安全解码客户端发来的 Base64 特征
    QByteArray incomingBytes = QByteArray::fromBase64(base64Feature.toUtf8());

    // 【严苛校验】：ArcFace w600k_r50 提取的特征必须是 512 个 float (2048 字节)
    const int EXPECTED_FEATURE_BYTES = 512 * sizeof(float); // 2048
    if (incomingBytes.size() != EXPECTED_FEATURE_BYTES) {
        QJsonObject res;
        res["status"] = "fail";
        res["msg"] = "安全拦截：非法或损坏的人脸特征包！";
        sendJson(socket, res);
        return;
    }

    // 2. 深度拷贝特征到安全的 OpenCV 矩阵
    cv::Mat incomingMat(1, 512, CV_32F);
    memcpy(incomingMat.data, incomingBytes.constData(), EXPECTED_FEATURE_BYTES);

    // 3. 服务端内存级 1:N 特征检索
    QString matchedName = "";
    double maxSim = 0.0;
    const double THRESHOLD = 0.6; // 余弦相似度阈值

    QSqlQuery q(db);
    q.exec("SELECT name, feature FROM users WHERE feature IS NOT NULL");
    while (q.next()) {
        QString dbName = q.value(0).toString();
        QByteArray dbFeatureBytes = q.value(1).toByteArray();

        // 校验数据库里读出的特征长度是否合法
        if (dbFeatureBytes.size() != EXPECTED_FEATURE_BYTES) continue;

        // 深度拷贝数据库特征到安全的 OpenCV 矩阵
        cv::Mat dbMat(1, 512, CV_32F);
        memcpy(dbMat.data, dbFeatureBytes.constData(), EXPECTED_FEATURE_BYTES);

        // 计算点乘（余弦相似度）
        double sim = incomingMat.dot(dbMat);

        if (sim > maxSim) {
            maxSim = sim;
            matchedName = dbName;
        }
    }

    // 4. 权威拦截判定
    if (matchedName.isEmpty() || maxSim < THRESHOLD) {
        QJsonObject res;
        res["status"] = "fail";
        res["msg"] = QString("未识别到匹配员工，或相似度过低 (%1)").arg(maxSim);
        sendJson(socket, res);

        QMetaObject::invokeMethod(server, [server]() {
            if (server) server->logMessage("<font color='red'>[防伪拦截] 打卡请求被拒绝，特征匹配失败！</font>");
            }, Qt::QueuedConnection);
        return;
    }

    // 5. 身份核验通过，利用服务端生成的权威身份落库
    QMetaObject::invokeMethod(server, [server, matchedName, maxSim]() {
        if (server) server->logMessage(QString("<font color='green'>[特征核验通过] 确认为 [%1]，相似度: %2</font>").arg(matchedName).arg(maxSim));
        }, Qt::QueuedConnection);

    // 构造内部信任 JSON 丢给落库函数
    QJsonObject secureJson;
    secureJson["name"] = matchedName;

    handlePunchRequest(db, socket, secureJson, server);
}