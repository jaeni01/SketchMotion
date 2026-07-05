#pragma once
// BridgeClient.h - Bridge Protocol v1 TCP 클라이언트 (워커 스레드 + PostMessage 마샬링)
// v1 CameraCapture와 동일 패턴: 워커는 MFC 객체를 만지지 않고, 힙 메시지 소유권을 UI로 이전.
#include "framework.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "../protocol/ProtoJson.h"

// LPARAM = BridgeNotify* (수신측이 delete). WPARAM = BridgeClient 식별자(디바이스 슬롯).
constexpr UINT WM_APP_BRIDGE = WM_APP + 10;

struct BridgeNotify {
    enum class Kind { Connected, Disconnected, Response, Event };
    Kind kind{};
    proto::JVal msg;        // Response/Event 원문
    std::string device;     // hello 이후 채워짐
};

class BridgeClient {
public:
    BridgeClient() = default;
    ~BridgeClient() { Disconnect(); }
    BridgeClient(const BridgeClient&) = delete;
    BridgeClient& operator=(const BridgeClient&) = delete;

    // slot: WPARAM으로 전달되는 식별자 (0=Arm, 1=AGV)
    void Connect(const std::string& host, int port, HWND notify, int slot);
    void Disconnect();
    bool IsConnected() const { return m_connected.load(); }

    // JSON 명령 전송. paramsJson은 "{...}" 또는 빈 문자열. 반환: 요청 id (-1이면 미연결)
    long long Send(const std::string& cmd, const std::string& paramsJson = "");

private:
    void Worker(std::string host, int port);
    void Notify(BridgeNotify::Kind kind, proto::JVal msg = {});

    std::thread m_thread;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_connected{ false };
    std::atomic<long long> m_nextId{ 1 };
    std::mutex m_sendMutex;
    uintptr_t m_socket = ~0ull; // SOCKET (헤더 오염 방지용 uintptr)
    HWND m_notify = nullptr;
    int m_slot = 0;
};
