// emg_e2e.cpp - EMG 파이프라인 가상 통합 (하드웨어 없이)
// MockBridge(emg) 스트림을 TCP로 받아 Core::EmgProcessor에 통과시켜,
// 근수축 구간에서 제스처(contracted)와 비례출력이 올라오는지 검증한다.
// App(BridgeClient/HardwarePane)가 실제로 하는 흐름의 콘솔 축소판.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "../Core/EmgProcessor.h"
#include "../protocol/ProtoJson.h"

using namespace std::chrono;

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9103);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // 접속 재시도 (MockBridge 기동 대기)
    bool connected = false;
    for (int i = 0; i < 30; ++i) {
        if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) { connected = true; break; }
        std::this_thread::sleep_for(milliseconds(100));
    }
    if (!connected) { std::printf("EMG-E2E FAIL: cannot connect 9103\n"); return 1; }

    auto send = [&](const std::string& line) {
        std::string l = line + "\n";
        ::send(s, l.c_str(), static_cast<int>(l.size()), 0);
    };
    send("{\"id\":1,\"cmd\":\"hello\"}");
    send("{\"id\":2,\"cmd\":\"start_stream\",\"params\":{\"rate\":1000}}");

    sm::EmgProcessor emg;
    double mvcMax = 0;
    bool sawContract = false;
    double relaxProp = 1.0, contractProp = 0.0;
    int emgCount = 0;
    std::string device;

    // 8초 수신 (MockBridge 시나리오: 2s 안정 → 2s 수축 → ... 6s 주기)
    const auto t0 = steady_clock::now();
    std::string acc;
    char buf[8192];
    bool mvcSet = false;
    while (duration<double>(steady_clock::now() - t0).count() < 9.0) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        acc.append(buf, n);
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            const std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            if (line.empty()) continue;
            const auto msg = proto::JParser::Parse(line);
            if (!msg) continue;
            if (const proto::JVal* res = msg->Get("result"))
                if (res->Get("device")) device = res->StrOr("device", "");
            const std::string ev = msg->StrOr("event", "");
            if (ev != "emg") continue;
            const proto::JVal* d = msg->Get("data");
            if (!d) continue;
            const double raw = d->NumOr("raw", 2048.0);
            // ADC 0..4095 -> 중심 제거 정규화 (실물 EmgProcessor 입력 규약과 동일 스케일)
            const auto o = emg.Push((raw - 2048.0) / 900.0);
            ++emgCount;
            const double el = duration<double>(steady_clock::now() - t0).count();
            // 첫 2.5초로 MVC 근사 후 등록
            if (el < 2.5) mvcMax = std::max(mvcMax, o.envelope);
            else if (!mvcSet) { emg.SetMvc(std::max(1e-3, mvcMax * 2.5)); mvcSet = true; }
            if (mvcSet) {
                if (el > 2.6 && el < 3.4) relaxProp = o.proportional; // 안정 구간 꼬리
                if (o.contracted) { sawContract = true; contractProp = std::max(contractProp, o.proportional); }
            }
        }
    }
    send("{\"id\":3,\"cmd\":\"stop_stream\"}");
    closesocket(s);
    WSACleanup();

    std::printf("EMG-E2E: device=%s samples=%d relaxProp=%.2f contractProp=%.2f contracted=%d\n",
                device.c_str(), emgCount, relaxProp, contractProp, sawContract ? 1 : 0);

    int fail = 0;
    if (device != "mock_emg") { std::printf("  FAIL hello device\n"); fail = 1; }
    if (emgCount < 3000) { std::printf("  FAIL sample count (%d)\n", emgCount); fail = 1; }
    if (!sawContract) { std::printf("  FAIL no contraction gesture detected\n"); fail = 1; }
    if (contractProp <= relaxProp) { std::printf("  FAIL proportional not higher in contraction\n"); fail = 1; }

    std::printf(fail ? "EMG-E2E: FAIL\n" : "EMG-E2E: PASS\n");
    return fail;
}
