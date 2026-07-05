#pragma once
// RobotSimPane.h - 2링크 로봇팔 드로잉 시뮬레이터 패널
#include "framework.h"
#include "Resource.h"
#include "../Core/ArmKinematics.h"
#include "../Core/PathPlanner.h"

class CRobotSimPane : public CDockablePane {
public:
    // 캔버스 폴리라인(캔버스 px 좌표)을 받아 시뮬레이션 시작
    void StartSimulation(const std::vector<std::vector<sm::PointF>>& canvasPolylines);
    void StopSimulation();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();
    afx_msg void OnPlay();
    afx_msg void OnStop();
    DECLARE_MESSAGE_MAP()

private:
    // 시뮬레이션 세그먼트: 팔이 이동해야 하는 직선 구간
    struct Segment {
        sm::PointF from;
        sm::PointF to;
        bool penDown = false;
        float duration = 0.0f; // 초
    };

    CRect CanvasRect() const;
    void BuildSegments(const std::vector<std::vector<sm::PointF>>& paperPaths);
    sm::PointF CurrentPenPoint() const;
    void RenderScene(Gdiplus::Graphics& g, const CRect& rc) const;
    Gdiplus::PointF ToScreen(const sm::PointF& world, const CRect& rc) const;

    // 시뮬 월드: 베이스 (0,0), y 위쪽. 종이는 베이스 앞쪽 영역.
    sm::ArmConfig m_arm{ { 0.0f, 0.0f }, 220.0f, 180.0f };
    static constexpr float kPaperLeft = -160.0f, kPaperRight = 160.0f;
    static constexpr float kPaperBottom = 120.0f, kPaperTop = 320.0f;
    static constexpr float kDrawSpeed = 130.0f;   // units/s
    static constexpr float kTravelSpeed = 420.0f; // units/s

    std::vector<Segment> m_segments;
    size_t m_currentSegment = 0;
    float m_segmentElapsed = 0.0f;   // 현재 세그먼트 경과 시간
    bool m_playing = false;

    std::vector<std::vector<sm::PointF>> m_trail;      // 그려진 잉크 자국
    std::vector<std::vector<sm::PointF>> m_paperPaths; // 전체 목표 경로 (미리보기)

    CButton m_btnPlay;
    CButton m_btnStop;
    CStatic m_statusText;
};
