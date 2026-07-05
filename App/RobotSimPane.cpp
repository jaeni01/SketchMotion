#include "RobotSimPane.h"
#include "../Core/Geometry.h"
#include <algorithm>

using namespace Gdiplus;

namespace {
constexpr int kControlsHeight = 40;
constexpr int kMargin = 6;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerIntervalMs = 30;
} // namespace

BEGIN_MESSAGE_MAP(CRobotSimPane, CDockablePane)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_BN_CLICKED(ID_ROBOT_PLAY, &CRobotSimPane::OnPlay)
    ON_BN_CLICKED(ID_ROBOT_STOP, &CRobotSimPane::OnStop)
END_MESSAGE_MAP()

int CRobotSimPane::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CDockablePane::OnCreate(lpCreateStruct) == -1)
        return -1;
    const DWORD btnStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    m_btnPlay.Create(L"Play", btnStyle, CRect(0, 0, 10, 10), this, ID_ROBOT_PLAY);
    m_btnStop.Create(L"Stop", btnStyle, CRect(0, 0, 10, 10), this, ID_ROBOT_STOP);
    m_statusText.Create(L"Draw something, then Motion > Simulate (F5).",
                        WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this);
    CFont* font = CFont::FromHandle(
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    m_btnPlay.SetFont(font);
    m_btnStop.SetFont(font);
    m_statusText.SetFont(font);
    return 0;
}

void CRobotSimPane::OnDestroy() {
    KillTimer(kTimerId);
    CDockablePane::OnDestroy();
}

CRect CRobotSimPane::CanvasRect() const {
    CRect rc;
    GetClientRect(&rc);
    rc.DeflateRect(kMargin, kControlsHeight, kMargin, kMargin);
    return rc;
}

void CRobotSimPane::OnSize(UINT nType, int cx, int cy) {
    CDockablePane::OnSize(nType, cx, cy);
    if (!m_btnPlay.GetSafeHwnd())
        return;
    const int btnW = 56, btnH = 24;
    m_btnPlay.MoveWindow(kMargin, kMargin, btnW, btnH);
    m_btnStop.MoveWindow(kMargin + btnW + 6, kMargin, btnW, btnH);
    m_statusText.MoveWindow(kMargin + 2 * (btnW + 6), kMargin + 4,
                            std::max(10, cx - (kMargin + 2 * (btnW + 6)) - kMargin), 18);
    Invalidate(FALSE);
    (void)cy;
}

// -------------------------------------------------------------- 시뮬 구성

void CRobotSimPane::StartSimulation(
        const std::vector<std::vector<sm::PointF>>& canvasPolylines) {
    StopSimulation();
    m_trail.clear();
    m_paperPaths.clear();

    if (canvasPolylines.empty()) {
        m_statusText.SetWindowTextW(L"Canvas is empty - nothing to draw.");
        Invalidate(FALSE);
        return;
    }

    // 1) 캔버스 좌표 -> 종이 좌표 매핑 (종횡비 유지, 중앙 정렬, y 반전)
    const sm::RectF bounds = sm::BoundsOf(canvasPolylines);
    if (bounds.IsEmpty())
        return;
    const float paperW = kPaperRight - kPaperLeft;
    const float paperH = kPaperTop - kPaperBottom;
    const float scale = std::min(paperW / bounds.Width(), paperH / bounds.Height()) * 0.9f;
    const float offX = kPaperLeft + (paperW - bounds.Width() * scale) * 0.5f;
    const float offY = kPaperBottom + (paperH - bounds.Height() * scale) * 0.5f;

    std::vector<std::vector<sm::PointF>> paper;
    paper.reserve(canvasPolylines.size());
    for (const auto& line : canvasPolylines) {
        std::vector<sm::PointF> mapped;
        mapped.reserve(line.size());
        for (const auto& p : line) {
            const float x = offX + (p.x - bounds.left) * scale;
            // 캔버스 y는 아래로 증가, 종이 y는 위로 증가
            const float y = offY + (bounds.bottom - p.y) * scale;
            mapped.push_back({ x, y });
        }
        paper.push_back(std::move(mapped));
    }

    // 2) 펜 이동 최소화 순서로 정렬
    const sm::PointF home{ 0.0f, kPaperBottom };
    const sm::PlannedPath planned = sm::PlanDrawingOrder(paper, home);
    m_paperPaths = planned.paths;

    // 3) 이동 세그먼트 생성
    BuildSegments(m_paperPaths);
    m_currentSegment = 0;
    m_segmentElapsed = 0.0f;
    m_playing = true;
    m_trail.emplace_back(); // 첫 잉크 폴리라인 준비

    CString status;
    status.Format(L"Simulating %zu paths (draw %.0f, travel %.0f units)...",
                  planned.stats.pathCount, planned.stats.drawDistance,
                  planned.stats.travelDistance);
    m_statusText.SetWindowTextW(status);

    SetTimer(kTimerId, kTimerIntervalMs, nullptr);
    Invalidate(FALSE);
}

void CRobotSimPane::BuildSegments(
        const std::vector<std::vector<sm::PointF>>& paperPaths) {
    m_segments.clear();
    sm::PointF pen{ 0.0f, kPaperBottom };
    for (const auto& path : paperPaths) {
        if (path.size() < 2)
            continue;
        // 펜 들고 시작점까지 이동
        const float travelLen = sm::Distance(pen, path.front());
        if (travelLen > 0.01f)
            m_segments.push_back({ pen, path.front(), false, travelLen / kTravelSpeed });
        // 펜 내리고 경로 따라 이동
        for (size_t i = 1; i < path.size(); ++i) {
            const float len = sm::Distance(path[i - 1], path[i]);
            if (len > 0.001f)
                m_segments.push_back({ path[i - 1], path[i], true, len / kDrawSpeed });
        }
        pen = path.back();
    }
}

void CRobotSimPane::StopSimulation() {
    KillTimer(kTimerId);
    m_playing = false;
}

void CRobotSimPane::OnPlay() {
    if (!m_segments.empty() && !m_playing) {
        // 처음부터 재생
        m_currentSegment = 0;
        m_segmentElapsed = 0.0f;
        m_trail.clear();
        m_trail.emplace_back();
        m_playing = true;
        SetTimer(kTimerId, kTimerIntervalMs, nullptr);
    }
}

void CRobotSimPane::OnStop() {
    StopSimulation();
    m_statusText.SetWindowTextW(L"Stopped.");
}

sm::PointF CRobotSimPane::CurrentPenPoint() const {
    if (m_segments.empty())
        return { 0.0f, kPaperBottom };
    if (m_currentSegment >= m_segments.size())
        return m_segments.back().to;
    const Segment& seg = m_segments[m_currentSegment];
    const float t = (seg.duration > 0.0f)
        ? std::clamp(m_segmentElapsed / seg.duration, 0.0f, 1.0f) : 1.0f;
    return seg.from + (seg.to - seg.from) * t;
}

void CRobotSimPane::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != kTimerId) {
        CDockablePane::OnTimer(nIDEvent);
        return;
    }
    if (!m_playing || m_currentSegment >= m_segments.size()) {
        StopSimulation();
        m_statusText.SetWindowTextW(L"Done.");
        Invalidate(FALSE);
        return;
    }

    float dt = kTimerIntervalMs / 1000.0f;
    while (dt > 0.0f && m_currentSegment < m_segments.size()) {
        Segment& seg = m_segments[m_currentSegment];
        const float remain = seg.duration - m_segmentElapsed;
        const float step = std::min(dt, remain);
        m_segmentElapsed += step;
        dt -= step;

        const sm::PointF pen = CurrentPenPoint();
        if (seg.penDown) {
            if (m_trail.empty())
                m_trail.emplace_back();
            if (m_trail.back().empty())
                m_trail.back().push_back(seg.from);
            m_trail.back().push_back(pen);
        }

        if (m_segmentElapsed >= seg.duration - 1e-6f) {
            ++m_currentSegment;
            m_segmentElapsed = 0.0f;
            // 다음 세그먼트가 travel이면 잉크 라인 분리
            if (m_currentSegment < m_segments.size() &&
                !m_segments[m_currentSegment].penDown &&
                !m_trail.back().empty())
                m_trail.emplace_back();
        }
    }

    if (m_currentSegment >= m_segments.size()) {
        StopSimulation();
        m_statusText.SetWindowTextW(L"Done.");
    } else {
        const float pct = 100.0f * m_currentSegment / m_segments.size();
        CString status;
        status.Format(L"Drawing... %.0f%%", pct);
        m_statusText.SetWindowTextW(status);
    }
    InvalidateRect(CanvasRect(), FALSE);
}

// ----------------------------------------------------------------- 렌더링

BOOL CRobotSimPane::OnEraseBkgnd(CDC* /*pDC*/) {
    return TRUE;
}

Gdiplus::PointF CRobotSimPane::ToScreen(const sm::PointF& world, const CRect& rc) const {
    // 월드: 베이스(0,0), y 위. 화면: 베이스를 아래 중앙에.
    const float reach = m_arm.l1 + m_arm.l2; // 400
    const float scale = std::min(rc.Width() / (2.2f * reach),
                                 rc.Height() / (1.15f * reach));
    const float baseX = rc.left + rc.Width() * 0.5f;
    const float baseY = rc.bottom - 24.0f;
    return { baseX + world.x * scale, baseY - world.y * scale };
}

void CRobotSimPane::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    if (client.IsRectEmpty())
        return;

    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dc, client.Width(), client.Height());
    CBitmap* oldBmp = memDC.SelectObject(&bmp);
    {
        Graphics g(memDC.GetSafeHdc());
        RenderScene(g, client);
    }
    dc.BitBlt(0, 0, client.Width(), client.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(oldBmp);
}

void CRobotSimPane::RenderScene(Graphics& g, const CRect& /*rcClient*/) const {
    CRect full;
    GetClientRect(&full);
    g.Clear(Color(255, 45, 47, 52));

    const CRect rc = CanvasRect();
    if (rc.Width() <= 20 || rc.Height() <= 20)
        return;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush bgBrush(Color(255, 28, 29, 33));
    g.FillRectangle(&bgBrush, rc.left, rc.top, rc.Width(), rc.Height());
    g.SetClip(Rect(rc.left, rc.top, rc.Width(), rc.Height()));

    // 작업 반경 (도넛 외곽)
    const PointF basePt = ToScreen({ 0, 0 }, rc);
    const PointF reachPt = ToScreen({ m_arm.l1 + m_arm.l2, 0 }, rc);
    const float reachR = reachPt.X - basePt.X;
    Pen reachPen(Color(60, 120, 160, 220), 1.0f);
    reachPen.SetDashStyle(DashStyleDash);
    g.DrawEllipse(&reachPen, basePt.X - reachR, basePt.Y - reachR,
                  reachR * 2, reachR * 2);

    // 종이 영역
    const PointF pTL = ToScreen({ kPaperLeft, kPaperTop }, rc);
    const PointF pBR = ToScreen({ kPaperRight, kPaperBottom }, rc);
    SolidBrush paperBrush(Color(255, 240, 240, 236));
    g.FillRectangle(&paperBrush, pTL.X, pTL.Y, pBR.X - pTL.X, pBR.Y - pTL.Y);

    // 목표 경로 (연한 미리보기)
    Pen previewPen(Color(50, 100, 100, 110), 1.0f);
    for (const auto& path : m_paperPaths) {
        if (path.size() < 2) continue;
        std::vector<PointF> pts;
        pts.reserve(path.size());
        for (const auto& p : path)
            pts.push_back(ToScreen(p, rc));
        g.DrawLines(&previewPen, pts.data(), static_cast<INT>(pts.size()));
    }

    // 잉크 자국
    Pen inkPen(Color(255, 25, 60, 160), 2.0f);
    inkPen.SetStartCap(LineCapRound);
    inkPen.SetEndCap(LineCapRound);
    inkPen.SetLineJoin(LineJoinRound);
    for (const auto& line : m_trail) {
        if (line.size() < 2) continue;
        std::vector<PointF> pts;
        pts.reserve(line.size());
        for (const auto& p : line)
            pts.push_back(ToScreen(p, rc));
        g.DrawLines(&inkPen, pts.data(), static_cast<INT>(pts.size()));
    }

    // 로봇팔 (IK)
    const sm::PointF pen = CurrentPenPoint();
    if (const auto q = sm::InverseKinematics(m_arm, pen, true)) {
        const sm::ArmPose pose = sm::ForwardKinematics(m_arm, *q);
        const PointF sShoulder = ToScreen(pose.shoulder, rc);
        const PointF sElbow = ToScreen(pose.elbow, rc);
        const PointF sWrist = ToScreen(pose.wrist, rc);

        Pen upperArm(Color(255, 210, 130, 40), 7.0f);
        upperArm.SetStartCap(LineCapRound);
        upperArm.SetEndCap(LineCapRound);
        Pen foreArm(Color(255, 235, 165, 60), 5.0f);
        foreArm.SetStartCap(LineCapRound);
        foreArm.SetEndCap(LineCapRound);
        g.DrawLine(&upperArm, sShoulder, sElbow);
        g.DrawLine(&foreArm, sElbow, sWrist);

        SolidBrush jointBrush(Color(255, 90, 94, 104));
        const auto drawJoint = [&](const PointF& c, float r) {
            g.FillEllipse(&jointBrush, c.X - r, c.Y - r, r * 2, r * 2);
        };
        drawJoint(sShoulder, 7.0f);
        drawJoint(sElbow, 5.0f);

        // 펜 끝
        const bool penDown = m_playing && m_currentSegment < m_segments.size() &&
                             m_segments[m_currentSegment].penDown;
        SolidBrush tipBrush(penDown ? Color(255, 220, 60, 60)
                                    : Color(255, 120, 200, 120));
        g.FillEllipse(&tipBrush, sWrist.X - 4, sWrist.Y - 4, 8.0f, 8.0f);
    }

    g.ResetClip();
}
