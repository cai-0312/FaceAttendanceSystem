#include "NetworkHelper.h"
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent>
#include <QMutexLocker>
QString NetworkHelper::s_serverIp = QStringLiteral("127.0.0.1");
quint16 NetworkHelper::s_serverPort = 9999;
QMutex  NetworkHelper::s_requestMutex;
// 设置服务器地址和端口
void NetworkHelper::setServer(const QString& ip, quint16 port)
{
    s_serverIp = ip;
    s_serverPort = port;
}
// 返回当前服务器地址
QString NetworkHelper::serverIp() { return s_serverIp; }
// 返回当前服务器端口
quint16 NetworkHelper::serverPort() { return s_serverPort; }
// 执行一次完整的请求流程
static QJsonObject doRequest(const QString& ip, quint16 port,
    const QJsonObject& jsonRequest,
    int connectTimeout, int readTimeout)
{
    QTcpSocket socket;
    socket.connectToHost(ip, port);
    if (!socket.waitForConnected(connectTimeout)) {
        return QJsonObject();
    }
    // 发送请求数据
    QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
    socket.write(block);
    socket.flush();
    QJsonObject responseJson;
    QByteArray responseData;
    // 先读取已有缓冲，再等待后续数据
    while (true) {
        responseData += socket.readAll();
        if (responseData.contains('\n')) {
            QJsonDocument doc = QJsonDocument::fromJson(responseData.trimmed());
            if (!doc.isNull() && doc.isObject()) {
                responseJson = doc.object();
            }
            break;
        }
        // 数据未完整时继续等待
        if (!socket.waitForReadyRead(readTimeout)) {
            break;
        }
    }
    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.waitForDisconnected(2000);
    }
    return responseJson;
}
// 线程安全地发送同步请求
QJsonObject NetworkHelper::request(const QJsonObject& jsonRequest, int connectTimeout, int readTimeout)
{
    QMutexLocker locker(&s_requestMutex);
    return doRequest(s_serverIp, s_serverPort, jsonRequest, connectTimeout, readTimeout);
}
// 在后台线程发送请求，不阻塞界面
void NetworkHelper::sendAsync(const QJsonObject& jsonRequest)
{
    QString ip = s_serverIp;
    quint16 port = s_serverPort;

    QtConcurrent::run([jsonRequest, ip, port]() {
        // 后台线程执行同样的请求流程
        doRequest(ip, port, jsonRequest, 2000, 3000);
        });
}
// 在后台线程执行耗时请求，并回调结果
void NetworkHelper::requestLongAsync(const QJsonObject& jsonRequest, QObject* context,
    std::function<void(const QJsonObject&)> callback, int connectTimeout, int readTimeout)
{
    QString ip = s_serverIp;
    quint16 port = s_serverPort;
    QtConcurrent::run([=]() {
        QJsonObject result = doRequest(ip, port, jsonRequest, connectTimeout, readTimeout);
        if (context && callback) {
            QMetaObject::invokeMethod(context, [callback, result]() {
                callback(result);
            }, Qt::QueuedConnection);
        }
    });
}
// 可靠同步发送请求
void NetworkHelper::sendReliable(const QJsonObject& jsonRequest)
{
    QMutexLocker locker(&s_requestMutex);
    doRequest(s_serverIp, s_serverPort, jsonRequest, 2000, 5000);
}