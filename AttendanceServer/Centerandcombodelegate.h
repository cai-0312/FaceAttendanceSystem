#ifndef CENTERANDCOMBODELEGATE_H
#define CENTERANDCOMBODELEGATE_H
#include <QStyledItemDelegate>
#include <QStringList>
class CenterAndComboDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    int comboColumn;                                // 指定应以下拉框编辑的列索引
    QStringList comboItems;                         // 下拉框可选项列表
    explicit CenterAndComboDelegate(int col = -1, QStringList items = QStringList(), QObject* parent = nullptr); // 构造并设置目标列与候选项
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override; // 设置单元格显示样式（如居中）
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override; // 创建用于编辑的下拉控件
    void setEditorData(QWidget* editor, const QModelIndex& index) const override; // 将模型当前值填充到编辑器中
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override; // 将编辑器选中值写回模型
};
#endif // CENTERANDCOMBODELEGATE_H