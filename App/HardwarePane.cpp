#include "HardwarePane.h"
#include <algorithm>
#include <memory>
#include "MainFrame.h"
#include "SketchDoc.h"
#include "../Core/Geometry.h"
#include "../Core/PathPlanner.h"
#include "../Core/PathSimplify.h"

namespace {
constexpr int kMargin = 8;
constexpr int kBtnH = 26;
constexpr int kRow = 30;

// 폴리라인 배열 -> JSON [[{"x":..,"y":..},..],..]
std::string PathsToJson(const std::vector<std::vector<sm::PointF>>& paths) {
    std::string s = "[";
    for (size_t p = 0; p < paths.size(); ++p) {
        if (p) s += ",";
        s += "[";
        for (size_t i = 0; i < paths[p].size(); ++i) {
            if (i) s += ",";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "{\"x\":%.2f,\"y\":%.2f}",
                          paths[p][i].x, paths[p][i].y);
            s += buf;
        }
        s += "]";
    }
    return s + "]";
}

std::string SinglePathToJson(const std::vector<sm::PointF>& path) {
    std::string s = "[";
    for (size_t i = 0; i < path.size(); ++i) {
        if (i) s += ",";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "{\"x\":%.3f,\"y\":%.3f}", path[i].x, path[i].y);
        s += buf;
    }
    return s + "]";
}
} // namespace

BEGIN_MESSAGE_MAP(CHardwarePane, CDockablePane)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_DESTROY()
    ON_BN_CLICKED(ID_HW_CONNECT_ARM, &CHardwarePane::OnConnectArm)
    ON_BN_CLICKED(ID_HW_CONNECT_AGV, &CHardwarePane::OnConnectAgv)
    ON_BN_CLICKED(ID_HW_DRAW_ROBOT, &CHardwarePane::OnDrawRobot)
    ON_BN_CLICKED(ID_HW_AGV_MISSION, &CHardwarePane::OnAgvMission)
    ON_BN_CLICKED(ID_HW_STOP_ALL, &CHardwarePane::OnStopAll)
    ON_BN_CLICKED(ID_HW_ESTOP, &CHardwarePane::OnEstop)
    ON_MESSAGE(WM_APP_BRIDGE, &CHardwarePane::OnBridge)
END_MESSAGE_MAP()

int CHardwarePane::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CDockablePane::OnCreate(lpCreateStruct) == -1)
        return -1;
    CFont* font = CFont::FromHandle(
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    const DWORD bs = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    const DWORD ss = WS_CHILD | WS_VISIBLE | SS_LEFT;

    m_btnConnArm.Create(L"Connect Arm", bs, CRect(), this, ID_HW_CONNECT_ARM);
    m_stArm.Create(L"Arm: disconnected", ss, CRect(), this);
    m_btnConnAgv.Create(L"Connect AGV", bs, CRect(), this, ID_HW_CONNECT_AGV);
    m_stAgv.Create(L"AGV: disconnected", ss, CRect(), this);
    m_btnDraw.Create(L"Draw on Robot", bs, CRect(), this, ID_HW_DRAW_ROBOT);
    m_btnMission.Create(L"AGV Mission", bs, CRect(), this, ID_HW_AGV_MISSION);
    m_btnStop.Create(L"Stop", bs, CRect(), this, ID_HW_STOP_ALL);
    m_btnEstop.Create(L"E-STOP", bs, CRect(), this, ID_HW_ESTOP);
    m_stEkf.Create(L"EKF: -", ss, CRect(), this);
    m_stProgress.Create(L"Idle. Connect bridges (MockBridge.exe arm / agv).", ss, CRect(), this);

    for (CWnd* w : { static_cast<CWnd*>(&m_btnConnArm), static_cast<CWnd*>(&m_btnConnAgv),
                     static_cast<CWnd*>(&m_btnDraw), static_cast<CWnd*>(&m_btnMission),
                     static_cast<CWnd*>(&m_btnStop), static_cast<CWnd*>(&m_btnEstop),
                     static_cast<CWnd*>(&m_stArm), static_cast<CWnd*>(&m_stAgv),
                     static_cast<CWnd*>(&m_stEkf), static_cast<CWnd*>(&m_stProgress) })
        w->SetFont(font);
    return 0;
}

void CHardwarePane::OnDestroy() {
    m_arm.Disconnect();
    m_agv.Disconnect();
    CDockablePane::OnDestroy();
}

void CHardwarePane::Layout(int cx) {
    if (!m_btnConnArm.GetSafeHwnd())
        return;
    const int w = cx - 2 * kMargin;
    const int half = (w - 6) / 2;
    int y = kMargin;
    m_btnConnArm.MoveWindow(kMargin, y, half, kBtnH);
    m_btnConnAgv.MoveWindow(kMargin + half + 6, y, half, kBtnH);
    y += kRow;
    m_stArm.MoveWindow(kMargin, y, w, 18); y += 20;
    m_stAgv.MoveWindow(kMargin, y, w, 18); y += 26;
    m_btnDraw.MoveWindow(kMargin, y, half, kBtnH);
    m_btnMission.MoveWindow(kMargin + half + 6, y, half, kBtnH);
    y += kRow + 4;
    m_btnStop.MoveWindow(kMargin, y, half, kBtnH);
    m_btnEstop.MoveWindow(kMargin + half + 6, y, half, kBtnH);
    y += kRow + 6;
    m_stEkf.MoveWindow(kMargin, y, w, 18); y += 20;
    m_stProgress.MoveWindow(kMargin, y, w, 40);
}

void CHardwarePane::OnSize(UINT nType, int cx, int cy) {
    CDockablePane::OnSize(nType, cx, cy);
    Layout(cx);
    (void)cy;
}

BOOL CHardwarePane::OnEraseBkgnd(CDC*) { return TRUE; }

void CHardwarePane::OnPaint() {
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);
    dc.FillSolidRect(rc, RGB(45, 47, 52));
}

// ------------------------------------------------------------- 버튼

void CHardwarePane::OnConnectArm() {
    m_arm.Connect("127.0.0.1", 9101, GetSafeHwnd(), 0);
    SetStatus(0, L"Arm: connecting...");
}

void CHardwarePane::OnConnectAgv() {
    m_agv.Connect("127.0.0.1", 9102, GetSafeHwnd(), 1);
    SetStatus(1, L"AGV: connecting...");
}

void CHardwarePane::OnDrawRobot() { SendDrawFromCanvas(); }
void CHardwarePane::OnAgvMission() { SendMissionFromCanvas(); }

void CHardwarePane::OnStopAll() {
    m_arm.Send("stop");
    m_agv.Send("stop");
    m_missionActive = false;
    m_stProgress.SetWindowTextW(L"Stopped.");
}

void CHardwarePane::OnEstop() { EmergencyStopAll(); }

void CHardwarePane::EmergencyStopAll() {
    m_arm.Send("estop");
    m_agv.Send("estop");
    m_missionActive = false;
    m_stProgress.SetWindowTextW(L"E-STOP broadcast.");
}

// ------------------------------------------------------ 캔버스 -> 명령

void CHardwarePane::SendDrawFromCanvas() {
    auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
    CSketchDoc* doc = frame ? frame->GetActiveSketchDoc() : nullptr;
    if (!doc || !m_arm.IsConnected()) {
        m_stProgress.SetWindowTextW(L"Arm not connected or no document.");
        return;
    }
    auto polys = doc->Model().AllPolylines();
    if (polys.empty()) {
        m_stProgress.SetWindowTextW(L"Canvas is empty.");
        return;
    }
    // 캔버스 px -> {paper} mm: 작업영역 160x110mm에 맞춤 (지그 마진 포함)
    const sm::RectF b = sm::BoundsOf(polys);
    const float scale = std::min(160.0f / std::max(1.0f, b.Width()),
                                 110.0f / std::max(1.0f, b.Height()));
    for (auto& path : polys)
        for (auto& p : path)
            p = { 15.0f + (p.x - b.left) * scale, 10.0f + (p.y - b.top) * scale };
    const auto planned = sm::PlanDrawingOrder(polys);
    m_arm.Send("draw_paths",
               "{\"frame\":\"paper\",\"unit\":\"mm\",\"feed\":20,\"travel\":60,\"paths\":" +
                   PathsToJson(planned.paths) + "}");
    m_stProgress.SetWindowTextW(L"Drawing sent to arm...");
}

void CHardwarePane::SendMissionFromCanvas() {
    auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
    CSketchDoc* doc = frame ? frame->GetActiveSketchDoc() : nullptr;
    if (!doc || !m_agv.IsConnected()) {
        m_stProgress.SetWindowTextW(L"AGV not connected or no document.");
        return;
    }
    auto polys = doc->Model().AllPolylines();
    if (polys.empty()) {
        m_stProgress.SetWindowTextW(L"Canvas is empty.");
        return;
    }
    // 미션 경로: 순서 최적화 후 이어붙여 단일 경로로
    const auto planned = sm::PlanDrawingOrder(polys);
    std::vector<sm::PointF> mission;
    for (const auto& path : planned.paths)
        for (const auto& p : path)
            if (mission.empty() || sm::Distance(mission.back(), p) > 1.0f)
                mission.push_back(p);
    // 캔버스 px -> {floor} m: 아레나 1.7x0.95 (마진 0.15), y 반전
    const sm::RectF b = sm::BoundsOf({ mission });
    const float scale = std::min(1.7f / std::max(1.0f, b.Width()),
                                 0.95f / std::max(1.0f, b.Height()));
    for (auto& p : mission)
        p = { 0.15f + (p.x - b.left) * scale,
              0.15f + (b.bottom - p.y) * scale };
    // ESP32 MAX_WP=64 제약: RDP로 웨이포인트 수 축소
    float eps = 0.005f;
    auto reduced = sm::SimplifyRdp(mission, eps);
    while (reduced.size() > 60 && eps < 0.2f) {
        eps *= 1.6f;
        reduced = sm::SimplifyRdp(mission, eps);
    }
    m_ekf = sm::AgvEkf(); // 미션마다 리셋
    m_poseTx = 0;
    m_poseTxFail = 0;
    m_missionActive = true;
    m_agv.Send("follow_path",
               "{\"frame\":\"floor\",\"unit\":\"m\",\"v_max\":0.25,\"path\":" +
                   SinglePathToJson(reduced) + "}");
    CString s;
    s.Format(L"Mission sent (%zu waypoints). Closed loop running...", reduced.size());
    m_stProgress.SetWindowTextW(s);
}

// ------------------------------------------------------ 브리지 이벤트

void CHardwarePane::SetStatus(int slot, const CString& s) {
    (slot == 0 ? m_stArm : m_stAgv).SetWindowTextW(s);
}

LRESULT CHardwarePane::OnBridge(WPARAM wParam, LPARAM lParam) {
    std::unique_ptr<BridgeNotify> n(reinterpret_cast<BridgeNotify*>(lParam));
    const int slot = static_cast<int>(wParam);
    if (!n)
        return 0;

    switch (n->kind) {
    case BridgeNotify::Kind::Connected:
        SetStatus(slot, slot == 0 ? L"Arm: connected" : L"AGV: connected");
        break;
    case BridgeNotify::Kind::Disconnected:
        SetStatus(slot, slot == 0 ? L"Arm: reconnecting..." : L"AGV: reconnecting...");
        if (slot == 1)
            m_missionActive = false;
        break;
    case BridgeNotify::Kind::Response: {
        // hello 응답에서 장치명 표기
        const proto::JVal* res = n->msg.Get("result");
        if (res && res->Get("device")) {
            CString s;
            s.Format(L"%s: connected (%hs)", slot == 0 ? L"Arm" : L"AGV",
                     res->StrOr("device", "?").c_str());
            SetStatus(slot, s);
        }
        const proto::JVal* okv = n->msg.Get("ok");
        if (okv && okv->type == proto::JVal::Type::Bool && !okv->b) {
            CString s;
            s.Format(L"Error: %hs", n->msg.StrOr("error", "?").c_str());
            m_stProgress.SetWindowTextW(s);
        }
        break;
    }
    case BridgeNotify::Kind::Event:
        HandleEvent(slot, n->msg);
        break;
    }
    return 0;
}

void CHardwarePane::HandleEvent(int slot, const proto::JVal& msg) {
    const std::string ev = msg.StrOr("event", "");
    const proto::JVal* d = msg.Get("data");

    if (ev == "mock_pose" && d) {
        // 가상 마커 관측 -> EKF -> set_pose_estimate (실물에선 카메라 파이프라인이 대체)
        const ULONGLONG now = GetTickCount64();
        const double dt = m_lastPoseTick ? (now - m_lastPoseTick) / 1000.0 : 0.033;
        m_lastPoseTick = now;
        m_ekf.Predict(std::min(0.2, dt));
        const auto nis = m_ekf.Update(d->NumOr("x", 0), d->NumOr("y", 0),
                                      d->NumOr("theta", 0));
        if (nis)
            m_lastNis = *nis;
        if (m_missionActive && m_ekf.Initialized()) {
            const auto& x = m_ekf.State();
            char p[256];
            std::snprintf(p, sizeof(p),
                          "{\"x\":%.4f,\"y\":%.4f,\"theta\":%.4f,\"t_cam\":%.3f}",
                          x[0], x[1], x[2], now / 1000.0);
            if (m_agv.Send("set_pose_estimate", p) >= 0)
                ++m_poseTx;
            else
                ++m_poseTxFail;
        }
        CString s;
        const auto& x = m_ekf.State();
        s.Format(L"EKF: x=%.3f y=%.3f th=%.2f  NIS=%.2f tx=%d/%d", x[0], x[1], x[2],
                 m_lastNis, m_poseTx, m_poseTxFail);
        m_stEkf.SetWindowTextW(s);
        return;
    }
    if (ev == "progress" && d) {
        CString s;
        s.Format(L"%s progress: %.0f%%", slot == 0 ? L"Draw" : L"Mission",
                 d->NumOr("pct", 0));
        m_stProgress.SetWindowTextW(s);
        return;
    }
    if (ev == "done") {
        m_stProgress.SetWindowTextW(slot == 0 ? L"Draw DONE." : L"Mission DONE.");
        if (slot == 1)
            m_missionActive = false;
        return;
    }
    if (ev == "fault" && d) {
        CString s;
        s.Format(L"FAULT(%hs): %hs", d->StrOr("reason", "?").c_str(),
                 d->StrOr("detail", "").c_str());
        m_stProgress.SetWindowTextW(s);
        if (slot == 1)
            m_missionActive = false;
    }
}
