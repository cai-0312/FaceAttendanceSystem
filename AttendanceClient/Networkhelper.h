#ifndef NETWORKHELPER_H
#define NETWORKHELPER_H

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QMutex>

class NetworkHelper
{
public:
    static void setServer(const QString& ip, quint16 port);
    static QString serverIp();
    static quint16 serverPort();

    // 同步请求：阻塞发送并返回JSON响应（线程安全，内部串行化）
    static QJsonObject request(const QJsonObject& jsonRequest, int connectTimeout = 2000, int readTimeout = 5000);

    // 异步发送：在后台线程中执行完整的 连接→发送→等响应→断开 生命周期
    static void sendAsync(const QJsonObject& jsonRequest);

    // 可靠同步发送：确保大体积数据完整写入后再断开
    static void sendReliable(const QJsonObject& jsonRequest);

private:
    static QString s_serverIp;
    static quint16 s_serverPort;
    static QMutex s_requestMutex;  // 保护同步请求不被并发

    NetworkHelper() = delete;
    ~NetworkHelper() = delete;
    NetworkHelper(const NetworkHelper&) = delete;
    NetworkHelper& operator=(const NetworkHelper&) = delete;
};

#endif // NETWORKHELPER_H