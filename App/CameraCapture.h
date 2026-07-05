#pragma once
// CameraCapture.h - Media Foundation 웹캠 캡처 (워커 스레드 → PostMessage 마샬링)
#include "framework.h"
#include <atomic>
#include <string>
#include <thread>
#include "../Core/ImageBuffer.h"

// 프레임 도착 알림: WPARAM=0, LPARAM = sm::BgraImage* (수신측이 delete 책임)
constexpr UINT WM_APP_CAMERA_FRAME = WM_APP + 1;
// 캡처 실패/종료 알림: LPARAM = 0
constexpr UINT WM_APP_CAMERA_STOPPED = WM_APP + 2;

class CameraCapture {
public:
    CameraCapture() = default;
    ~CameraCapture() { Stop(); }

    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    // notifyWnd로 WM_APP_CAMERA_FRAME을 보낸다. 이미 실행 중이면 no-op.
    void Start(HWND notifyWnd);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    // 마지막 실패 사유 (UI 스레드에서 Stop 알림 후 읽기)
    std::wstring LastError() const { return m_lastError; }

private:
    void CaptureLoop(HWND notifyWnd);

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopRequested{ false };
    std::wstring m_lastError;
};

// 웹캠이 없을 때 데모용 합성 테스트 이미지 (집+해+나무 라인아트 씬)
sm::BgraImage MakeTestImage(int width = 640, int height = 480);
