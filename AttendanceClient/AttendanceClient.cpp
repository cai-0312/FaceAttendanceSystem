#include "AttendanceClient.h"
#include "ui_AttendanceClient.h"
#include "MainWidget.h"
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSqlDatabase>

AttendanceClient::AttendanceClient(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AttendanceClientClass)
{
    ui->setupUi(this);

    // 确保程序启动时默认显示第 0 页（登录页）
    ui->stackedWidget->setCurrentIndex(0);

    // ★ 保持之前的数据库连接逻辑
    if (!QSqlDatabase::contains("qt_sql_default_connection")) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC");
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
            "SERVER=127.0.0.1;PORT=3305;DATABASE=attendance_db;"
            "UID=root;PWD=root;");
        db.setDatabaseName(dsn);
        if (!db.open()) {
            QMessageBox::critical(this, "数据库启动失败", "无法连接到 MySQL！\n" + db.lastError().text());
        }
    }
}

AttendanceClient::~AttendanceClient() {
    delete ui;
}

// ==========================================
// 🚀 1. 登录与注册页面的“丝滑翻页”
// ==========================================
void AttendanceClient::on_btn_GoRegister_clicked() {
    ui->stackedWidget->setCurrentIndex(1); // 翻到第1页：注册页面
}

void AttendanceClient::on_btn_BackLogin_clicked() {
    ui->stackedWidget->setCurrentIndex(0); // 翻回第0页：登录页面
}

// ==========================================
// 🚀 2. 登录验证逻辑
// ==========================================
void AttendanceClient::on_btn_Login_clicked() {
    QString account = ui->lineEdit_Account->text();
    QString pwd = ui->lineEdit_pwd->text();
    QString role = ui->comboBox_Role->currentText();

    if (account.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "提示", "工号和密码不能为空！");
        return;
    }

    QSqlQuery query;
    query.prepare("SELECT name, department FROM users WHERE account = :account AND password = :pwd AND role = :role");
    query.bindValue(":account", account);
    query.bindValue(":pwd", pwd);
    query.bindValue(":role", role);

    if (query.exec() && query.next()) {
        QString realName = query.value(0).toString(); // 获取真实姓名
        // QString dept = query.value(1).toString();  // 部门(如果你之前查了的话)

        QMessageBox::information(this, "成功", "登录成功！欢迎 " + role + "：" + realName);

        // 🚀 核心交接：在这里把“真实姓名”和“角色”传递给主窗口！
        MainWidget* mainWindow = new MainWidget(realName, role);
        mainWindow->show();
        this->hide();
    }
    else {
        QMessageBox::critical(this, "失败", "登录失败：工号、密码或角色错误！");
    }
}

// ==========================================
// 🚀 3. 提交注册资料并写入数据库
// ==========================================
void AttendanceClient::on_btn_ConfirmRegister_clicked() {
    // 获取注册表单的所有数据
    QString account = ui->lineEdit_RegAccount->text();
    QString name = ui->lineEdit_RegName->text();
    QString pwd = ui->lineEdit_RegPwd->text();
    QString phone = ui->lineEdit_RegPhone->text();
    QString gender = ui->comboBox_RegGender->currentText();
    QString dept = ui->comboBox_RegDept->currentText();

    if (account.isEmpty() || name.isEmpty() || pwd.isEmpty()) {
        QMessageBox::warning(this, "提示", "工号、姓名、密码为必填项！");
        return;
    }

    QSqlQuery query;
    // 默认注册的都是“普通员工”
    query.prepare("INSERT INTO users (account, password, name, role, department, phone, gender) "
        "VALUES (:account, :pwd, :name, '普通员工', :dept, :phone, :gender)");

    query.bindValue(":account", account);
    query.bindValue(":pwd", pwd);
    query.bindValue(":name", name);
    query.bindValue(":dept", dept);
    query.bindValue(":phone", phone);
    query.bindValue(":gender", gender);

    if (query.exec()) {
        QMessageBox::information(this, "注册成功", "账号注册成功，已自动为您跳转至登录页！");
        // 注册成功后，自动清空输入框并翻回登录页
        ui->lineEdit_RegAccount->clear();
        ui->lineEdit_RegName->clear();
        ui->lineEdit_RegPwd->clear();
        ui->lineEdit_RegPhone->clear();
        ui->stackedWidget->setCurrentIndex(0);
    }
    else {
        QMessageBox::warning(this, "注册失败", "该工号可能已被占用或数据库错误！\n" + query.lastError().text());
    }
}