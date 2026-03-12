#include "DatabaseManager.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDateTime>
#include <QThread>

// ==========================================
// ODBC 连接字符串（所有模块统一使用）
// ==========================================
static const QString kOdbcDsn =
"DRIVER={MySQL ODBC 8.0 Unicode Driver};"
"SERVER=127.0.0.1;"
"PORT=3305;"
"DATABASE=attendance_db;"
"UID=root;"
"PWD=root;"
"CHARSET=utf8mb4;";

void DatabaseManager::initDatabase()
{
    // ---------- 建立主线程连接 ----------
    if (!QSqlDatabase::contains("server_db_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", "server_db_connection");
        db.setDatabaseName(kOdbcDsn);
        if (!db.open()) return;
    }

    QSqlDatabase db = QSqlDatabase::database("server_db_connection");
    QSqlQuery query(db);

    // 初始化字符集
    query.exec("SET NAMES utf8mb4");
    query.exec("SET GLOBAL max_allowed_packet = 67108864");

    // ---------- DDL：建表 & 补列 ----------
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
    // 防止列已存在时报错（忽略错误即可）
    query.exec("ALTER TABLE offline_messages ADD COLUMN department VARCHAR(50) DEFAULT ''");

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
    query.exec(
        "INSERT IGNORE INTO shift_rules "
        "(dept, rule_name, start_time, end_time, late_mins, absent_mins) "
        "VALUES ('全部', '常规班', '09:00:00', '18:00:00', 30, 120)"
    );

    query.exec(
        "CREATE TABLE IF NOT EXISTS system_announcements ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  publisher VARCHAR(50),"
        "  content TEXT,"
        "  publish_time DATETIME"
        ")"
    );

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

    query.exec(
        "CREATE TABLE IF NOT EXISTS ai_chat_logs ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  session_id VARCHAR(50),"
        "  role VARCHAR(20),"
        "  content LONGTEXT,"
        "  create_time DATETIME"
        ")"
    );

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
    query.exec(
        "ALTER TABLE chat_history "
        "CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci"
    );
}

QString DatabaseManager::makeThreadConnName()
{
    return QString("ServerConn_%1_%2")
        .arg(quintptr(QThread::currentThreadId()))
        .arg(QDateTime::currentMSecsSinceEpoch());
}

bool DatabaseManager::openThreadConnection(const QString& connName)
{
    QSqlDatabase threadDb = QSqlDatabase::addDatabase("QODBC", connName);
    threadDb.setDatabaseName(kOdbcDsn);

    if (!threadDb.open()) return false;

    QSqlQuery initQ(threadDb);
    initQ.exec("SET NAMES utf8mb4");
    return true;
}