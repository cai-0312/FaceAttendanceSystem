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

// 触发人脸录入流程：弹出身份验证表单并进行数据库前置核验
void RegisterModule::triggerRegistration() {
    // 动态创建对话框及其布局，用于录入员工基本信息
    QDialog dialog(m_parentWidget);
    dialog.setWindowTitle("身份确认 - 录入人脸");
    QFormLayout form(&dialog);

    QLineEdit nameEdit(&dialog);
    QComboBox deptCombo(&dialog);

    // 同步最新的企业组织架构
    deptCombo.addItems({
        "总裁办",
        "人力资源部",
        "财务部",
        "销售部",
        "研发部",
        "市场部",
        "客户服务部"
        });

    form.addRow("员工姓名:", &nameEdit);
    form.addRow("所属部门:", &deptCombo);

    // 添加标准按钮盒：确定与取消
    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

    // 运行对话框并处理确认逻辑
    if (dialog.exec() == QDialog::Accepted) {
        QString inputName = nameEdit.text().trimmed();
        QString inputDept = deptCombo.currentText();

        // 校验输入项是否为空
        if (inputName.isEmpty()) {
            QMessageBox::warning(m_parentWidget, "提示", "姓名不能为空！");
            return;
        }

        // 进行数据库安全核查：确保该员工已在系统中有注册档案
        // 核心修复：直接使用 arg() 拼接，避免 ODBC 绑值失效
        QString sql = QString("SELECT id FROM users WHERE name = '%1' AND department = '%2'")
            .arg(inputName, inputDept);
        QSqlQuery checkQuery(sql);

        if (checkQuery.next()) {
            // 身份核验通过，向算法线程发送信号启动摄像头抓取
            emit startRegistration(inputName);

            // 告知用户后续操作指导
            QMessageBox::information(m_parentWidget, "授权成功",
                "身份核验通过！\n请正视摄像头，系统正在提取您的面部特征...");
        }
        else {
            // 拦截非法录入请求，防止在无账号情况下录入人脸
            QMessageBox::warning(m_parentWidget, "核验拦截",
                "系统中不存在该姓名与部门的组合！\n请确认信息无误，或先返回登录页完成【新员工注册】！");
        }
    }
}

// 处理特征提取结果：将生成的特征向量二进制数据更新到数据库对应记录中
void RegisterModule::onFeatureReady(QString name, QByteArray featureBytes) {
    QSqlQuery query;
    // 使用更新语句为已有员工账号绑定人脸特征数据
    query.prepare("UPDATE users SET feature = :feature WHERE name = :name");
    query.bindValue(":feature", featureBytes);
    // 这里因为 name 是已知且之前刚查过的有效数据，且 feature 是二进制，所以保留 bindValue
    query.bindValue(":name", name);

    if (query.exec()) {
        if (query.numRowsAffected() > 0) {
            // 更新成功，发送数据变更信号以通知系统刷新内存特征库
            QMessageBox::information(m_parentWidget, "录入成功", "员工【" + name + "】的人脸特征绑定成功！可以去打卡了！");
            emit dataChanged();
        }
        else {
            // 兜底逻辑：防止更新了不存在的记录
            QMessageBox::warning(m_parentWidget, "警告", "写入特征失败，找不到名为【" + name + "】的员工！");
        }
    }
    else {
        // 数据库操作异常报错
        QMessageBox::critical(m_parentWidget, "数据库错误", "特征写入失败：" + query.lastError().text());
    }
}

// 处理录入失败：接收来自AI处理线程的错误信息并弹窗提示
void RegisterModule::onRegisterFailed(QString errorMsg) {
    QMessageBox::warning(m_parentWidget, "录入失败", errorMsg);
}