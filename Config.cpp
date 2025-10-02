//Config.cpp
#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

// аккуратная обрезка пробелов и CR/LF
static inline std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

map<string, string> loadConfig(const string& filename) {
    map<string, string> config;
    ifstream file(filename);

    if (!file.is_open()) {
        // если файла нет — создаём с настройками по умолчанию
        ofstream newFile(filename);
        newFile << "ip=127.0.0.1\n";
        newFile << "port=5000\n";
        newFile << "max_message_length=200\n";
        newFile.close();

        config["ip"] = "127.0.0.1";
        config["port"] = "5000";
        config["max_message_length"] = "200";
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        // убираем \r (на случай CRLF) и пробелы
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue; // комментарии

        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        if (!key.empty()) config[key] = value;
    }

    // подставим дефолты, если каких-то ключей нет
    if (!config.count("ip")) config["ip"] = "127.0.0.1";
    if (!config.count("port")) config["port"] = "5000";
    if (!config.count("max_message_length")) config["max_message_length"] = "200";

    return config;
}
