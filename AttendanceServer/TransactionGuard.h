#ifndef TRANSACTION_GUARD_H
#define TRANSACTION_GUARD_H

#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

class TransactionGuard
{
public:
    explicit TransactionGuard(QSqlDatabase& db) : m_db(db), m_committed(false)
    {
        m_active = m_db.transaction();
        if (!m_active) {
            qWarning() << "[TransactionGuard] Failed to begin transaction:"
                << m_db.lastError().text();
        }
    }
    ~TransactionGuard()
    {
        if (m_active && !m_committed) {
            if (!m_db.rollback()) {
                qCritical() << "[TransactionGuard] ROLLBACK failed:"
                    << m_db.lastError().text();
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
        qCritical() << "[TransactionGuard] COMMIT failed:"
            << m_db.lastError().text();
        return false;
    }
    bool isActive() const { return m_active; }
    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

private:
    QSqlDatabase& m_db;
    bool m_active;
    bool m_committed;
};

#endif // TRANSACTION_GUARD_H