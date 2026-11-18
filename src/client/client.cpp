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
const uint32_t MAX_UNPACK = 1 << 20;        // лимит после DECOMPRESS (1 МБ)

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

// возвращает false при ошибке; out — результат
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

// + общий код RLE/кадров

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    int port = BASE_PORT;
    int clientId = 1;
    int nMsg = 20;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        if (!strcmp(argv[i], "--id") && i + 1 < argc) clientId = atoi(argv[++i]);
        if (!strcmp(argv[i], "--count") && i + 1 < argc) nMsg = atoi(argv[++i]);
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        printf("connect failed %d\n", WSAGetLastError());
        return 1;
    }
    Log("CLIENT", "CONNECTED", clientId);

    // --- тест 1: данные с повторами ---
    std::string rep(500, 'A');
    rep += std::string(300, 'B');
    auto t0 = GetTickCount();
    for (int i = 0; i < nMsg; ++i) {
        auto req = MakePayload(MSG_COMPRESS, i + 1, 0,
            (uint8_t*)rep.data(), (uint32_t)rep.size());
        SendFrame(s, req);
        std::vector<uint8_t> resp;
        RecvFrame(s, resp);
        uint8_t type, flags; uint32_t rid, dlen; const uint8_t* data;
        ParsePayload(resp, type, rid, flags, data, dlen);
        if (type == MSG_RESULT) {
            // проверка: decompress обратно
            std::vector<uint8_t> back;
            RleDecompress(data, dlen, back);
            bool ok = (back.size() == rep.size() &&
                memcmp(back.data(), rep.data(), rep.size()) == 0);
            Log("CLIENT", ok ? "OK_ROUNDTRIP" : "FAIL_ROUNDTRIP", i + 1);
        }
    }
    printf("Repetitive: %lu ms for %d compress\n", GetTickCount() - t0, nMsg);

    // --- тест 2: без повторов ---
    std::string rnd(800, 0);
    for (size_t i = 0; i < rnd.size(); ++i) rnd[i] = (char)(i % 251);
    t0 = GetTickCount();
    for (int i = 0; i < nMsg; ++i) {
        auto req = MakePayload(MSG_COMPRESS, 1000 + i, 0,
            (uint8_t*)rnd.data(), (uint32_t)rnd.size());
        SendFrame(s, req);
        std::vector<uint8_t> resp;
        RecvFrame(s, resp);
    }
    printf("Random-like: %lu ms for %d compress\n", GetTickCount() - t0, nMsg);

    // --- тест 3: некорректный RLE ---
    uint8_t bad[] = { 5, 'X' }; // ок, но можно послать нечётную длину
    uint8_t bad2[] = { 3 };     // нечётная
    auto req = MakePayload(MSG_DECOMPRESS, 9999, 0, bad2, 1);
    SendFrame(s, req);
    std::vector<uint8_t> resp;
    RecvFrame(s, resp);
    uint8_t type, flags; uint32_t rid, dlen; const uint8_t* data;
    ParsePayload(resp, type, rid, flags, data, dlen);
    Log("CLIENT", type == MSG_ERROR ? "GOT_ERROR_OK" : "UNEXPECTED", 9999);

    closesocket(s);
    WSACleanup();
    Log("CLIENT", "EXIT", clientId);
    return 0;
}