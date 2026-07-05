#include "CameraPane.h"

namespace {
constexpr int kControlsHeight = 64;
constexpr int kMargin = 6;
} // namespace

BEGIN_MESSAGE_MAP(CCameraPane, CDockablePane)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_DESTROY()
    ON_BN_CLICKED(ID_CAM_START, &CCameraPane::OnCamStart)
    ON_BN_CLICKED(ID_CAM_STOP, &CCameraPane::OnCamStop)
    ON_CBN_SELCHANGE(ID_CAM_FILTER_COMBO, &CCameraPane::OnFilterChanged)
    ON_MESSAGE(WM_APP_CAMERA_FRAME, &CCameraPane::OnCameraFrame)
    ON_MESSAGE(WM_APP_CAMERA_STOPPED, &CCameraPane::OnCameraStopped)
END_MESSAGE_MAP()

int CCameraPane::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CDockablePane::OnCreate(lpCreateStruct) == -1)
        return -1;

    const DWORD btnStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    m_btnStart.Create(L"Start", btnStyle, CRect(0, 0, 10, 10), this, ID_CAM_START);
    m_btnStop.Create(L"Stop", btnStyle, CRect(0, 0, 10, 10), this, ID_CAM_STOP);

    m_filterCombo.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                         CRect(0, 0, 10, 200), this, ID_CAM_FILTER_COMBO);
    m_filterCombo.AddString(L"No Filter");
    m_filterCombo.AddString(L"Grayscale");
    m_filterCombo.AddString(L"Gaussian Blur");
    m_filterCombo.AddString(L"Sobel Edge");
    m_filterCombo.AddString(L"Threshold (Otsu)");
    m_filterCombo.SetCurSel(0);

    m_statusText.Create(L"Camera idle. Start or load a test image (Vision menu).",
                        WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this);

    // 패널 폰트 통일
    CFont* font = CFont::FromHandle(
        static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    m_btnStart.SetFont(font);
    m_btnStop.SetFont(font);
    m_filterCombo.SetFont(font);
    m_statusText.SetFont(font);
    return 0;
}

void CCameraPane::OnDestroy() {
    m_capture.Stop();
    CDockablePane::OnDestroy();
}

CRect CCameraPane::PreviewRect() const {
    CRect rc;
    GetClientRect(&rc);
    rc.DeflateRect(kMargin, kControlsHeight, kMargin, kMargin);
    return rc;
}

void CCameraPane::OnSize(UINT nType, int cx, int cy) {
    CDockablePane::OnSize(nType, cx, cy);
    if (!m_btnStart.GetSafeHwnd())
        return;
    const int btnW = 64, btnH = 24;
    int x = kMargin, y = kMargin;
    m_btnStart.MoveWindow(x, y, btnW, btnH);
    x += btnW + kMargin;
    m_btnStop.MoveWindow(x, y, btnW, btnH);
    x += btnW + kMargin;
    m_filterCombo.MoveWindow(x, y, std::max(80, cx - x - kMargin), 200);
    m_statusText.MoveWindow(kMargin, y + btnH + 4, cx - 2 * kMargin, 18);
    Invalidate(FALSE);
    (void)cy;
}

BOOL CCameraPane::OnEraseBkgnd(CDC* /*pDC*/) {
    return TRUE;
}

void CCameraPane::OnPaint() {
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);

    // 컨트롤 영역 배경
    dc.FillSolidRect(client, RGB(45, 47, 52));

    CRect preview = PreviewRect();
    if (preview.Width() <= 0 || preview.Height() <= 0)
        return;

    if (m_display.Empty()) {
        dc.FillSolidRect(preview, RGB(25, 26, 30));
        dc.SetTextColor(RGB(140, 140, 140));
        dc.SetBkMode(TRANSPARENT);
        dc.DrawText(L"No camera frame", preview,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    // 종횡비 유지 letterbox
    const double imgAspect = static_cast<double>(m_display.Width()) / m_display.Height();
    const double rcAspect = static_cast<double>(preview.Width()) / preview.Height();
    CRect target = preview;
    if (imgAspect > rcAspect) {
        const int h = static_cast<int>(preview.Width() / imgAspect);
        target.top = preview.top + (preview.Height() - h) / 2;
        target.bottom = target.top + h;
    } else {
        const int w = static_cast<int>(preview.Height() * imgAspect);
        target.left = preview.left + (preview.Width() - w) / 2;
        target.right = target.left + w;
    }
    dc.FillSolidRect(preview, RGB(25, 26, 30));

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_display.Width();
    bmi.bmiHeader.biHeight = -m_display.Height(); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dc.SetStretchBltMode(HALFTONE);
    StretchDIBits(dc.GetSafeHdc(),
                  target.left, target.top, target.Width(), target.Height(),
                  0, 0, m_display.Width(), m_display.Height(),
                  m_display.Data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void CCameraPane::OnCamStart() {
    if (m_capture.IsRunning())
        return;
    m_statusText.SetWindowTextW(L"Starting camera...");
    m_capture.Start(GetSafeHwnd());
}

void CCameraPane::OnCamStop() {
    m_capture.Stop();
    m_statusText.SetWindowTextW(L"Camera stopped.");
}

void CCameraPane::OnFilterChanged() {
    m_filter = static_cast<CamFilter>(m_filterCombo.GetCurSel());
    ApplyFilter();
    InvalidateRect(PreviewRect(), FALSE);
}

LRESULT CCameraPane::OnCameraFrame(WPARAM /*wParam*/, LPARAM lParam) {
    std::unique_ptr<sm::BgraImage> frame(reinterpret_cast<sm::BgraImage*>(lParam));
    if (!frame)
        return 0;
    m_rawFrame = std::move(*frame);
    ApplyFilter();
    if (m_capture.IsRunning())
        m_statusText.SetWindowTextW(L"Camera running.");
    InvalidateRect(PreviewRect(), FALSE);
    return 0;
}

LRESULT CCameraPane::OnCameraStopped(WPARAM /*wParam*/, LPARAM /*lParam*/) {
    const std::wstring err = m_capture.LastError();
    if (!err.empty()) {
        CString msg;
        msg.Format(L"Camera unavailable (%s). Use Vision > Load Test Image for the demo.",
                   err.c_str());
        m_statusText.SetWindowTextW(msg);
    }
    return 0;
}

void CCameraPane::LoadTestImage() {
    m_capture.Stop();
    m_rawFrame = MakeTestImage();
    ApplyFilter();
    m_statusText.SetWindowTextW(L"Test image loaded.");
    InvalidateRect(PreviewRect(), FALSE);
}

void CCameraPane::ApplyFilter() {
    if (m_rawFrame.Empty()) {
        m_display = {};
        return;
    }
    switch (m_filter) {
    case CamFilter::None:
        m_display = m_rawFrame;
        break;
    case CamFilter::Grayscale:
        m_display = sm::ToBgra(sm::ToGrayscale(m_rawFrame));
        break;
    case CamFilter::Blur:
        m_display = sm::ToBgra(sm::GaussianBlur5(sm::ToGrayscale(m_rawFrame)));
        break;
    case CamFilter::Edge:
        m_display = sm::ToBgra(sm::SobelMagnitude(
            sm::GaussianBlur5(sm::ToGrayscale(m_rawFrame))));
        break;
    case CamFilter::Threshold: {
        const sm::GrayImage gray = sm::ToGrayscale(m_rawFrame);
        m_display = sm::ToBgra(sm::Threshold(gray, sm::OtsuThreshold(gray)));
        break;
    }
    }
}
