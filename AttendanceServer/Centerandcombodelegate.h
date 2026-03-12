#ifndef CENTERANDCOMBODELEGATE_H
#define CENTERANDCOMBODELEGATE_H

#include <QStyledItemDelegate>
#include <QStringList>

/**
 * @brief UI 辅助代理：用于服务端界面表格渲染
 *        - 列内容居中对齐
 *        - 指定列使用 QComboBox 进行编辑（用于权限角色修改）
 */
class CenterAndComboDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    int         comboColumn;
    QStringList comboItems;

    explicit CenterAndComboDelegate(int col = -1,
        QStringList items = QStringList(),
        QObject* parent = nullptr);

    void     initStyleOption(QStyleOptionViewItem* option,
        const QModelIndex& index) const override;

    QWidget* createEditor(QWidget* parent,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;

    void     setEditorData(QWidget* editor,
        const QModelIndex& index) const override;

    void     setModelData(QWidget* editor,
        QAbstractItemModel* model,
        const QModelIndex& index) const override;
};

#endif // CENTERANDCOMBODELEGATE_H