// MockBridge.cpp - 실물과 동일한 프로토콜을 말하는 가짜 브리지 (콘솔)
//
// 용도: 하드웨어 없는 개발/CI. 사용법:
//   MockBridge.exe arm   (포트 9101, myCobot 흉내)
//   MockBridge.exe agv   (포트 9102, 메카넘 AGV 흉내 - 내부 동역학 시뮬 + mock_pose 발행)
//
// AGV 모드는 실물 ESP32 펌웨어와 같은 구조(수신 태스크 + 50Hz 제어 태스크)로 돌며,
// PathTracker(Core)를 온보드 실행한다. 제어에는 PC가 준 추정 자세를 쓰고(실물과 동일),
// 참(truth) 자세는 슬립을 섞어 별도로 적분한다 - 폐루프가 진짜로 검증되는 구조.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../Core/PathTracker.h"
#include "../../protocol/ProtoJson.h"

using namespace std::chrono;

namespace {

std::mutex g_sendMutex;
SOCKET g_client = INVALID_SOCKET;

void SendLine(const std::string& json) {
    std::lock_guard<std::mutex> lk(g_sendMutex);
    if (g_client == INVALID_SOCKET)
        return;
    std::string line = json + "\n";
    send(g_client, line.c_str(), static_cast<int>(line.size()), 0);
}

void SendOk(long long id, const std::string& resultJson = "{}") {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "{\"id\":%lld,\"ok\":true,\"result\":%s}", id, resultJson.c_str());
    SendLine(buf);
}
void SendErr(long long id, const std::string& msg) {
    char buf[320];
    std::snprintf(buf, sizeof(buf), "{\"id\":%lld,\"ok\":false,\"error\":\"%s\"}", id, proto::Esc(msg).c_str());
    SendLine(buf);
}
void SendEvent(const std::string& name, const std::string& dataJson) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "{\"event\":\"%s\",\"data\":%s}", name.c_str(), dataJson.c_str());
    SendLine(buf);
}

// 결정론 노이즈
struct Rng {
    uint64_t s = 0x9e3779b97f4a7c15ull;
    double Uniform() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s % 1000000007ull) / 1000000007.0; }
    double Gauss(double sigma) {
        const double u1 = std::max(1e-12, Uniform()), u2 = Uniform();
        return sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307 * u2);
    }
};

// ---------------------------------------------------------------- Arm mock
struct ArmState {
    std::mutex m;
    std::atomic<bool> drawing{ false };
    std::atomic<bool> abort{ false };
    double totalLen = 0, feed = 20;
    int pathCount = 0;
};
ArmState g_arm;

void ArmDrawThread() {
    // 실제 소요시간 근사하되 테스트 친화적으로 25초 상한
    const double durationSec = std::min(25.0,
        g_arm.totalLen / std::max(1.0, g_arm.feed) + g_arm.pathCount * 0.5);
    const auto t0 = steady_clock::now();
    while (!g_arm.abort.load()) {
        const double el = duration<double>(steady_clock::now() - t0).count();
        const double pct = std::min(100.0, el / durationSec * 100.0);
        char d[128];
        std::snprintf(d, sizeof(d), "{\"phase\":\"draw\",\"seg\":%d,\"of\":%d,\"pct\":%.1f}",
                      static_cast<int>(pct / 100.0 * g_arm.pathCount), g_arm.pathCount, pct);
        SendEvent("progress", d);
        if (pct >= 100.0)
            break;
        std::this_thread::sleep_for(milliseconds(300));
    }
    if (!g_arm.abort.load())
        SendEvent("done", "{\"phase\":\"draw\"}");
    g_arm.drawing = false;
}

void HandleArm(const proto::JVal& msg) {
    const long long id = static_cast<long long>(msg.NumOr("id", -1));
    const std::string cmd = msg.StrOr("cmd", "");
    if (cmd == "hello") { SendOk(id, "{\"device\":\"mock_arm\",\"proto\":1,\"version\":\"mock-1.0\"}"); return; }
    if (cmd == "home") { SendOk(id); return; }
    if (cmd == "stop" || cmd == "estop") { g_arm.abort = true; SendOk(id); return; }
    if (cmd == "telemetry") { SendOk(id, "{\"joints\":[0,0,0,0,0,0],\"moving\":false,\"queued\":0}"); return; }
    if (cmd == "draw_paths") {
        if (g_arm.drawing.load()) { SendErr(id, "busy"); return; }
        const proto::JVal* params = msg.Get("params");
        const proto::JVal* paths = params ? params->Get("paths") : nullptr;
        if (!paths || !paths->IsArr() || paths->arr.empty()) { SendErr(id, "paths missing"); return; }
        double total = 0;
        for (const auto& path : paths->arr) {
            for (size_t i = 1; i < path.arr.size(); ++i) {
                const double dx = path.arr[i].NumOr("x", 0) - path.arr[i - 1].NumOr("x", 0);
                const double dy = path.arr[i].NumOr("y", 0) - path.arr[i - 1].NumOr("y", 0);
                total += std::hypot(dx, dy);
            }
        }
        g_arm.totalLen = total;
        g_arm.feed = params->NumOr("feed", 20.0);
        g_arm.pathCount = static_cast<int>(paths->arr.size());
        g_arm.abort = false;
        g_arm.drawing = true;
        SendOk(id, "{\"accepted\":true}");
        std::thread(ArmDrawThread).detach();
        return;
    }
    SendErr(id, "unknown cmd: " + cmd);
}

// ---------------------------------------------------------------- AGV mock
struct AgvState {
    std::mutex m;
    sm::PathTracker tracker;
    bool follow = false;
    steady_clock::time_point followStart{};
    // 참 자세 (시뮬 내부)
    double tx = 0.35, ty = 0.30, tth = 0.0;
    // PC가 준 추정 자세
    double ex = 0, ey = 0, eth = 0;
    steady_clock::time_point lastPose = steady_clock::time_point::min();
    bool everPose = false;
};
AgvState g_agv;
std::atomic<bool> g_quit{ false };
std::atomic<int> g_poseEmitted{ 0 }; // 진단: mock_pose 방출 수

void AgvSimThread() {
    Rng rng;
    auto lastTick = steady_clock::now();
    auto lastMockPose = lastTick;
    auto lastProgress = lastTick;
    while (!g_quit.load()) {
        std::this_thread::sleep_for(milliseconds(20)); // 50Hz
        const auto now = steady_clock::now();
        const double dt = duration<double>(now - lastTick).count();
        lastTick = now;

        std::unique_lock<std::mutex> lk(g_agv.m);
        if (g_agv.follow) {
            // 워치독: 추정 자세 500ms 결측 -> 정지.
            // 시작 직후에는 첫 pose 도착까지 500ms 유예 (즉시 fault 방지 - E2E에서 발견된 버그)
            const bool stale = g_agv.everPose
                ? duration<double>(now - g_agv.lastPose).count() > 0.5
                : duration<double>(now - g_agv.followStart).count() > 0.5;
            if (stale) {
                std::printf("[agv] WATCHDOG: everPose=%d sinceStart=%.0fms sincePose=%.0fms emitted=%d\n",
                            g_agv.everPose ? 1 : 0,
                            duration<double>(now - g_agv.followStart).count() * 1000,
                            g_agv.everPose ? duration<double>(now - g_agv.lastPose).count() * 1000 : -1.0,
                            g_poseEmitted.load());
                g_agv.follow = false;
                lk.unlock();
                SendEvent("fault", "{\"reason\":\"watchdog\",\"detail\":\"pose stream stale\"}");
                continue;
            }
            // 첫 pose 수신 전에는 대기 (grace 구간)
            if (!g_agv.everPose)
                continue;
            // 실물처럼 '추정' 자세로 제어
            const auto cmd = g_agv.tracker.Step(g_agv.ex, g_agv.ey, g_agv.eth);
            if (cmd.done) {
                g_agv.follow = false;
                lk.unlock();
                SendEvent("done", "{\"phase\":\"path\"}");
                continue;
            }
            // 참 자세 적분 (슬립 3% + 미세 외란)
            const double slip = 0.97;
            const double c = std::cos(g_agv.tth), s = std::sin(g_agv.tth);
            g_agv.tx += (cmd.vx * c - cmd.vy * s) * dt * slip + rng.Gauss(0.0005);
            g_agv.ty += (cmd.vx * s + cmd.vy * c) * dt * slip + rng.Gauss(0.0005);
            g_agv.tth += cmd.omega * dt * slip + rng.Gauss(0.001);
        }
        const double px = g_agv.tx, py = g_agv.ty, pth = g_agv.tth;
        const bool following = g_agv.follow;
        const double prog = g_agv.tracker.Progress();
        lk.unlock();

        // 가상 마커 관측 (30Hz, 노이즈 = 실측 마커 수준)
        if (duration<double>(now - lastMockPose).count() > 0.033) {
            lastMockPose = now;
            char d[160];
            std::snprintf(d, sizeof(d), "{\"x\":%.4f,\"y\":%.4f,\"theta\":%.4f}",
                          px + rng.Gauss(0.002), py + rng.Gauss(0.002), pth + rng.Gauss(0.017));
            SendEvent("mock_pose", d);
            ++g_poseEmitted;
        }
        if (following && duration<double>(now - lastProgress).count() > 1.0) {
            lastProgress = now;
            char d[96];
            std::snprintf(d, sizeof(d), "{\"phase\":\"path\",\"seg\":0,\"of\":0,\"pct\":%.1f}", prog * 100.0);
            SendEvent("progress", d);
        }
    }
}

void HandleAgv(const proto::JVal& msg) {
    const long long id = static_cast<long long>(msg.NumOr("id", -1));
    const std::string cmd = msg.StrOr("cmd", "");
    static int poseLog = 0;
    if (cmd != "set_pose_estimate" || poseLog++ < 3)
        std::printf("[agv] recv cmd=%s id=%lld\n", cmd.c_str(), id);
    if (cmd == "hello") { SendOk(id, "{\"device\":\"mock_agv\",\"proto\":1,\"version\":\"mock-1.0\"}"); return; }
    if (cmd == "stop" || cmd == "estop") {
        std::lock_guard<std::mutex> lk(g_agv.m);
        g_agv.follow = false;
        SendOk(id);
        return;
    }
    if (cmd == "telemetry") {
        std::lock_guard<std::mutex> lk(g_agv.m);
        char r[160];
        std::snprintf(r, sizeof(r), "{\"pose_age_ms\":%d,\"wheel\":[0,0,0,0],\"state\":\"%s\"}",
                      g_agv.everPose ? static_cast<int>(duration<double>(steady_clock::now() - g_agv.lastPose).count() * 1000) : -1,
                      g_agv.follow ? "follow" : "idle");
        SendOk(id, r);
        return;
    }
    if (cmd == "set_pose_estimate") {
        const proto::JVal* p = msg.Get("params");
        if (!p) { SendErr(id, "params missing"); return; }
        std::lock_guard<std::mutex> lk(g_agv.m);
        if (!g_agv.everPose)
            std::printf("[agv] first pose estimate received (%.0f ms after follow)\n",
                        duration<double>(steady_clock::now() - g_agv.followStart).count() * 1000);
        g_agv.ex = p->NumOr("x", 0);
        g_agv.ey = p->NumOr("y", 0);
        g_agv.eth = p->NumOr("theta", 0);
        g_agv.lastPose = steady_clock::now();
        g_agv.everPose = true;
        SendOk(id);
        return;
    }
    if (cmd == "follow_path") {
        const proto::JVal* p = msg.Get("params");
        const proto::JVal* path = p ? p->Get("path") : nullptr;
        if (!path || !path->IsArr() || path->arr.size() < 2) { SendErr(id, "path missing"); return; }
        std::vector<sm::PointF> pts;
        for (const auto& n : path->arr)
            pts.push_back({ static_cast<float>(n.NumOr("x", 0)), static_cast<float>(n.NumOr("y", 0)) });
        std::lock_guard<std::mutex> lk(g_agv.m);
        sm::TrackerParams tp;
        tp.vMax = p->NumOr("v_max", 0.30);
        g_agv.tracker = sm::PathTracker(tp);
        g_agv.tracker.SetPath(std::move(pts));
        g_agv.follow = true;
        g_agv.followStart = steady_clock::now();
        g_agv.everPose = false; // 새 미션은 신선한 pose 스트림 필요
        SendOk(id, "{\"accepted\":true}");
        return;
    }
    SendErr(id, "unknown cmd: " + cmd);
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // 리다이렉트 시에도 즉시 flush (디버그 로그 보존)
    const std::string mode = argc > 1 ? argv[1] : "arm";
    const bool isAgv = mode == "agv";
    const int port = argc > 2 ? std::atoi(argv[2]) : (isAgv ? 9102 : 9101);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("WSAStartup failed\n");
        return 1;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    const BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(listener, 1) != 0) {
        std::printf("bind/listen failed on port %d\n", port);
        return 1;
    }
    std::printf("MockBridge [%s] listening on 127.0.0.1:%d\n", mode.c_str(), port);

    std::thread sim;
    if (isAgv)
        sim = std::thread(AgvSimThread);

    for (;;) {
        SOCKET c = accept(listener, nullptr, nullptr);
        if (c == INVALID_SOCKET)
            break;
        {
            std::lock_guard<std::mutex> lk(g_sendMutex);
            if (g_client != INVALID_SOCKET)
                closesocket(g_client);
            g_client = c;
        }
        std::printf("client connected\n");
        std::string acc;
        char buf[4096];
        for (;;) {
            const int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            acc.append(buf, n);
            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                const std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                if (line.empty())
                    continue;
                const auto msg = proto::JParser::Parse(line);
                if (!msg)
                    continue;
                if (isAgv)
                    HandleAgv(*msg);
                else
                    HandleArm(*msg);
            }
        }
        std::printf("client disconnected\n");
        // 연결 워치독: 끊기면 즉시 정지
        g_arm.abort = true;
        {
            std::lock_guard<std::mutex> lk(g_agv.m);
            g_agv.follow = false;
        }
        std::lock_guard<std::mutex> lk(g_sendMutex);
        if (g_client == c)
            g_client = INVALID_SOCKET;
        closesocket(c);
    }
    g_quit = true;
    if (sim.joinable())
        sim.join();
    WSACleanup();
    return 0;
}
