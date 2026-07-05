#pragma once
// HardwarePane.h - 하드웨어 연결/실행 패널 + v2 오케스트레이션
// Arm(9101)·AGV(9102) 브리지 연결, 드로잉/미션 실행, E-stop.
// AGV 폐루프: mock_pose(또는 추후 카메라 관측) -> AgvEkf -> set_pose_estimate 스트림.
#include "framework.h"
#include "Resource.h"
#include "BridgeClient.h"
#include "../Core/KalmanFilter.h"

class CHardwarePane : public CDockablePane {
public:
    void EmergencyStopAll();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
    afx_msg void OnDestroy();
    afx_msg void OnConnectArm();
    afx_msg void OnConnectAgv();
    afx_msg void OnDrawRobot();
    afx_msg void OnAgvMission();
    afx_msg void OnStopAll();
    afx_msg void OnEstop();
    afx_msg LRESULT OnBridge(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    void Layout(int cx);
    void SetStatus(int slot, const CString& s);
    void HandleEvent(int slot, const proto::JVal& msg);
    void SendMissionFromCanvas();
    void SendDrawFromCanvas();

    BridgeClient m_arm;   // slot 0
    BridgeClient m_agv;   // slot 1

    // AGV 폐루프 상태 (UI 스레드에서만 접근)
    sm::AgvEkf m_ekf;
    ULONGLONG m_lastPoseTick = 0;
    double m_lastNis = 0;
    bool m_missionActive = false;
    int m_poseTx = 0;      // set_pose_estimate 송신 수 (진단)
    int m_poseTxFail = 0;  // 송신 실패 수 (진단)

    CButton m_btnConnArm, m_btnConnAgv, m_btnDraw, m_btnMission, m_btnStop, m_btnEstop;
    CStatic m_stArm, m_stAgv, m_stEkf, m_stProgress;
};
