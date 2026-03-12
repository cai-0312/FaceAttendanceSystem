#include "CenterAndComboDelegate.h"

#include <QComboBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QDebug>

CenterAndComboDelegate::CenterAndComboDelegate(int col,
    QStringList items,
    QObject* parent)
    : QStyledItemDelegate(parent), comboColumn(col), comboItems(items)
{}

void CenterAndComboDelegate::initStyleOption(QStyleOptionViewItem* option,
    const QModelIndex& index) const
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

QWidget* CenterAndComboDelegate::createEditor(QWidget* parent,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        QComboBox* cb = new QComboBox(parent);
        cb->addItems(comboItems);
        cb->setStyleSheet("QComboBox { padding-left: 10px; }");
        return cb;
    }
    return nullptr;
}

void CenterAndComboDelegate::setEditorData(QWidget* editor,
    const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        QString val = index.model()->data(index, Qt::EditRole).toString();
        QComboBox* cb = qobject_cast<QComboBox*>(editor);
        if (cb) cb->setCurrentText(val);
    }
}

void CenterAndComboDelegate::setModelData(QWidget* editor,
    QAbstractItemModel* model,
    const QModelIndex& index) const
{
    if (index.column() == comboColumn && !comboItems.isEmpty()) {
        QComboBox* cb = qobject_cast<QComboBox*>(editor);
        if (cb) {
            QString newRole = cb->currentText();
            model->setData(index, newRole, Qt::EditRole);
            int userId = model->index(index.row(), 0).data().toInt();

            if (userId > 0) {
                QSqlQuery query(QSqlDatabase::database("server_db_connection"));
                query.prepare("UPDATE users SET role = :r WHERE id = :id");
                query.bindValue(":r", newRole);
                query.bindValue(":id", userId);
                if (!query.exec()) {
                    qDebug() << "严重错误：修改底层数据库失败！" << query.lastError().text();
                }
            }
        }
    }
}