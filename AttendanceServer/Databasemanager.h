#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>

/**
 * @brief 数据库管理模块
 *        负责：
 *        1. 初始化主线程 ODBC 连接（"server_db_connection"）
 *        2. 建表 / 补列等 DDL 操作
 *        3. 提供线程连接名生成工具函数
 *
 *  注意：所有方法均为静态方法，无需实例化。
 */
class DatabaseManager
{
public:
    /**
     * @brief 初始化主数据库连接并执行建表 DDL
     *        在 AttendanceServer 构造函数中调用一次即可。
     */
    static void initDatabase();

    /**
     * @brief 生成一个基于线程 ID + 时间戳的唯一连接名
     *        供 QtConcurrent 工作线程创建独立 ODBC 连接使用。
     * @return 唯一连接名字符串
     */
    static QString makeThreadConnName();

    /**
     * @brief 在子线程内打开一条独立的 ODBC 连接并完成基础初始化
     * @param connName  由 makeThreadConnName() 生成的连接名
     * @return 连接是否成功打开
     */
    static bool openThreadConnection(const QString& connName);
};

#endif // DATABASEMANAGER_H