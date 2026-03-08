#ifndef RECORDMODULE_H
#define RECORDMODULE_H

#include <QObject>
#include <QTableView>
#include <QCalendarWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel> // 🚀 引入标准模型
#include <QComboBox>
#include <QDateEdit>

class RecordModule : public QObject
{
    Q_OBJECT
public:
    RecordModule(QTableView* tableView, QCalendarWidget* calendar,
        QLabel* summaryLabel, QLabel* detailDateLabel,
        QLineEdit* searchNameEdit, QPushButton* filterBtn, QPushButton* exportBtn,
        QString loginName, QString role, QObject* parent = nullptr);

    void refreshData();

private slots:
    void onCalendarClicked(const QDate& date);
    void onCalendarPageChanged(int year, int month);
    void onFilterClicked();
    void onExportClicked();
    void onExportMonthClicked();
    void onCustomContextMenu(const QPoint& pos);

private:
    QTableView* m_tableView;
    QCalendarWidget* m_calendarWidget;
    QLabel* m_summaryLabel;
    QLabel* m_detailDateLabel;
    QLineEdit* m_searchNameEdit;
    QPushButton* m_filterBtn;
    QPushButton* m_exportBtn;

    QStandardItemModel* m_tableModel; // 🚀 修复了此处的语法错误

    QString m_loginName;
    QString m_role;
    QDate m_currentSelectedDate;

    QComboBox* m_statusCombo;
    QDateEdit* m_startDateEdit;
    QDateEdit* m_endDateEdit;
    QPushButton* m_exportMonthBtn;

    void loadMonthlyDataAndColorize(int year, int month);
    void injectAdvancedUI();
};

#endif // RECORDMODULE_H