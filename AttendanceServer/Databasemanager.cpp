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

// ═══════════════════════════════════════════════════════════════════
//  DatabaseManager 实现（原有逻辑保持不变）
// ═══════════════════════════════════════════════════════════════════

// 执行底层存储介质的初始化：分配持久化主连接并自动检测与补全缺失的业务数据表拓扑
void DatabaseManager::initDatabase()
{
    // 挂载主控线程专用的 ODBC 数据库代理通道，避免并发资源跨线程争夺
    if (!QSqlDatabase::contains("server_db_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", "server_db_connection");
        db.setDatabaseName(kOdbcDsn);
        if (!db.open()) return;
    }
    QSqlDatabase db = QSqlDatabase::database("server_db_connection");
    QSqlQuery query(db);
    // 下发底层环境变量配置：激活四字节完整 UTF-8 支持集与高通量报文传输阈值
    query.exec("SET NAMES utf8mb4");
    query.exec("SET GLOBAL max_allowed_packet = 67108864");
    // 核心表映射：离线消息与文件投递记录实体表结构同步
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
    // 执行非侵入式热更新：为旧版本实体结构动态补全归属部门字段约束，规避重复执行异常
    query.exec("ALTER TABLE offline_messages ADD COLUMN department VARCHAR(50) DEFAULT ''");
    // 核心表映射：OA系统请假审批单据状态追踪结构同步
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
    // 核心表映射：异常考勤主动申诉流程防篡改追踪结构同步
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
    // 核心表映射：企业考勤班次规则配置字典同步
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
    // 注入系统内置基准参数：当配置字典为空时，自动填充全员默认作息时间标准
    query.exec(
        "INSERT IGNORE INTO shift_rules "
        "(dept, rule_name, start_time, end_time, late_mins, absent_mins) "
        "VALUES ('全部', '常规班', '09:00:00', '18:00:00', 30, 120)"
    );
    // 核心表映射：系统全员广播与公共公告信息发布池结构同步
    query.exec(
        "CREATE TABLE IF NOT EXISTS system_announcements ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  publisher VARCHAR(50),"
        "  content TEXT,"
        "  publish_time DATETIME"
        ")"
    );
    // 核心表映射：大语言模型智能会话上下文环境标识同步
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
    // 核心表映射：大语言模型高维度对话向量与多轮时序上下文留档同步
    query.exec(
        "CREATE TABLE IF NOT EXISTS ai_chat_logs ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  session_id VARCHAR(50),"
        "  role VARCHAR(20),"
        "  content LONGTEXT,"
        "  create_time DATETIME"
        ")"
    );
    // 核心表映射：局域网即时通讯记录与组播归档结构同步
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
    // 动态强制刷新存储引擎的字符对齐及排序规格，保障全量多语种特征安全落地
    query.exec(
        "ALTER TABLE chat_history "
        "CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci"
    );
    // 核心表映射：分布式定时任务执行审计日志表（问题二配套）
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

// 基于并行架构调度系统计算专属通道名，避免底层数据库句柄的越权复用与死锁
QString DatabaseManager::makeThreadConnName()
{
    return QString("ServerConn_%1_%2")
        .arg(quintptr(QThread::currentThreadId()))
        .arg(QDateTime::currentMSecsSinceEpoch());
}

// 在完全分离的子线程环境中建立并挂载底层安全连接信道，大幅提升网络I/O吞吐极限
bool DatabaseManager::openThreadConnection(const QString& connName)
{
    QSqlDatabase threadDb = QSqlDatabase::addDatabase("QODBC", connName);
    threadDb.setDatabaseName(kOdbcDsn);
    if (!threadDb.open()) return false;
    QSqlQuery initQ(threadDb);
    initQ.exec("SET NAMES utf8mb4");
    return true;
}
// 获取全局唯一的连接池单例
ConnectionPool& ConnectionPool::instance()
{
    static ConnectionPool pool;
    return pool;
}

// 初始化连接池：预创建 initialSize 个 ODBC 连接放入空闲队列
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

// 从空闲队列中取出一个连接名；队列空时自动扩容创建新连接
QString ConnectionPool::acquire()
{
    QMutexLocker locker(&m_mutex);
    // 优先从空闲队列中取出
    while (!m_availableConnections.isEmpty()) {
        QString connName = m_availableConnections.dequeue();
        QSqlDatabase db = QSqlDatabase::database(connName);
        // ⭐️ 核心修复：绝不能只信赖 db.isOpen()，必须用真实的 SQL 去 Ping 一下测试！
        bool isAlive = false;
        if (db.isOpen()) {
            QSqlQuery pingQuery(db);
            // 发送最轻量级的语句，如果成功，说明 MySQL 没有挂断这个连接
            if (pingQuery.exec("SELECT 1")) {
                isAlive = true;
            }
        }
        // 如果连接健康，直接返回使用
        if (isAlive) {
            return connName;
        }
        // 如果走到这里，说明连接已失效，将其彻底销毁
        QSqlDatabase::removeDatabase(connName);
        m_totalCreated--;
        qDebug() << "[ConnectionPool] 回收失效连接:" << connName;
    }
    // 空闲队列已空，尝试创建新连接
    if (m_totalCreated < m_maxSize) {
        QString newConn = createNewConnection();
        if (!newConn.isEmpty()) {
            return newConn;
        }
    }
    // 已达池容量上限，强制创建溢出连接
    qWarning() << "[ConnectionPool] 警告：连接池已满(" << m_totalCreated
        << "/" << m_maxSize << ")，创建溢出连接！";
    return createNewConnection();
}

// 将用完的连接归还到空闲队列
void ConnectionPool::release(const QString& connName)
{
    if (connName.isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    if (QSqlDatabase::contains(connName)) {
        m_availableConnections.enqueue(connName);
    }
}

// 关闭并销毁所有池内连接（服务端退出时调用）
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

// 内部方法：创建一个新的 ODBC 连接
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
    // 初始化连接的字符集配置
    QSqlQuery initQ(db);
    initQ.exec("SET NAMES utf8mb4");
    return connName;
}