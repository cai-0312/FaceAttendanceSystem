#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H
#include <QString>
class DatabaseManager
{
public:
    static void initDatabase();                                // 主控线程持久化连接池代理，并执行底层系统架构的数据表构建与约束更新
    static QString makeThreadConnName();                       // 系统物理线程ID与微秒级时间戳，动态生成全局唯一的安全连接标识符
    static bool openThreadConnection(const QString& connName); // 独立的子线程作用域内分配并打开ODBC物理通信信道，以保障高并发数据安全
};
#endif // DATABASEMANAGER_H