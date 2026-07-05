#include "SketchMotionApp.h"
#include "MainFrame.h"
#include "Resource.h"
#include "SketchDoc.h"
#include "SketchView.h"

CSketchMotionApp theApp;

BOOL CSketchMotionApp::InitInstance() {
    // 공용 컨트롤 초기화
    INITCOMMONCONTROLSEX initCtrls{};
    initCtrls.dwSize = sizeof(initCtrls);
    initCtrls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&initCtrls);

    CWinAppEx::InitInstance();

    // GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr)
            != Gdiplus::Ok)
        return FALSE;

    SetRegistryKey(L"SketchMotion");

    // CFrameWndEx 인프라
    InitContextMenuManager();
    InitKeyboardManager();
    InitTooltipManager();
    CMFCToolTipInfo ttParams;
    ttParams.m_bVislManagerTheme = TRUE;
    GetTooltipManager()->SetTooltipParams(AFX_TOOLTIP_TYPE_ALL,
        RUNTIME_CLASS(CMFCToolTipCtrl), &ttParams);

    // 다크 비주얼 테마
    CMFCVisualManagerOffice2007::SetStyle(
        CMFCVisualManagerOffice2007::Office2007_ObsidianBlack);
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOffice2007));

    // SDI 문서 템플릿
    auto* docTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME,
        RUNTIME_CLASS(CSketchDoc),
        RUNTIME_CLASS(CMainFrame),
        RUNTIME_CLASS(CSketchView));
    AddDocTemplate(docTemplate);

    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo))
        return FALSE;

    m_pMainWnd->ShowWindow(SW_SHOWMAXIMIZED);
    m_pMainWnd->UpdateWindow();
    return TRUE;
}

int CSketchMotionApp::ExitInstance() {
    if (m_gdiplusToken)
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    return CWinAppEx::ExitInstance();
}
