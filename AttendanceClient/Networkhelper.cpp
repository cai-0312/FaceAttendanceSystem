#include "NetworkHelper.h"
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent>
#include <QMutexLocker>
#include <QSettings>
#include <QCoreApplication>
// 延迟初始化标志，避免在 QApplication 构造前调用 applicationDirPath()
static bool s_configLoaded = false;
QString NetworkHelper::s_serverIp = QStringLiteral("127.0.0.1");
quint16 NetworkHelper::s_serverPort = 9999;
QMutex  NetworkHelper::s_requestMutex;
QString NetworkHelper::s_sessionToken;
// 首次使用时从 server.ini 加载服务器地址（此时 QApplication 已存在）
static void ensureConfigLoaded()
{
    if (s_configLoaded) return;
    s_configLoaded = true;
    QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty()) return;
    QSettings cfg(appDir + "/server.ini", QSettings::IniFormat);
    QString ip   = cfg.value("Server/ip",   "127.0.0.1").toString();
    quint16 port = static_cast<quint16>(cfg.value("Server/port", 9999).toUInt());
    NetworkHelper::setServer(ip, port);
}
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
// 执行一次完整的请求流程（token 由调用方传入，避免锁重入死锁）
static QJsonObject doRequest(const QString& ip, quint16 port,
    const QJsonObject& jsonRequest,
    int connectTimeout, int readTimeout,
    const QString& token = QString())
{
    QTcpSocket socket;
    socket.connectToHost(ip, port);
    if (!socket.waitForConnected(connectTimeout)) {
        return QJsonObject();
    }
    // 自动注入会话令牌，让服务端认证临时连接
    QJsonObject req = jsonRequest;
    if (!token.isEmpty()) {
        req["session_token"] = token;
    }
    // 发送请求数据
    QByteArray block = QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
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
    ensureConfigLoaded();
    // 在持有锁期间直接读取 s_sessionToken，避免调用 sessionToken() 造成锁重入
    QString token = s_sessionToken;
    return doRequest(s_serverIp, s_serverPort, jsonRequest, connectTimeout, readTimeout, token);
}
// 在后台线程发送请求，不阻塞界面
void NetworkHelper::sendAsync(const QJsonObject& jsonRequest)
{
    QMutexLocker locker(&s_requestMutex);
    ensureConfigLoaded();
    QString ip = s_serverIp;
    quint16 port = s_serverPort;
    QString token = s_sessionToken;
    locker.unlock();
    QtConcurrent::run([jsonRequest, ip, port, token]() {
        // 后台线程执行同样的请求流程
        doRequest(ip, port, jsonRequest, 2000, 3000, token);
        });
}
// 在后台线程执行耗时请求，并回调结果
void NetworkHelper::requestLongAsync(const QJsonObject& jsonRequest, QObject* context,
    std::function<void(const QJsonObject&)> callback, int connectTimeout, int readTimeout)
{
    QMutexLocker locker(&s_requestMutex);
    ensureConfigLoaded();
    QString ip = s_serverIp;
    quint16 port = s_serverPort;
    QString token = s_sessionToken;
    locker.unlock();
    QtConcurrent::run([=]() {
        QJsonObject result = doRequest(ip, port, jsonRequest, connectTimeout, readTimeout, token);
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
    ensureConfigLoaded();
    QString token = s_sessionToken;
    doRequest(s_serverIp, s_serverPort, jsonRequest, 2000, 5000, token);
}
// 保存服务端分配的会话令牌
void NetworkHelper::setSessionToken(const QString& token)
{
    QMutexLocker locker(&s_requestMutex);
    s_sessionToken = token;
}
// 获取当前会话令牌
QString NetworkHelper::sessionToken()
{
    QMutexLocker locker(&s_requestMutex);
    return s_sessionToken;
}