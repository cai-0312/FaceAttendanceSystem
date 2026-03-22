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
// 构造函数：初始化人脸特征注册模块并绑定前端视频流回显控件
RegisterModule::RegisterModule(QLabel* cameraLabel, QWidget* parentWidget)
    : QObject(parentWidget), m_cameraLabel(cameraLabel), m_parentWidget(parentWidget) {}
// 渲染实时画面：接收底层算法线程抛出的视频帧并按照等比例平滑缩放策略更新至UI界面
void RegisterModule::renderFrame(const QImage& img) {
    if (m_cameraLabel) {
        m_cameraLabel->setPixmap(QPixmap::fromImage(img).scaled(
            m_cameraLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
// 触发注册流程：弹出员工身份信息核验表单，向服务器校验合法性后启动摄像采集任务
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
        // 组装校验身份用的 JSON 通讯请求体
        QJsonObject req;
        req["type"] = "verify_user_for_registration";
        req["name"] = inputName;
        req["dept"] = inputDept;
        // 调用统一的网络模块同步发起身份验证请求
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
// 处理特征提取成功事件：将序列化的 Base64 格式高维特征数据上传至服务器数据库进行持久化落盘
void RegisterModule::onFeatureReady(QString name, QByteArray featureBytes) {
    QJsonObject req;
    req["type"] = "register_face";
    req["name"] = name;
    req["feature"] = QString(featureBytes.toBase64());
    // 将组装好的特征网络请求下发至服务器
    NetworkHelper::request(req);
    QMessageBox::information(m_parentWidget, "录入提交", "员工【" + name + "】的人脸已成功发送至服务器！");
    // 抛出数据变更信号通知系统环境刷新本地内存哈希映射
    emit dataChanged();
}
// 处理特征提取异常事件：向前端用户提示录入超时、偏头或画质光线不达标等逻辑错误信息
void RegisterModule::onRegisterFailed(QString errorMsg) {
    QMessageBox::warning(m_parentWidget, "录入失败", errorMsg);
}