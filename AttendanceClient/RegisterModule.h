#pragma once
#include <QObject>
#include <QWidget>
#include <QByteArray>

class RegisterModule : public QObject {
    Q_OBJECT
public:
    explicit RegisterModule(QWidget* parentWidget);

signals:
    void startRegistration(QString name); // 通知 AI 线程开始拍照提取
    void dataChanged();                   // 通知主界面数据库有变动

public slots:
    void triggerRegistration(); // 响应按钮点击 (我们等下在这里大做文章)
    void onFeatureReady(QString name, QByteArray featureBytes); // 接收 AI 提取完毕的特征
    void onRegisterFailed(QString errorMsg); // 接收 AI 的报错

private:
    QWidget* m_parentWidget;
};