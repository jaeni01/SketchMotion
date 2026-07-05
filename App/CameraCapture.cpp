#include "CameraCapture.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------- 캡처 스레드

void CameraCapture::Start(HWND notifyWnd) {
    if (m_running.load())
        return;
    Stop(); // 이전 스레드 정리
    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread([this, notifyWnd] { CaptureLoop(notifyWnd); });
}

void CameraCapture::Stop() {
    m_stopRequested = true;
    if (m_thread.joinable())
        m_thread.join();
    m_running = false;
}

void CameraCapture::CaptureLoop(HWND notifyWnd) {
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool mfStarted = false;
    m_lastError.clear();

    do {
        if (FAILED(MFStartup(MF_VERSION))) {
            m_lastError = L"MFStartup failed";
            break;
        }
        mfStarted = true;

        // 1) 비디오 캡처 장치 열거
        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 1)) ||
            FAILED(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
            m_lastError = L"MFCreateAttributes failed";
            break;
        }

        IMFActivate** devices = nullptr;
        UINT32 deviceCount = 0;
        if (FAILED(MFEnumDeviceSources(attrs.Get(), &devices, &deviceCount)) ||
            deviceCount == 0) {
            m_lastError = L"No video capture device found";
            if (devices) CoTaskMemFree(devices);
            break;
        }

        ComPtr<IMFMediaSource> source;
        const HRESULT hrActivate =
            devices[0]->ActivateObject(IID_PPV_ARGS(&source));
        for (UINT32 i = 0; i < deviceCount; ++i)
            devices[i]->Release();
        CoTaskMemFree(devices);
        if (FAILED(hrActivate)) {
            m_lastError = L"Failed to activate capture device";
            break;
        }

        // 2) 소스 리더 생성 + RGB32 출력 강제
        ComPtr<IMFSourceReader> reader;
        if (FAILED(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader))) {
            m_lastError = L"MFCreateSourceReaderFromMediaSource failed";
            break;
        }

        ComPtr<IMFMediaType> rgbType;
        if (FAILED(MFCreateMediaType(&rgbType)) ||
            FAILED(rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
            FAILED(reader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                nullptr, rgbType.Get()))) {
            m_lastError = L"Device does not support RGB32 output";
            break;
        }

        ComPtr<IMFMediaType> actualType;
        UINT32 width = 0, height = 0;
        if (FAILED(reader->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actualType)) ||
            FAILED(MFGetAttributeSize(actualType.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
            width == 0 || height == 0) {
            m_lastError = L"Failed to query frame size";
            break;
        }

        LONG defaultStride = static_cast<LONG>(width * 4);
        {
            UINT32 strideAttr = 0;
            if (SUCCEEDED(actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideAttr)))
                defaultStride = static_cast<LONG>(static_cast<INT32>(strideAttr));
        }

        // 3) 프레임 루프
        while (!m_stopRequested.load()) {
            DWORD streamIndex = 0, flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader->ReadSample(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                    0, &streamIndex, &flags, &timestamp, &sample))) {
                m_lastError = L"ReadSample failed";
                break;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                break;
            if (!sample)
                continue;

            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
                continue;

            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen)))
                continue;

            const size_t needed = static_cast<size_t>(width) * height * 4;
            if (curLen >= needed) {
                auto frame = std::make_unique<sm::BgraImage>(
                    static_cast<int>(width), static_cast<int>(height));
                if (defaultStride >= 0) {
                    // top-down
                    for (UINT32 y = 0; y < height; ++y)
                        memcpy(frame->Pixel(0, static_cast<int>(y)),
                               data + static_cast<size_t>(y) * defaultStride,
                               static_cast<size_t>(width) * 4);
                } else {
                    // bottom-up: 행 순서 뒤집기
                    const LONG absStride = -defaultStride;
                    for (UINT32 y = 0; y < height; ++y)
                        memcpy(frame->Pixel(0, static_cast<int>(y)),
                               data + static_cast<size_t>(height - 1 - y) * absStride,
                               static_cast<size_t>(width) * 4);
                }
                if (::PostMessage(notifyWnd, WM_APP_CAMERA_FRAME, 0,
                                  reinterpret_cast<LPARAM>(frame.get())))
                    frame.release(); // 소유권은 수신측으로
            }
            buffer->Unlock();
        }
    } while (false);

    if (mfStarted)
        MFShutdown();
    if (SUCCEEDED(coInit))
        CoUninitialize();

    m_running = false;
    ::PostMessage(notifyWnd, WM_APP_CAMERA_STOPPED, 0, 0);
}

// ------------------------------------------------------------- 테스트 이미지

namespace {

void FillRect32(sm::BgraImage& img, int x0, int y0, int x1, int y1,
                uint8_t b, uint8_t g, uint8_t r) {
    for (int y = std::max(0, y0); y < std::min(img.Height(), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(img.Width(), x1); ++x) {
            uint8_t* p = img.Pixel(x, y);
            p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
        }
}

void FillCircle32(sm::BgraImage& img, int cx, int cy, int radius,
                  uint8_t b, uint8_t g, uint8_t r) {
    for (int y = std::max(0, cy - radius); y < std::min(img.Height(), cy + radius + 1); ++y)
        for (int x = std::max(0, cx - radius); x < std::min(img.Width(), cx + radius + 1); ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= radius * radius) {
                uint8_t* p = img.Pixel(x, y);
                p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
            }
        }
}

void FillTriangle32(sm::BgraImage& img, int apexX, int apexY, int baseY, int halfBase,
                    uint8_t b, uint8_t g, uint8_t r) {
    for (int y = std::max(0, apexY); y < std::min(img.Height(), baseY); ++y) {
        const float t = static_cast<float>(y - apexY) / std::max(1, baseY - apexY);
        const int half = static_cast<int>(halfBase * t);
        for (int x = std::max(0, apexX - half); x < std::min(img.Width(), apexX + half + 1); ++x) {
            uint8_t* p = img.Pixel(x, y);
            p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
        }
    }
}

} // namespace

sm::BgraImage MakeTestImage(int width, int height) {
    sm::BgraImage img(width, height);
    // 밝은 배경
    FillRect32(img, 0, 0, width, height, 235, 235, 235);

    const int groundY = height * 3 / 4;
    // 땅
    FillRect32(img, 0, groundY, width, height, 190, 200, 190);
    // 해
    FillCircle32(img, width * 4 / 5, height / 5, height / 9, 60, 200, 250);
    // 집 몸체
    const int hx0 = width / 6, hx1 = width / 2;
    const int hy0 = height * 2 / 5, hy1 = groundY;
    FillRect32(img, hx0, hy0, hx1, hy1, 120, 140, 210);
    // 지붕
    FillTriangle32(img, (hx0 + hx1) / 2, height / 5, hy0, (hx1 - hx0) / 2 + 20, 90, 90, 190);
    // 문
    FillRect32(img, (hx0 + hx1) / 2 - 25, hy1 - 90, (hx0 + hx1) / 2 + 25, hy1, 60, 70, 110);
    // 나무 줄기 + 잎
    const int tx = width * 7 / 10;
    FillRect32(img, tx - 12, groundY - 130, tx + 12, groundY, 60, 100, 140);
    FillCircle32(img, tx, groundY - 170, 60, 80, 170, 90);
    return img;
}
