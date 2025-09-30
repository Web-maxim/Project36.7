﻿// Database.h
#pragma once
#include <string>
#include <vector>
#include "sqlite3.h"
#include <ctime>

using namespace std;

// структура для сообщений
struct Message {
    int id;
    string sender;
    string recipient;
    string text;
};

class Database {
private:
    sqlite3* db;

public:
    Database(const string& filename);
    ~Database();

    bool init(); // теперь без аргументов

    bool addUser(const string& login, const string& password, const string& name);
    bool checkUser(const string& login, const string& password);

    bool addMessage(const string& sender, const string& recipient, const string& text);
    vector<Message> getAllMessages();

    bool addBan(const string& login, long long untilEpochSec, const string& reason);
    bool removeBan(const string& login);
    bool isBanned(const string& login);

    void printAllMessages();
    vector<string> getAllUsers();
};
