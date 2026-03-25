#include "NetworkHelper.h"
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent>
#include <QMutexLocker>

QString NetworkHelper::s_serverIp = QStringLiteral("127.0.0.1");
quint16 NetworkHelper::s_serverPort = 9999;
QMutex  NetworkHelper::s_requestMutex;

void NetworkHelper::setServer(const QString& ip, quint16 port)
{
    s_serverIp = ip;
    s_serverPort = port;
}

QString NetworkHelper::serverIp() { return s_serverIp; }
quint16 NetworkHelper::serverPort() { return s_serverPort; }

// ==================== 内部工具函数 ====================
// 完整的连接生命周期：连接 → 发送 → 等响应 → 读取 → 优雅断开
// 每个调用独立创建socket，不复用，但确保服务端处理完毕后才断开
static QJsonObject doRequest(const QString& ip, quint16 port,
    const QJsonObject& jsonRequest,
    int connectTimeout, int readTimeout)
{
    QTcpSocket socket;
    socket.connectToHost(ip, port);
    if (!socket.waitForConnected(connectTimeout)) {
        return QJsonObject(); // 返回空
    }

    // 发送请求
    QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
    socket.write(block);
    socket.flush(); // 🚩 用 flush 替代 waitForBytesWritten，极大减少事件循环阻塞

    QJsonObject responseJson;
    QByteArray responseData;

    // 🚩 核心修复：绝对安全的同步读取循环！先处理缓冲区已有数据，再等待新数据
    while (true) {
        responseData += socket.readAll();
        if (responseData.contains('\n')) {
            QJsonDocument doc = QJsonDocument::fromJson(responseData.trimmed());
            if (!doc.isNull() && doc.isObject()) {
                responseJson = doc.object();
            }
            break; // 成功拿到完整包，跳出
        }

        // 只有在数据还不完整时，才进入等待
        if (!socket.waitForReadyRead(readTimeout)) {
            break; // 真正超时，跳出
        }
    }

    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.waitForDisconnected(2000);
    }

    return responseJson;
}

// ==================== 同步请求 ====================
// 线程安全：用互斥锁保护，防止多个线程同时创建大量临时连接
QJsonObject NetworkHelper::request(const QJsonObject& jsonRequest, int connectTimeout, int readTimeout)
{
    QMutexLocker locker(&s_requestMutex);
    return doRequest(s_serverIp, s_serverPort, jsonRequest, connectTimeout, readTimeout);
}

// ==================== 异步发送 ====================
// 在后台线程中执行完整的请求-响应周期，不阻塞主线程
void NetworkHelper::sendAsync(const QJsonObject& jsonRequest)
{
    QString ip = s_serverIp;
    quint16 port = s_serverPort;

    QtConcurrent::run([jsonRequest, ip, port]() {
        // 后台线程中也使用完整的请求-响应周期
        doRequest(ip, port, jsonRequest, 2000, 3000);
        });
}

// ==================== 可靠同步发送 ====================
void NetworkHelper::sendReliable(const QJsonObject& jsonRequest)
{
    QMutexLocker locker(&s_requestMutex);
    doRequest(s_serverIp, s_serverPort, jsonRequest, 2000, 5000);
}