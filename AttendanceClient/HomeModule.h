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
    HomeModule(QVBoxLayout* mainLayout, QString role, QString loginName, QObject* parent = nullptr);
    ~HomeModule();

    void refreshDashboard();

signals:  
    void requestQuickLeave();
    void requestQuickAppeal();
    void requestApproveLeave();
    void requestApproveAppeal();

private:
    QVBoxLayout* m_mainLayout;
    QString m_role;
    QString m_loginName;

    QWidget* m_dashboardWidget;
    QVBoxLayout* m_dashboardLayout;

    void clearLayout(QLayout* layout);

    // 🚀 核心改造：不再直连数据库，所有渲染函数通过接收 JSON 绘制界面
    void renderTopCards(QVBoxLayout* parentLayout, const QJsonObject& data);
    void renderMiddleCharts(QVBoxLayout* parentLayout, const QJsonObject& res);
    void renderBottomFeed(QVBoxLayout* parentLayout, const QJsonObject& res);

    QFrame* createDataCard(const QString& title, const QString& value, const QString& subText, const QString& colorHex);
    QChartView* createPieChart(const QJsonArray& data);
    QChartView* createBarChart(const QJsonArray& data);
    QChartView* createLineChart(const QJsonArray& data);
};

#endif // HOMEMODULE_H
