#include "RegisterModule.h"
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>

RegisterModule::RegisterModule(QWidget* parentWidget)
    : QObject(parentWidget), m_parentWidget(parentWidget) {
}

// ==========================================
// 🚀 核心升级：录入前的双重核验与专业表单
// ==========================================
void RegisterModule::triggerRegistration() {
    // 1. 纯代码动态生成专业表单弹窗，无需修改 .ui 文件
    QDialog dialog(m_parentWidget);
    dialog.setWindowTitle("身份确认 - 录入人脸");
    QFormLayout form(&dialog);

    QLineEdit nameEdit(&dialog);
    QComboBox deptCombo(&dialog);
    deptCombo.addItems({ "总办", "研发部", "财务部", "人事行政部", "客服部", "产品部", "市场部", "营销部" });

    form.addRow("员工姓名:", &nameEdit);
    form.addRow("所属部门:", &deptCombo);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

    // 2. 拦截并处理用户的提交
    if (dialog.exec() == QDialog::Accepted) {
        QString inputName = nameEdit.text().trimmed();
        QString inputDept = deptCombo.currentText();

        if (inputName.isEmpty()) {
            QMessageBox::warning(m_parentWidget, "提示", "姓名不能为空！");
            return;
        }

        // 3. ★ 核心防御：去数据库核查该人员是否已经在登录页注册过档案
        QSqlQuery checkQuery;
        checkQuery.prepare("SELECT id FROM users WHERE name = :name AND department = :dept");
        checkQuery.bindValue(":name", inputName);
        checkQuery.bindValue(":dept", inputDept);

        if (checkQuery.exec() && checkQuery.next()) {
            // 验证通过！这个人确实在数据库里。
            // 抛出命令给 AI 线程，让摄像头开始抓取人脸
            emit startRegistration(inputName);

            // 给用户一个明确的指示
            QMessageBox::information(m_parentWidget, "授权成功",
                "身份核验通过！\n请正视摄像头，系统正在提取您的面部特征...");
        }
        else {
            // 查无此人，或者部门填错了，直接拦截录入请求！
            QMessageBox::warning(m_parentWidget, "核验拦截",
                "系统中不存在该姓名与部门的组合！\n请确认信息无误，或先返回登录页完成【新员工注册】！");
        }
    }
}

// ==========================================
// 🚀 接收 AI 线程的结果并落库
// ==========================================
void RegisterModule::onFeatureReady(QString name, QByteArray featureBytes) {
    QSqlQuery query;
    // 从 INSERT 改成 UPDATE，完美更新已有账号的空白人脸特征（feature列）
    query.prepare("UPDATE users SET feature = :feature WHERE name = :name");
    query.bindValue(":feature", featureBytes);
    query.bindValue(":name", name);

    if (query.exec()) {
        if (query.numRowsAffected() > 0) {
            QMessageBox::information(m_parentWidget, "录入成功", "员工【" + name + "】的人脸特征绑定成功！可以去打卡了！");
            emit dataChanged(); // 通知主界面，数据库有变动（可用于刷新内存里的特征库）
        }
        else {
            // 这个分支理论上不会触发，因为我们在 triggerRegistration 已经做过前置校验了，属于双重保险
            QMessageBox::warning(m_parentWidget, "警告", "写入特征失败，找不到名为【" + name + "】的员工！");
        }
    }
    else {
        QMessageBox::critical(m_parentWidget, "数据库错误", "特征写入失败：" + query.lastError().text());
    }
}

void RegisterModule::onRegisterFailed(QString errorMsg) {
    QMessageBox::warning(m_parentWidget, "录入失败", errorMsg);
}