#pragma once
#include <QObject>
#include <QTableView>
#include <QSqlQueryModel>
#include <QWidget>
#include <QDateEdit>
#include <QPushButton>

class RecordModule : public QObject {
    Q_OBJECT
public:
    explicit RecordModule(QTableView* tableView, QDateEdit* dateEdit, QPushButton* filterBtn, QWidget* parentWidget);

    // ★ 新增：提供给主界面，用于设置当前登录的员工姓名
    void setCurrentUser(const QString& userName);

    void refreshTable(QString filterDate = "");

public slots:
    void exportToCsv();

private slots:
    void onFilterClicked();

private:
    QTableView* m_tableView;
    QDateEdit* m_dateEdit;
    QSqlQueryModel* m_recordModel;
    QWidget* m_parentWidget;

    // ★ 新增：保存当前登录用户的姓名，用于 SQL 过滤
    QString m_currentUser;
};