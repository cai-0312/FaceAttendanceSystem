#include "RegisterModule.h"
#include "NetworkHelper.h"
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
// 构造函数，保存界面指针
RegisterModule::RegisterModule(QLabel* cameraLabel, QWidget* parentWidget)
    : QObject(parentWidget), m_cameraLabel(cameraLabel), m_parentWidget(parentWidget) {}
// 显示摄像头画面
void RegisterModule::renderFrame(const QImage& img) {
    if (m_cameraLabel) {
        m_cameraLabel->setPixmap(QPixmap::fromImage(img).scaled(
            m_cameraLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
// 发起人脸录入流程
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
        // 组装身份核验请求
        QJsonObject req;
        req["type"] = "verify_user_for_registration";
        req["name"] = inputName;
        req["dept"] = inputDept;
        // 向服务器验证身份
        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() == "success") {
            QMessageBox::information(m_parentWidget, "授权成功", "身份核验通过！\n请正视摄像头，系统正在提取您的面部特征...");
            emit startRegistration(inputName);
        }
        else {
            QMessageBox::warning(m_parentWidget, "核验拦截", "系统中不存在该姓名与部门的组合！\n请确认信息无误，或联系管理员确认花名册。");
        }
    }
}
// 上传人脸特征
void RegisterModule::onFeatureReady(QString name, QByteArray featureBytes) {
    QJsonObject req;
    req["type"] = "register_face";
    req["name"] = name;
    req["feature"] = QString(featureBytes.toBase64());
    // 提交录入数据
    NetworkHelper::request(req);
    QMessageBox::information(m_parentWidget, "录入提交", "员工【" + name + "】的人脸已成功！");
    // 通知外部刷新数据
    emit dataChanged();
}
// 提示录入失败
void RegisterModule::onRegisterFailed(QString errorMsg) {
    QMessageBox::warning(m_parentWidget, "录入失败", errorMsg);
}