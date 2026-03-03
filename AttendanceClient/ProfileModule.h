#pragma once
#include <QObject>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

class ProfileModule : public QObject {
    Q_OBJECT
public:
    // ★ 构造函数更新：去掉了 ageLabel，增加了 genderLabel 和 avatarBtn
    explicit ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel, QLabel* genderLabel, QPushButton* avatarBtn, QObject* parent = nullptr);

    // 提供一个刷新的接口
    void loadUserProfile(const QString& username);

private slots:
    // ★ 新增：点击更换头像的逻辑
    void onChangeAvatarClicked();

private:
    QLabel* m_avatarLabel;
    QLabel* m_nameLabel;
    QLabel* m_deptLabel;
    QLabel* m_genderLabel;
    QPushButton* m_avatarBtn;

    QString m_currentUser; // 记录当前登录的人，用于给头像命名
};