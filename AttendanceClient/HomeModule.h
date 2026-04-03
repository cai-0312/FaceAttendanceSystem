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
    HomeModule(QVBoxLayout* mainLayout, QString role, QString loginName,
        QString department, QString jobTitle, QObject* parent = nullptr); // 初始化首页模块并传入权限信息
    ~HomeModule(); // 释放首页模块资源
    void refreshDashboard(); // 刷新首页数据
signals:
    void requestQuickLeave(); // 请求发起快捷请假
    void requestQuickAppeal(); // 请求发起快捷申诉
    void requestApproveLeave(); // 请求进入请假审批
    void requestApproveAppeal(); // 请求进入申诉审批
private:
    QVBoxLayout* m_mainLayout; // 主布局指针
    QString m_role; // 用户角色
    QString m_loginName; // 登录账号
    QString m_department; // 当前用户所属部门
    QString m_jobTitle; // 当前用户职务
    QString m_timeRange; // 当前选择的时间范围
    QString m_filterDept; // 人资经理筛选部门
    QWidget* m_dashboardWidget; // 首页容器控件
    QVBoxLayout* m_dashboardLayout; // 首页布局
    QComboBox* m_timeCombo = nullptr; // 时间范围切换控件
    QComboBox* m_deptCombo = nullptr; // 部门筛选控件
    bool m_deptFetched = false; // 是否已加载部门数据
    void clearLayout(QLayout* layout); // 清空布局内容
    void renderTopCards(QVBoxLayout* parentLayout, const QJsonObject& data); // 渲染顶部统计卡片
    void renderToolBar(QVBoxLayout* parentLayout); // 渲染筛选工具栏
    void renderMiddleCharts(QVBoxLayout* parentLayout, const QJsonObject& res); // 渲染中部图表区域
    void renderBottomFeed(QVBoxLayout* parentLayout, const QJsonObject& res); // 渲染底部信息流
    QFrame* createDataCard(const QString& title, const QString& value, const QString& subText, const QString& colorHex); // 创建统计卡片
    QChartView* createPieChart(const QJsonArray& data); // 创建饼图
    QChartView* createBarChart(const QJsonArray& data, const QString& title); // 创建柱状图
    QChartView* createLineChart(const QJsonArray& data, const QString& title); // 创建折线图
};
#endif 