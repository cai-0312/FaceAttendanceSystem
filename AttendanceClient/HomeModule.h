#pragma once
#include <QObject>
#include <QVBoxLayout>
#include <QtCharts>

class HomeModule : public QObject
{
    Q_OBJECT

public:
    explicit HomeModule(QVBoxLayout* pieLayout, QVBoxLayout* barLayout, QVBoxLayout* lineLayout, QObject* parent = nullptr);
    ~HomeModule() override;

    // 每次切换到大屏页面时调用此方法刷新数据
    void refreshDashboard();

private:
    // 清除指定布局内的所有控件，防止重绘叠加
    void clearLayout(QLayout* layout);

    // 渲染各个子图表的逻辑
    void renderPieChart();
    void renderBarChart();
    void renderLineChart();

private:
    QVBoxLayout* m_pieLayout;
    QVBoxLayout* m_barLayout;
    QVBoxLayout* m_lineLayout;
};