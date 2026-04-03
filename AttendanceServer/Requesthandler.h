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
    // 人脸 & 账号
    static void handleQueryFaceFeatures(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询并返回用户人脸特征
    static void handleRegisterFace(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 注册或更新用户人脸特征
    static void handleClientLoginAuth(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 验证客户端登录凭据并建立会话
    static void handleClientRegisterAccount(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 处理客户端账号注册请求
    static void handleVerifyUserForRegistration(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 校验注册信息合法性
    static void handleSecurePunchRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 安全打卡请求：包含人脸特征比对
    // 登录/在线状态
    static void handleLogin(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 客户端登录并更新在线状态
    static void handleStatusUpdate(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 更新客户端状态信息
    // 聊天&消息路由
    static void handleChatMessage(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json,
        const QByteArray& rawData, AttendanceServer* server); // 处理并转发聊天消息
    static void handleQueryChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询聊天历史
    static void handleQueryChatContacts(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询聊天联系人
    static void handleQueryGroupMembers(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询群组成员列表
    static void handleReadReceipt(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 处理已读回执
    static void handleBroadcast(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json,
        const QByteArray& rawData, AttendanceServer* server); // 处理广播消息
    static void handlePublishAnnouncement(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 发布系统公告
    // 用户
    static void handleQueryUserProfile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询用户档案
    static void handleUpdateProfileField(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 更新用户档案字段
    static void handleQueryUserList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询用户列表
    static void handleQueryUserDept(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询用户部门信息
    static void handleVerifyAndUpdatePassword(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 验证并修改密码
    static void handleFaceReregisterRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 处理人脸重录申请
    static void handleUploadAvatarFile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 处理头像上传
    // 管理员操作
    static void handleAdminResetPassword(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 管理员重置密码
    static void handleAdminDeleteUser(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 管理员删除用户
    static void handleAdminModifyStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 管理员修改用户状态
    static void handleQueryAvatarFile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询头像文件
    static void handleQueryDeptList(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询部门列表
    // 考勤核心
    static void handlePunchRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 处理普通打卡并落库
    static void handlePunchCheat(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 记录作弊打卡并告警
    static void handleQueryTodayStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询今日考勤状态
    static void handleQueryMonthlyStatus(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 返回月度按日状态
    static void handleQueryMonthlySummaryAll(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 月度汇总数据导出
    static void handleQueryAttendanceDetail(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询考勤明细
    static void handleQueryTodayAttendanceForAi(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 提供当天数据给 AI
    static void handleQueryHomeDashboard(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 聚合首页看板数据
    static void handleQueryDeptSummary(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 生成部门汇总统计
    // 排班规则
    static void handleQueryShiftRule(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询排班规则
    static void handleRuleSettings(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 更新排班规则
    // 请假 & 申诉
    static void handleLeaveRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 提交请假申请
    static void handleLeaveApprove(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 处理请假审批
    static void handleAppealRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 提交申诉
    static void handleAppealApprove(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // 处理申诉审批并修正记录
    static void handleQueryPendingLeaves(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询待审批请假
    static void handleQueryPendingAppeals(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询待审批申诉
    static void handleQueryMyRequests(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询我的申请历史
    static void handleQueryApprovalCandidates(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 获取可选审批人列表
    // AI 助手
    static void handleAiSaveMessage(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 保存 AI 会话消息
    static void handleCreateAiSession(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 创建 AI 会话
    static void handleQueryAiSessions(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询 AI 会话列表
    static void handleRenameAiSession(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 重命名 AI 会话
    static void handleDeleteAiSession(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 删除 AI 会话
    static void handleSearchAiHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 搜索 AI 历史记录
    static void handleQueryAiChatHistory(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json); // 查询 AI 聊天历史
    static void handleAiAuditFile(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); // AI 审计文件并记录
    static void handleAiChatRequest(QSqlDatabase& db, QTcpSocket* socket, const QJsonObject& json, AttendanceServer* server); //AI路由
    // 定时业务脚本
    static void executeDailyAbsentCheck(QSqlDatabase& db, AttendanceServer* server); // 执行每日旷工盘点
};

#endif // REQUESTHANDLER_H