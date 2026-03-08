#ifndef PROFILEMODULE_H
#define PROFILEMODULE_H

#include <QObject>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QPixmap>
#include <QByteArray>
#include <QEvent>

class ProfileModule : public QObject
{
    Q_OBJECT
public:
    ProfileModule(QLabel* avatarLabel, QLabel* nameLabel, QLabel* deptLabel,
        QLabel* genderLabel, QLabel* phoneLabel,
        QPushButton* avatarBtn, QPushButton* editBtn, QObject* parent = nullptr);

    void loadUserProfile(const QString& username);

signals:
    void requestFaceReRegister(QString username);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onChangeAvatarClicked();
    void onChangePasswordClicked();
    void onReRegisterFaceClicked();
    void onPreferencesClicked();
    void onExportProfilePdfClicked();
    void onEditGenderClicked();
    void onEditPhoneClicked();

private:
    void injectAdvancedUI();

    QLabel* m_avatarLabel;
    QLabel* m_nameLabel;
    QLabel* m_deptLabel;
    QLabel* m_genderLabel;
    QLabel* m_phoneLabel;
    QPushButton* m_avatarBtn;
    QPushButton* m_editBtn; // 排版锚点按钮

    QPushButton* m_pwdBtn;
    QPushButton* m_faceBtn;
    QPushButton* m_settingsBtn;
    QPushButton* m_exportPdfBtn;

    QString m_currentUser;
};

#endif // PROFILEMODULE_H