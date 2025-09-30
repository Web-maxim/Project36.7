// MainWindow.cpp
#include "MainWindow.h"

#include <ctime>        // time()
#include <QAction>
#include <QCoreApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QHeaderView>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
    tabs_(new QTabWidget(this)),
    usersView_(new QTableView(this)),
    messagesView_(new QTableView(this)) {

    // БД: создаст таблицы при первом запуске
    db_.init();

    // Центр и вкладки
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(tabs_);
    setCentralWidget(central);

    // Модели
    usersModel_ = new QStandardItemModel(this);
    usersModel_->setHorizontalHeaderLabels({ tr("Login") });

    messagesModel_ = new QStandardItemModel(this);
    messagesModel_->setHorizontalHeaderLabels({ tr("ID"), tr("Sender"), tr("Recipient"), tr("Text") });

    // Привязка к таблицам
    usersView_->setModel(usersModel_);
    usersView_->horizontalHeader()->setStretchLastSection(true);

    // Выделяем строки целиком и только одну строку
    usersView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    usersView_->setSelectionMode(QAbstractItemView::SingleSelection);

    // Делаем, чтобы выделение было одинаковым и при потере фокуса
    QPalette pal = usersView_->palette();
    pal.setColor(QPalette::Inactive, QPalette::Highlight, pal.color(QPalette::Active, QPalette::Highlight));
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, pal.color(QPalette::Active, QPalette::HighlightedText));
    usersView_->setPalette(pal);


    messagesView_->setModel(messagesModel_);
    messagesView_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    messagesView_->horizontalHeader()->setStretchLastSection(true);

    // Вкладки
    tabs_->addTab(usersView_, tr("Users"));
    tabs_->addTab(messagesView_, tr("Messages"));

    // Тулбар (обновление + управление сервером/клиентом)
    auto* tb = addToolBar(tr("Actions"));
    auto* actRefresh = tb->addAction(tr("Refresh"));
    connect(actRefresh, &QAction::triggered, this, [this] {
        refreshUsers();
        refreshMessages();
        });
    auto* actSrvStart = tb->addAction(tr("Start Server"));
    connect(actSrvStart, &QAction::triggered, this, &MainWindow::startServer);
    auto* actSrvStop = tb->addAction(tr("Stop Server"));
    connect(actSrvStop, &QAction::triggered, this, &MainWindow::stopServer);
    auto* actCliStart = tb->addAction(tr("Start Client"));
    connect(actCliStart, &QAction::triggered, this, &MainWindow::startClient);
    auto* actCliStop = tb->addAction(tr("Stop Client"));
    connect(actCliStop, &QAction::triggered, this, &MainWindow::stopClient);
    auto* actBan = tb->addAction(tr("Ban..."));
    connect(actBan, &QAction::triggered, this, &MainWindow::banSelected);
    auto* actUnban = tb->addAction(tr("Unban"));
    connect(actUnban, &QAction::triggered, this, &MainWindow::unbanSelected);
    auto* actKick = tb->addAction(tr("Kick"));
    connect(actKick, &QAction::triggered, this, &MainWindow::kickSelected);

    // Периодическое автообновление раз в 2 сек
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(2000);
    connect(refreshTimer_, &QTimer::timeout, this, [this] {
        refreshUsers();
        refreshMessages();
        });
    refreshTimer_->start();

    setWindowTitle(tr("AdminServer"));
    resize(900, 600);

    // Первая загрузка
    refreshUsers();
    refreshMessages();
}

void MainWindow::banSelected() {
    // Берём выделенную строку из вкладки Users
    if (tabs_->currentWidget() != usersView_) tabs_->setCurrentWidget(usersView_);

    auto idx = usersView_->currentIndex();
    if (!idx.isValid()) {
        QMessageBox::warning(this, tr("Ban"), tr("Выберите пользователя в списке Users."));
        return;
    }
    const QString login = usersModel_->item(idx.row(), 0)->text();

    // Спросим длительность в минутах (0 = перманентный)
    bool ok = false;
    int minutes = QInputDialog::getInt(
        this, tr("Ban"),
        tr("Длительность (минуты, 0 = навсегда):"),
        60, 0, 5256000, 1, &ok); // до ~10 лет
    if (!ok) return;

    // Причина (необязательно)
    QString reason = QInputDialog::getText(
        this, tr("Ban"),
        tr("Причина (опционально):"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;

    long long until = 0;
    if (minutes > 0) {
        const std::time_t now = std::time(nullptr);
        until = static_cast<long long>(now) + static_cast<long long>(minutes) * 60LL;
    }

    if (db_.addBan(login.toStdString(), until, reason.toStdString())) {
        statusBar()->showMessage(tr("Пользователь %1 забанен.").arg(login), 3000);
    }
    else {
        QMessageBox::critical(this, tr("Ban"), tr("Не удалось применить бан."));
    }
}

void MainWindow::unbanSelected() {
    if (tabs_->currentWidget() != usersView_) tabs_->setCurrentWidget(usersView_);

    auto idx = usersView_->currentIndex();
    if (!idx.isValid()) {
        QMessageBox::warning(this, tr("Unban"), tr("Выберите пользователя в списке Users."));
        return;
    }
    const QString login = usersModel_->item(idx.row(), 0)->text();

    if (db_.removeBan(login.toStdString())) {
        statusBar()->showMessage(tr("Пользователь %1 разбанен.").arg(login), 3000);
    }
    else {
        QMessageBox::critical(this, tr("Unban"), tr("Не удалось снять бан."));
    }
}

void MainWindow::kickSelected() {
    if (tabs_->currentWidget() != usersView_) tabs_->setCurrentWidget(usersView_);

    auto idx = usersView_->currentIndex();
    if (!idx.isValid()) {
        QMessageBox::warning(this, tr("Kick"), tr("Выберите пользователя в списке Users."));
        return;
    }

    // если в таблице помечен "  [BANNED]" — отрежем суффикс
    QString loginFull = usersModel_->item(idx.row(), 0)->text();
    QString login = loginFull.split(' ').first();

    if (db_.requestKick(login.toStdString())) {
        statusBar()->showMessage(tr("Запрошен кик для %1").arg(login), 3000);
    }
    else {
        QMessageBox::critical(this, tr("Kick"), tr("Не удалось запросить кик."));
    }
}

void MainWindow::refreshUsers() {
    auto users = db_.getAllUsers();
    usersModel_->removeRows(0, usersModel_->rowCount());
    usersModel_->setRowCount((int)users.size());
    for (int i = 0; i < (int)users.size(); ++i) {
        const auto& u = users[i];
        auto* item = new QStandardItem(QString::fromStdString(u));
        if (db_.isBanned(u)) {
            item->setText(item->text() + "  [BANNED]");
            item->setForeground(Qt::red);
        }
        usersModel_->setItem(i, 0, item);
    }
}


void MainWindow::refreshMessages() {
    auto msgs = db_.getAllMessages();
    messagesModel_->removeRows(0, messagesModel_->rowCount());
    messagesModel_->setRowCount((int)msgs.size());
    for (int i = 0; i < (int)msgs.size(); ++i) {
        const auto& m = msgs[i];
        messagesModel_->setItem(i, 0, new QStandardItem(QString::number(m.id)));
        messagesModel_->setItem(i, 1, new QStandardItem(QString::fromStdString(m.sender)));
        messagesModel_->setItem(i, 2, new QStandardItem(QString::fromStdString(m.recipient)));
        messagesModel_->setItem(i, 3, new QStandardItem(QString::fromStdString(m.text)));
    }
}

// ===== Кнопки "запустить/остановить" =====

void MainWindow::startServer() {
    if (serverProc_) return;
    serverProc_ = new QProcess(this);
    serverProc_->setProgram(QCoreApplication::applicationFilePath());
    serverProc_->setArguments({ "--server" });
    serverProc_->setWorkingDirectory(QCoreApplication::applicationDirPath());
    serverProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(serverProc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, [this](int, QProcess::ExitStatus) {
            serverProc_->deleteLater();
            serverProc_ = nullptr;
        });
    serverProc_->start();
}

void MainWindow::stopServer() {
    if (!serverProc_) return;
    serverProc_->terminate();
    if (!serverProc_->waitForFinished(2000))
        serverProc_->kill();
}

void MainWindow::startClient() {
    if (clientProc_) return;
    clientProc_ = new QProcess(this);
    clientProc_->setProgram(QCoreApplication::applicationFilePath());
    clientProc_->setArguments({ "--client" });
    clientProc_->setWorkingDirectory(QCoreApplication::applicationDirPath());
    clientProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(clientProc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, [this](int, QProcess::ExitStatus) {
            clientProc_->deleteLater();
            clientProc_ = nullptr;
        });
    clientProc_->start();
}

void MainWindow::stopClient() {
    if (!clientProc_) return;
    clientProc_->terminate();
    if (!clientProc_->waitForFinished(2000))
        clientProc_->kill();
}
