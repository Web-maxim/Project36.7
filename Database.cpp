// Database.cpp
#include "Database.h"
#include <iostream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include "sha1.h"  

using namespace std;

// ---- helpers: SHA1 как "A,B,C,D,E" и распознавание такого формата ----
static string sha1_csv5(const string& s) {
    uint* d = sha1(s, static_cast<uint>(s.size()));
    ostringstream oss;
    oss << d[0] << "," << d[1] << "," << d[2] << "," << d[3] << "," << d[4];
    delete[] d;
    return oss.str();
}

static bool looks_like_csv5(const string& s) {
    // очень простая эвристика: 4 запятые и все символы только [0-9,]
    int commas = 0;
    for (char c : s) {
        if (c == ',') ++commas;
        else if (!isdigit(static_cast<unsigned char>(c))) return false;
    }
    return commas == 4;
}

// ----------------------------------------------------------------------

Database::Database(const string& filename) {
    if (sqlite3_open(filename.c_str(), &db) != SQLITE_OK) {
        cerr << "Ошибка открытия БД: " << sqlite3_errmsg(db) << endl;
        db = nullptr;
    }
}

Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

bool Database::init() {
    if (!db) return false;

    // пользователи
    const char* createUsers =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  login TEXT UNIQUE, "
        "  password TEXT, "
        "  name TEXT"
        ");";

    // сообщения
    const char* createMessages =
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  sender TEXT, "
        "  recipient TEXT, "
        "  text TEXT"
        ");";

    // БАНЫ: login (PK), until (UNIX-эпоха, NULL = перманентный бан), reason
    const char* createBans =
        "CREATE TABLE IF NOT EXISTS bans ("
        "  login TEXT PRIMARY KEY, "
        "  until INTEGER NULL, "
        "  reason TEXT"
        ");";

    // таблица «киков»: одно активное требование на логин
    const char* createKicks =
        "CREATE TABLE IF NOT EXISTS kicks ("
        "  login TEXT PRIMARY KEY, "
        "  requested_at INTEGER NOT NULL"
        ");";

    char* errMsg = nullptr;

    if (sqlite3_exec(db, createUsers, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        cerr << "Ошибка SQL (пользователи): " << errMsg << endl;
        sqlite3_free(errMsg);
        return false;
    }
    if (sqlite3_exec(db, createMessages, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        cerr << "Ошибка SQL (сообщения): " << errMsg << endl;
        sqlite3_free(errMsg);
        return false;
    }
    if (sqlite3_exec(db, createBans, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        cerr << "Ошибка SQL (баны): " << errMsg << endl;
        sqlite3_free(errMsg);
        return false;
    }
    if (sqlite3_exec(db, createKicks, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        cerr << "Ошибка SQL (kicks): " << errMsg << endl;
        sqlite3_free(errMsg);
        return false;
    }


    cout << "Database is ready.\n"; //  "База данных готова.\n";
    return true;
}


bool Database::addUser(const string& login, const string& password, const string& name) {
    if (!db) return false;

    // Храним уже ХЕШ, а не открытый пароль
    const string pass_hash = sha1_csv5(password);

    const char* sql = "INSERT OR IGNORE INTO users (login, password, name) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Ошибка подготовки запроса addUser\n";
        return false;
    }
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, pass_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_STATIC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::checkUser(const string& login, const string& password) {
    if (!db) return false;

    // 1) читаем, что лежит в поле password
    const char* sql = "SELECT password FROM users WHERE login=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Ошибка подготовки запроса checkUser\n";
        return false;
    }
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);

    string stored;
    bool found = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* p = sqlite3_column_text(stmt, 0);
        stored = p ? reinterpret_cast<const char*>(p) : "";
        found = true;
    }
    sqlite3_finalize(stmt);

    if (!found) return false;

    // 2) сравниваем
    const string candidate_hash = sha1_csv5(password);

    if (looks_like_csv5(stored)) {
        // уже хранится хеш
        return (stored == candidate_hash);
    }
    else {
        // видимо, старый пользователь с открытым паролем → мигрируем на хеш
        if (stored == password) {
            const char* upd = "UPDATE users SET password=? WHERE login=?;";
            if (sqlite3_prepare_v2(db, upd, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, candidate_hash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, login.c_str(), -1, SQLITE_STATIC);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            return true;
        }
        return false;
    }
}

bool Database::addMessage(const string& sender, const string& recipient, const string& text) {
    if (!db) return false;
    const char* sql = "INSERT INTO messages (sender, recipient, text) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Ошибка подготовки запроса addMessage\n";
        return false;
    }
    sqlite3_bind_text(stmt, 1, sender.c_str(), -1, SQLITE_STATIC);
    if (recipient.empty()) {
        sqlite3_bind_null(stmt, 2);
    }
    else {
        sqlite3_bind_text(stmt, 2, recipient.c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::addBan(const std::string& login, long long untilEpochSec, const std::string& reason) {
    if (!db) return false;
    const char* sql = "INSERT OR REPLACE INTO bans (login, until, reason) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Ошибка SQL (addBan)\n";
        return false;
    }

    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);

    if (untilEpochSec > 0) {
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(untilEpochSec));
    }
    else {
        // NULL => перманентный бан
        sqlite3_bind_null(stmt, 2);
    }

    sqlite3_bind_text(stmt, 3, reason.c_str(), -1, SQLITE_STATIC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::removeBan(const std::string& login) {
    if (!db) return false;
    const char* sql = "DELETE FROM bans WHERE login=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Ошибка SQL (removeBan)\n";
        return false;
    }

    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::isBanned(const std::string& login) {
    if (!db) return false;

    const char* sql = "SELECT until FROM bans WHERE login=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Ошибка SQL (isBanned)\n";
        return false; // осторожный дефолт — считаем «не забанен», но ты можешь вернуть true
    }

    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);

    bool hasRow = false;
    bool untilIsNull = true;
    long long until = 0;

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        hasRow = true;
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            untilIsNull = false;
            until = sqlite3_column_int64(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    if (!hasRow) return false;     // бан-записи нет — не забанен
    if (untilIsNull) return true;  // перманентный бан

    const long long now = static_cast<long long>(time(nullptr));
    if (until > now) {
        return true;               // временный бан ещё действует
    }

    // истёк — снимем и вернём false
    removeBan(login);
    return false;
}

vector<Message> Database::getAllMessages() {
    vector<Message> result;
    if (!db) return result;

    const char* sql = "SELECT id, sender, recipient, text FROM messages;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Message m;
            m.id = sqlite3_column_int(stmt, 0);

            // безопасно читаем sender
            const unsigned char* s = sqlite3_column_text(stmt, 1);
            m.sender = s ? reinterpret_cast<const char*>(s) : "";

            // безопасно читаем recipient (у тебя уже было ок)
            const unsigned char* r = sqlite3_column_text(stmt, 2);
            m.recipient = r ? reinterpret_cast<const char*>(r) : "";

            // безопасно читаем text
            const unsigned char* t = sqlite3_column_text(stmt, 3);
            m.text = t ? reinterpret_cast<const char*>(t) : "";

            result.push_back(m);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void Database::printAllMessages() {
    for (const auto& m : getAllMessages()) {
        cout << "[" << m.id << "] " << m.sender << " -> "
            << (m.recipient.empty() ? "ALL" : m.recipient) << ": "
            << m.text << endl;
    }
} 

vector<string> Database::getAllUsers() {
    vector<string> users;
    if (!db) return users;

    const char* sql = "SELECT login FROM users ORDER BY login;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* login = sqlite3_column_text(stmt, 0);
            if (login) users.emplace_back(reinterpret_cast<const char*>(login));
        }
        sqlite3_finalize(stmt);
    }
    else {
        cerr << "Ошибка подготовки запроса getAllUsers\n";
    }

    return users;
}

bool Database::requestKick(const string& login) {
    if (!db) return false;
    const char* sql = "INSERT OR REPLACE INTO kicks(login, requested_at) VALUES(?, strftime('%s','now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::consumeKick(const string& login) {
    if (!db) return false;

    // проверим наличие
    const char* sel = "SELECT 1 FROM kicks WHERE login=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sel, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);
    bool has = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (!has) return false;

    // удалим отметку — «поглотим» кик
    const char* del = "DELETE FROM kicks WHERE login=?;";
    if (sqlite3_prepare_v2(db, del, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return true;
}
