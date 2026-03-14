#include "RegisterModule.h"
#include "NetworkHelper.h" // 🚀 引入加固后的网络引擎
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPixmap> 
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

RegisterModule::RegisterModule(QLabel* cameraLabel, QWidget* parentWidget)
    : QObject(parentWidget), m_cameraLabel(cameraLabel), m_parentWidget(parentWidget) {}

void RegisterModule::renderFrame(const QImage& img) {
    if (m_cameraLabel) {
        m_cameraLabel->setPixmap(QPixmap::fromImage(img).scaled(
            m_cameraLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void RegisterModule::triggerRegistration() {
    QDialog dialog(m_parentWidget);
    dialog.setWindowTitle("身份确认 - 录入人脸");
    QFormLayout form(&dialog);

    QLineEdit nameEdit(&dialog);
    QComboBox deptCombo(&dialog);

    deptCombo.addItems({
        "总经办", "人力资源部", "财务部", "销售部",
        "研发部", "市场部", "客户服务部"
        });

    form.addRow("员工姓名:", &nameEdit);
    form.addRow("所属部门:", &deptCombo);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

    if (dialog.exec() == QDialog::Accepted) {
        QString inputName = nameEdit.text().trimmed();
        QString inputDept = deptCombo.currentText();

        if (inputName.isEmpty()) {
            QMessageBox::warning(m_parentWidget, "提示", "姓名不能为空！");
            return;
        }

        QJsonObject req;
        req["type"] = "verify_user_for_registration";
        req["name"] = inputName;
        req["dept"] = inputDept;

        // 🚀 核心替换：使用大一统的 NetworkHelper 同步验证
        QJsonObject res = NetworkHelper::request(req);

        if (res["status"].toString() == "success") {
            emit startRegistration(inputName);
            QMessageBox::information(m_parentWidget, "授权成功", "身份核验通过！\n请正视摄像头，系统正在提取您的面部特征...");
        }
        else {
            QMessageBox::warning(m_parentWidget, "核验拦截", "系统中不存在该姓名与部门的组合！\n请确认信息无误，或联系管理员确认花名册。");
        }
    }
}

void RegisterModule::onFeatureReady(QString name, QByteArray featureBytes) {
    QJsonObject req;
    req["type"] = "register_face";
    req["name"] = name;
    req["feature"] = QString(featureBytes.toBase64());

    // 🚀 核心替换：以前的微小延时与死循环已全部在 NetworkHelper 底层得到了更优的实现
    NetworkHelper::request(req);

    QMessageBox::information(m_parentWidget, "录入提交", "员工【" + name + "】的人脸特征已成功发送至服务器并落盘！");
    emit dataChanged();
}

void RegisterModule::onRegisterFailed(QString errorMsg) {
    QMessageBox::warning(m_parentWidget, "录入失败", errorMsg);
}