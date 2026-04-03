#ifndef NETWORKHELPER_H
#define NETWORKHELPER_H
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QMutex>
#include <functional>
class NetworkHelper
{
public:
    static void setServer(const QString& ip, quint16 port); // 设置服务端 IP 与 端口
    static QString serverIp();                               // 获取当前配置的服务端 IP
    static quint16 serverPort();                             // 获取当前配置的服务端端口
    static QJsonObject request(const QJsonObject& jsonRequest, int connectTimeout = 2000, int readTimeout = 5000); // 同步阻塞请求并返回 JSON 响应
    static void sendAsync(const QJsonObject& jsonRequest); // 异步发送请求，在后台线程完成连接/发送/接收/断开流程
    static void sendReliable(const QJsonObject& jsonRequest); // 可靠发送以保证大数据完整写入后再断开连接
    // 长超时异步请求
    static void requestLongAsync(const QJsonObject& jsonRequest, QObject* context, std::function<void(const QJsonObject&)> callback, int connectTimeout = 2000, int readTimeout = 65000); 
private:
    static QString s_serverIp;      // 存储服务端 IP
    static quint16 s_serverPort;    // 存储服务端端口
    static QMutex s_requestMutex;   // 保护同步请求的互斥锁
    NetworkHelper() = delete;       // 禁用实例化
    ~NetworkHelper() = delete;      // 禁用析构
    NetworkHelper(const NetworkHelper&) = delete; // 禁用拷贝构造
    NetworkHelper& operator=(const NetworkHelper&) = delete; // 禁用拷贝赋值
};
#endif // NETWORKHELPER_H