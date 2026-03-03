#include "RecordModule.h"
#include <QHeaderView>
#include <QFileDialog>
#include <QDateTime>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QTextStream>
#include <QStringConverter>

RecordModule::RecordModule(QTableView* tableView, QDateEdit* dateEdit, QPushButton* filterBtn, QWidget* parentWidget)
    : QObject(parentWidget), m_tableView(tableView), m_dateEdit(dateEdit), m_parentWidget(parentWidget)
{
    m_recordModel = new QSqlQueryModel(this);
    m_tableView->setModel(m_recordModel);

    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(false);

    if (m_dateEdit) {
        m_dateEdit->setDate(QDate::currentDate());
    }

    if (filterBtn) {
        connect(filterBtn, &QPushButton::clicked, this, &RecordModule::onFilterClicked);
    }
}

void RecordModule::setCurrentUser(const QString& userName) {
    m_currentUser = userName;
    refreshTable(""); // 切换用户后，默认加载该用户的所有记录
}

void RecordModule::onFilterClicked() {
    if (m_dateEdit) {
        QString selectedDate = m_dateEdit->date().toString("yyyy-MM-dd");
        refreshTable(selectedDate);
    }
}

// ==========================================
// 🚀 核心升级：MySQL连续序号 + 数据权限隔离！
// ==========================================
void RecordModule::refreshTable(QString filterDate) {
    if (m_currentUser.isEmpty()) return;

    // ★ 魔法 SQL：
    // 1. 使用 ROW_NUMBER() OVER (...) 生成动态的连续序号 1,2,3...
    // 2. 强制加入 WHERE r.name = '%1'，保证普通员工只查自己！
    // ★ 注意下面 DATE_FORMAT(r.punch_time, '%Y-%m-%d %H:%i:%s') 这句！
    QString sql = QString("SELECT ROW_NUMBER() OVER (ORDER BY r.punch_time DESC) AS '序号', "
        "r.name AS '姓名', u.department AS '部门', "
        "DATE_FORMAT(r.punch_time, '%Y-%m-%d %H:%i:%s') AS '打卡时间', r.status AS '状态' "
        "FROM attendance_records r "
        "LEFT JOIN users u ON r.name = u.name "
        "WHERE r.name = '%1' ").arg(m_currentUser);

    if (!filterDate.isEmpty()) {
        sql += QString("AND DATE(r.punch_time) = '%1' ").arg(filterDate);
    }

    sql += "ORDER BY r.punch_time DESC";
    m_recordModel->setQuery(sql);

    m_recordModel->setQuery(sql);

    if (m_recordModel->lastError().isValid()) {
        QMessageBox::critical(m_parentWidget, "查询失败", "数据库查询出错：" + m_recordModel->lastError().text());
    }
}

void RecordModule::exportToCsv() {
    QString fileName = QFileDialog::getSaveFileName(m_parentWidget, "导出报表",
        "个人考勤报表_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv", "CSV (*.csv)");

    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#else
        out.setEncoding(QStringConverter::Utf8);
#endif
        out << QString("\xEF\xBB\xBF") << "序号,姓名,部门,打卡时间,状态\n";

        int rowCount = m_recordModel->rowCount();
        int colCount = m_recordModel->columnCount();

        for (int r = 0; r < rowCount; ++r) {
            QStringList rowData;
            for (int c = 0; c < colCount; ++c) {
                rowData << m_recordModel->data(m_recordModel->index(r, c)).toString();
            }
            out << rowData.join(",") << "\n";
        }

        file.close();
        QMessageBox::information(m_parentWidget, "成功", QString("成功导出 %1 条个人考勤记录！").arg(rowCount));
    }
}