// MainWindow.h
#pragma once
#include <QMainWindow>
class QTabWidget;
class QTableView;
class QStandardItemModel;
class QTimer;
class QProcess;

#include "Database.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshUsers();
    void refreshMessages();
    void startServer();
    void stopServer();
    void startClient();
    void stopClient();
    void banSelected();
    void unbanSelected();
    void kickSelected(); // для кнопки кик

private:
    QTabWidget* tabs_;
    QTableView* usersView_;
    QTableView* messagesView_;

    QStandardItemModel* usersModel_ = nullptr;
    QStandardItemModel* messagesModel_ = nullptr;
    QTimer* refreshTimer_ = nullptr;

    Database db_{ "chat.db" };

    QProcess* serverProc_ = nullptr;
    QProcess* clientProc_ = nullptr;
};
