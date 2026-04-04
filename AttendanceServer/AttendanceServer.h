#ifndef ATTENDANCESERVER_H
#define ATTENDANCESERVER_H
#include <QWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QSqlQueryModel>
#include <QSqlTableModel>
#include "ui_AttendanceServer.h"
#include <QHash>
#include <QSet>
#include <QFile>
struct FileTransferState {
    QString fileName;       // 文件原名
    QString savedPath;      // 服务端保存的完整路径
    QString sender;         // 发送者账号
    QString receiver;       // 接收者账号
    qint64 totalSize;       // 总字节数
    qint64 receivedSize;    // 已接收字节数
    QFile*  file;           // 本地文件句柄
    bool    isGroup;        // 是否为群组传输
    QString department;     // 群组所属部门
};
struct ClientInfo {
    QString name;        // 客户端绑定的员工姓名
    QString dept;        // 客户端所属部门
    QString jobTitle;    // 客户端的岗位或职称
    QString ip;          // 客户端远端 IP 地址
    QString loginTime;   // 客户端登录时间戳
};
using Handler = std::function<void(QSqlDatabase&, QTcpSocket*, const QJsonObject&, const QByteArray&)>; // 路由处理函数签名
class AttendanceServer : public QWidget
{
    Q_OBJECT
public:
    explicit AttendanceServer(QWidget* parent = nullptr); // 构造并初始化服务端
    ~AttendanceServer();                                   // 析构并释放资源
    void logMessage(const QString& msg);                   // 向界面输出格式化日志
    void loadGlobalRecords();                              // 刷新全局考勤视图数据
    void refreshPermModel();                               // 刷新权限表模型
    void registerClient(QTcpSocket* socket, const QString& name, const QString& dept, const QString& jobTitle, const QString& ip); // 注册已认证客户端
    bool isClientOnline(const QString& name) const;       // 检查指定员工是否在线
    QTcpSocket* getSocketByName(const QString& name) const; // 根据姓名查找套接字
    QString getClientName(QTcpSocket* socket) const;        // 根据套接字查找已认证的员工姓名
private slots:
    void on_btn_StartServer_clicked();    // 启动 TCP 服务并开始监听
    void on_btn_StopServer_clicked();     // 停止 TCP 服务并断开所有连接
    void on_btn_RefreshData_clicked();    // 手动刷新界面数据
    void on_btn_ExportGlobal_clicked();   // 导出全局考勤为 CSV
    void onNewConnection();               // 处理新到的 TCP 连接
    void onReadyRead();                   // 读取并解析客户端消息
    void onClientDisconnected();          // 处理客户端断线清理
private:
    void initUI();                        // 初始化 UI 和模型绑定
    void updateOnlineUsersTable();        // 更新在线用户显示
    void initDispatchTable();             // 构建路由分发表
    void handleBinaryData(QTcpSocket* socket, const QByteArray& chunk); // 处理文件传输二进制块
    void onFileTransferComplete(QTcpSocket* socket); // 处理文件传输完成
    void handleFileDownloadRequest(QTcpSocket* socket, const QJsonObject& json); // 处理下载请求
    void handleFileTransferStart(QTcpSocket* socket, const QJsonObject& json); // 初始化文件上传
    Ui::AttendanceServerClass* ui;        // UI 句柄
    QTcpServer* m_tcpServer;              // TCP 服务实例
    QMap<QTcpSocket*, ClientInfo> m_clients; // 套接字到客户端信息映射
    QMap<QString, QTcpSocket*> m_nameToSocket; // 姓名到套接字映射
    QMap<QTcpSocket*, QByteArray> m_buffers; // 套接字接收缓冲区
    QSqlQueryModel* m_globalRecordModel;  // 全局考勤只读模型
    QSqlTableModel* m_permModel;          // 权限表模型
    QHash<QString, Handler> m_dispatchTable; // 路由分发表
    QMap<QTcpSocket*, FileTransferState> m_fileTransfers; // 文件传输状态映射
    QSet<QTcpSocket*> m_authenticatedSockets; // 已通过登录认证的 socket 集合
    QMap<QString, QString> m_sessionTokens;    // 会话令牌映射（token → 用户名）
    QMap<QTcpSocket*, QString> m_tokenAuthNames; // 令牌认证的临时连接映射（socket → 用户名，用于 sendAsync 临时连接）
public:
    void addSessionToken(const QString& token, const QString& userName); // 注册会话令牌
    void removeSessionTokensForUser(const QString& userName);           // 移除指定用户的所有令牌
    QString validateSessionToken(const QString& token) const;           // 校验令牌并返回用户名
};
#endif // ATTENDANCESERVER_H