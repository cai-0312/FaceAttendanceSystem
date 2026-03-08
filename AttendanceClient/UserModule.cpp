#include "UserModule.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QBoxLayout>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDate>
#include <QTimer>
#include <QStandardItem>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 🚀 核心通讯组件：绝不绕过服务器，统一通过 TCP 发送 JSON 请求数据
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

UserModule::UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget)
    : QObject(parentWidget), m_tableView(tableView), m_deptCombo(deptCombo), m_filterBtn(filterBtn), m_parentWidget(parentWidget)
{
    // 替换为纯净的内存模型
    m_userModel = new QStandardItemModel(this);
    m_tableView->setModel(m_userModel);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers); // 双击不可编辑
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(true);

    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &UserModule::onCustomContextMenu);

    if (m_deptCombo) {
        m_deptCombo->clear();
        m_deptCombo->addItems({ "全部", "总经办", "人力资源部", "财务部", "销售部", "研发部", "市场部", "客户服务部" });
    }

    if (m_filterBtn) {
        connect(m_filterBtn, &QPushButton::clicked, this, &UserModule::onFilterClicked);
    }

    QTimer::singleShot(100, this, [this]() {
        injectAdvancedUI();
        });
}

void UserModule::injectAdvancedUI() {
    if (!m_deptCombo) return;
    QWidget* parentW = m_deptCombo->parentWidget();
    if (!parentW || !parentW->layout()) return;

    if (m_parentWidget && m_parentWidget->findChild<QLineEdit*>("UserModule_SearchBox")) {
        return;
    }

    QBoxLayout* lay = qobject_cast<QBoxLayout*>(parentW->layout());
    if (!lay) return;

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("UserModule_SearchBox");
    m_searchEdit->setPlaceholderText("🔍 输入姓名或工号进行模糊搜索...");
    m_searchEdit->setMinimumHeight(32);
    m_searchEdit->setStyleSheet("QLineEdit { border: 1px solid #DCDFE6; border-radius: 15px; padding: 0 15px; background: white; }");

    m_exportBtn = new QPushButton("📊 导出企业花名册");
    m_exportBtn->setMinimumHeight(32);
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    m_exportBtn->setStyleSheet("QPushButton { background-color: #00B42A; color: white; border: none; border-radius: 15px; padding: 0 15px; font-weight: bold; } QPushButton:hover { background-color: #23C343; }");

    int insertIdx = lay->indexOf(m_deptCombo) + 1;
    lay->insertWidget(insertIdx, m_searchEdit);

    if (m_filterBtn) {
        lay->insertWidget(lay->indexOf(m_filterBtn) + 1, m_exportBtn);
    }

    connect(m_searchEdit, &QLineEdit::returnPressed, this, &UserModule::onFilterClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &UserModule::onExportRoster);
}

void UserModule::onFilterClicked() {
    QString dept = m_deptCombo ? m_deptCombo->currentText() : "全部";
    refreshTable(dept);
}

// 🚀 核心修复：完全走 TCP 获取数据，拒绝直接 select()
void UserModule::refreshTable(QString filterDept) {
    m_userModel->clear();
    // 定义我们需要的列
    m_userModel->setHorizontalHeaderLabels({ "工号", "隐藏的Account", "姓名", "部门", "企业职务", "手机号", "性别" });

    QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : "";

    QJsonObject req;
    req["type"] = "query_user_list";
    req["dept"] = filterDept;
    req["keyword"] = keyword;

    QJsonObject res = requestDataFromServer(req);

    if (res["status"].toString() == "success") {
        QJsonArray users = res["data"].toArray();
        for (int i = 0; i < users.size(); ++i) {
            QJsonObject u = users[i].toObject();
            QList<QStandardItem*> rowItems;

            rowItems << new QStandardItem(u["id"].toString());
            rowItems << new QStandardItem(u["account"].toString()); // 隐藏使用
            rowItems << new QStandardItem(u["name"].toString());
            rowItems << new QStandardItem(u["department"].toString());
            rowItems << new QStandardItem(u["job_title"].toString());
            rowItems << new QStandardItem(u["phone"].toString());
            rowItems << new QStandardItem(u["gender"].toString());

            for (auto item : rowItems) item->setTextAlignment(Qt::AlignCenter);
            m_userModel->appendRow(rowItems);
        }
    }

    // 隐藏 Account 列（作为后台操作的唯一凭证）
    m_tableView->hideColumn(1);
}

void UserModule::onCustomContextMenu(const QPoint& pos) {
    QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid()) return;

    int row = index.row();
    QString empName = m_userModel->item(row, 2)->text(); // 第2列是姓名

    QMenu menu(m_tableView);

    QAction* resetAct = menu.addAction("🔑 重置密码为 123456");
    QAction* deleteAct = menu.addAction("🗑️ 删除员工 [" + empName + "]");

    connect(resetAct, &QAction::triggered, [=]() { onResetPassword(row); });
    connect(deleteAct, &QAction::triggered, [=]() {
        m_tableView->selectRow(row);
        deleteSelectedUser();
        });

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void UserModule::onResetPassword(int row) {
    QString account = m_userModel->item(row, 1)->text(); // 隐藏的account
    QString name = m_userModel->item(row, 2)->text();

    if (QMessageBox::question(m_parentWidget, "确认重置",
        QString("确定要将员工【%1】的登录密码重置为默认密码 123456 吗？").arg(name)) == QMessageBox::Yes) {

        QJsonObject req;
        req["type"] = "admin_reset_password";
        req["account"] = account;
        req["name"] = name;
        QJsonObject res = requestDataFromServer(req);

        if (res["status"].toString() == "success") {
            QMessageBox::information(m_parentWidget, "成功", "密码已成功重置为：123456");
        }
        else {
            QMessageBox::critical(m_parentWidget, "失败", "数据库更新失败，请检查服务端连接！");
        }
    }
}

void UserModule::deleteSelectedUser() {
    QItemSelectionModel* selectModel = m_tableView->selectionModel();
    if (!selectModel->hasSelection()) {
        QMessageBox::warning(m_parentWidget, "提示", "请先在表格中选中要删除的员工！");
        return;
    }

    int row = selectModel->selectedRows().first().row();
    QString account = m_userModel->item(row, 1)->text();
    QString name = m_userModel->item(row, 2)->text();

    if (account == "admin" || name == "超级管理员") {
        QMessageBox::critical(m_parentWidget, "权限拒绝", "系统内置超级管理员账号禁止删除！");
        return;
    }

    if (QMessageBox::question(m_parentWidget, "高危操作", "确认彻底删除员工【" + name + "】？\n删除后该人员的考勤和人脸将被抹除！") == QMessageBox::Yes) {

        QJsonObject req;
        req["type"] = "admin_delete_user";
        req["account"] = account;
        req["name"] = name;
        QJsonObject res = requestDataFromServer(req);

        if (res["status"].toString() == "success") {
            QMessageBox::information(m_parentWidget, "成功", "员工已彻底离职/删除！");
            emit dataChanged();
            refreshTable(m_deptCombo ? m_deptCombo->currentText() : "全部");
        }
        else {
            QMessageBox::critical(m_parentWidget, "失败", "删除失败，请检查服务端状态。");
        }
    }
}

void UserModule::onExportRoster() {
    QString filePath = QFileDialog::getSaveFileName(m_parentWidget, "导出企业花名册", "Roster_" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(m_parentWidget, "错误", "无法创建文件！");
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QString("\xEF\xBB\xBF");
    out << QStringLiteral("工号,姓名,性别,部门,职务,联系电话\n");

    for (int i = 0; i < m_userModel->rowCount(); ++i) {
        out << QString("%1,%2,%3,%4,%5,%6\n").arg(
            m_userModel->item(i, 0)->text(), // id
            m_userModel->item(i, 2)->text(), // name
            m_userModel->item(i, 6)->text(), // gender
            m_userModel->item(i, 3)->text(), // dept
            m_userModel->item(i, 4)->text(), // job_title
            m_userModel->item(i, 5)->text()  // phone
        );
    }
    file.close();
    QMessageBox::information(m_parentWidget, "导出成功", "《企业花名册》已成功导出！");
}