#pragma once
#include <QObject>
#include <QLabel>
#include <QPushButton>

class ProfileModule : public QObject
{
    Q_OBJECT
public:
    ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel,
        QLabel* genderLabel, QLabel* phoneLabel,
        QPushButton* avatarBtn, QPushButton* editBtn, QObject* parent = nullptr);

    void loadUserProfile(const QString& username);

private slots:
    void onChangeAvatarClicked();
    void onEditProfileClicked();

private:
    QLabel* m_avatarLabel;
    QLabel* m_nameLabel;
    QLabel* m_deptLabel;
    QLabel* m_genderLabel;
    QLabel* m_phoneLabel;  // 补全电话标签
    QPushButton* m_avatarBtn;
    QPushButton* m_editBtn; // 修改资料按钮
    QString m_currentUser;
};