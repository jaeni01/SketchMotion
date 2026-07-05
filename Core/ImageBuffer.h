#pragma once
// ImageBuffer.h - 래스터 버퍼 (MFC 비의존)
#include <cstdint>
#include <vector>

namespace sm {

// 8bit 그레이스케일 이미지. 영상처리 파이프라인의 공용 통화.
class GrayImage {
public:
    GrayImage() = default;
    GrayImage(int w, int h, uint8_t fill = 0)
        : m_w(w), m_h(h), m_data(static_cast<size_t>(w) * h, fill) {}

    int Width() const { return m_w; }
    int Height() const { return m_h; }
    bool Empty() const { return m_data.empty(); }

    uint8_t At(int x, int y) const { return m_data[static_cast<size_t>(y) * m_w + x]; }
    void Set(int x, int y, uint8_t v) { m_data[static_cast<size_t>(y) * m_w + x] = v; }

    // 경계 클램프 접근 (필터 커널용)
    uint8_t AtClamped(int x, int y) const {
        if (x < 0) x = 0; else if (x >= m_w) x = m_w - 1;
        if (y < 0) y = 0; else if (y >= m_h) y = m_h - 1;
        return At(x, y);
    }

    const uint8_t* Data() const { return m_data.data(); }
    uint8_t* Data() { return m_data.data(); }

private:
    int m_w = 0;
    int m_h = 0;
    std::vector<uint8_t> m_data;
};

// 32bit BGRA 이미지 (Windows DIB/Media Foundation 프레임과 배치 호환)
class BgraImage {
public:
    BgraImage() = default;
    BgraImage(int w, int h)
        : m_w(w), m_h(h), m_data(static_cast<size_t>(w) * h * 4, 0) {}

    int Width() const { return m_w; }
    int Height() const { return m_h; }
    bool Empty() const { return m_data.empty(); }
    int Stride() const { return m_w * 4; }

    const uint8_t* Pixel(int x, int y) const {
        return m_data.data() + (static_cast<size_t>(y) * m_w + x) * 4;
    }
    uint8_t* Pixel(int x, int y) {
        return m_data.data() + (static_cast<size_t>(y) * m_w + x) * 4;
    }

    const uint8_t* Data() const { return m_data.data(); }
    uint8_t* Data() { return m_data.data(); }

private:
    int m_w = 0;
    int m_h = 0;
    std::vector<uint8_t> m_data; // B,G,R,A 순
};

} // namespace sm
