#pragma once
// SketchView.h - 캔버스 뷰: GDI+ 더블 버퍼 렌더링, 도구 입력, 팬/줌
#include "framework.h"
#include "Resource.h"
#include "SketchDoc.h"

enum class DrawTool { Pen = 0, Line, Rect, Ellipse, Eraser };

class CSketchView : public CView {
    DECLARE_DYNCREATE(CSketchView)
protected:
    CSketchView() = default;

public:
    CSketchDoc* GetDocument() const { return static_cast<CSketchDoc*>(m_pDocument); }

    DrawTool Tool() const { return m_tool; }
    void SetTool(DrawTool t);
    void SetStrokeColor(sm::ColorRGBA c) { m_color = c; }
    void SetStrokeWidth(float w) { m_width = w; }
    sm::ColorRGBA StrokeColor() const { return m_color; }
    float StrokeWidth() const { return m_width; }

    void OnDraw(CDC* pDC) override;
    void OnInitialUpdate() override;

protected:
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnToolCommand(UINT nId);
    afx_msg void OnUpdateToolCommand(CCmdUI* pCmdUI);
    afx_msg void OnFileExportPng();
    DECLARE_MESSAGE_MAP()

private:
    // 좌표 변환: world(캔버스 픽셀) <-> device(뷰 클라이언트 픽셀)
    sm::PointF DeviceToWorld(CPoint pt) const;
    CPoint WorldToDevice(const sm::PointF& p) const;

    void CommitActiveStroke();
    void EraseAt(const sm::PointF& worldPt);
    void RenderScene(Gdiplus::Graphics& g, const CRect& client) const;
    static void RenderStroke(Gdiplus::Graphics& g, const sm::Stroke& s, BYTE alphaOverride = 0);
    void UpdateStatusBar(const sm::PointF* worldPos) const;
    void CenterCanvas();

    DrawTool m_tool = DrawTool::Pen;
    sm::ColorRGBA m_color{ 30, 30, 30, 255 };
    float m_width = 3.0f;

    float m_zoom = 1.0f;
    CPoint m_pan{ 0, 0 };          // device px
    bool m_panning = false;
    CPoint m_lastMouse{ 0, 0 };

    bool m_drawing = false;
    std::vector<sm::PointF> m_activePoints; // world 좌표
};
