#include "DatabaseManager.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QThread>
#include <QMutexLocker>
// 定义全局统一的底层 ODBC 数据源通信连接配置基准字符串
static const QString kOdbcDsn =
"DRIVER={MySQL ODBC 8.0 Unicode Driver};"
"SERVER=127.0.0.1;"
"PORT=3305;"
"DATABASE=attendance_db;"
"UID=root;"
"PWD=root;"
"CHARSET=utf8mb4;";
// 初始化数据库：创建连接并确保必要表结构与系统默认数据存在
void DatabaseManager::initDatabase()
{
    // 创建或获取主控线程专用的 ODBC 数据库连接，避免跨线程复用
    if (!QSqlDatabase::contains("server_db_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", "server_db_connection");
        db.setDatabaseName(kOdbcDsn);
        if (!db.open()) return;
    }
    QSqlDatabase db = QSqlDatabase::database("server_db_connection");
    QSqlQuery query(db);
    // 配置数据库字符集与传输包大小以支持多字节字符与大文件
    query.exec("SET NAMES utf8mb4");
    query.exec("SET GLOBAL max_allowed_packet = 268435456");
    // 创建离线消息表以保存离线文件与文本元数据
    query.exec(
        "CREATE TABLE IF NOT EXISTS offline_messages ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  sender VARCHAR(50),"
        "  receiver VARCHAR(50),"
        "  department VARCHAR(50),"
        "  msg_type VARCHAR(20),"
        "  content LONGTEXT,"
        "  filename VARCHAR(255),"
        "  send_time DATETIME"
        ")"
    );
    // 向旧表结构补齐 department 列以兼容历史版本
    query.exec("ALTER TABLE offline_messages ADD COLUMN department VARCHAR(50) DEFAULT ''");
    // 创建请假申请表以支持 OA 流程存储
    query.exec(
        "CREATE TABLE IF NOT EXISTS leave_requests ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  applicant VARCHAR(50),"
        "  leave_type VARCHAR(50),"
        "  start_time DATETIME,"
        "  end_time DATETIME,"
        "  duration VARCHAR(50),"
        "  reason TEXT,"
        "  approver VARCHAR(255),"
        "  cc VARCHAR(255),"
        "  status VARCHAR(50)"
        ")"
    );
    // 创建申诉表以记录异常考勤申诉流水
    query.exec(
        "CREATE TABLE IF NOT EXISTS appeals ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  applicant VARCHAR(50),"
        "  abnormal_time DATETIME,"
        "  original_status VARCHAR(50),"
        "  reason TEXT,"
        "  approver VARCHAR(255),"
        "  status VARCHAR(50)"
        ")"
    );
    // 创建班次规则表以保存各部门的考勤规则
    query.exec(
        "CREATE TABLE IF NOT EXISTS shift_rules ("
        "  dept VARCHAR(50) PRIMARY KEY,"
        "  rule_name VARCHAR(50),"
        "  start_time TIME,"
        "  end_time TIME,"
        "  late_mins INT,"
        "  absent_mins INT"
        ")"
    );
    // 向班次规则表注入默认全员规则以保证有基线配置
    query.exec(
        "INSERT IGNORE INTO shift_rules "
        "(dept, rule_name, start_time, end_time, late_mins, absent_mins) "
        "VALUES ('全部', '常规班', '09:00:00', '18:00:00', 30, 120)"
    );
    // 创建系统公告表以持久化发布的公告
    query.exec(
        "CREATE TABLE IF NOT EXISTS system_announcements ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  publisher VARCHAR(50),"
        "  content TEXT,"
        "  publish_time DATETIME"
        ")"
    );
    // 创建 AI 会话元信息表以支持会话管理
    query.exec(
        "CREATE TABLE IF NOT EXISTS ai_sessions ("
        "  session_id VARCHAR(50) PRIMARY KEY,"
        "  user_name VARCHAR(50),"
        "  title VARCHAR(100),"
        "  create_time DATETIME,"
        "  is_visible INT DEFAULT 1,"
        "  last_message TEXT"
        ")"
    );
    // 创建 AI 聊天日志表以存储对话历史
    query.exec(
        "CREATE TABLE IF NOT EXISTS ai_chat_logs ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  session_id VARCHAR(50),"
        "  role VARCHAR(20),"
        "  content LONGTEXT,"
        "  create_time DATETIME"
        ")"
    );
    // 创建聊天历史表以存储即时通讯的元数据（不存大文件实体）
    query.exec(
        "CREATE TABLE IF NOT EXISTS chat_history ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  sender VARCHAR(50),"
        "  receiver VARCHAR(50),"
        "  msg_type VARCHAR(20),"
        "  content LONGTEXT,"
        "  filename VARCHAR(255),"
        "  send_time DATETIME,"
        "  is_group INT"
        ")"
    );
    // 强制设置 chat_history 的字符集以保证多语种支持
    query.exec(
        "ALTER TABLE chat_history "
        "CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci"
    );
    // 创建任务执行日志表以记录调度任务的执行结果
    query.exec(
        "CREATE TABLE IF NOT EXISTS task_execution_log ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  task_name VARCHAR(100) NOT NULL,"
        "  node_id VARCHAR(100),"
        "  exec_date DATE NOT NULL,"
        "  exec_time DATETIME,"
        "  result VARCHAR(20),"
        "  affected_rows INT DEFAULT 0,"
        "  UNIQUE KEY uk_task_date (task_name, exec_date)"
        ")"
    );
}
// 生成线程唯一的数据库连接名以避免句柄跨线程复用
QString DatabaseManager::makeThreadConnName()
{
    return QString("ServerConn_%1_%2")
        .arg(quintptr(QThread::currentThreadId()))
        .arg(QDateTime::currentMSecsSinceEpoch());
}
// 在子线程中创建并打开一个独立的数据库连接用于并发操作
bool DatabaseManager::openThreadConnection(const QString& connName)
{
    QSqlDatabase threadDb = QSqlDatabase::addDatabase("QODBC", connName);
    threadDb.setDatabaseName(kOdbcDsn);
    if (!threadDb.open()) return false;
    QSqlQuery initQ(threadDb);
    initQ.exec("SET NAMES utf8mb4");
    return true;
}
// 获取连接池的全局单例实例
ConnectionPool& ConnectionPool::instance()
{
    static ConnectionPool pool;
    return pool;
}
// 初始化连接池并预创建若干数据库连接以提高并发性能
void ConnectionPool::init(const QString& odbcDsn, int initialSize, int maxSize)
{
    QMutexLocker locker(&m_mutex);
    m_odbcDsn = odbcDsn;
    m_maxSize = maxSize;
    for (int i = 0; i < initialSize; ++i) {
        QString connName = createNewConnection();
        if (!connName.isEmpty()) {
            m_availableConnections.enqueue(connName);
        }
    }
    qDebug() << "[ConnectionPool] 初始化完成，预创建连接数:" << m_totalCreated
        << "，池容量上限:" << m_maxSize;
}
// 从连接池获取一个可用连接名，必要时会创建新连接或回收失效连接
QString ConnectionPool::acquire()
{
    QMutexLocker locker(&m_mutex);
    // 优先从空闲队列中取出
    while (!m_availableConnections.isEmpty()) {
        QString connName = m_availableConnections.dequeue();
        QSqlDatabase db = QSqlDatabase::database(connName);
        bool isAlive = false;
        if (db.isOpen()) {
            QSqlQuery pingQuery(db);
            // 发送最轻量级的语句，如果成功，说明连接可用
            if (pingQuery.exec("SELECT 1")) {
                isAlive = true;
            }
        }
        // 如果连接健康，直接返回使用
        if (isAlive) {
            return connName;
        }
        // 连接失效则移除并统计回收
        QSqlDatabase::removeDatabase(connName);
        m_totalCreated--;
        qDebug() << "[ConnectionPool] 回收失效连接:" << connName;
    }
    // 空闲队列已空且未到最大容量时创建新连接
    if (m_totalCreated < m_maxSize) {
        QString newConn = createNewConnection();
        if (!newConn.isEmpty()) {
            return newConn;
        }
    }
    // 池已满则记录警告并创建溢出连接
    qWarning() << "[ConnectionPool] 警告：连接池已满(" << m_totalCreated
        << "/" << m_maxSize << ")，创建溢出连接！";
    return createNewConnection();
}
// 将使用完的连接归还到空闲队列以便复用
void ConnectionPool::release(const QString& connName)
{
    if (connName.isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    if (QSqlDatabase::contains(connName)) {
        m_availableConnections.enqueue(connName);
    }
}
// 关闭并销毁连接池中所有连接以释放资源
void ConnectionPool::shutdown()
{
    QMutexLocker locker(&m_mutex);
    while (!m_availableConnections.isEmpty()) {
        QString connName = m_availableConnections.dequeue();
        {
            QSqlDatabase db = QSqlDatabase::database(connName);
            if (db.isOpen()) db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }
    m_totalCreated = 0;
    qDebug() << "[ConnectionPool] 连接池已完全关闭。";
}
// 创建并返回一个新的 ODBC 连接名，失败则返回空字符串
QString ConnectionPool::createNewConnection()
{
    QString connName = QString("pool_conn_%1").arg(m_totalCreated++);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", connName);
    db.setDatabaseName(m_odbcDsn);
    if (!db.open()) {
        qCritical() << "[ConnectionPool] 创建连接失败:" << connName
            << "，错误:" << db.lastError().text();
        QSqlDatabase::removeDatabase(connName);
        m_totalCreated--;
        return QString();
    }
    // 为新连接设置字符集以支持多字节编码
    QSqlQuery initQ(db);
    initQ.exec("SET NAMES utf8mb4");
    return connName;
}