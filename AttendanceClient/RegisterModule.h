#pragma once
#include <QObject>
#include <QWidget>
#include <QByteArray>

class RegisterModule : public QObject {
    Q_OBJECT
public:
    explicit RegisterModule(QWidget* parentWidget);

signals:
    // 开始注册信号：通知后台 AI 线程启动摄像头并进行特征提取
    void startRegistration(QString name);
    // 数据变动信号：通知主界面或其他模块数据库中的人脸特征已更新
    void dataChanged();

public slots:
    // 触发注册逻辑：处理按钮点击事件，执行身份核验并弹出录入确认表单
    void triggerRegistration();
    // 特征就绪回调：接收由 AI 线程提取完成的二进制人脸特征数据并写入数据库
    void onFeatureReady(QString name, QByteArray featureBytes);
    // 注册失败回调：接收并处理 AI 线程返回的录入异常错误信息
    void onRegisterFailed(QString errorMsg);

private:
    // 父窗口指针，用于作为各类消息提示框的父对象
    QWidget* m_parentWidget;
};