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
    // 初始化考勤记录模块并绑定所需的界面查询与展示控件
    RecordModule(QTableView* tableView, QCalendarWidget* calendar, QLabel* summaryLabel, QLabel* detailDateLabel, QLineEdit* searchNameEdit, QPushButton* filterBtn, QPushButton* exportBtn, QString loginName, QString role, QObject* parent = nullptr); 
    void refreshData();                  // 手动刷新机制：重新拉取日历状态及当前条件下的过滤表格数据
private slots:
    void onCalendarClicked(const QDate& date);
    void onCalendarPageChanged(int year, int month);
    void onFilterClicked();
    void onCustomContextMenu(const QPoint& pos);
    void onExportPersonal();
    void onExportDept();
    void onExportAllMonthly();
private:
    QTableView* m_tableView;                                         // 界面控件：用于展示考勤打卡流水明细的表格视图
    QCalendarWidget* m_calendarWidget;                                // 界面控件：用于宏观展示当月每日考勤状态颜色的日历面板
    QLabel* m_summaryLabel;                                     // 界面控件：用于汇总展示当前统计区间的考勤指标文本
    QLabel* m_detailDateLabel;                                 // 界面控件：用于展示当前正在查阅的详细日期区间文本
    QLineEdit* m_searchNameEdit;                                   // 界面控件：针对管理员开放的员工姓名检索输入框
    QPushButton* m_filterBtn;                                      // 界面控件：执行复合条件数据筛选的触发按钮
    QPushButton* m_exportBtn;               // 界面控件：触发报表导出业务流的按钮
    QStandardItemModel* m_tableModel;      // 数据模型：承载底层数据与前端视图双向绑定的标准项模型
    QString m_loginName;                 // 权限标识：当前查阅模块的用户账号名
    QString m_role;                  // 权限标识：当前查阅模块的系统角色权限
    QDate m_currentSelectedDate;          // 状态维护：当前用户在日历面板上选中焦点的日期对象
    QComboBox* m_statusCombo;                      // 动态控件：根据界面注入生成的考勤状态下拉过滤框
    QDateEdit* m_startDateEdit;                   // 动态控件：检索过滤区间的起始日期选择器
    QDateEdit* m_endDateEdit;                     // 动态控件：检索过滤区间的结束日期选择器
    QPushButton* m_exportMonthBtn;               // 动态控件：针对管理员注入的月度总报表导出按钮
    void loadMonthlyDataAndColorize(int year, int month);                    // 视图渲染：向服务端请求整月打卡状态并利用特定色彩渲染日历单元格
    void injectAdvancedUI();                                                // 视图构建：脱离静态UI文件，通过代码动态注入高级筛选和导航控制面板
    QPushButton* m_exportCenterBtn; // 导出中心父按钮
};
#endif // RECORDMODULE_H