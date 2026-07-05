#include "BridgeClient.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace {
struct WsaInit {
    WsaInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { WSACleanup(); }
};
WsaInit g_wsa; // 프로세스 수명 동안 1회
} // namespace

void BridgeClient::Connect(const std::string& host, int port, HWND notify, int slot) {
    Disconnect();
    m_notify = notify;
    m_slot = slot;
    m_stop = false;
    m_thread = std::thread(&BridgeClient::Worker, this, host, port);
}

void BridgeClient::Disconnect() {
    m_stop = true;
    {
        std::lock_guard<std::mutex> lk(m_sendMutex);
        if (m_socket != ~0ull) {
            closesocket(static_cast<SOCKET>(m_socket));
            m_socket = ~0ull;
        }
    }
    if (m_thread.joinable())
        m_thread.join();
    m_connected = false;
}

void BridgeClient::Notify(BridgeNotify::Kind kind, proto::JVal msg) {
    if (!m_notify)
        return;
    auto* n = new BridgeNotify{ kind, std::move(msg), {} };
    if (!::PostMessage(m_notify, WM_APP_BRIDGE, static_cast<WPARAM>(m_slot),
                       reinterpret_cast<LPARAM>(n)))
        delete n;
}

long long BridgeClient::Send(const std::string& cmd, const std::string& paramsJson) {
    std::lock_guard<std::mutex> lk(m_sendMutex);
    if (m_socket == ~0ull)
        return -1;
    const long long id = m_nextId++;
    std::string line = "{\"id\":" + std::to_string(id) + ",\"cmd\":\"" + cmd + "\"";
    if (!paramsJson.empty())
        line += ",\"params\":" + paramsJson;
    line += "}\n";
    if (send(static_cast<SOCKET>(m_socket), line.c_str(),
             static_cast<int>(line.size()), 0) <= 0)
        return -1;
    return id;
}

void BridgeClient::Worker(std::string host, int port) {
    while (!m_stop.load()) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closesocket(s);
            for (int i = 0; i < 20 && !m_stop.load(); ++i)
                Sleep(100); // 2초 후 재시도
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(m_sendMutex);
            m_socket = static_cast<uintptr_t>(s);
        }
        m_connected = true;
        Notify(BridgeNotify::Kind::Connected);
        Send("hello");

        std::string acc;
        char buf[8192];
        while (!m_stop.load()) {
            const int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            acc.append(buf, n);
            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                const std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                if (line.empty())
                    continue;
                auto msg = proto::JParser::Parse(line);
                if (!msg)
                    continue;
                const bool isEvent = msg->Get("event") != nullptr;
                Notify(isEvent ? BridgeNotify::Kind::Event : BridgeNotify::Kind::Response,
                       std::move(*msg));
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_sendMutex);
            if (m_socket != ~0ull) {
                closesocket(static_cast<SOCKET>(m_socket));
                m_socket = ~0ull;
            }
        }
        m_connected = false;
        Notify(BridgeNotify::Kind::Disconnected);
        // 재접속 루프로
    }
}
