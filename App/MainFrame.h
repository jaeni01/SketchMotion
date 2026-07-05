#pragma once
// MainFrame.h - 메인 프레임: 도킹 패널 3종 + 상태바 + 다크 테마
#include "framework.h"
#include "Resource.h"
#include "CameraPane.h"
#include "RobotSimPane.h"
#include "ToolPane.h"

class CSketchView;
class CSketchDoc;
enum class DrawTool;

class CMainFrame : public CFrameWndEx {
    DECLARE_DYNCREATE(CMainFrame)
protected:
    CMainFrame() = default;

public:
    CSketchView* GetActiveSketchView();
    CSketchDoc* GetActiveSketchDoc();

    void UpdateStatus(DrawTool tool, const sm::PointF* worldPos,
                      float zoom, size_t strokeCount);
    void OnViewToolChanged(DrawTool tool);

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnAppAbout();
    afx_msg void OnTogglePane(UINT nId);
    afx_msg void OnUpdateTogglePane(CCmdUI* pCmdUI);
    afx_msg void OnVisionTrace();
    afx_msg void OnVisionLoadTestImage();
    afx_msg void OnMotionExportGCode();
    afx_msg void OnMotionSimulate();
    DECLARE_MESSAGE_MAP()

private:
    CDockablePane* PaneFromId(UINT nId);
    void ApplyDarkTitleBar();

    CMFCStatusBar m_wndStatusBar;
    CToolPane m_toolPane;
    CCameraPane m_cameraPane;
    CRobotSimPane m_robotPane;
};
