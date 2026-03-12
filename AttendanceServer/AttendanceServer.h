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
#include <functional>

struct ClientInfo {
    QString name;
    QString dept;
    QString jobTitle;
    QString ip;
    QString loginTime;
};

/**
 * @brief 服务端主窗口
 *
 *  职责划分（拆分后）：
 *  ┌─────────────────────────────────────────────────────────┐
 *  │  AttendanceServer       主框架：UI、TCP 监听、路由分发   │
 *  │  DatabaseManager        数据库连接初始化 / DDL           │
 *  │  RequestHandler         业务逻辑处理（各 Part）          │
 *  │  CenterAndComboDelegate 表格 UI 渲染代理                 │
 *  └─────────────────────────────────────────────────────────┘
 */

 // 统一处理函数签名（rawData 仅聊天/广播需要，其他忽略即可）
using Handler = std::function<void(QSqlDatabase&, QTcpSocket*,
    const QJsonObject&, const QByteArray&)>;

class AttendanceServer : public QWidget
{
    Q_OBJECT

public:
    explicit AttendanceServer(QWidget* parent = nullptr);
    ~AttendanceServer();

    // ── 供 RequestHandler 回调主线程使用的公开接口 ──────────────

    /** 打印一条带时间戳的日志到 textBrowser */
    void logMessage(const QString& msg);

    /** 刷新全局考勤记录表格 */
    void loadGlobalRecords();

    /** 刷新权限管理模型（用户增删后调用） */
    void refreshPermModel();

    /** 注册一个新上线的客户端 */
    void registerClient(QTcpSocket* socket, const QString& name,
        const QString& dept, const QString& jobTitle,
        const QString& ip);

    /** 查询指定姓名的客户端是否在线 */
    bool isClientOnline(const QString& name) const;

    /** 根据姓名获取对应 Socket（不在线则返回 nullptr） */
    QTcpSocket* getSocketByName(const QString& name) const;

private slots:
    void on_btn_StartServer_clicked();
    void on_btn_StopServer_clicked();
    void on_btn_RefreshData_clicked();
    void on_btn_ExportGlobal_clicked();
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    void initUI();          ///< 初始化控件、模型、信号槽
    void updateOnlineUsersTable();

    Ui::AttendanceServerClass* ui;

    QTcpServer* m_tcpServer;
    QMap<QTcpSocket*, ClientInfo> m_clients;
    QMap<QString, QTcpSocket*>    m_nameToSocket;

    QSqlQueryModel* m_globalRecordModel;
    QSqlTableModel* m_permModel;        ///< RBAC 权限管理模型

    void initDispatchTable();          // 新增
    QHash<QString, Handler> m_dispatchTable;  // 新增
};

#endif // ATTENDANCESERVER_H