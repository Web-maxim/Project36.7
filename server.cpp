// server.cpp
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define _HAS_STD_BYTE 0
#define NOMINMAX
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <cstring>      // для strlen
#include "Database.h"
#include "Config.h"     // для port и max_message_length
#include "net_proto.h"  // единые константы протокола

// --- Fallback, если что-то не определено в net_proto.h ---
#ifndef PROTO_USERS_BEGIN
#define PROTO_USERS_BEGIN "[USERS]"
#endif
#ifndef PROTO_USERS_END
#define PROTO_USERS_END   "[END]"
#endif
#ifndef PROTO_SERVER_TAG
#define PROTO_SERVER_TAG  "[Сервер]"
#endif
#ifndef PROTO_CMD_USERS
#define PROTO_CMD_USERS   "/users"
#endif
#ifndef PROTO_CMD_WHISPER
#define PROTO_CMD_WHISPER "/w"
#endif
#ifndef PROTO_CMD_HELP
#define PROTO_CMD_HELP    "/help"
#endif
#ifndef PROTO_AUTH_OK
#define PROTO_AUTH_OK     "OK"
#endif
#ifndef PROTO_AUTH_FAIL
#define PROTO_AUTH_FAIL   "FAIL"
#endif
// ----------------------------------------------------------

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

using namespace std;

// теперь не const, чтобы можно было подхватить значение из конфига
static size_t MAX_MSG_LEN = 200;

// мапим логин -> сокет (для личных сообщений)
static unordered_map<string, SOCKET> loginToSock;

// аккуратно обрезаем пробелы/CR/LF по краям
static inline std::string trim_copy(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// отправка списка пользователей конкретному клиенту
static void sendUsersListTo(SOCKET client, Database& db) {
    auto users = db.getAllUsers();
    string start = string(PROTO_USERS_BEGIN) + "\n";
    sendAll(client, start.c_str(), start.size());
    for (const auto& u : users) {
        string line = u + "\n";
        sendAll(client, line.c_str(), line.size());
    }
    string end = string(PROTO_USERS_END) + "\n";
    sendAll(client, end.c_str(), end.size());
}

int server_main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // читаем конфиг (порт и лимит длины сообщений)
    auto cfg = loadConfig("config.txt");
    int port = 5000;
    try { port = stoi(cfg.at("port")); }
    catch (...) {}
    try { MAX_MSG_LEN = static_cast<size_t>(stoul(cfg.at("max_message_length"))); }
    catch (...) {}

    // БД
    Database db("chat.db");
    if (!db.init()) {
        cerr << "Ошибка инициализации базы данных!" << endl;
        return 1;
    }

    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета!" << endl;
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Ошибка bind!" << endl;
#ifdef _WIN32
        closesocket(serverSock);
        WSACleanup();
#else
        close(serverSock);
#endif
        return 1;
    }

    listen(serverSock, 5);
    cout << "Сервер запущен на порту " << port << endl;

    fd_set master;
    FD_ZERO(&master);
    FD_SET(serverSock, &master);

    map<SOCKET, string> clientNames;
    unordered_map<SOCKET, string> acc; // аккумуляторы построчного приёма

    while (true) {
        fd_set copy = master;
        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int socketCount = select(0, &copy, nullptr, nullptr, &tv);

        // фоновая проверка запросов на кик
        vector<SOCKET> toKick;
        for (u_int j = 0; j < master.fd_count; j++) {
            SOCKET s = master.fd_array[j];
            if (s == serverSock) continue;
            auto it = clientNames.find(s);
            if (it == clientNames.end()) continue;
            const string& who = it->second;
            if (db.consumeKick(who)) {
                toKick.push_back(s);
            }
        }
        for (SOCKET s : toKick) {
            string who = clientNames[s];
            string byeSelf = string(PROTO_SERVER_TAG) + " Вас отключил администратор (kick).\n";
            sendAll(s, byeSelf.c_str(), byeSelf.size());

            string byeAll = string(PROTO_SERVER_TAG) + " " + who + " отключён (kick)\n";
            cout << byeAll;

            clientNames.erase(s);
            acc.erase(s);
            auto itLS = loginToSock.find(who);
            if (itLS != loginToSock.end() && itLS->second == s) loginToSock.erase(itLS);

            FD_CLR(s, &master);
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            for (u_int j2 = 0; j2 < master.fd_count; j2++) {
                SOCKET outSock = master.fd_array[j2];
                if (outSock != serverSock) {
                    sendAll(outSock, byeAll.c_str(), byeAll.size());
                }
            }
        }

        // фоновая проверка банов (выкидываем даже молчащих клиентов)
        vector<SOCKET> toBan;
        for (u_int j = 0; j < master.fd_count; j++) {
            SOCKET s = master.fd_array[j];
            if (s == serverSock) continue;
            auto it = clientNames.find(s);
            if (it == clientNames.end()) continue;
            const string& who = it->second;
            if (db.isBanned(who)) {
                toBan.push_back(s);
            }
        }
        for (SOCKET s : toBan) {
            string who = clientNames[s];
            string byeSelf = string(PROTO_SERVER_TAG) + " Вы забанены. Соединение будет закрыто.\n";
            sendAll(s, byeSelf.c_str(), byeSelf.size());

            string byeAll = string(PROTO_SERVER_TAG) + " " + who + " отключён (ban)\n";
            cout << byeAll;

            clientNames.erase(s);
            acc.erase(s);
            auto itLS = loginToSock.find(who);
            if (itLS != loginToSock.end() && itLS->second == s) loginToSock.erase(itLS);

            FD_CLR(s, &master);
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            for (u_int j2 = 0; j2 < master.fd_count; j2++) {
                SOCKET outSock = master.fd_array[j2];
                if (outSock != serverSock) {
                    sendAll(outSock, byeAll.c_str(), byeAll.size());
                }
            }
        }

        if (socketCount <= 0) continue;

        for (int i = 0; i < socketCount; i++) {
            SOCKET sock = copy.fd_array[i];

            if (sock == serverSock) {
                // новый клиент
                SOCKET client = accept(serverSock, nullptr, nullptr);
                if (client == INVALID_SOCKET) continue;

                // --- таймаут чтения только для первой строки авторизации ---
#ifdef _WIN32
                DWORD tmp_to = 3000; // 3 сек
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmp_to, sizeof(tmp_to));
#else
                timeval tmp_tv; tmp_tv.tv_sec = 3; tmp_tv.tv_usec = 0;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tmp_tv, sizeof(tmp_tv));
#endif
                // ------------------------------------------------------------

                FD_SET(client, &master);

                bool authorized = false;
                string login = "guest", pass;

                // читаем первую строку: "login:password\n"
                string firstMsg;
                char ch;
                while (true) {
                    int m = recv(client, &ch, 1, 0);
                    if (m <= 0) { authorized = false; break; }
                    if (ch == '\n') break;
                    if (ch != '\r') firstMsg.push_back(ch);
                }

                // --- вернуть сокет к обычному (блокирующему) режиму: убрать таймаут ---
#ifdef _WIN32
                DWORD tmp_to0 = 0; // 0 = бесконечный таймаут (блокирующий)
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmp_to0, sizeof(tmp_to0));
#else
                timeval tmp_tv0; tmp_tv0.tv_sec = 0; tmp_tv0.tv_usec = 0;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tmp_tv0, sizeof(tmp_tv0));
#endif
                // ----------------------------------------------------------------------

                if (!firstMsg.empty()) {
                    size_t pos = firstMsg.find(':');
                    if (pos != string::npos) {
                        login = firstMsg.substr(0, pos);
                        pass = firstMsg.substr(pos + 1);
                    }
                    else {
                        login = firstMsg;
                        pass = "nopass";
                    }

                    if (db.checkUser(login, pass)) {
                        authorized = true;
                    }
                    else {
                        // авто-регистрация, если такого логина не было
                        authorized = db.addUser(login, pass, login);
                    }
                }

                // --- МГНОВЕННЫЙ КИК ... ---
                if (authorized && db.isBanned(login)) {
                    // имитируем отказ, как при неуспешной авторизации
                    string err = string(PROTO_AUTH_FAIL) + "\n";
                    sendAll(client, err.c_str(), err.size());
#ifdef _WIN32
                    closesocket(client);
#else
                    close(client);
#endif
                    FD_CLR(client, &master);
                    continue;
                }

                // если на логин уже висит «кик» — сразу не пускаем
                if (db.consumeKick(login)) {
                    string err = string(PROTO_AUTH_FAIL) + "\n";
                    sendAll(client, err.c_str(), err.size());
#ifdef _WIN32
                    closesocket(client);
#else
                    close(client);
#endif
                    FD_CLR(client, &master);
                    continue;
                }

                if (!authorized) {
                    string err = string(PROTO_AUTH_FAIL) + "\n";
                    sendAll(client, err.c_str(), err.size());
#ifdef _WIN32
                    closesocket(client);
#else
                    close(client);
#endif
                    FD_CLR(client, &master);
                    continue;
                }

                // сюда попадаем только для авторизованных и не забаненных
                clientNames[client] = login;
                loginToSock[login] = client;

                string ok = string(PROTO_AUTH_OK) + "\n";
                sendAll(client, ok.c_str(), ok.size());

                // сообщение о подключении 
                string msg = string(PROTO_SERVER_TAG) + " " + login + " подключился\n";
                cout << msg;

                // отправляем историю (только публичное и мои приватные)
                const string& me = login;
                auto history = db.getAllMessages();
                for (const auto& m : history) {
                    const bool isPublic = m.recipient.empty();
                    const bool iAmSender = (m.sender == me);
                    const bool iAmRecipient = (!m.recipient.empty() && m.recipient == me);
                    if (!(isPublic || iAmSender || iAmRecipient)) continue;

                    string line = "[" + m.sender +
                        (m.recipient.empty() ? " -> ALL" : " -> " + m.recipient) +
                        "] " + m.text + "\n";
                    sendAll(client, line.c_str(), line.size());
                }

                // список пользователей подключившемуся
                sendUsersListTo(client, db);

                // оповестим остальных
                for (u_int j = 0; j < master.fd_count; j++) {
                    SOCKET outSock = master.fd_array[j];
                    if (outSock != serverSock && outSock != client) {
                        sendAll(outSock, msg.c_str(), msg.size());
                    }
                }
            }

            else {
                char buffer[4096]; // было 1024
                int n = recv(sock, buffer, sizeof(buffer), 0);

                if (n <= 0) {
                    string name = clientNames[sock];
                    string msg = string(PROTO_SERVER_TAG) + " " + name + " отключился\n";
                    cout << msg;

                    // чистим структуры
                    clientNames.erase(sock);
                    acc.erase(sock);
                    if (!name.empty()) {
                        auto it = loginToSock.find(name);
                        if (it != loginToSock.end() && it->second == sock) {
                            loginToSock.erase(it);
                        }
                    }

                    FD_CLR(sock, &master);
#ifdef _WIN32
                    closesocket(sock);
#else
                    close(sock);
#endif
                    // рассылаем уведомление
                    for (u_int j = 0; j < master.fd_count; j++) {
                        SOCKET outSock = master.fd_array[j];
                        if (outSock != serverSock) {
                            sendAll(outSock, msg.c_str(), msg.size());
                        }
                    }
                }
                else {

                    // --- МГНОВЕННЫЙ КИК ЕСЛИ СТАЛ ЗАБАНЕН ПОКА ОН В СЕТИ ---
                    {
                        auto itName = clientNames.find(sock);
                        if (itName != clientNames.end()) {
                            const std::string& who = itName->second;
                            if (db.isBanned(who)) {
                                // Сообщим пользователю и всем остальным
                                std::string byeSelf = std::string(PROTO_SERVER_TAG) + " Вы забанены. Соединение будет закрыто.\n";
                                sendAll(sock, byeSelf.c_str(), byeSelf.size());

                                std::string byeAll = std::string(PROTO_SERVER_TAG) + " " + who + " отключён (ban)\n";
                                std::cout << byeAll;

                                // удалить из структур
                                clientNames.erase(sock);
                                acc.erase(sock);
                                auto itLS = loginToSock.find(who);
                                if (itLS != loginToSock.end() && itLS->second == sock) loginToSock.erase(itLS);

                                FD_CLR(sock, &master);
#ifdef _WIN32
                                closesocket(sock);
#else
                                close(sock);
#endif
                                // оповестим остальных
                                for (u_int j = 0; j < master.fd_count; j++) {
                                    SOCKET outSock = master.fd_array[j];
                                    if (outSock != serverSock) {
                                        sendAll(outSock, byeAll.c_str(), byeAll.size());
                                    }
                                }
                                // и переходим к следующему сокету
                                continue;
                            }
                        }
                    }   // --- конец проверки бана ---

                    // построчный приём + фильтрация пустых
                    acc[sock].append(buffer, buffer + n);

                    size_t pos;
                    while ((pos = acc[sock].find('\n')) != string::npos) {
                        string line = acc[sock].substr(0, pos);
                        acc[sock].erase(0, pos + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();

                        string text = trim_copy(line);
                        if (text.empty()) continue;

                        // /help — краткая справка
                        if (text == PROTO_CMD_HELP) {
                            string help;
                            help.reserve(256);
                            help += string(PROTO_SERVER_TAG) + " Команды:\n";
                            help += "  " + string(PROTO_CMD_USERS) + "              — список пользователей\n";
                            help += "  " + string(PROTO_CMD_WHISPER) + " <login> <текст>  — личное сообщение\n";
                            help += "  exit                — выход (на клиенте)\n";
                            sendAll(sock, help.c_str(), help.size());
                            continue;
                        }

                        // /users — выдать список
                        if (text == PROTO_CMD_USERS) {
                            sendUsersListTo(sock, db);
                            continue;
                        }

                        // /w <login> <текст> — личное сообщение
                        if (text.rfind(string(PROTO_CMD_WHISPER) + " ", 0) == 0) {
                            string rest = trim_copy(text.substr(string(PROTO_CMD_WHISPER).size() + 1));
                            size_t sp = rest.find(' ');
                            if (sp == string::npos) {
                                string help = string(PROTO_SERVER_TAG) + " Использование: " + string(PROTO_CMD_WHISPER) + " <login> <текст>\n";
                                sendAll(sock, help.c_str(), help.size());
                                continue;
                            }
                            string toLogin = trim_copy(rest.substr(0, sp));
                            string body = trim_copy(rest.substr(sp + 1));
                            if (toLogin.empty() || body.empty()) {
                                string help = string(PROTO_SERVER_TAG) + " Использование: " + string(PROTO_CMD_WHISPER) + " <login> <текст>\n";
                                sendAll(sock, help.c_str(), help.size());
                                continue;
                            }

                            auto it = loginToSock.find(toLogin);
                            if (it == loginToSock.end()) {
                                string err = string(PROTO_SERVER_TAG) + " Пользователь '" + toLogin + "' не в сети\n";
                                sendAll(sock, err.c_str(), err.size());
                                continue;
                            }

                            // ограничение длины
                            if (body.size() > MAX_MSG_LEN) body.resize(MAX_MSG_LEN);

                            string from = clientNames[sock];
                            string out = "[" + from + " -> " + toLogin + "] " + body + "\n";

                            // отправляем адресату и отправителю (подтверждение)
                            sendAll(it->second, out.c_str(), out.size());
                            sendAll(sock, out.c_str(), out.size());

                            // сохраняем в БД как приватное
                            db.addMessage(from, toLogin, body);
                            continue;
                        }

                        // обычное сообщение во весь чат
                        if (text.size() > MAX_MSG_LEN)
                            text.resize(MAX_MSG_LEN);

                        string out = "[" + clientNames[sock] + "] " + text + "\n";
                        cout << out;

                        db.addMessage(clientNames[sock], "", text);

                        for (u_int j = 0; j < master.fd_count; j++) {
                            SOCKET outSock = master.fd_array[j];
                            if (outSock != serverSock && outSock != sock) {
                                sendAll(outSock, out.c_str(), out.size());
                            }
                        }
                    }
                }
            }
        }
    }

#ifdef _WIN32
    closesocket(serverSock);
    WSACleanup();
#else
    close(serverSock);
#endif
    return 0;
}
