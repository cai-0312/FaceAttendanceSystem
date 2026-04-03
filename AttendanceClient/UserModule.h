#ifndef USERMODULE_H
#define USERMODULE_H
#include <QObject>
#include <QTableView>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QPoint>
class UserModule : public QObject
{
    Q_OBJECT
public:
    // 初始化花名册模块并绑定控件
    UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget = nullptr); 
signals:
    void dataChanged(); // 通知外层刷新关联数据
public slots:
    void refreshTable(QString filterDept = ""); // 刷新花名册列表
    void deleteSelectedUser(); // 删除选中用户
private slots:
    void onFilterClicked(); // 处理筛选按钮点击
    void onCustomContextMenu(const QPoint& pos); // 显示右键菜单
    void onResetPassword(int row); // 重置指定用户密码
    void onExportRoster(); // 导出花名册报表
private:
    void injectAdvancedUI(); // 注入高级筛选与导出控件
    QTableView* m_tableView; // 花名册表格视图
    QComboBox* m_deptCombo; // 部门筛选下拉框
    QPushButton* m_filterBtn; // 筛选按钮
    QWidget* m_parentWidget; // 父窗口指针
    QLineEdit* m_searchEdit = nullptr; // 模糊搜索输入框
    QPushButton* m_exportBtn = nullptr; // 导出按钮
    QStandardItemModel* m_userModel; // 用户数据模型
};
#endif // USERMODULE_H#endif // USERMODULE_H