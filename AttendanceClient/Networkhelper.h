#ifndef NETWORKHELPER_H
#define NETWORKHELPER_H
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
class NetworkHelper
{
public:
    static void setServer(const QString& ip, quint16 port);                 // 全局配置：设置服务器地址，默认连接127.0.0.1:9999
    static QString serverIp();                                              // 获取当前服务器IP配置
    static quint16 serverPort();                                            // 获取当前服务器端口配置
    static QJsonObject request(const QJsonObject& jsonRequest, int connectTimeout = 2000, int readTimeout = 5000);        // 同步请求：阻塞发送并返回JSON响应，适用于需要立即获取结果的查询
    static void sendAsync(const QJsonObject& jsonRequest);                                                                // 异步发送：非阻塞后台投递，无返回值，适用于状态更新或日志上报
    static void sendReliable(const QJsonObject& jsonRequest);                                                             // 同步发送：阻塞至数据完整写入网卡，适用于大体积文件或特征传输
private:
    static QString s_serverIp;                                                      // 内部保存的服务器IP地址
    static quint16 s_serverPort;                                                      // 内部保存的服务器端口号
    NetworkHelper() = delete;                                                          // 禁止实例化：纯静态工具类
    ~NetworkHelper() = delete;                                                          // 禁止实例化：纯静态工具类
    NetworkHelper(const NetworkHelper&) = delete;                                      // 禁止拷贝构造
    NetworkHelper& operator=(const NetworkHelper&) = delete;                           // 禁止赋值操作
};
#endif // NETWORKHELPER_H