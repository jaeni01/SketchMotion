#pragma once
// ToolPane.h - 도구/색/두께 도킹 패널
#include "framework.h"
#include "Resource.h"
#include <afxcolorbutton.h>
#include "../Core/Stroke.h"

enum class DrawTool; // SketchView.h

class CToolPane : public CDockablePane {
public:
    void SetActiveTool(int toolIndex); // 뷰에서 도구가 바뀌면 하이라이트 동기화

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
    afx_msg void OnToolButton(UINT nId);
    afx_msg void OnColorChanged();
    afx_msg void OnWidthChanged();
    DECLARE_MESSAGE_MAP()

private:
    void NotifyView();

    CMFCButton m_toolButtons[5];
    CMFCColorButton m_colorButton;
    CComboBox m_widthCombo;
    CStatic m_labelTools;
    CStatic m_labelStyle;
    int m_activeTool = 0;
};
