#ifndef CENTERANDCOMBODELEGATE_H
#define CENTERANDCOMBODELEGATE_H
#include <QStyledItemDelegate>
#include <QStringList>
class CenterAndComboDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    int comboColumn;                                                                                                     // 渲染下拉选择框交互模式的目标列索引
    QStringList comboItems;                                                                                              // 下拉选择框展示的候选字符串列表集
    explicit CenterAndComboDelegate(int col = -1, QStringList items = QStringList(), QObject* parent = nullptr);         // 初始化自定义表格渲染代理组件并绑定控制参数
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;                         // 拦截并重新定义表格单元格的基础绘制样式与文本对齐方式
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override; // 在用户双击指定单元格时动态生成下拉选择框编辑组件
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;                                        // 将底层数据模型中当前选中项的值同步展示至下拉选择框视图中
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;              // 将用户在下拉框中选择的新值覆盖回数据模型并执行数据库层同步
};
#endif // CENTERANDCOMBODELEGATE_H