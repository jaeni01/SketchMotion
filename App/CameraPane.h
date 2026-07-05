#pragma once
// CameraPane.h - 웹캠 프리뷰 + 필터 도킹 패널
#include "framework.h"
#include "Resource.h"
#include "CameraCapture.h"
#include "../Core/Filters.h"

enum class CamFilter { None = 0, Grayscale, Blur, Edge, Threshold };

class CCameraPane : public CDockablePane {
public:
    // 트레이싱용 원본 프레임 (없으면 Empty). UI 스레드에서만 접근.
    const sm::BgraImage& RawFrame() const { return m_rawFrame; }
    bool HasFrame() const { return !m_rawFrame.Empty(); }

    void LoadTestImage();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnDestroy();
    afx_msg void OnCamStart();
    afx_msg void OnCamStop();
    afx_msg void OnFilterChanged();
    afx_msg LRESULT OnCameraFrame(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCameraStopped(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    void ApplyFilter();
    CRect PreviewRect() const;

    CameraCapture m_capture;
    sm::BgraImage m_rawFrame;     // 원본 (트레이싱 입력)
    sm::BgraImage m_display;      // 필터 적용된 표시용
    CamFilter m_filter = CamFilter::None;

    CButton m_btnStart;
    CButton m_btnStop;
    CComboBox m_filterCombo;
    CStatic m_statusText;
};
