#include "UserModule.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QSqlError>

UserModule::UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget)
    : QObject(parentWidget), m_tableView(tableView), m_deptCombo(deptCombo), m_parentWidget(parentWidget)
{
    m_userModel = new QSqlTableModel(this);
    m_userModel->setTable("users");

    // 开启自动化“修改信息”功能：单元格修改后自动 UPDATE 到数据库
    m_userModel->setEditStrategy(QSqlTableModel::OnFieldChange);

    // 先执行一次 select，确保模型获取到了字段结构，使得 fieldIndex 能够正确返回索引
    m_userModel->select();

    // 动态获取字段索引，并进行表头汉化
    m_userModel->setHeaderData(m_userModel->fieldIndex("account"), Qt::Horizontal, "工号");
    m_userModel->setHeaderData(m_userModel->fieldIndex("name"), Qt::Horizontal, "姓名");
    m_userModel->setHeaderData(m_userModel->fieldIndex("role"), Qt::Horizontal, "角色");
    m_userModel->setHeaderData(m_userModel->fieldIndex("department"), Qt::Horizontal, "部门");
    m_userModel->setHeaderData(m_userModel->fieldIndex("job_title"), Qt::Horizontal, "企业职务");
    m_userModel->setHeaderData(m_userModel->fieldIndex("phone"), Qt::Horizontal, "手机号");
    m_userModel->setHeaderData(m_userModel->fieldIndex("gender"), Qt::Horizontal, "性别");

    m_tableView->setModel(m_userModel);

    // 允许管理员双击单元格直接修改人员信息
    m_tableView->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(true);

    // 精准隐藏不需要在员工管理列表中展示的列
    m_tableView->setColumnHidden(m_userModel->fieldIndex("id"), true);
    m_tableView->setColumnHidden(m_userModel->fieldIndex("password"), true);
    m_tableView->setColumnHidden(m_userModel->fieldIndex("feature"), true);
    m_tableView->setColumnHidden(m_userModel->fieldIndex("avatar"), true);
    m_tableView->setColumnHidden(m_userModel->fieldIndex("shift_name"), true);
    m_tableView->setColumnHidden(m_userModel->fieldIndex("status_icon"), true);

    // 同步组织架构到下拉框
    if (m_deptCombo) {
        m_deptCombo->clear();
        m_deptCombo->addItems({ "全部", "总裁办", "人力资源部", "财务部", "销售部", "研发部", "市场部", "客户服务部" });
    }

    if (filterBtn) {
        connect(filterBtn, &QPushButton::clicked, this, &UserModule::onFilterClicked);
    }

    // 初始化时加载数据
    refreshTable();
}

void UserModule::onFilterClicked() {
    if (m_deptCombo) {
        refreshTable(m_deptCombo->currentText());
    }
}

void UserModule::refreshTable(QString filterDept) {
    // 核心安全修复：排除 account 为 admin 和 name 为 超级管理员 的行
    QString baseFilter = "account NOT LIKE '%admin%' AND name NOT LIKE '%超级管理员%'";

    if (filterDept == "全部" || filterDept.isEmpty()) {
        m_userModel->setFilter(baseFilter);
    }
    else {
        m_userModel->setFilter(baseFilter + QString(" AND department = '%1'").arg(filterDept));
    }
    m_userModel->select();
}

void UserModule::deleteSelectedUser() {
    QItemSelectionModel* selectModel = m_tableView->selectionModel();
    if (!selectModel->hasSelection()) {
        QMessageBox::warning(m_parentWidget, "提示", "请先在表格中选中要删除的员工！");
        return;
    }

    int row = selectModel->selectedRows().first().row();

    int accountCol = m_userModel->fieldIndex("account");
    int nameCol = m_userModel->fieldIndex("name");

    QString account = m_userModel->data(m_userModel->index(row, accountCol)).toString();
    QString name = m_userModel->data(m_userModel->index(row, nameCol)).toString();

    // 终极防御机制：针对真实字段进行拦截保护
    if (account == "admin" || name == "超级管理员") {
        QMessageBox::critical(m_parentWidget, "权限拒绝", "系统内置超级管理员账号属于绝对隐藏和保护数据，禁止删除！");
        return;
    }

    // 弹窗确认删除操作
    if (QMessageBox::question(m_parentWidget, "警告", "确认彻底删除员工【" + name + "】？\n删除后该人员将无法打卡！") == QMessageBox::Yes) {
        m_userModel->removeRow(row);

        // 提交到数据库并验证是否成功
        if (m_userModel->submitAll()) {
            QMessageBox::information(m_parentWidget, "成功", "已彻底删除！");
            emit dataChanged();
            refreshTable(m_deptCombo ? m_deptCombo->currentText() : "全部");
        }
        else {
            QMessageBox::critical(m_parentWidget, "失败", "删除失败，数据库拒绝访问：" + m_userModel->lastError().text());
            m_userModel->revertAll();
        }
    }
}