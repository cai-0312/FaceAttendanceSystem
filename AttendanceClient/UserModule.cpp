#include "UserModule.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QSqlError>

UserModule::UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget)
    : QObject(parentWidget), m_tableView(tableView), m_deptCombo(deptCombo), m_parentWidget(parentWidget)
{
    m_userModel = new QSqlTableModel(this);
    m_userModel->setTable("users"); // 绑定 users 表

    // ==========================================
    // 🚀 核心升级 1：开启自动化“修改信息”功能！
    // ==========================================
    // 设置为：当你在表格里改完一个格子离开时，自动执行 UPDATE 语句保存到 MySQL
    m_userModel->setEditStrategy(QSqlTableModel::OnFieldChange);

    m_userModel->setHeaderData(1, Qt::Horizontal, "工号");
    m_userModel->setHeaderData(3, Qt::Horizontal, "姓名");
    m_userModel->setHeaderData(4, Qt::Horizontal, "角色");
    m_userModel->setHeaderData(5, Qt::Horizontal, "部门");
    m_userModel->setHeaderData(7, Qt::Horizontal, "手机号");
    m_userModel->setHeaderData(8, Qt::Horizontal, "性别");

    m_tableView->setModel(m_userModel);

    // ★ 把原本的 NoEditTriggers 改成 DoubleClicked，允许管理员双击单元格直接修改人员信息！
    m_tableView->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // ==========================================
    // 🚀 核心升级 2：修复删除人员后的“序号断层”问题
    // ==========================================
    // 我们将真实的 ID 列隐藏（保护底层数据），并显示 Qt 自带的垂直表头，
    // 这样不管你怎么删，左侧的序号永远是连续的 1, 2, 3, 4！
    m_tableView->verticalHeader()->setVisible(true);

    m_tableView->setColumnHidden(0, true); // 隐藏真实的、断层的主键 id
    m_tableView->setColumnHidden(2, true); // 隐藏密码列
    m_tableView->setColumnHidden(6, true); // 隐藏特征码列

    if (filterBtn) {
        connect(filterBtn, &QPushButton::clicked, this, &UserModule::onFilterClicked);
    }

    refreshTable();
}

void UserModule::onFilterClicked() {
    if (m_deptCombo) {
        refreshTable(m_deptCombo->currentText());
    }
}

void UserModule::refreshTable(QString filterDept) {
    if (filterDept == "全部" || filterDept.isEmpty()) {
        m_userModel->setFilter("");
    }
    else {
        m_userModel->setFilter(QString("department = '%1'").arg(filterDept));
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

    // 取出工号(第1列)和姓名(第3列)
    QString account = m_userModel->data(m_userModel->index(row, 1)).toString();
    QString name = m_userModel->data(m_userModel->index(row, 3)).toString();

    // 防御机制
    if (account == "admin") {
        QMessageBox::warning(m_parentWidget, "权限拒绝", "系统内置超级管理员账号禁止删除！");
        return;
    }

    if (QMessageBox::question(m_parentWidget, "警告", "确认彻底删除员工【" + name + "】？\n删除后该人员将无法打卡！") == QMessageBox::Yes) {
        m_userModel->removeRow(row);

        if (m_userModel->submitAll()) {
            QMessageBox::information(m_parentWidget, "成功", "已彻底删除！");
            emit dataChanged(); // 广播刷新特征库
            refreshTable(m_deptCombo ? m_deptCombo->currentText() : "全部");
        }
        else {
            QMessageBox::critical(m_parentWidget, "失败", "删除失败，数据库拒绝访问：" + m_userModel->lastError().text());
            m_userModel->revertAll();
        }
    }
}