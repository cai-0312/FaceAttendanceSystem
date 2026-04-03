#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H
#include <QMutex>
#include <QQueue>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>
#include <QDebug>
class DatabaseManager
{
public:
    static void initDatabase();                                         // 初始化数据库驱动与全局连接参数
    static QString makeThreadConnName();                                 // 生成线程专属的连接名称
    static bool openThreadConnection(const QString& connName);           // 打开指定名称的线程数据库连接
};
class ConnectionPool
{
public:
    static ConnectionPool& instance();                                   // 返回单例的连接池实例
    void init(const QString& odbcDsn, int initialSize = 5, int maxSize = 20); // 初始化连接池与 OD PBCN 字符串
    QString acquire();                                                   // 获取一个可用连接名（阻塞或新建）
    void release(const QString& connName);                               // 释放并归还连接名到池中
    void shutdown();                                                      // 关闭并清理所有连接
private:
    ConnectionPool() = default;
    ~ConnectionPool() = default;
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    QString createNewConnection();                                      // 创建并注册一个新的数据库连接
    QMutex  m_mutex;                                                     // 保护内部队列的互斥量
    QQueue<QString> m_availableConnections;                              // 存放可用连接名的队列
    int     m_totalCreated = 0;                                          // 已创建连接的计数
    int     m_maxSize = 20;                                              // 连接池允许的最大连接数
    QString m_odbcDsn;                                                   // 保存初始化时的 ODBC DSN 字符串
};
class ScopedConnection
{
public:
    explicit ScopedConnection()
        : m_connName(ConnectionPool::instance().acquire())
        , m_db(QSqlDatabase::database(m_connName))
    {}
    ~ScopedConnection()
    {
        ConnectionPool::instance().release(m_connName);
    }
    QSqlDatabase& database() { return m_db; }                            // 返回当前线程获得的数据库引用
    bool isValid() const { return m_db.isOpen(); }                       // 检查数据库连接是否处于打开状态
    ScopedConnection(const ScopedConnection&) = delete;                   // 禁用拷贝构造
    ScopedConnection& operator=(const ScopedConnection&) = delete;        // 禁用拷贝赋值
private:
    QString      m_connName;
    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H