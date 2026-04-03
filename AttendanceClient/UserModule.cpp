#include "UserModule.h"
#include "NetworkHelper.h" 
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QIcon>
// 构造函数，初始化员工管理模块并绑定界面事件
UserModule::UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget)
    : QObject(parentWidget), m_tableView(tableView), m_deptCombo(deptCombo), m_filterBtn(filterBtn), m_parentWidget(parentWidget)
{
    // 创建数据模型并绑定到表格
    m_userModel = new QStandardItemModel(this);
    m_tableView->setModel(m_userModel);
    // 设置表格只读和整行选择
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(true);
    // 启用右键菜单
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &UserModule::onCustomContextMenu);
    // 初始化部门筛选项
    if (m_deptCombo) {
        m_deptCombo->clear();
        m_deptCombo->addItems({ "全部", "总经办", "人力资源部", "财务部", "销售部", "研发部", "市场部", "客户服务部" });
    }
    if (m_filterBtn) {
        connect(m_filterBtn, &QPushButton::clicked, this, &UserModule::onFilterClicked);
    }
    // 延迟注入扩展控件
    QTimer::singleShot(100, this, [this]() {
        injectAdvancedUI();
        });
}
// 注入搜索框和导出按钮
void UserModule::injectAdvancedUI() {
    if (!m_deptCombo) return;
    QWidget* parentW = m_deptCombo->parentWidget();
    if (!parentW || !parentW->layout()) return;
    // 避免重复注入
    if (m_parentWidget && m_parentWidget->findChild<QLineEdit*>("UserModule_SearchBox")) {
        return;
    }
    QBoxLayout* lay = qobject_cast<QBoxLayout*>(parentW->layout());
    if (!lay) return;
    // 图标路径
    QString iconBase = "../../AttendanceClient/icon_library/";
    // 创建搜索框
    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("UserModule_SearchBox");
    m_searchEdit->setPlaceholderText("输入姓名或工号进行模糊搜索...");
    m_searchEdit->setMinimumHeight(32);
    m_searchEdit->setStyleSheet(
        "QLineEdit { border: 1px solid #DCDFE6; border-radius: 15px; padding: 0 15px 0 35px; background: white; }");
    // 添加搜索图标
    m_searchEdit->addAction(QIcon(iconBase + "User/icon_search.svg"), QLineEdit::LeadingPosition);
    // 创建导出按钮
    m_exportBtn = new QPushButton(QIcon(iconBase + "User/btn_export_roster.svg"), " 导出企业花名册");
    m_exportBtn->setIconSize(QSize(18, 18));
    m_exportBtn->setMinimumHeight(32);
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #00B42A; color: white; border: none; border-radius: 15px; padding: 0 15px; font-weight: bold; }"
        " QPushButton:hover { background-color: #23C343; }");
    // 插入到筛选区域
    int insertIdx = lay->indexOf(m_deptCombo) + 1;
    lay->insertWidget(insertIdx, m_searchEdit);
    if (m_filterBtn) {
        lay->insertWidget(lay->indexOf(m_filterBtn) + 1, m_exportBtn);
    }
    // 给删除按钮设置图标
    QPushButton* deleteBtn = m_parentWidget ? m_parentWidget->findChild<QPushButton*>("btn_deleteUser") : nullptr;
    if (deleteBtn) {
        deleteBtn->setIcon(QIcon(iconBase + "User/btn_delete_user.svg"));
        deleteBtn->setIconSize(QSize(18, 18));
    }
    // 绑定事件
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &UserModule::onFilterClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &UserModule::onExportRoster);
}
// 按条件筛选数据
void UserModule::onFilterClicked() {
    QString dept = m_deptCombo ? m_deptCombo->currentText() : "全部";
    refreshTable(dept);
}
// 刷新员工表格
void UserModule::refreshTable(QString filterDept) {
    m_userModel->clear();
    // 设置表头
    m_userModel->setHorizontalHeaderLabels({ "工号", "隐藏的Account", "姓名", "部门", "企业职务", "手机号", "性别" });
    QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : "";
    QJsonObject req;
    req["type"] = "query_user_list";
    req["dept"] = filterDept;
    req["keyword"] = keyword;
    QJsonObject res = NetworkHelper::request(req);
    if (res["status"].toString() == "success") {
        QJsonArray users = res["data"].toArray();
        for (int i = 0; i < users.size(); ++i) {
            QJsonObject u = users[i].toObject();
            QList<QStandardItem*> rowItems;
            // 填充一行数据
            rowItems << new QStandardItem(u["id"].toString());
            rowItems << new QStandardItem(u["account"].toString());
            rowItems << new QStandardItem(u["name"].toString());
            rowItems << new QStandardItem(u["department"].toString());
            rowItems << new QStandardItem(u["job_title"].toString());
            rowItems << new QStandardItem(u["phone"].toString());
            rowItems << new QStandardItem(u["gender"].toString());

            for (auto item : rowItems) item->setTextAlignment(Qt::AlignCenter);
            m_userModel->appendRow(rowItems);
        }
    }
    // 隐藏账号列
    m_tableView->hideColumn(1);
}
// 显示右键菜单
void UserModule::onCustomContextMenu(const QPoint& pos) {
    QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid()) return;
    int row = index.row();
    QString empName = m_userModel->item(row, 2)->text();
    // 图标路径
    QString iconBase = "../../AttendanceClient/icon_library/";
    QMenu menu(m_tableView);
    // 设置菜单样式
    menu.setStyleSheet(
        "QMenu { background-color: #FFFFFF; border: 1px solid #DEE0E3; border-radius: 8px; padding: 5px 0; }"
        "QMenu::item { padding: 8px 20px; color: #1F2329; }"
        "QMenu::item:selected { background-color: #F2F3F5; }"
        "QMenu::icon { padding-left: 10px; }");
    // 添加菜单项
    QAction* resetAct = menu.addAction(QIcon(iconBase + "User/btn_reset_pwd.svg"), "重置密码为 123456");
    QAction* deleteAct = menu.addAction(QIcon(iconBase + "User/btn_delete_user.svg"), "删除员工 [" + empName + "]");
    connect(resetAct, &QAction::triggered, [=]() { onResetPassword(row); });
    connect(deleteAct, &QAction::triggered, [=]() {
        m_tableView->selectRow(row);
        deleteSelectedUser();
        });
    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}
// 重置密码
void UserModule::onResetPassword(int row) {
    QString account = m_userModel->item(row, 1)->text();
    QString name = m_userModel->item(row, 2)->text();
    if (QMessageBox::question(m_parentWidget, "确认重置",
        QString("确定要将员工【%1】的登录密码重置为默认密码 123456 吗？").arg(name)) == QMessageBox::Yes) {
        QJsonObject req;
        req["type"] = "admin_reset_password";
        req["account"] = account;
        req["name"] = name;
        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() == "success") {
            QMessageBox::information(m_parentWidget, "成功", "密码已成功重置为：123456");
        }
        else {
            QMessageBox::critical(m_parentWidget, "失败", "数据库更新失败，请检查服务端连接！");
        }
    }
}
// 删除选中员工
void UserModule::deleteSelectedUser() {
    QItemSelectionModel* selectModel = m_tableView->selectionModel();
    if (!selectModel->hasSelection()) {
        QMessageBox::warning(m_parentWidget, "提示", "请先在表格中选中要删除的员工！");
        return;
    }
    int row = selectModel->selectedRows().first().row();
    QString account = m_userModel->item(row, 1)->text();
    QString name = m_userModel->item(row, 2)->text();
    if (QMessageBox::question(m_parentWidget, "高危操作", "确认彻底删除员工【" + name + "】？\n删除后该人员的考勤和人脸将被抹除！") == QMessageBox::Yes) {
        QJsonObject req;
        req["type"] = "admin_delete_user";
        req["account"] = account;
        req["name"] = name;
        QJsonObject res = NetworkHelper::request(req);
        if (res["status"].toString() == "success") {
            QMessageBox::information(m_parentWidget, "成功", "员工已彻底离职/删除！");
            // 通知外部刷新
            emit dataChanged();
            refreshTable(m_deptCombo ? m_deptCombo->currentText() : "全部");
        }
        else {
            QMessageBox::critical(m_parentWidget, "失败", "删除失败，请检查服务端状态。");
        }
    }
}
// 导出花名册为 CSV
void UserModule::onExportRoster() {
    QString filePath = QFileDialog::getSaveFileName(m_parentWidget, "导出企业花名册",
        "Roster_" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(m_parentWidget, "错误", "无法创建文件！");
        return;
    }
    // 写入 UTF-8 BOM
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QString("\xEF\xBB\xBF");
    out << QStringLiteral("工号,姓名,性别,部门,职务,联系电话\n");
    for (int i = 0; i < m_userModel->rowCount(); ++i) {
        out << QString("%1,%2,%3,%4,%5,%6\n").arg(
            m_userModel->item(i, 0)->text(),
            m_userModel->item(i, 2)->text(),
            m_userModel->item(i, 6)->text(),
            m_userModel->item(i, 3)->text(),
            m_userModel->item(i, 4)->text(),
            m_userModel->item(i, 5)->text()
        );
    }
    file.close();
    QMessageBox::information(m_parentWidget, "导出成功", "《企业花名册》已成功导出！");
}