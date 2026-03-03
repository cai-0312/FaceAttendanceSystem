#include "ProfileModule.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>

ProfileModule::ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel, QLabel* genderLabel, QPushButton* avatarBtn, QObject* parent)
    : QObject(parent), m_avatarLabel(avatarLabel), m_nameLabel(nameLabel), m_deptLabel(deptLabel), m_genderLabel(genderLabel), m_avatarBtn(avatarBtn)
{
    // ★ 绑定更换头像按钮
    if (m_avatarBtn) {
        connect(m_avatarBtn, &QPushButton::clicked, this, &ProfileModule::onChangeAvatarClicked);
    }
}

void ProfileModule::loadUserProfile(const QString& username) {
    m_currentUser = username;

    // 1. 去数据库查真实的部门和性别
    QSqlQuery query;
    query.prepare("SELECT department, gender FROM users WHERE name = :name");
    query.bindValue(":name", username);

    if (query.exec() && query.next()) {
        QString dept = query.value(0).toString();
        QString gender = query.value(1).toString();

        m_nameLabel->setText("👤 姓名: " + username);
        m_deptLabel->setText("🏢 部门: " + dept);
        m_genderLabel->setText("⚧ 性别: " + (gender.isEmpty() ? "未知" : gender));
    }
    else {
        m_nameLabel->setText("👤 姓名: " + username);
        m_deptLabel->setText("🏢 部门: 获取失败");
        m_genderLabel->setText("⚧ 性别: 获取失败");
    }

    // 2. 加载本地专属头像 (存放在程序运行目录的 avatars 文件夹下)
    QDir dir("avatars");
    if (!dir.exists()) {
        dir.mkpath("."); // 如果没有该文件夹则自动创建
    }

    QString avatarPath = QString("avatars/%1.png").arg(username);
    QPixmap avatar;

    // 如果该用户已经上传过头像，就加载；否则显示默认图片
    if (avatar.load(avatarPath)) {
        m_avatarLabel->setPixmap(avatar.scaled(m_avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    else {
        // 默认纯色头像
        QPixmap defaultAvatar(150, 150);
        defaultAvatar.fill(Qt::darkCyan);
        m_avatarLabel->setPixmap(defaultAvatar);
    }
}

void ProfileModule::onChangeAvatarClicked() {
    if (m_currentUser.isEmpty()) return;

    // 1. 弹出文件选择框，让用户选一张本地图片
    QString filePath = QFileDialog::getOpenFileName((QWidget*)this->parent(), "选择专属头像", "", "图片文件 (*.png *.jpg *.jpeg *.bmp)");
    if (filePath.isEmpty()) return;

    // 2. 加载并检查图片
    QPixmap newAvatar;
    if (!newAvatar.load(filePath)) {
        QMessageBox::warning((QWidget*)this->parent(), "错误", "图片加载失败，请检查文件格式！");
        return;
    }

    // 3. 将图片压缩并显示在 UI 上
    m_avatarLabel->setPixmap(newAvatar.scaled(m_avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));

    // 4. 将这张图片永久保存在本地，以当前用户的名字命名！
    QDir dir("avatars");
    if (!dir.exists()) dir.mkpath(".");

    QString savePath = QString("avatars/%1.png").arg(m_currentUser);
    if (newAvatar.save(savePath, "PNG")) {
        QMessageBox::information((QWidget*)this->parent(), "成功", "专属头像更换成功！");
    }
    else {
        QMessageBox::critical((QWidget*)this->parent(), "失败", "头像保存失败，请检查系统权限！");
    }
}