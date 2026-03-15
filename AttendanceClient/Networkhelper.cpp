#include "NetworkHelper.h"
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent>
// 初始化全局静态变量：默认服务器IP地址与通信端口号
QString NetworkHelper::s_serverIp = QStringLiteral("127.0.0.1");
quint16 NetworkHelper::s_serverPort = 9999;
// 全局网络配置：配置目标服务器的IP地址与通信端口
void NetworkHelper::setServer(const QString& ip, quint16 port)
{
    s_serverIp = ip;
    s_serverPort = port;
}
// 获取服务器IP：返回当前系统配置的目标服务器IP地址
QString NetworkHelper::serverIp() {
    return s_serverIp;
}
// 获取服务器端口：返回当前系统配置的目标服务器端口号
quint16 NetworkHelper::serverPort() {
    return s_serverPort;
}
// 同步请求核心机制：发起TCP连接，阻塞发送JSON请求体，并等待解析完整的JSON响应数据
QJsonObject NetworkHelper::request(const QJsonObject& jsonRequest, int connectTimeout, int readTimeout)
{
    QTcpSocket socket;
    socket.connectToHost(s_serverIp, s_serverPort);
    QJsonObject responseJson;
    QString reqType = jsonRequest["type"].toString();
    // 等待建立TCP连接，超时则直接返回空对象
    if (!socket.waitForConnected(connectTimeout)) {
        return responseJson;
    }
    // 将 JSON 请求体序列化并追加换行符作为数据包边界，写入套接字底层缓冲区
    QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
    socket.write(block);
    socket.waitForBytesWritten(2000);
    QByteArray responseData;
    // 核心读取循环：只要没有超时断开，就持续读取底层数据，直到接收到完整的包尾换行符
    while (socket.waitForReadyRead(readTimeout)) {
        responseData += socket.readAll();
        // 只要数据包包含了服务端发来的 \n 结束符，就代表接收完成
        if (responseData.contains("\n")) {
            QJsonDocument checkDoc = QJsonDocument::fromJson(responseData.trimmed());
            if (!checkDoc.isNull() && checkDoc.isObject()) {
                responseJson = checkDoc.object();
                break;
            }
        }
    }
    // 释放资源：断开当前短连接套接字
    socket.disconnectFromHost();
    return responseJson;
}
// 异步发送机制：利用QtConcurrent开启后台独立线程完成网络投递，不阻塞主线程且不等待响应
void NetworkHelper::sendAsync(const QJsonObject& jsonRequest)
{
    QString ip = s_serverIp;
    quint16 port = s_serverPort;
    QString reqType = jsonRequest["type"].toString();
    // 开启独立线程作用域进行网络 IO，避免阻塞界面主线程
    QtConcurrent::run([jsonRequest, ip, port, reqType]() {
        QTcpSocket socket;
        socket.connectToHost(ip, port);
        // 尝试连接并在成功后迅速写入数据
        if (socket.waitForConnected(1500)) {
            QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
            socket.write(block);
            socket.waitForBytesWritten(1000);
            socket.disconnectFromHost();
        }
        });
}
// 可靠同步发送机制：阻塞当前线程，确保大体积JSON数据完整写入底层网卡发送缓冲区后再断开连接
void NetworkHelper::sendReliable(const QJsonObject& jsonRequest)
{
    QTcpSocket socket;
    socket.connectToHost(s_serverIp, s_serverPort);
    // 等待连接建立成功
    if (socket.waitForConnected(1500)) {
        QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);
        // 循环等待，确保缓冲区内的所有高维特征或文件数据均被物理清空
        while (socket.bytesToWrite() > 0) {
            socket.waitForBytesWritten(100);
        }
        // 强制刷新缓冲区
        socket.flush();
        QThread::msleep(300); // 略微延时以保证底层协议栈的数据物理送达
        socket.disconnectFromHost();
    }
}