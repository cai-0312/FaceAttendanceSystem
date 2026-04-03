#ifndef PROFILEMODULE_H
#define PROFILEMODULE_H
#include <QObject>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QPixmap>
#include <QNetworkInterface>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QTimer>
#include <QTextBrowser>
#include <QFrame>
#include <QByteArray>
#include <QEvent>
class ProfileModule : public QObject
{
    Q_OBJECT
public:
    // 初始化个人资料模块并绑定界面控件
    ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel, QLabel* genderLabel, QLabel* phoneLabel, QPushButton* avatarBtn, QPushButton* editBtn, QObject* parent = nullptr); 
    void loadUserProfile(const QString& username); // 加载并显示用户档案数据
    void updateCameraId(int camId); // 更新当前摄像头编号
signals:
    void requestFaceReRegister(QString username); // 请求重新采集人脸特征
protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // 拦截头像点击事件
private slots:
    void onChangeAvatarClicked(); // 选择并上传新头像
    void onChangePasswordClicked(); // 打开修改密码窗口
    void onReRegisterFaceClicked(); // 进入人脸重新采集流程
    void onPreferencesClicked(); // 打开偏好设置
    void onExportProfilePdfClicked(); // 导出个人档案为PDF
    void onEditGenderClicked(); // 修改性别信息
    void onEditPhoneClicked(); // 修改联系电话
private:
    void injectAdvancedUI(); // 动态生成高级操作区域
    void renderDiagnosticsCard(); // 渲染诊断信息卡片
    void renderHelpAccordion(); // 渲染帮助说明区域

    QLabel* m_avatarLabel; // 头像显示控件
    QLabel* m_nameLabel; // 姓名显示控件
    QLabel* m_deptLabel; // 部门显示控件
    QLabel* m_genderLabel; // 性别显示控件
    QLabel* m_phoneLabel; // 电话显示控件
    QPushButton* m_avatarBtn; // 原始头像按钮
    QPushButton* m_editBtn; // 原始资料按钮
    QPushButton* m_pwdBtn; // 修改密码按钮
    QPushButton* m_faceBtn; // 人脸重采集按钮
    QPushButton* m_settingsBtn; // 偏好设置按钮
    QPushButton* m_exportPdfBtn; // 导出PDF按钮
    QString m_currentUser; // 当前操作用户
};
#endif // PROFILEMODULE_H