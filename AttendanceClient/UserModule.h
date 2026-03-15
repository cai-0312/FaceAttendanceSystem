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
    // 初始化企业花名册模块并绑定对应的数据过滤与视图展示控件
    UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget = nullptr); 
signals:
    void dataChanged();                                       //员工删除或修改时，通知上层调度系统同步刷新特征库等级联数据
public slots:
    void refreshTable(QString filterDept = "");              // 向服务端拉取员工花名册列表并渲染至表格
    void deleteSelectedUser();                              // 将当前表格选中的员工记录进行硬删除或标记离职
private slots:
    void onFilterClicked();                                // 获取当前过滤条件并触发视图数据重载
    void onCustomContextMenu(const QPoint& pos);          // 计算映射偏移并弹出针对指定行的管理员快捷菜单
    void onResetPassword(int row);                        // 指定行员工的系统登录密码强制重置为默认初始化状态
    void onExportRoster();                                   // 将当前表格中已加载的所有员工数据模型序列化为CSV文件导出至本地
private:
    void injectAdvancedUI();                                      // 注入模糊搜索框与报表导出等高级控制组件
    QTableView* m_tableView;                                      // 用于展示并承载底层数据模型（花名册）的主体表格视图
    QComboBox* m_deptCombo;                                       // 按部门结构快速过滤下拉选项的复合框组件
    QPushButton* m_filterBtn;                                     // 触发综合查询与数据刷新的动作按钮
    QWidget* m_parentWidget;                                      // 用于交互弹窗的父级模态锁定
    QLineEdit* m_searchEdit = nullptr;                            // 用于按姓名或工号实施模糊检索的文本输入框
    QPushButton* m_exportBtn = nullptr;                           // 用于触发全量花名册报表导出的操作按钮
    QStandardItemModel* m_userModel;                              // 用于内存级存储与展示从服务端拉取的JSON解析数据
};
#endif // USERMODULE_H