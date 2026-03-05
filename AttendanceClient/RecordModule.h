#pragma once
#include <QObject>
#include <QTableView>
#include <QCalendarWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QSqlTableModel>
#include <QTextCharFormat>
#include <QDate>

class RecordModule : public QObject {
    Q_OBJECT
public:
    // 构造函数，接收从 UI 传过来的各种控件指针
    explicit RecordModule(QTableView* tableView, QCalendarWidget* calendar,
        QLabel* summaryLabel, QLabel* detailDateLabel,
        QLineEdit* searchNameEdit, QPushButton* filterBtn,
        QPushButton* exportBtn, QString loginName, QString role, QObject* parent = nullptr);

    // 核心统计与日历上色函数
    void loadMonthlyDataAndColorize(int year, int month);

public slots:
    // 供外部（如 MainWidget）调用的刷新方法
    void refreshData();

private slots:
    // 内部槽函数：处理日历点击、翻页、搜索过滤、导出
    void onCalendarClicked(const QDate& date);
    void onCalendarPageChanged(int year, int month);
    void onFilterClicked();
    void onExportClicked();

private:
    // 绑定的 UI 控件指针
    QTableView* m_tableView;
    QCalendarWidget* m_calendarWidget;
    QLabel* m_summaryLabel;
    QLabel* m_detailDateLabel;
    QLineEdit* m_searchNameEdit;
    QPushButton* m_filterBtn;
    QPushButton* m_exportBtn;

    // 核心数据模型与状态记录
    QSqlTableModel* m_model;
    QString m_loginName;
    QString m_role;
    QDate m_currentSelectedDate; // 记录当前在日历上选中的日期
};