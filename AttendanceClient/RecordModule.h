#ifndef RECORDMODULE_H
#define RECORDMODULE_H
#include <QObject>
#include <QTableView>
#include <QCalendarWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QComboBox>
#include <QDateEdit>
class RecordModule : public QObject
{
    Q_OBJECT
public:
    // 初始化考勤记录模块并绑定界面控件
    RecordModule(QTableView* tableView, QCalendarWidget* calendar, QLabel* summaryLabel, QLabel* detailDateLabel, QLineEdit* searchNameEdit, QPushButton* filterBtn, QPushButton* exportBtn, QString loginName, QString role, QObject* parent = nullptr); 
    void refreshData(); // 刷新当前数据视图
private slots:
    void onCalendarClicked(const QDate& date); // 处理日历日期点击
    void onCalendarPageChanged(int year, int month); // 处理日历翻页
    void onFilterClicked(); // 执行筛选并刷新表格
    void onCustomContextMenu(const QPoint& pos); // 显示表格右键菜单
    void onExportPersonal(); // 导出个人考勤明细
    void onExportDept(); // 导出部门考勤报表
    void onExportAllMonthly(); // 导出全月考勤汇总
private:
    QTableView* m_tableView; // 考勤明细表格
    QCalendarWidget* m_calendarWidget; // 考勤日历控件
    QLabel* m_summaryLabel; // 统计摘要标签
    QLabel* m_detailDateLabel; // 日期范围标签
    QLineEdit* m_searchNameEdit; // 姓名搜索输入框
    QPushButton* m_filterBtn; // 筛选按钮
    QPushButton* m_exportBtn; // 导出按钮
    QStandardItemModel* m_tableModel; // 表格数据模型
    QString m_loginName; // 当前登录账号
    QString m_role; // 当前登录角色
    QDate m_currentSelectedDate; // 当前选中日期
    QComboBox* m_statusCombo; // 状态筛选下拉框
    QDateEdit* m_startDateEdit; // 起始日期选择器
    QDateEdit* m_endDateEdit; // 结束日期选择器
    QPushButton* m_exportMonthBtn; // 月度导出按钮
    void loadMonthlyDataAndColorize(int year, int month); // 加载整月数据并标记日历
    void injectAdvancedUI(); // 动态注入高级筛选界面
    QPushButton* m_exportCenterBtn; // 导出中心按钮
};
#endif 