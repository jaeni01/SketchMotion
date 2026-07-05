#pragma once
// ContourTracer.h - 이진 이미지 윤곽 추적 (Moore-neighbor tracing)
#include <vector>
#include "Geometry.h"
#include "ImageBuffer.h"

namespace sm {

struct ContourOptions {
    uint8_t onValue = 255;   // 전경 픽셀 값
    int minPoints = 8;       // 이보다 짧은 윤곽은 노이즈로 버림
    int maxContours = 4096;  // 안전 상한
};

// 이진 이미지에서 외곽 윤곽들을 추출한다.
// 반환된 각 윤곽은 픽셀 좌표의 폐곡선(시작점 미복제)이다.
std::vector<std::vector<PointF>> TraceContours(const GrayImage& binary,
                                               const ContourOptions& opt = {});

} // namespace sm
