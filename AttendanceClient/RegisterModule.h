#pragma once
#include <QObject>
#include <QWidget>
#include <QByteArray>
#include <QLabel> // ★ 新增
#include <QImage> // ★ 新增

class RegisterModule : public QObject {
    Q_OBJECT
public:
    // ★ 核心修改：在构造函数中加入 QLabel* cameraLabel
    RegisterModule(QLabel* cameraLabel, QWidget* parentWidget = nullptr);
    void triggerRegistration();

public slots:
    void onFeatureReady(QString name, QByteArray featureBytes);
    void onRegisterFailed(QString errorMsg);
    // ★ 核心修改：新增接收每一帧画面的槽函数
    void renderFrame(const QImage& img);

signals:
    void startRegistration(QString name);
    void dataChanged();

private:
    QLabel* m_cameraLabel; // ★ 新增：保存录入界面的摄像头 Label 指针
    QWidget* m_parentWidget;
};