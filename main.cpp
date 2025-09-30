// main.cpp
#ifdef _WIN32
#include <Windows.h>
#include <cstdio>
#include <iostream>

static void openConsole() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONIN$", "r", stdin);
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    // ВАЖНО: UTF-8 для ввода/вывода в консоли
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // (не обязательно, но удобно)
    std::ios::sync_with_stdio(false);
}
#endif


#include <string>
#include "server.h"
#include "client.h"
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--server") {
#ifdef _WIN32
            openConsole();
#endif
            return server_main();
        }
        if (mode == "--client") {
#ifdef _WIN32
            openConsole();
#endif
            return client_main();
        }
    }
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
