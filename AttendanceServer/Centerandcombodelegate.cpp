#include "CenterAndComboDelegate.h"
#include <QComboBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
// 构造函数：初始化自定义项代理实例，配置目标处理列索引及对应的下拉列表数据源字典
CenterAndComboDelegate::CenterAndComboDelegate(int col, QStringList items, QObject* parent)
    : QStyledItemDelegate(parent), comboColumn(col), comboItems(items)
{}
// 视图样式重写配置：强制所有接管的单元格实施水平居中对齐，并针对特定业务 ID 列执行前置补零格式化
void CenterAndComboDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    option->displayAlignment = Qt::AlignCenter;

    if (index.column() == 0) {
        int idVal = index.data().toInt();
        if (idVal > 0) {
            option->text = QString("%1").arg(idVal, 3, 10, QChar('0'));
        }
    }
}
// 动态交互编辑器生成：当视图进入编辑生命周期时，拦截默认输入框，为指定列生成并返回下拉选择框实例
QWidget* CenterAndComboDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        QComboBox* cb = new QComboBox(parent);
        cb->addItems(comboItems);
        cb->setStyleSheet("QComboBox { padding-left: 10px; }");
        return cb;
    }
    return nullptr;
}
// 视图编辑器数据装载：提取底层内存模型中原有的旧值，并同步渲染至刚刚生成的下拉选择框中
void CenterAndComboDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        QString val = index.model()->data(index, Qt::EditRole).toString();
        QComboBox* cb = qobject_cast<QComboBox*>(editor);
        if (cb) cb->setCurrentText(val);
    }
}
// 业务数据回写与物理落盘：捕获用户的下拉框最终选择动作，更新模型内存，并构造底层 SQL 指令向下发往数据库
void CenterAndComboDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        QComboBox* cb = qobject_cast<QComboBox*>(editor);
        if (cb) {
            QString newRole = cb->currentText();
            model->setData(index, newRole, Qt::EditRole);
            int userId = model->index(index.row(), 0).data().toInt();
            // 如果成功抓取到目标行对应的有效员工 ID，则建立数据库交互连接并执行角色变更操作
            if (userId > 0) {
                QSqlQuery query(QSqlDatabase::database("server_db_connection"));
                query.prepare("UPDATE users SET role = :r WHERE id = :id");
                query.bindValue(":r", newRole);
                query.bindValue(":id", userId);
                // 执行更新操作，并严格屏蔽一切异常输出以防范调试信息泄露
                query.exec();
            }
        }
    }
}