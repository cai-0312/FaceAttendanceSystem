#pragma once
#include <QWidget>
#include <QMap>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "FaceProcessThread.h"
#include "RecordModule.h"
#include "PunchModule.h"
#include "UserModule.h"
#include "RegisterModule.h"
#include "ProfileModule.h"
#include "ChatModule.h"
#include "AIAssistantModule.h"
#include "HomeModule.h"

namespace Ui { class MainWidget; }

// 考勤状态结构体：用于记录单个用户的最后打卡时间及打卡计数
struct PunchState {
    QDateTime lastPunchTime;
    int punchCount = 0;
};

class MainWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainWidget(QString loginName, QString role, QWidget* parent = nullptr);
    ~MainWidget();

private slots:
    // 更换个人头像按钮的槽函数
    void onBtnChangeAvatarClicked();
    // 状态变动回调槽函数
    void onStatusChanged(const QString& status);
    // 修改个人密码按钮的槽函数
    void onChangePasswordClicked();

private:
    // 数据库连接初始化与环境校验
    void initDatabase();
    // 从数据库特征库加载已注册的人脸数据到内存
    void loadRegisteredUsers();
    // 刷新并加载当前登录用户的个人资料信息
    void loadProfileInfo();
    // 图像处理：将矩形头像裁剪并合成为圆形样式
    QImage makeCircularAvatar(const QImage& src, int size);
    // 二维码生成逻辑：根据输入字符串生成对应的二维码图像
    void generateQRCode(const QString& dataStr);

private:
    // 界面布局指针
    Ui::MainWidget* ui;
    // 当前登录用户名
    QString m_loginName;
    // 当前登录角色（管理员/普通员工）
    QString m_role;

    // 各功能模块子类指针
    FaceProcessThread* aiThread;      // 人脸检测识别
    RecordModule* recordModule;       // 考勤记录模块
    PunchModule* punchModule;         // 考勤打卡模块
    UserModule* userModule;           // 员工管理模块
    RegisterModule* registerModule;   // 人脸录入模块
    ProfileModule* profileModule;     // 个人信息模块
    ChatModule* chatModule;           // 聊天模块
    AIAssistantModule* m_aiModule;    // AI助手模块
    HomeModule* homeModule;           //首页大屏模块

    // 网络管理对象：用于处理远程API请求
    QNetworkAccessManager* m_netManager;
    // 内存缓存：存储各用户的打卡频率状态映射表
    QMap<QString, PunchState> m_punchStates;
};