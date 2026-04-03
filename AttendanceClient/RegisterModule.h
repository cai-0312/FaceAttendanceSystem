#ifndef REGISTERMODULE_H
#define REGISTERMODULE_H
#include <QObject>
#include <QWidget>
#include <QByteArray>
#include <QLabel>
#include <QImage>
class RegisterModule : public QObject {
    Q_OBJECT
public:
    RegisterModule(QLabel* cameraLabel, QWidget* parentWidget = nullptr); // 初始化人脸注册模块并绑定显示控件
    void triggerRegistration(); // 启动人脸注册流程
public slots:
    void onFeatureReady(QString name, QByteArray featureBytes); // 接收人脸特征并准备上传
    void onRegisterFailed(QString errorMsg); // 处理注册失败提示
    void renderFrame(const QImage& img); // 将摄像头画面渲染到界面
signals:
    void startRegistration(QString name); // 通知底层开始采集指定用户特征
    void dataChanged(); // 通知外部刷新特征库
private:
    QLabel* m_cameraLabel; // 人脸画面显示控件
    QWidget* m_parentWidget; // 父窗口指针
};
#endif 