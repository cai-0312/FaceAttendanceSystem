#ifndef HOMEMODULE_H
#define HOMEMODULE_H
#include <QObject>
#include <QVBoxLayout>
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QComboBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QLineSeries>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>

class HomeModule : public QObject
{
    Q_OBJECT
public:
    // 问题1：构造函数新增 department 和 jobTitle 参数用于权限过滤
    HomeModule(QVBoxLayout* mainLayout, QString role, QString loginName,
        QString department, QString jobTitle, QObject* parent = nullptr);
    ~HomeModule();
    void refreshDashboard();                                // 发起网络请求刷新大屏各项统计数据

signals:
    void requestQuickLeave();                               // 触发快捷请假流程信号
    void requestQuickAppeal();                              // 触发快捷异常申诉信号
    void requestApproveLeave();                             // 触发审批请假单流程信号
    void requestApproveAppeal();                            // 触发审批异常申诉流程信号

private:
    QVBoxLayout* m_mainLayout;
    QString m_role;
    QString m_loginName;
    QString m_department;                                    // 问题1：当前用户所属部门
    QString m_jobTitle;                                      // 问题1：当前用户职务（用于区分部门经理/普通员工）
    QString m_timeRange;                                     // 问题2：当前选中的时间维度（本周/本月/本自然月）
    QString m_filterDept;                                    // 问题1：人资经理筛选部门用
    QWidget* m_dashboardWidget;
    QVBoxLayout* m_dashboardLayout;
    QComboBox* m_timeCombo = nullptr;                        // 问题2：时间维度切换控件
    QComboBox* m_deptCombo = nullptr;                        // 问题1：部门筛选控件（仅人资经理可见）
    bool m_deptFetched = false;                              // 是否已从服务端获取过部门信息

    void clearLayout(QLayout* layout);
    void renderTopCards(QVBoxLayout* parentLayout, const QJsonObject& data);
    void renderToolBar(QVBoxLayout* parentLayout);                                                                        // 问题1+2：渲染筛选工具栏
    void renderMiddleCharts(QVBoxLayout* parentLayout, const QJsonObject& res);
    void renderBottomFeed(QVBoxLayout* parentLayout, const QJsonObject& res);
    QFrame* createDataCard(const QString& title, const QString& value, const QString& subText, const QString& colorHex);
    QChartView* createPieChart(const QJsonArray& data);
    QChartView* createBarChart(const QJsonArray& data, const QString& title);                                             // 问题2：标题动态化
    QChartView* createLineChart(const QJsonArray& data, const QString& title);                                            // 问题2：标题动态化
};
#endif // HOMEMODULE_H