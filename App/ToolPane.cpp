#include "ToolPane.h"
#include "MainFrame.h"
#include "SketchView.h"

namespace {
constexpr int kMargin = 8;
constexpr int kButtonHeight = 28;
const wchar_t* kToolNames[5] = { L"Pen", L"Line", L"Rectangle", L"Ellipse", L"Eraser" };
const float kWidths[] = { 1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f };
} // namespace

BEGIN_MESSAGE_MAP(CToolPane, CDockablePane)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_COMMAND_RANGE(ID_TOOLPANE_FIRST_TOOL, ID_TOOLPANE_FIRST_TOOL + 4,
                     &CToolPane::OnToolButton)
    ON_BN_CLICKED(ID_TOOLPANE_COLOR, &CToolPane::OnColorChanged)
    ON_CBN_SELCHANGE(ID_TOOLPANE_WIDTH_COMBO, &CToolPane::OnWidthChanged)
END_MESSAGE_MAP()

int CToolPane::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CDockablePane::OnCreate(lpCreateStruct) == -1)
        return -1;

    CFont* font = CFont::FromHandle(
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));

    m_labelTools.Create(L"TOOLS", WS_CHILD | WS_VISIBLE | SS_LEFT,
                        CRect(0, 0, 10, 10), this);
    m_labelTools.SetFont(font);

    for (int i = 0; i < 5; ++i) {
        m_toolButtons[i].Create(kToolNames[i],
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                CRect(0, 0, 10, 10), this,
                                ID_TOOLPANE_FIRST_TOOL + i);
        m_toolButtons[i].SetFont(font);
        m_toolButtons[i].m_bTransparent = FALSE;
    }

    m_labelStyle.Create(L"STROKE STYLE", WS_CHILD | WS_VISIBLE | SS_LEFT,
                        CRect(0, 0, 10, 10), this);
    m_labelStyle.SetFont(font);

    m_colorButton.Create(L"Color", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         CRect(0, 0, 10, 10), this, ID_TOOLPANE_COLOR);
    m_colorButton.SetFont(font);
    m_colorButton.EnableOtherButton(L"More Colors...");
    m_colorButton.SetColor(RGB(30, 30, 30));

    m_widthCombo.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                        CRect(0, 0, 10, 200), this, ID_TOOLPANE_WIDTH_COMBO);
    m_widthCombo.SetFont(font);
    for (const float w : kWidths) {
        CString s;
        s.Format(L"%.0f px", w);
        m_widthCombo.AddString(s);
    }
    m_widthCombo.SetCurSel(2); // 3 px

    SetActiveTool(0);
    return 0;
}

void CToolPane::OnSize(UINT nType, int cx, int cy) {
    CDockablePane::OnSize(nType, cx, cy);
    if (!m_labelTools.GetSafeHwnd())
        return;
    int y = kMargin;
    const int w = cx - 2 * kMargin;
    m_labelTools.MoveWindow(kMargin, y, w, 16);
    y += 20;
    for (auto& btn : m_toolButtons) {
        btn.MoveWindow(kMargin, y, w, kButtonHeight);
        y += kButtonHeight + 4;
    }
    y += 12;
    m_labelStyle.MoveWindow(kMargin, y, w, 16);
    y += 20;
    m_colorButton.MoveWindow(kMargin, y, w, kButtonHeight);
    y += kButtonHeight + 4;
    m_widthCombo.MoveWindow(kMargin, y, w, 200);
    (void)cy;
}

BOOL CToolPane::OnEraseBkgnd(CDC* /*pDC*/) {
    return TRUE;
}

void CToolPane::OnPaint() {
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);
    dc.FillSolidRect(rc, RGB(45, 47, 52));
}

void CToolPane::SetActiveTool(int toolIndex) {
    m_activeTool = toolIndex;
    for (int i = 0; i < 5; ++i) {
        if (!m_toolButtons[i].GetSafeHwnd())
            continue;
        // 선택된 도구는 체크 상태로 표시
        m_toolButtons[i].SetFaceColor(
            i == toolIndex ? RGB(0, 120, 212) : RGB(70, 73, 80), TRUE);
        m_toolButtons[i].SetTextColor(RGB(240, 240, 240));
    }
}

void CToolPane::OnToolButton(UINT nId) {
    const int idx = static_cast<int>(nId) - ID_TOOLPANE_FIRST_TOOL;
    SetActiveTool(idx);
    if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
        if (CSketchView* view = frame->GetActiveSketchView())
            view->SetTool(static_cast<DrawTool>(idx));
}

void CToolPane::NotifyView() {
    auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
    CSketchView* view = frame ? frame->GetActiveSketchView() : nullptr;
    if (!view)
        return;
    const COLORREF c = m_colorButton.GetColor();
    view->SetStrokeColor({ GetRValue(c), GetGValue(c), GetBValue(c), 255 });
    const int sel = m_widthCombo.GetCurSel();
    if (sel >= 0 && sel < static_cast<int>(std::size(kWidths)))
        view->SetStrokeWidth(kWidths[sel]);
}

void CToolPane::OnColorChanged() {
    NotifyView();
}

void CToolPane::OnWidthChanged() {
    NotifyView();
}
