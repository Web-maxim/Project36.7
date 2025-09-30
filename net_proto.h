// net_proto.h
#pragma once
#ifndef _HAS_STD_BYTE
#define _HAS_STD_BYTE 0
#endif
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
typedef int SOCKET;
#endif
using namespace std;

inline bool sendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int rc = ::send(s, data + sent, (int)(len - sent), 0);
        if (rc <= 0) return false;
        sent += (size_t)rc;
    }
    return true;
}

inline bool recvExact(SOCKET s, char* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int rc = ::recv(s, buf + got, (int)(len - got), 0);
        if (rc <= 0) return false;
        got += (size_t)rc;
    }
    return true;
}

// Отправить одно сообщение (UTF-8 строка)
inline bool sendMessage(SOCKET s, const string& payloadUtf8) {
    uint32_t n = (uint32_t)payloadUtf8.size();
    uint32_t be = htonl(n);
    char header[4];
    memcpy(header, &be, 4);
    if (!sendAll(s, header, 4)) return false;
    if (n == 0) return true;
    return sendAll(s, payloadUtf8.data(), payloadUtf8.size());
}

// Прочитать РОВНО одно сообщение (возвращает false при разрыве)
inline bool recvMessage(SOCKET s, string& outUtf8) {
    char header[4];
    if (!recvExact(s, header, 4)) return false;
    uint32_t be;
    memcpy(&be, header, 4);
    uint32_t n = ntohl(be);
    if (n > (1024u * 1024u * 32u)) { // защита от мусора (32 МБ)
        return false;
    }
    outUtf8.resize(n);
    if (n == 0) return true;
    return recvExact(s, outUtf8.data(), n);
}
