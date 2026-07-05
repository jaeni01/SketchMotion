#include "SketchView.h"
#include "MainFrame.h"
#include <algorithm>

using namespace Gdiplus;

IMPLEMENT_DYNCREATE(CSketchView, CView)

BEGIN_MESSAGE_MAP(CSketchView, CView)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MBUTTONDOWN()
    ON_WM_MBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSEWHEEL()
    ON_COMMAND_RANGE(ID_TOOL_PEN, ID_TOOL_ERASER, &CSketchView::OnToolCommand)
    ON_UPDATE_COMMAND_UI_RANGE(ID_TOOL_PEN, ID_TOOL_ERASER, &CSketchView::OnUpdateToolCommand)
    ON_COMMAND(ID_FILE_EXPORT_PNG, &CSketchView::OnFileExportPng)
END_MESSAGE_MAP()

// ------------------------------------------------------------- 좌표 변환

sm::PointF CSketchView::DeviceToWorld(CPoint pt) const {
    return { (pt.x - m_pan.x) / m_zoom, (pt.y - m_pan.y) / m_zoom };
}

CPoint CSketchView::WorldToDevice(const sm::PointF& p) const {
    return { static_cast<int>(p.x * m_zoom + m_pan.x),
             static_cast<int>(p.y * m_zoom + m_pan.y) };
}

void CSketchView::CenterCanvas() {
    CRect rc;
    GetClientRect(&rc);
    const auto& model = GetDocument()->Model();
    const float cw = model.CanvasWidth() * m_zoom;
    const float ch = model.CanvasHeight() * m_zoom;
    m_pan.x = static_cast<int>((rc.Width() - cw) * 0.5f);
    m_pan.y = static_cast<int>((rc.Height() - ch) * 0.5f);
}

void CSketchView::OnInitialUpdate() {
    CView::OnInitialUpdate();
    m_zoom = 0.75f;
    CenterCanvas();
}

// ----------------------------------------------------------------- 도구

void CSketchView::SetTool(DrawTool t) {
    m_tool = t;
    UpdateStatusBar(nullptr);
    if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
        frame->OnViewToolChanged(t);
}

void CSketchView::OnToolCommand(UINT nId) {
    SetTool(static_cast<DrawTool>(nId - ID_TOOL_PEN));
}

void CSketchView::OnUpdateToolCommand(CCmdUI* pCmdUI) {
    pCmdUI->SetRadio(static_cast<UINT>(m_tool) == pCmdUI->m_nID - ID_TOOL_PEN);
}

// ------------------------------------------------------------ 마우스 입력

void CSketchView::OnLButtonDown(UINT /*nFlags*/, CPoint point) {
    SetCapture();
    const sm::PointF w = DeviceToWorld(point);
    if (m_tool == DrawTool::Eraser) {
        m_drawing = true;
        EraseAt(w);
        return;
    }
    m_drawing = true;
    m_activePoints.clear();
    m_activePoints.push_back(w);
    if (m_tool != DrawTool::Pen)
        m_activePoints.push_back(w); // 도형: {앵커, 현재점}
}

void CSketchView::OnMouseMove(UINT /*nFlags*/, CPoint point) {
    const sm::PointF w = DeviceToWorld(point);
    UpdateStatusBar(&w);

    if (m_panning) {
        m_pan += point - m_lastMouse;
        m_lastMouse = point;
        Invalidate(FALSE);
        return;
    }
    if (!m_drawing)
        return;

    if (m_tool == DrawTool::Eraser) {
        EraseAt(w);
        return;
    }
    if (m_tool == DrawTool::Pen) {
        if (m_activePoints.empty() ||
            sm::Distance(m_activePoints.back(), w) > 0.75f / m_zoom)
            m_activePoints.push_back(w);
    } else {
        m_activePoints.back() = w;
    }
    Invalidate(FALSE);
}

void CSketchView::OnLButtonUp(UINT /*nFlags*/, CPoint /*point*/) {
    if (GetCapture() == this)
        ReleaseCapture();
    if (!m_drawing)
        return;
    m_drawing = false;
    if (m_tool != DrawTool::Eraser)
        CommitActiveStroke();
}

void CSketchView::OnMButtonDown(UINT /*nFlags*/, CPoint point) {
    m_panning = true;
    m_lastMouse = point;
    SetCapture();
}

void CSketchView::OnMButtonUp(UINT /*nFlags*/, CPoint /*point*/) {
    m_panning = false;
    if (GetCapture() == this)
        ReleaseCapture();
}

BOOL CSketchView::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint pt) {
    ScreenToClient(&pt);
    const sm::PointF anchor = DeviceToWorld(pt);
    const float factor = (zDelta > 0) ? 1.15f : 1.0f / 1.15f;
    m_zoom = std::clamp(m_zoom * factor, 0.1f, 8.0f);
    // 커서 아래 world 점이 고정되도록 팬 보정
    m_pan.x = static_cast<int>(pt.x - anchor.x * m_zoom);
    m_pan.y = static_cast<int>(pt.y - anchor.y * m_zoom);
    UpdateStatusBar(&anchor);
    Invalidate(FALSE);
    return TRUE;
}

// ----------------------------------------------------------------- 편집

void CSketchView::CommitActiveStroke() {
    if (m_activePoints.size() < 2) {
        m_activePoints.clear();
        Invalidate(FALSE);
        return;
    }
    CSketchDoc* doc = GetDocument();
    sm::StrokeKind kind = sm::StrokeKind::Freehand;
    switch (m_tool) {
    case DrawTool::Line:    kind = sm::StrokeKind::Line; break;
    case DrawTool::Rect:    kind = sm::StrokeKind::Rect; break;
    case DrawTool::Ellipse: kind = sm::StrokeKind::Ellipse; break;
    default: break;
    }
    sm::Stroke s(doc->Model().NextId(), kind, m_color, m_width);
    s.SetPoints(std::move(m_activePoints));
    m_activePoints.clear();
    doc->ExecuteCommand(std::make_unique<sm::AddStrokeCommand>(std::move(s)));
    UpdateStatusBar(nullptr);
}

void CSketchView::EraseAt(const sm::PointF& worldPt) {
    CSketchDoc* doc = GetDocument();
    const float tolerance = 4.0f / m_zoom;
    if (const auto hit = doc->Model().TopmostHit(worldPt, tolerance)) {
        if (const sm::Stroke* s = doc->Model().FindStroke(*hit)) {
            doc->ExecuteCommand(std::make_unique<sm::RemoveStrokeCommand>(*s));
            UpdateStatusBar(nullptr);
        }
    }
}

// ----------------------------------------------------------------- 렌더링

BOOL CSketchView::OnEraseBkgnd(CDC* /*pDC*/) {
    return TRUE; // 더블 버퍼링이 전부 그림
}

void CSketchView::OnSize(UINT nType, int cx, int cy) {
    CView::OnSize(nType, cx, cy);
    Invalidate(FALSE);
}

void CSketchView::OnDraw(CDC* /*pDC*/) {
    // OnPaint에서 처리
}

void CSketchView::OnPaint() {
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

void CSketchView::RenderScene(Graphics& g, const CRect& client) const {
    g.Clear(Color(255, 34, 36, 41)); // 작업영역 배경 (다크)

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.TranslateTransform(static_cast<REAL>(m_pan.x), static_cast<REAL>(m_pan.y));
    g.ScaleTransform(m_zoom, m_zoom);

    const auto& model = GetDocument()->Model();
    const REAL cw = model.CanvasWidth();
    const REAL ch = model.CanvasHeight();

    // 종이 그림자 + 종이
    SolidBrush shadow(Color(90, 0, 0, 0));
    g.FillRectangle(&shadow, 6.0f / m_zoom, 6.0f / m_zoom, cw, ch);
    SolidBrush paper(Color(255, 250, 250, 248));
    g.FillRectangle(&paper, 0.0f, 0.0f, cw, ch);
    Pen border(Color(255, 70, 74, 82), 1.0f / m_zoom);
    g.DrawRectangle(&border, 0.0f, 0.0f, cw, ch);

    // 종이 밖으로 넘치지 않게 클립
    g.SetClip(RectF(0, 0, cw, ch));
    for (const auto& s : model.Strokes())
        RenderStroke(g, s);

    // 진행 중 스트로크 (반투명 프리뷰)
    if (m_drawing && m_activePoints.size() >= 2 && m_tool != DrawTool::Eraser) {
        sm::StrokeKind kind = sm::StrokeKind::Freehand;
        switch (m_tool) {
        case DrawTool::Line:    kind = sm::StrokeKind::Line; break;
        case DrawTool::Rect:    kind = sm::StrokeKind::Rect; break;
        case DrawTool::Ellipse: kind = sm::StrokeKind::Ellipse; break;
        default: break;
        }
        sm::Stroke preview(0, kind, m_color, m_width);
        preview.SetPoints(m_activePoints);
        RenderStroke(g, preview, 150);
    }
    g.ResetClip();
    (void)client;
}

void CSketchView::RenderStroke(Graphics& g, const sm::Stroke& s, BYTE alphaOverride) {
    const sm::ColorRGBA c = s.Color();
    const BYTE alpha = alphaOverride ? alphaOverride : c.a;
    Pen pen(Color(alpha, c.r, c.g, c.b), s.Width());
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    for (const auto& line : s.ToPolylines(96)) {
        if (line.size() < 2)
            continue;
        std::vector<PointF> pts;
        pts.reserve(line.size());
        for (const auto& p : line)
            pts.emplace_back(p.x, p.y);
        g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
    }
}

// ------------------------------------------------------------- 상태 표시

void CSketchView::UpdateStatusBar(const sm::PointF* worldPos) const {
    auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
    if (!frame)
        return;
    frame->UpdateStatus(m_tool, worldPos, m_zoom, GetDocument()->Model().Count());
}

// ------------------------------------------------------------ PNG 내보내기

namespace {

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    if (GetImageEncodersSize(&num, &size) != Ok || size == 0)
        return -1;
    std::vector<BYTE> buffer(size);
    auto* pImageCodecInfo = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(num, size, pImageCodecInfo) != Ok)
        return -1;
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return static_cast<int>(j);
        }
    }
    return -1;
}

} // namespace

void CSketchView::OnFileExportPng() {
    CFileDialog dlg(FALSE, L"png", L"drawing.png",
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"PNG Image (*.png)|*.png||", this);
    if (dlg.DoModal() != IDOK)
        return;

    const auto& model = GetDocument()->Model();
    const int w = static_cast<int>(model.CanvasWidth());
    const int h = static_cast<int>(model.CanvasHeight());

    Bitmap bmp(w, h, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(Color(255, 255, 255, 255));
        for (const auto& s : model.Strokes())
            RenderStroke(g, s);
    }

    CLSID pngClsid;
    if (GetEncoderClsid(L"image/png", &pngClsid) < 0) {
        AfxMessageBox(L"PNG encoder not available.", MB_ICONERROR);
        return;
    }
    if (bmp.Save(dlg.GetPathName(), &pngClsid, nullptr) != Ok)
        AfxMessageBox(L"Failed to save PNG file.", MB_ICONERROR);
}
