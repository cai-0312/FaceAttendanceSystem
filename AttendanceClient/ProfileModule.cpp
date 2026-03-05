#include "ProfileModule.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>

ProfileModule::ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel,
    QLabel* genderLabel, QLabel* phoneLabel,
    QPushButton* avatarBtn, QPushButton* editBtn, QObject* parent)
    : QObject(parent), m_avatarLabel(avatarLabel), m_nameLabel(nameLabel),
    m_deptLabel(deptLabel), m_genderLabel(genderLabel), m_phoneLabel(phoneLabel),
    m_avatarBtn(avatarBtn), m_editBtn(editBtn)
{
    if (m_avatarBtn) {
        connect(m_avatarBtn, &QPushButton::clicked, this, &ProfileModule::onChangeAvatarClicked);
    }
    // 绑定新增加的修改资料按钮
    if (m_editBtn) {
        connect(m_editBtn, &QPushButton::clicked, this, &ProfileModule::onEditProfileClicked);
    }
}

// 加载用户资料
void ProfileModule::loadUserProfile(const QString& username) {
    m_currentUser = username;

    QSqlQuery query;
    // 补全查询 phone 字段
    query.prepare("SELECT department, gender, phone FROM users WHERE name = :name");
    query.bindValue(":name", username);

    if (query.exec() && query.next()) {
        QString dept = query.value(0).toString();
        QString gender = query.value(1).toString();
        QString phone = query.value(2).toString();

        m_nameLabel->setText("👤 姓名: " + username);
        m_deptLabel->setText(dept);
        m_genderLabel->setText(gender.isEmpty() ? "未知" : gender);

        if (m_phoneLabel) {
            m_phoneLabel->setText(phone.isEmpty() ? "未设置" : phone);
        }
    }

    // 头像加载逻辑保持不变
    QDir dir("avatars");
    if (!dir.exists()) dir.mkpath(".");

    QString avatarPath = QString("avatars/%1.png").arg(username);
    QPixmap avatar;
    if (avatar.load(avatarPath)) {
        m_avatarLabel->setPixmap(avatar.scaled(m_avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    else {
        QPixmap defaultAvatar(150, 150);
        defaultAvatar.fill(Qt::darkCyan);
        m_avatarLabel->setPixmap(defaultAvatar);
    }
}

// 弹出修改资料对话框并执行 MySQL 更新
void ProfileModule::onEditProfileClicked() {
    if (m_currentUser.isEmpty()) return;

    QDialog dialog((QWidget*)this->parent());
    dialog.setWindowTitle("修改个人资料");
    dialog.resize(300, 150);
    QFormLayout form(&dialog);

    QComboBox* genderCombo = new QComboBox(&dialog);
    genderCombo->addItems({ "男", "女", "保密" });
    // 设置当前性别为默认选项
    if (m_genderLabel) genderCombo->setCurrentText(m_genderLabel->text());

    QLineEdit* phoneEdit = new QLineEdit(&dialog);
    phoneEdit->setPlaceholderText("请输入新的联系电话");
    if (m_phoneLabel && m_phoneLabel->text() != "未设置" && m_phoneLabel->text() != "-") {
        phoneEdit->setText(m_phoneLabel->text());
    }

    form.addRow("性别:", genderCombo);
    form.addRow("联系电话:", phoneEdit);

    QDialogButtonBox buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);

    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 确认保存
    if (dialog.exec() == QDialog::Accepted) {
        QString newGender = genderCombo->currentText();
        QString newPhone = phoneEdit->text().trimmed();

        QSqlQuery query;
        query.prepare("UPDATE users SET gender = :g, phone = :p WHERE name = :name");
        query.bindValue(":g", newGender);
        query.bindValue(":p", newPhone);
        query.bindValue(":name", m_currentUser);

        if (query.exec()) {
            QMessageBox::information((QWidget*)this->parent(), "成功", "资料修改成功！");
            loadUserProfile(m_currentUser); // 更新成功后主动刷新界面
        }
        else {
            QMessageBox::critical((QWidget*)this->parent(), "失败", "数据库更新失败：" + query.lastError().text());
        }
    }
}

// 头像更换功能保持原样
void ProfileModule::onChangeAvatarClicked() {
    if (m_currentUser.isEmpty()) return;
    QString filePath = QFileDialog::getOpenFileName((QWidget*)this->parent(), "选择专属头像", "", "图片文件 (*.png *.jpg *.jpeg *.bmp)");
    if (filePath.isEmpty()) return;
    QPixmap newAvatar;
    if (!newAvatar.load(filePath)) { QMessageBox::warning((QWidget*)this->parent(), "错误", "图片加载失败！"); return; }
    m_avatarLabel->setPixmap(newAvatar.scaled(m_avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    QDir dir("avatars"); if (!dir.exists()) dir.mkpath(".");
    if (newAvatar.save(QString("avatars/%1.png").arg(m_currentUser), "PNG")) {
        QMessageBox::information((QWidget*)this->parent(), "成功", "专属头像更换成功！");
    }
}   