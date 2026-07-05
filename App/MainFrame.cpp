#include "MainFrame.h"
#include "SketchDoc.h"
#include "SketchView.h"
#include "../Core/ContourTracer.h"
#include "../Core/Filters.h"
#include "../Core/GCodeWriter.h"
#include "../Core/PathPlanner.h"
#include "../Core/PathSimplify.h"

#include <dwmapi.h>
#include <fstream>

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWndEx)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWndEx)
    ON_WM_CREATE()
    ON_COMMAND(ID_APP_ABOUT, &CMainFrame::OnAppAbout)
    ON_COMMAND_RANGE(ID_VIEW_TOOL_PANE, ID_VIEW_ROBOT_PANE, &CMainFrame::OnTogglePane)
    ON_UPDATE_COMMAND_UI_RANGE(ID_VIEW_TOOL_PANE, ID_VIEW_ROBOT_PANE,
                               &CMainFrame::OnUpdateTogglePane)
    ON_COMMAND(ID_VISION_TRACE, &CMainFrame::OnVisionTrace)
    ON_COMMAND(ID_VISION_LOAD_TEST_IMAGE, &CMainFrame::OnVisionLoadTestImage)
    ON_COMMAND(ID_MOTION_EXPORT_GCODE, &CMainFrame::OnMotionExportGCode)
    ON_COMMAND(ID_MOTION_SIMULATE, &CMainFrame::OnMotionSimulate)
END_MESSAGE_MAP()

static UINT kStatusIndicators[] = {
    ID_SEPARATOR,
    ID_INDICATOR_TOOL,
    ID_INDICATOR_POS,
    ID_INDICATOR_ZOOM,
    ID_INDICATOR_STROKES,
};

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWndEx::OnCreate(lpCreateStruct) == -1)
        return -1;

    ApplyDarkTitleBar();

    if (!m_wndStatusBar.Create(this) ||
        !m_wndStatusBar.SetIndicators(kStatusIndicators,
                                      static_cast<int>(std::size(kStatusIndicators))))
        return -1;

    EnableDocking(CBRS_ALIGN_ANY);

    const DWORD paneStyle = WS_CHILD | WS_VISIBLE | CBRS_LEFT |
                            CBRS_FLOAT_MULTI;
    if (!m_toolPane.Create(L"Tools", this, CRect(0, 0, 190, 400), TRUE,
                           ID_VIEW_TOOL_PANE,
                           paneStyle | CBRS_LEFT))
        return -1;
    if (!m_cameraPane.Create(L"Camera", this, CRect(0, 0, 360, 340), TRUE,
                             ID_VIEW_CAMERA_PANE,
                             WS_CHILD | WS_VISIBLE | CBRS_RIGHT | CBRS_FLOAT_MULTI))
        return -1;
    if (!m_robotPane.Create(L"Robot Simulator", this, CRect(0, 0, 360, 420), TRUE,
                            ID_VIEW_ROBOT_PANE,
                            WS_CHILD | WS_VISIBLE | CBRS_RIGHT | CBRS_FLOAT_MULTI))
        return -1;

    m_toolPane.EnableDocking(CBRS_ALIGN_ANY);
    m_cameraPane.EnableDocking(CBRS_ALIGN_ANY);
    m_robotPane.EnableDocking(CBRS_ALIGN_ANY);

    DockPane(&m_toolPane);
    DockPane(&m_cameraPane);
    // 로봇 패널은 카메라 패널 아래에 도킹
    m_robotPane.DockToWindow(&m_cameraPane, CBRS_ALIGN_BOTTOM);

    return 0;
}

void CMainFrame::ApplyDarkTitleBar() {
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(GetSafeHwnd(), DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &dark, sizeof(dark));
}

// ------------------------------------------------------------ 뷰/문서 접근

CSketchView* CMainFrame::GetActiveSketchView() const {
    return dynamic_cast<CSketchView*>(GetActiveView());
}

CSketchDoc* CMainFrame::GetActiveSketchDoc() const {
    return dynamic_cast<CSketchDoc*>(GetActiveDocument());
}

// ---------------------------------------------------------------- 상태바

void CMainFrame::UpdateStatus(DrawTool tool, const sm::PointF* worldPos,
                              float zoom, size_t strokeCount) {
    if (!m_wndStatusBar.GetSafeHwnd())
        return;
    static const wchar_t* kToolNames[] = { L"Pen", L"Line", L"Rectangle",
                                           L"Ellipse", L"Eraser" };
    CString s;
    s.Format(L"Tool: %s", kToolNames[static_cast<int>(tool)]);
    m_wndStatusBar.SetPaneText(1, s);
    if (worldPos) {
        s.Format(L"X: %.0f  Y: %.0f", worldPos->x, worldPos->y);
        m_wndStatusBar.SetPaneText(2, s);
    }
    s.Format(L"Zoom: %.0f%%", zoom * 100.0f);
    m_wndStatusBar.SetPaneText(3, s);
    s.Format(L"Strokes: %zu", strokeCount);
    m_wndStatusBar.SetPaneText(4, s);
}

void CMainFrame::OnViewToolChanged(DrawTool tool) {
    m_toolPane.SetActiveTool(static_cast<int>(tool));
}

// ------------------------------------------------------------- 패널 토글

CDockablePane* CMainFrame::PaneFromId(UINT nId) {
    switch (nId) {
    case ID_VIEW_TOOL_PANE:   return &m_toolPane;
    case ID_VIEW_CAMERA_PANE: return &m_cameraPane;
    case ID_VIEW_ROBOT_PANE:  return &m_robotPane;
    }
    return nullptr;
}

void CMainFrame::OnTogglePane(UINT nId) {
    if (CDockablePane* pane = PaneFromId(nId))
        pane->ShowPane(!pane->IsVisible(), FALSE, TRUE);
}

void CMainFrame::OnUpdateTogglePane(CCmdUI* pCmdUI) {
    if (CDockablePane* pane = PaneFromId(pCmdUI->m_nID))
        pCmdUI->SetCheck(pane->IsVisible());
}

// ----------------------------------------------------------------- Vision

void CMainFrame::OnVisionLoadTestImage() {
    m_cameraPane.ShowPane(TRUE, FALSE, TRUE);
    m_cameraPane.LoadTestImage();
}

void CMainFrame::OnVisionTrace() {
    CSketchDoc* doc = GetActiveSketchDoc();
    if (!doc)
        return;

    // 카메라 프레임이 없으면 테스트 이미지로 자동 대체
    if (!m_cameraPane.HasFrame())
        OnVisionLoadTestImage();

    const sm::BgraImage& frame = m_cameraPane.RawFrame();
    if (frame.Empty())
        return;

    CWaitCursor wait;

    // 파이프라인: gray -> blur -> sobel -> threshold -> contours -> RDP
    const sm::GrayImage gray = sm::ToGrayscale(frame);
    const sm::GrayImage blurred = sm::GaussianBlur5(gray);
    const sm::GrayImage edges = sm::SobelMagnitude(blurred);
    const uint8_t otsu = sm::OtsuThreshold(edges);
    const sm::GrayImage binary = sm::Threshold(edges, std::max<uint8_t>(40, otsu));

    sm::ContourOptions copt;
    copt.minPoints = 16;
    const auto contours = sm::TraceContours(binary, copt);
    if (contours.empty()) {
        AfxMessageBox(L"No contours found in the camera frame.", MB_ICONINFORMATION);
        return;
    }
    const auto simplified = sm::SimplifyAll(contours, 1.4f);

    // 캔버스에 맞게 스케일 (85% 채움, 중앙 정렬)
    auto& model = doc->Model();
    const sm::RectF b = sm::BoundsOf(simplified);
    if (b.IsEmpty())
        return;
    const float scale = std::min(model.CanvasWidth() / b.Width(),
                                 model.CanvasHeight() / b.Height()) * 0.85f;
    const float offX = (model.CanvasWidth() - b.Width() * scale) * 0.5f;
    const float offY = (model.CanvasHeight() - b.Height() * scale) * 0.5f;

    std::vector<sm::Stroke> strokes;
    strokes.reserve(simplified.size());
    for (const auto& contour : simplified) {
        sm::Stroke s(model.NextId(), sm::StrokeKind::Freehand,
                     { 40, 45, 60, 255 }, 2.0f);
        std::vector<sm::PointF> pts;
        pts.reserve(contour.size() + 1);
        for (const auto& p : contour)
            pts.push_back({ offX + (p.x - b.left) * scale,
                            offY + (p.y - b.top) * scale });
        if (pts.size() >= 2)
            pts.push_back(pts.front()); // 윤곽 폐곡선 닫기
        s.SetPoints(std::move(pts));
        strokes.push_back(std::move(s));
    }

    doc->ExecuteCommand(std::make_unique<sm::AddStrokesCommand>(std::move(strokes)));

    CString msg;
    msg.Format(L"Traced %zu contours onto the canvas.", simplified.size());
    m_wndStatusBar.SetPaneText(0, msg);
}

// ----------------------------------------------------------------- Motion

void CMainFrame::OnMotionExportGCode() {
    CSketchDoc* doc = GetActiveSketchDoc();
    if (!doc)
        return;
    const auto polylines = doc->Model().AllPolylines();
    if (polylines.empty()) {
        AfxMessageBox(L"Canvas is empty - draw something first.", MB_ICONINFORMATION);
        return;
    }

    CFileDialog dlg(FALSE, L"gcode", L"drawing.gcode",
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"G-code (*.gcode)|*.gcode|All Files (*.*)|*.*||", this);
    if (dlg.DoModal() != IDOK)
        return;

    CWaitCursor wait;
    const float naiveTravel = sm::TravelDistanceOf(polylines);
    const sm::PlannedPath planned = sm::PlanDrawingOrder(polylines);
    const sm::RectF bounds = sm::BoundsOf(planned.paths);
    const std::string gcode = sm::WriteGCode(planned.paths, bounds);

    std::ofstream out(dlg.GetPathName(), std::ios::binary | std::ios::trunc);
    if (!out) {
        AfxMessageBox(L"Failed to write G-code file.", MB_ICONERROR);
        return;
    }
    out << gcode;
    out.close();

    CString msg;
    msg.Format(L"G-code exported.\n\nPaths: %zu\nDraw distance: %.0f px\n"
               L"Pen-up travel: %.0f px (unoptimized: %.0f px, saved %.0f%%)",
               planned.stats.pathCount, planned.stats.drawDistance,
               planned.stats.travelDistance, naiveTravel,
               naiveTravel > 0.0f
                   ? (1.0f - planned.stats.travelDistance / naiveTravel) * 100.0f
                   : 0.0f);
    AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CMainFrame::OnMotionSimulate() {
    CSketchDoc* doc = GetActiveSketchDoc();
    if (!doc)
        return;
    m_robotPane.ShowPane(TRUE, FALSE, TRUE);
    m_robotPane.StartSimulation(doc->Model().AllPolylines());
}

// ------------------------------------------------------------------ 기타

void CMainFrame::OnAppAbout() {
    AfxMessageBox(
        L"SketchMotion 1.0\n\n"
        L"Vision-to-Motion drawing suite built with MFC / C++20.\n"
        L"Draw - Trace from camera - Simulate on a 2-link robot arm - Export G-code.\n\n"
        L"All image processing and kinematics implemented from scratch (no OpenCV).",
        MB_ICONINFORMATION);
}
