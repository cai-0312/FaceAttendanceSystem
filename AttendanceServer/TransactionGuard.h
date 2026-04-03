#ifndef TRANSACTION_GUARD_H
#define TRANSACTION_GUARD_H
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>
class TransactionGuard
{
public:
    explicit TransactionGuard(QSqlDatabase& db) : m_db(db), m_committed(false) // 构造时尝试开启事务并记录激活状态
    {
        m_active = m_db.transaction();
        if (!m_active) {
            qWarning() << "[TransactionGuard] Failed to begin transaction:" << m_db.lastError().text();
        }
    }
    ~TransactionGuard() // 析构时若事务未提交则自动回滚以保证一致性
    {
        if (m_active && !m_committed) {
            if (!m_db.rollback()) {
                qCritical() << "[TransactionGuard] ROLLBACK failed:" << m_db.lastError().text();
            }
            else {
                qDebug() << "[TransactionGuard] Auto-ROLLBACK executed.";
            }
        }
    }
    bool commit()
    {
        if (!m_active) return false;
        if (m_db.commit()) {
            m_committed = true;
            return true;
        }
        qCritical() << "[TransactionGuard] COMMIT failed:" << m_db.lastError().text();
        return false;
    }
    bool isActive() const { return m_active; } // 返回事务是否成功开启
    TransactionGuard(const TransactionGuard&) = delete; // 禁用拷贝构造
    TransactionGuard& operator=(const TransactionGuard&) = delete; // 禁用拷贝赋值
private:
    QSqlDatabase& m_db;    // 关联的数据库连接引用
    bool m_active;         // 标记事务是否已激活
    bool m_committed;      // 标记事务是否已被提交
};

#endif // TRANSACTION_GUARD_H