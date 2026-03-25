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
struct ClientInfo {                                                                                              // 客户端信息结构体：记录连接终端的基础标识与网络状态
    QString name;                                                                                                // 员工姓名：当前连接终端绑定的操作员真实姓名
    QString dept;                                                                                                // 所属部门：当前连接终端绑定操作员的归属部门
    QString jobTitle;                                                                                            // 企业职务：当前连接终端绑定操作员的职务层级
    QString ip;                                                                                                  // 网络地址：当前连接终端的物理IP地址
    QString loginTime;                                                                                           // 接入时间：当前连接终端成功通过鉴权接入服务端的系统时间
};
using Handler = std::function<void(QSqlDatabase&, QTcpSocket*, const QJsonObject&, const QByteArray&)>;          // 路由分发句柄：统一定义基于数据库、套接字及JSON报文的处理回调签名
class AttendanceServer : public QWidget
{
    Q_OBJECT
public:
    explicit AttendanceServer(QWidget* parent = nullptr);                                                        // 构造函数：初始化服务端总控UI、TCP监听引擎及数据库连接池
    ~AttendanceServer();                                                                                         // 析构函数：安全释放UI句柄与底层网络、数据库资源
    void logMessage(const QString& msg);                                                                         // 日志记录：向服务端监控面板输出携带标准时间戳的格式化运行日志
    void loadGlobalRecords();                                                                                    // 视图重载：查询底层数据库并刷新全局视角的考勤流水记录表
    void refreshPermModel();                                                                                     // 模型重载：刷新基于RBAC的权限管理模型，用于数据变动后的视图同步
    void registerClient(QTcpSocket* socket, const QString& name, const QString& dept, const QString& jobTitle, const QString& ip); // 终端注册：将通过安全鉴权的新接入客户端实例写入内存级路由表
    bool isClientOnline(const QString& name) const;                                                              // 状态探针：根据指定的员工姓名查询其客户端是否处于活跃在线状态
    QTcpSocket* getSocketByName(const QString& name) const;                                                      // 路由寻址：根据指定的员工姓名检索并返回其绑定的TCP物理通信套接字

private slots:
    void on_btn_StartServer_clicked();                                                                           // 交互响应：触发TCP核心路由引擎绑定指定端口并开启侦听模式
    void on_btn_StopServer_clicked();                                                                            // 交互响应：安全关闭TCP监听并强制断开所有已连接的客户端套接字
    void on_btn_RefreshData_clicked();                                                                           // 交互响应：手动触发全局考勤视图与底层数据库状态的强制同步
    void on_btn_ExportGlobal_clicked();                                                                          // 交互响应：将当前已加载的全局考勤流水序列化导出为CSV格式文件
    void onNewConnection();                                                                                      // 网络事件：响应底层TCP连接到达，建立通信信道并挂载数据接收钩子
    void onReadyRead();                                                                                          // 网络事件：异步读取底层套接字缓冲区字节流，解析JSON报文并实施路由分发
    void onClientDisconnected();                                                                                 // 网络事件：处理客户端物理掉线或超时断开，执行内存路由表的安全清理
private:
    void initUI();                                                                                               // 界面初始化：构建服务端核心视图组件、绑定数据模型及交互信号槽
    void updateOnlineUsersTable();                                                                               // 视图重载：遍历内存路由表并刷新展示当前实时在线终端的监控视图
    void initDispatchTable();                                                                                    // 路由初始化：构建并挂载处理不同业务指令类型的回调分发哈希表
    Ui::AttendanceServerClass* ui;                                                                               // 界面指针：由UI设计器自动生成的服务端控制台句柄
    QTcpServer* m_tcpServer;                                                                                     // 网络核心：负责监听网络端口及派发新连接请求的TCP服务端实体
    QMap<QTcpSocket*, ClientInfo> m_clients;                                                                     // 内存路由表：维护物理套接字指针与其对应客户端业务信息的正向映射
    QMap<QString, QTcpSocket*> m_nameToSocket;                                                                   // 内存路由表：维护员工真实姓名与其对应物理套接字指针的反向寻址映射
    QMap<QTcpSocket*, QByteArray> m_buffers;                                                                     // 数据缓冲区：处理TCP底层粘包/半包问题，实现完整应用层协议帧的拼接
    QSqlQueryModel* m_globalRecordModel;                                                                         // 数据模型：承载全局考勤流水展示的只读SQL查询模型
    QSqlTableModel* m_permModel;                                                                                 // 数据模型：承载底层用户权限角色配置的可编辑SQL数据表模型
    QHash<QString, Handler> m_dispatchTable;                                                                     // 路由分发表：映射JSON指令类型字符串至对应的业务逻辑处理函数的哈希映射表
};
#endif // ATTENDANCESERVER_H