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
    RegisterModule(QLabel* cameraLabel, QWidget* parentWidget = nullptr);    // 构造函数：初始化人脸采集模块并绑定实时画面展示的UI标签控件
    void triggerRegistration();                                // 触发注册：唤出身份核验表单，校验真实性后向底层发起特征采集指令
public slots:
    void onFeatureReady(QString name, QByteArray featureBytes);     // 特征就绪槽：接收底层算法提取的高维面部特征向量并封装上传至服务端
    void onRegisterFailed(QString errorMsg);                        // 注册异常槽：处理特征采集超时或画质不达标等异常情况并向用户抛出提示
    void renderFrame(const QImage& img);                            // 画面渲染槽：接收硬件相机推流的实时视频帧数据并缩放绘制于界面面板上
signals:
    void startRegistration(QString name);                       // 采集触发信号：通知底层算法模块针对指定身份的用户开始执行面部特征抓取
    void dataChanged();                                         // 数据变更信号：向主控中心广播员工特征库已更新，以触发特征哈希表的延时重载
private:
    QLabel* m_cameraLabel;                                   // 界面指针：用于实时回显人脸采集工作流画面的独立图像标签组件
    QWidget* m_parentWidget;                                 // 窗口指针：指向挂载当前模块的主控窗体，用于弹窗交互时的模态依附控制
};
#endif // REGISTERMODULE_H