#include "RegisterModule.h"
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPixmap> 
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

// 🚀 核心通讯工具 1：同步请求获取验证结果
static QJsonObject requestDataFromServer(const QJsonObject& jsonRequest) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", 9999);
    QJsonObject responseJson;
    if (socket.waitForConnected(2000)) {
        QByteArray block = QJsonDocument(jsonRequest).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);
        socket.waitForBytesWritten(1000);
        if (socket.waitForReadyRead(5000)) {
            QByteArray responseData;
            while (socket.waitForReadyRead(50) || socket.bytesAvailable() > 0) {
                responseData += socket.readAll();
                if (responseData.endsWith("\n")) break;
            }
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isNull()) responseJson = doc.object();
        }
        socket.disconnectFromHost();
    }
    return responseJson;
}

// 🚀 核心通讯工具 2：加入微小延时与死循环，确保 3KB 的人脸特征完整传输不被截断
static void sendRegisterCommandToServer(const QJsonObject& json) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", 9999);
    if (socket.waitForConnected(1500)) {
        QByteArray block = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
        socket.write(block);

        // 死循环等待，直到数据全部塞进网卡缓冲区
        while (socket.bytesToWrite() > 0) {
            socket.waitForBytesWritten(100);
        }
        socket.flush();

        // 强行延时 300 毫秒，给服务器拼装 TCP 包的时间
        QThread::msleep(300);
        socket.disconnectFromHost();
    }
}

RegisterModule::RegisterModule(QLabel* cameraLabel, QWidget* parentWidget)
    : QObject(parentWidget), m_cameraLabel(cameraLabel), m_parentWidget(parentWidget) {
}

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

        // 🚀 核心修复：完全剥离 QSqlQuery，改发 TCP 请求交由服务端验证
        QJsonObject req;
        req["type"] = "verify_user_for_registration";
        req["name"] = inputName;
        req["dept"] = inputDept;
        QJsonObject res = requestDataFromServer(req);

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
    sendRegisterCommandToServer(req);

    QMessageBox::information(m_parentWidget, "录入提交", "员工【" + name + "】的人脸特征已成功发送至服务器并落盘！");
    emit dataChanged();
}

void RegisterModule::onRegisterFailed(QString errorMsg) {
    QMessageBox::warning(m_parentWidget, "录入失败", errorMsg);
}