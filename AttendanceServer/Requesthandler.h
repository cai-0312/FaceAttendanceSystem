#ifndef REQUESTHANDLER_H
#define REQUESTHANDLER_H
#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>
#include <QSqlDatabase>
class AttendanceServer;
class RequestHandler
{
public:
    // ── 人脸 & 账号 ──────────────────────────────────────────────
    static void handleQueryFaceFeatures(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleRegisterFace(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleClientLoginAuth(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleClientRegisterAccount(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleVerifyUserForRegistration(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleSecurePunchRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    // ── 登录 / 在线状态 ───────────────────────────────────────────
    static void handleLogin(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleStatusUpdate(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    // ── 聊天 & 消息路由 ───────────────────────────────────────────
    static void handleChatMessage(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json,
        const QByteArray& rawData, AttendanceServer* server);
    static void handleQueryChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryChatContacts(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryGroupMembers(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleReadReceipt(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleBroadcast(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json,
        const QByteArray& rawData, AttendanceServer* server);
    static void handlePublishAnnouncement(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    // ── 用户档案 ─────────────────────────────────────────────────
    static void handleQueryUserProfile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleUpdateProfileField(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryUserList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryUserDept(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    // ── 管理员操作 ───────────────────────────────────────────────
    static void handleAdminResetPassword(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleAdminDeleteUser(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleAdminModifyStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    // ── 考勤核心 ─────────────────────────────────────────────────
    static void handlePunchRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handlePunchCheat(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleQueryTodayStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryMonthlyStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryMonthlySummaryAll(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryAttendanceDetail(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryTodayAttendanceForAi(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryHomeDashboard(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryDeptSummary(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    // ── 排班规则 ─────────────────────────────────────────────────
    static void handleQueryShiftRule(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleRuleSettings(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    // ── 请假 & 申诉 ──────────────────────────────────────────────
    static void handleLeaveRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleLeaveApprove(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleAppealRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleAppealApprove(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    static void handleQueryPendingLeaves(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryPendingAppeals(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryMyRequests(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryApprovalCandidates(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    // ── AI 助手 ──────────────────────────────────────────────────
    static void handleAiSaveMessage(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleCreateAiSession(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryAiSessions(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleRenameAiSession(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleDeleteAiSession(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleSearchAiHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleQueryAiChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json);
    static void handleAiAuditFile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server);
    // ── 定时业务脚本 ──────────────────────────────────────────────
    static void executeDailyAbsentCheck(QSqlDatabase& db, AttendanceServer* server); // 每日隐性旷工盘点逻辑
};

#endif // REQUESTHANDLER_H