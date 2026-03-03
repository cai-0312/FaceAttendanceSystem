#pragma once
#include <QObject>
#include <QTableView>
#include <QSqlTableModel>
#include <QWidget>
#include <QComboBox>
#include <QPushButton>

class UserModule : public QObject {
    Q_OBJECT
public:
    // ★ 构造函数升级：接收下拉框和筛选按钮的指针
    explicit UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget);

    // 刷新接口，默认加载全部，传入部门名即可筛选
    void refreshTable(QString filterDept = "全部");

signals:
    // ★ 核心机制：当删除员工导致数据库变动时，向外发出通知！
    void dataChanged();

public slots:
    void deleteSelectedUser();

private slots:
    // ★ 新增：处理点击“筛选人员”按钮的逻辑
    void onFilterClicked();

private:
    QTableView* m_tableView;
    QSqlTableModel* m_userModel;
    QComboBox* m_deptCombo;
    QWidget* m_parentWidget;
};