#ifndef HOMEMODULE_H
#define HOMEMODULE_H
#include <QObject>
#include <QVBoxLayout>
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QJsonObject>
#include <QJsonArray>
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
    HomeModule(QVBoxLayout* mainLayout, QString role, QString loginName, QObject* parent = nullptr);         // 初始化首页大屏模块，接收父级布局容器
    ~HomeModule();                                          // 释放首页模块相关资源
    void refreshDashboard();                                // 发起网络请求刷新大屏各项统计数据
    
signals:
    void requestQuickLeave();                               // 触发快捷请假流程信号
    void requestQuickAppeal();                             // 触发快捷异常申诉信号
    void requestApproveLeave();                           // 触发审批请假单流程信号
    void requestApproveAppeal();                         // 触发审批异常申诉流程信号
private:
    QVBoxLayout* m_mainLayout;                                      // 主界面的父布局指针
    QString m_role;                                                 // 当前登录用户的权限角色
    QString m_loginName;                                            // 当前登录系统的用户名
    QWidget* m_dashboardWidget;                                     // 动态创建的仪表盘主容器组件
    QVBoxLayout* m_dashboardLayout;                                // 仪表盘容器的垂直主布局
    void clearLayout(QLayout* layout);                                                                                   // 清理指定布局中的所有动态控件以防止内存泄漏
    void renderTopCards(QVBoxLayout* parentLayout, const QJsonObject& data);                                             // 动态生成并渲染顶部四个核心指标卡片
    void renderMiddleCharts(QVBoxLayout* parentLayout, const QJsonObject& res);                                          // 动态生成并渲染中部的数据可视化图表
    void renderBottomFeed(QVBoxLayout* parentLayout, const QJsonObject& res);                                            // 动态生成并渲染底部的实时动态与系统公告
    QFrame* createDataCard(const QString& title, const QString& value, const QString& subText, const QString& colorHex); // 通过代码动态构建单张数据展示卡片
    QChartView* createPieChart(const QJsonArray& data);                                                                  // 根据考勤状态分布数据动态生成饼状图
    QChartView* createBarChart(const QJsonArray& data);                                                                  // 根据各部门异常统计数据动态生成柱状图
    QChartView* createLineChart(const QJsonArray& data);                                                                 // 根据近期出勤趋势数据动态生成折线图
    
};
#endif // HOMEMODULE_H