#include "Filters.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sm {

GrayImage ToGrayscale(const BgraImage& src) {
    GrayImage out(src.Width(), src.Height());
    const uint8_t* p = src.Data();
    uint8_t* q = out.Data();
    const size_t n = static_cast<size_t>(src.Width()) * src.Height();
    for (size_t i = 0; i < n; ++i, p += 4) {
        // BT.601: Y = 0.299R + 0.587G + 0.114B (정수 근사)
        q[i] = static_cast<uint8_t>((299 * p[2] + 587 * p[1] + 114 * p[0]) / 1000);
    }
    return out;
}

BgraImage ToBgra(const GrayImage& src) {
    BgraImage out(src.Width(), src.Height());
    const uint8_t* p = src.Data();
    uint8_t* q = out.Data();
    const size_t n = static_cast<size_t>(src.Width()) * src.Height();
    for (size_t i = 0; i < n; ++i) {
        q[i * 4 + 0] = p[i];
        q[i * 4 + 1] = p[i];
        q[i * 4 + 2] = p[i];
        q[i * 4 + 3] = 255;
    }
    return out;
}

GrayImage GaussianBlur5(const GrayImage& src) {
    if (src.Empty())
        return {};
    static constexpr std::array<int, 5> kKernel = { 1, 4, 6, 4, 1 }; // 합 16
    const int w = src.Width(), h = src.Height();

    // 수평 패스
    GrayImage tmp(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int acc = 0;
            for (int k = -2; k <= 2; ++k)
                acc += kKernel[k + 2] * src.AtClamped(x + k, y);
            tmp.Set(x, y, static_cast<uint8_t>(acc / 16));
        }

    // 수직 패스
    GrayImage out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int acc = 0;
            for (int k = -2; k <= 2; ++k)
                acc += kKernel[k + 2] * tmp.AtClamped(x, y + k);
            out.Set(x, y, static_cast<uint8_t>(acc / 16));
        }
    return out;
}

GrayImage SobelMagnitude(const GrayImage& src) {
    if (src.Empty())
        return {};
    const int w = src.Width(), h = src.Height();
    GrayImage out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int p00 = src.AtClamped(x - 1, y - 1), p01 = src.AtClamped(x, y - 1), p02 = src.AtClamped(x + 1, y - 1);
            const int p10 = src.AtClamped(x - 1, y),                                    p12 = src.AtClamped(x + 1, y);
            const int p20 = src.AtClamped(x - 1, y + 1), p21 = src.AtClamped(x, y + 1), p22 = src.AtClamped(x + 1, y + 1);
            const int gx = (p02 + 2 * p12 + p22) - (p00 + 2 * p10 + p20);
            const int gy = (p20 + 2 * p21 + p22) - (p00 + 2 * p01 + p02);
            const int mag = static_cast<int>(std::sqrt(static_cast<double>(gx) * gx + static_cast<double>(gy) * gy));
            out.Set(x, y, static_cast<uint8_t>(std::min(mag, 255)));
        }
    return out;
}

GrayImage Threshold(const GrayImage& src, uint8_t threshold) {
    GrayImage out(src.Width(), src.Height());
    const uint8_t* p = src.Data();
    uint8_t* q = out.Data();
    const size_t n = static_cast<size_t>(src.Width()) * src.Height();
    for (size_t i = 0; i < n; ++i)
        q[i] = (p[i] >= threshold) ? 255 : 0;
    return out;
}

uint8_t OtsuThreshold(const GrayImage& src) {
    if (src.Empty())
        return 128;
    std::array<uint64_t, 256> hist{};
    const uint8_t* p = src.Data();
    const size_t n = static_cast<size_t>(src.Width()) * src.Height();
    for (size_t i = 0; i < n; ++i)
        ++hist[p[i]];

    double totalSum = 0;
    for (int v = 0; v < 256; ++v)
        totalSum += static_cast<double>(v) * hist[v];

    double bgSum = 0;
    uint64_t bgCount = 0;
    double bestVar = -1.0;
    uint8_t bestT = 128;
    for (int t = 0; t < 256; ++t) {
        bgCount += hist[t];
        if (bgCount == 0) continue;
        const uint64_t fgCount = n - bgCount;
        if (fgCount == 0) break;
        bgSum += static_cast<double>(t) * hist[t];
        const double bgMean = bgSum / bgCount;
        const double fgMean = (totalSum - bgSum) / fgCount;
        const double betweenVar = static_cast<double>(bgCount) * fgCount
                                * (bgMean - fgMean) * (bgMean - fgMean);
        if (betweenVar > bestVar) {
            bestVar = betweenVar;
            bestT = static_cast<uint8_t>(t);
        }
    }
    return bestT;
}

GrayImage Invert(const GrayImage& src) {
    GrayImage out(src.Width(), src.Height());
    const uint8_t* p = src.Data();
    uint8_t* q = out.Data();
    const size_t n = static_cast<size_t>(src.Width()) * src.Height();
    for (size_t i = 0; i < n; ++i)
        q[i] = static_cast<uint8_t>(255 - p[i]);
    return out;
}

} // namespace sm
