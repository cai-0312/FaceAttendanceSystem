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
    static void initDatabase();
    static QString makeThreadConnName();
    static bool openThreadConnection(const QString& connName);
};
class ConnectionPool
{
public:
    // 获取全局唯一实例
    static ConnectionPool& instance();
    void init(const QString& odbcDsn, int initialSize = 5, int maxSize = 20);
    QString acquire();
    void release(const QString& connName);
    void shutdown();

private:
    ConnectionPool() = default;
    ~ConnectionPool() = default;
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    QString createNewConnection();
    QMutex  m_mutex;                            // 线程互斥锁
    QQueue<QString> m_availableConnections;      // 空闲连接名队列
    int     m_totalCreated = 0;                  // 已创建连接总数
    int     m_maxSize = 20;                      // 连接池上限
    QString m_odbcDsn;                           // ODBC 连接字符串
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
    QSqlDatabase& database() { return m_db; }
    bool isValid() const { return m_db.isOpen(); }
    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

private:
    QString      m_connName;
    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H