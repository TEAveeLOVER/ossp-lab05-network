#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

#pragma comment(lib, "ws2_32.lib")

const int STUDENT_ID = 90;
const int BASE_PORT = 20000 + STUDENT_ID;  // 20090
const uint32_t MAX_MSG = 65536;
const uint32_t MAX_UNPACK = 1 << 20;        // лимит после DECOMPRESS (1 ћЅ)

enum MsgType : uint8_t {
    MSG_COMPRESS = 1,
    MSG_DECOMPRESS = 2,
    MSG_RESULT = 3,
    MSG_ERROR = 4
};

enum ErrCode : uint8_t {
    ERR_BAD_RLE = 1,
    ERR_TOO_LARGE = 2,
    ERR_UNKNOWN = 3,
    ERR_BAD_LEN = 4
};

void Log(const char* role, const char* event, int id = -1, const char* extra = "") {
    SYSTEMTIME st; GetLocalTime(&st);
    printf("[%02d:%02d:%02d.%03d] %-7s %-16s",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, role, event);
    if (id >= 0) printf(" id=%d", id);
    if (extra && extra[0]) printf(" %s", extra);
    printf("\n");
}

bool SendAll(SOCKET s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool RecvAll(SOCKET s, char* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

bool SendFrame(SOCKET s, const std::vector<uint8_t>& payload) {
    if (payload.size() > MAX_MSG) return false;
    uint32_t len = htonl((uint32_t)payload.size());
    if (!SendAll(s, (char*)&len, 4)) return false;
    if (payload.empty()) return true;
    return SendAll(s, (char*)payload.data(), (int)payload.size());
}

bool RecvFrame(SOCKET s, std::vector<uint8_t>& payload) {
    uint32_t len_n = 0;
    if (!RecvAll(s, (char*)&len_n, 4)) return false;
    uint32_t len = ntohl(len_n);
    if (len > MAX_MSG) return false;
    payload.resize(len);
    if (len == 0) return true;
    return RecvAll(s, (char*)payload.data(), (int)len);
}

// payload: ver, type, reqId(4), flags, dataLen(4), data
std::vector<uint8_t> MakePayload(uint8_t type, uint32_t reqId, uint8_t flags,
    const uint8_t* data, uint32_t dataLen) {
    std::vector<uint8_t> p;
    p.push_back(1); // version
    p.push_back(type);
    uint32_t rid = htonl(reqId);
    p.insert(p.end(), (uint8_t*)&rid, (uint8_t*)&rid + 4);
    p.push_back(flags);
    uint32_t dl = htonl(dataLen);
    p.insert(p.end(), (uint8_t*)&dl, (uint8_t*)&dl + 4);
    if (data && dataLen)
        p.insert(p.end(), data, data + dataLen);
    return p;
}

bool ParsePayload(const std::vector<uint8_t>& p,
    uint8_t& type, uint32_t& reqId, uint8_t& flags,
    const uint8_t*& data, uint32_t& dataLen) {
    if (p.size() < 11) return false;
    if (p[0] != 1) return false;
    type = p[1];
    memcpy(&reqId, &p[2], 4); reqId = ntohl(reqId);
    flags = p[6];
    memcpy(&dataLen, &p[7], 4); dataLen = ntohl(dataLen);
    if (11 + dataLen != p.size()) return false;
    data = dataLen ? &p[11] : nullptr;
    return true;
}

// ----- RLE -----
std::vector<uint8_t> RleCompress(const uint8_t* src, size_t n) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < n) {
        uint8_t b = src[i];
        size_t j = i + 1;
        while (j < n && src[j] == b && (j - i) < 255) ++j;
        out.push_back((uint8_t)(j - i));
        out.push_back(b);
        i = j;
    }
    return out;
}

// возвращает false при ошибке; out Ч результат
bool RleDecompress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    if (n % 2 != 0) return false; // пары (count, byte)
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint8_t cnt = src[i];
        uint8_t b = src[i + 1];
        if (cnt == 0) return false;
        if (out.size() + cnt > MAX_UNPACK) return false;
        for (uint8_t k = 0; k < cnt; ++k)
            out.push_back(b);
    }
    return true;
}

// + общий код выше

void HandleClient(SOCKET client, sockaddr_in peer) {
    char addr[64];
    sprintf(addr, "%s:%d", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    Log("SERVER", "CLIENT_CONNECTED", -1, addr);

    for (;;) {
        std::vector<uint8_t> frame;
        if (!RecvFrame(client, frame)) break;

        uint8_t type, flags;
        uint32_t reqId, dataLen;
        const uint8_t* data = nullptr;
        if (!ParsePayload(frame, type, reqId, flags, data, dataLen)) {
            auto err = MakePayload(MSG_ERROR, 0, ERR_BAD_LEN, nullptr, 0);
            SendFrame(client, err);
            continue;
        }

        char buf[64];
        sprintf(buf, "req=%u size=%u", reqId, dataLen);
        Log("SERVER", type == MSG_COMPRESS ? "COMPRESS" : "DECOMPRESS", (int)reqId, buf);

        std::vector<uint8_t> result;
        uint8_t err = 0;

        if (type == MSG_COMPRESS) {
            result = RleCompress(data, dataLen);
        }
        else if (type == MSG_DECOMPRESS) {
            if (!RleDecompress(data, dataLen, result))
                err = ERR_BAD_RLE; // или TOO_LARGE внутри
            if (result.size() > MAX_UNPACK)
                err = ERR_TOO_LARGE;
        }
        else {
            err = ERR_UNKNOWN;
        }

        if (err) {
            auto ep = MakePayload(MSG_ERROR, reqId, err, nullptr, 0);
            SendFrame(client, ep);
            Log("SERVER", "SEND_ERROR", (int)reqId);
        }
        else {
            auto rp = MakePayload(MSG_RESULT, reqId, 0, result.data(), (uint32_t)result.size());
            SendFrame(client, rp);
            sprintf(buf, "out=%zu", result.size());
            Log("SERVER", "SEND_RESULT", (int)reqId, buf);
        }
    }
    Log("SERVER", "CLIENT_DISCONNECTED", -1, addr);
    closesocket(client);
}

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    int port = BASE_PORT;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    int yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        printf("bind error %d\n", WSAGetLastError());
        return 1;
    }
    listen(listenSock, 8);
    Log("SERVER", "START", -1, (std::string("port=") + std::to_string(port)).c_str());

    for (;;) {
        sockaddr_in peer = {};
        int plen = sizeof(peer);
        SOCKET client = accept(listenSock, (sockaddr*)&peer, &plen);
        if (client == INVALID_SOCKET) continue;
        // упрощЄнно: один клиент за раз в этом потоке;
        // дл€ 3 клиентов Ч CreateThread на HandleClient
        HandleClient(client, peer);
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}