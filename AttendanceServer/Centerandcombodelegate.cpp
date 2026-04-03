#include "CenterAndComboDelegate.h"
#include <QComboBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
// 构造函数，保存需要下拉框的列索引和下拉选项列表
CenterAndComboDelegate::CenterAndComboDelegate(int col, QStringList items, QObject* parent)
    : QStyledItemDelegate(parent), comboColumn(col), comboItems(items)
{}
// 设置单元格显示样式：所有列居中对齐，ID列补零为三位数
void CenterAndComboDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    // 居中对齐
    option->displayAlignment = Qt::AlignCenter;
    // 第0列为ID列，格式化为三位补零显示
    if (index.column() == 0) {
        int idVal = index.data().toInt();
        if (idVal > 0) {
            option->text = QString("%1").arg(idVal, 3, 10, QChar('0'));
        }
    }
}
// 创建编辑器：对指定列返回下拉框，其余列不可编辑
QWidget* CenterAndComboDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        // 创建下拉框并填充选项
        QComboBox* cb = new QComboBox(parent);
        cb->addItems(comboItems);
        cb->setStyleSheet("QComboBox { padding-left: 10px; }");
        return cb;
    }
    return nullptr;
}
// 回显数据：将模型当前值设置到下拉框中
void CenterAndComboDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        // 读取当前单元格的值并同步到下拉框选中项
        QString val = index.model()->data(index, Qt::EditRole).toString();
        QComboBox* cb = qobject_cast<QComboBox*>(editor);
        if (cb) cb->setCurrentText(val);
    }
}
// 提交数据：将下拉框选中值写回模型并同步更新数据库
void CenterAndComboDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        QComboBox* cb = qobject_cast<QComboBox*>(editor);
        if (cb) {
            // 获取下拉框选中的新角色值，写入模型
            QString newRole = cb->currentText();
            model->setData(index, newRole, Qt::EditRole);
            // 取当前行第0列的用户ID
            int userId = model->index(index.row(), 0).data().toInt();
            // 用户ID有效时，更新数据库中对应用户的角色
            if (userId > 0) {
                QSqlQuery query(QSqlDatabase::database("server_db_connection"));
                query.prepare("UPDATE users SET role = :r WHERE id = :id");
                query.bindValue(":r", newRole);
                query.bindValue(":id", userId);
                query.exec();
            }
        }
    }
}