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
    ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel, QLabel* genderLabel, QLabel* phoneLabel, QPushButton* avatarBtn, QPushButton* editBtn, QObject* parent = nullptr); // 初始化个人资料模块并绑定界面控件
    void loadUserProfile(const QString& username);                             // 发起网络请求加载并渲染用户的个人档案数据
signals:
    void requestFaceReRegister(QString username);                              // 触发重新采集当前用户人脸特征的通知信号
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;                // 拦截并处理头像标签的鼠标点击事件
private slots:
    void onChangeAvatarClicked();                                   // 响应操作：选择并上传全新的个人头像图片
    void onChangePasswordClicked();                                 // 响应操作：弹出修改系统登录密码的交互窗口
    void onReRegisterFaceClicked();                                 // 响应操作：触发跳转至人脸特征重新采集流程
    void onPreferencesClicked();                                    // 响应操作：配置本地客户端相关的偏好设置选项
    void onExportProfilePdfClicked();                               // 响应操作：将当前个人档案数据渲染并导出为PDF文件
    void onEditGenderClicked();                                     // 响应操作：更新用户的性别信息字段
    void onEditPhoneClicked();                                      // 响应操作：校验并更新用户的联系电话字段
private:
    void injectAdvancedUI();                                                         // 动态注入：移除原生按钮并动态生成高级交互控制面板
    void renderDiagnosticsCard();
    void renderHelpAccordion();

    QLabel* m_avatarLabel;                                                           // 界面控件：用于展示用户Base64头像数据的标签
    QLabel* m_nameLabel;                                                             // 界面控件：用于展示用户真实姓名的标签
    QLabel* m_deptLabel;                                                             // 界面控件：用于展示用户所属部门的标签
    QLabel* m_genderLabel;                                                   // 界面控件：用于展示与修改用户性别的交互标签
    QLabel* m_phoneLabel;                                                    // 界面控件：用于展示与修改用户联系电话的交互标签
    QPushButton* m_avatarBtn;                                                // 界面控件：原生修改头像触发按钮（预留引用指针）
    QPushButton* m_editBtn;                                                       // 界面控件：原生修改资料触发按钮（用作动态排版锚点）
    QPushButton* m_pwdBtn;                                                        // 动态控件：执行密码修改操作的触发按钮
    QPushButton* m_faceBtn;                                                       // 动态控件：执行人脸重新采集的触发按钮
    QPushButton* m_settingsBtn;                                                   // 动态控件：执行偏好设置面板唤出的触发按钮
    QPushButton* m_exportPdfBtn;                                                  // 动态控件：执行入职档案报表PDF导出的触发按钮
    QString m_currentUser;                                                        // 状态记录：当前处于浏览及编辑状态的目标用户标识
};
#endif // PROFILEMODULE_H