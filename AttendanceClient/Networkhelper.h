#pragma once
#ifndef NETWORKHELPER_H
#define NETWORKHELPER_H

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

class NetworkHelper
{
public:
    // ========================================================================
    //  全局配置：设置服务器地址（程序启动时调用一次即可）
    //  若不调用，默认连接 127.0.0.1:9999
    // ========================================================================
    static void setServer(const QString& ip, quint16 port);

    // ========================================================================
    //  获取当前服务器配置（供调试或日志使用）
    // ========================================================================
    static QString serverIp();
    static quint16 serverPort();

    // ========================================================================
    //  【核心方法 1】同步请求（阻塞式，有返回值）
    // ========================================================================
    //  发送 JSON 请求到服务器，阻塞等待并返回服务器的 JSON 响应。
    //
    //  适用场景：
    //    - 登录验证（client_login_auth）
    //    - 各类数据查询（query_home_dashboard / query_attendance_detail 等）
    //    - 任何需要立即拿到服务器返回结果的操作
    //
    //  参数：
    //    jsonRequest    → 要发送的 JSON 请求体（必须包含 "type" 字段）
    //    connectTimeout → TCP 连接超时，单位毫秒，默认 2000
    //    readTimeout    → 等待服务器首次回包超时，单位毫秒，默认 5000
    //
    //  返回值：
    //    成功 → 服务器返回的完整 QJsonObject
    //    失败 → 空的 QJsonObject（可用 .isEmpty() 判断）
    //
    //  使用示例：
    //    QJsonObject req;
    //    req["type"] = "query_home_dashboard";
    //    req["name"] = m_loginName;
    //    QJsonObject res = NetworkHelper::request(req);
    //    if (res["status"].toString() == "success") { ... }
    // ========================================================================
    static QJsonObject request(const QJsonObject& jsonRequest,
        int connectTimeout = 2000,
        int readTimeout = 5000);

    // ========================================================================
    //  【核心方法 2】异步发送（非阻塞，无返回值，fire-and-forget）
    // ========================================================================
    //  将 JSON 请求投递到独立的后台线程发送，主线程立即返回，不等待响应。
    //
    //  适用场景：
    //    - 打卡指令下发（punch_request）
    //    - 审计日志上报（ai_audit / ai_audit_file）
    //    - 状态更新（status_update）
    //    - 系统公告发布（publish_announcement）
    //    - 所有"只管发出去、不关心服务器回什么"的操作
    //
    //  使用示例：
    //    QJsonObject req;
    //    req["type"] = "punch_request";
    //    req["name"] = userName;
    //    NetworkHelper::sendAsync(req);
    // ========================================================================
    static void sendAsync(const QJsonObject& jsonRequest);

    // ========================================================================
    //  【核心方法 3】同步发送（阻塞式，无返回值）
    // ========================================================================
    //  阻塞式发送 JSON 请求，确保数据完整写入网卡缓冲区后才返回。
    //  不关心服务器响应内容，但保证数据一定送达。
    //
    //  适用场景：
    //    - 人脸特征注册（register_face）—— 数据量约 3KB，必须完整传输
    //    - 大体积头像上传（update_profile_field + avatar）
    //    - 任何对"数据完整性"有严格要求、但不需要读回响应的操作
    //
    //  使用示例：
    //    QJsonObject req;
    //    req["type"] = "register_face";
    //    req["name"] = name;
    //    req["feature"] = QString(featureBytes.toBase64());
    //    NetworkHelper::sendReliable(req);
    // ========================================================================
    static void sendReliable(const QJsonObject& jsonRequest);

private:
    static QString  s_serverIp;
    static quint16  s_serverPort;

    // 禁止实例化：纯静态工具类
    NetworkHelper() = delete;
    ~NetworkHelper() = delete;
    NetworkHelper(const NetworkHelper&) = delete;
    NetworkHelper& operator=(const NetworkHelper&) = delete;
};

#endif // NETWORKHELPER_H