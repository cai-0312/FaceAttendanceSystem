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
// 构造函数：初始化员工模块，配置纯内存表格模型并挂载原生界面交互事件
UserModule::UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget)
    : QObject(parentWidget), m_tableView(tableView), m_deptCombo(deptCombo), m_filterBtn(filterBtn), m_parentWidget(parentWidget)
{
    // 配置标准的纯内存数据模型以切断与底层数据库的强耦合
    m_userModel = new QStandardItemModel(this);
    m_tableView->setModel(m_userModel);
    // 设置表格视图的基础交互策略：禁止双击编辑、强制整行选中、开启表头自适应伸缩
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(true);
    // 启用并挂钩自定义的右键上下文菜单策略，用于管理员级别的快捷指令调度
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &UserModule::onCustomContextMenu);
    // 初始化基础部门过滤选项列表
    if (m_deptCombo) {
        m_deptCombo->clear();
        m_deptCombo->addItems({ "全部", "总经办", "人力资源部", "财务部", "销售部", "研发部", "市场部", "客户服务部" });
    }
    if (m_filterBtn) {
        connect(m_filterBtn, &QPushButton::clicked, this, &UserModule::onFilterClicked);
    }
    // 利用微小时序延时调度，等待主窗口布局渲染完毕后注入动态的高级控制组件
    QTimer::singleShot(100, this, [this]() {
        injectAdvancedUI();
        });
}
// 动态界面构建：识别原始布局挂载点，通过C++代码无损注入搜索输入框与导出按钮
void UserModule::injectAdvancedUI() {
    if (!m_deptCombo) return;
    QWidget* parentW = m_deptCombo->parentWidget();
    if (!parentW || !parentW->layout()) return;
    // 幂等性校验：如果当前布局树中已存在目标搜索框，则不再重复执行注入以防止UI重叠
    if (m_parentWidget && m_parentWidget->findChild<QLineEdit*>("UserModule_SearchBox")) {
        return;
    }
    QBoxLayout* lay = qobject_cast<QBoxLayout*>(parentW->layout());
    if (!lay) return;
    // 构造高级模糊检索文本框控件
    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("UserModule_SearchBox");
    m_searchEdit->setPlaceholderText("🔍 输入姓名或工号进行模糊搜索...");
    m_searchEdit->setMinimumHeight(32);
    m_searchEdit->setStyleSheet("QLineEdit { border: 1px solid #DCDFE6; border-radius: 15px; padding: 0 15px; background: white; }");
    // 构造花名册报表导出触发按钮
    m_exportBtn = new QPushButton("📊 导出企业花名册");
    m_exportBtn->setMinimumHeight(32);
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    m_exportBtn->setStyleSheet("QPushButton { background-color: #00B42A; color: white; border: none; border-radius: 15px; padding: 0 15px; font-weight: bold; } QPushButton:hover { background-color: #23C343; }");
    // 计算插入锚点：紧跟在原始部门下拉框及过滤按钮之后执行流式挂载
    int insertIdx = lay->indexOf(m_deptCombo) + 1;
    lay->insertWidget(insertIdx, m_searchEdit);

    if (m_filterBtn) {
        lay->insertWidget(lay->indexOf(m_filterBtn) + 1, m_exportBtn);
    }
    // 为新增的扩展控制组件绑定对应的底层逻辑处理槽
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &UserModule::onFilterClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &UserModule::onExportRoster);
}
// 执行综合查询：提取前端组合的筛选条件参数并交由下层数据刷新机制处理
void UserModule::onFilterClicked() {
    QString dept = m_deptCombo ? m_deptCombo->currentText() : "全部";
    refreshTable(dept);
}
// 获取并重载视图核心流：基于TCP网络协议向服务端发包拉取人员集合，反序列化后填充内存表格
void UserModule::refreshTable(QString filterDept) {
    m_userModel->clear();
    // 初始化标准数据模型的表头定义
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
            // 构建单元格视图项：包含用于业务主键寻址的隐藏登录凭证
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
    // 后台列保护机制：将用作业务操作唯一凭证的Account列实施视图级隐藏脱敏
    m_tableView->hideColumn(1);
}
// 捕获表格区域右键鼠标操作事件并调出特定权限的快捷管理上下文面板
void UserModule::onCustomContextMenu(const QPoint& pos) {
    QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid()) return;
    int row = index.row();
    QString empName = m_userModel->item(row, 2)->text();
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
// 执行高危变更管理指令：由管理员发起的将选定员工系统密码强制初始化的网络请求操作
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
// 执行记录销毁指令：删除员工档案及关联考勤流水，包含防止删库等逻辑校验
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
            // 抛出变更信号并级联触发底层缓存数据的同步重载
            emit dataChanged();
            refreshTable(m_deptCombo ? m_deptCombo->currentText() : "全部");
        }
        else {
            QMessageBox::critical(m_parentWidget, "失败", "删除失败，请检查服务端状态。");
        }
    }
}
// 执行本地文件流输出：将挂载至底层表格内存模型中的人员矩阵全量序列化并生成CSV格式持久化文件
void UserModule::onExportRoster() {
    QString filePath = QFileDialog::getSaveFileName(m_parentWidget, "导出企业花名册", "Roster_" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(m_parentWidget, "错误", "无法创建文件！");
        return;
    }
    // 为导出文件装配标准的 UTF-8 字节顺序标记，适配不同操作系统下表格软件的中文编解码
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