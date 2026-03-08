#ifndef USERMODULE_H
#define USERMODULE_H

#include <QObject>
#include <QTableView>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QPoint>

class UserModule : public QObject
{
    Q_OBJECT
public:
    UserModule(QTableView* tableView, QComboBox* deptCombo, QPushButton* filterBtn, QWidget* parentWidget = nullptr);

signals:
    void dataChanged(); // 通知其他模块数据已刷新

public slots:
    void refreshTable(QString filterDept = "");
    void deleteSelectedUser();

private slots:
    void onFilterClicked();
    void onCustomContextMenu(const QPoint& pos);
    void onResetPassword(int row);
    void onExportRoster();

private:
    QTableView* m_tableView;
    QComboBox* m_deptCombo;
    QPushButton* m_filterBtn;
    QWidget* m_parentWidget;

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_exportBtn = nullptr;

    // 🚀 核心替换：抛弃直接连库的 QSqlTableModel，改用纯内存模型 QStandardItemModel
    QStandardItemModel* m_userModel;

    void injectAdvancedUI();
};

#endif // USERMODULE_H