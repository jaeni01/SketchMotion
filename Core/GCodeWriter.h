#pragma once
// GCodeWriter.h - 펜 플로터용 G-code 생성
#include <string>
#include <vector>
#include "Geometry.h"

namespace sm {

struct GCodeOptions {
    // 캔버스(px) -> 작업 영역(mm) 매핑
    float workWidthMm = 200.0f;   // 출력물 가로 크기. 세로는 비율 유지.
    bool flipY = true;            // 화면 y-아래방향 -> 기계 y-위방향 변환

    // 이송 속도
    float feedRateDraw = 1500.0f;   // mm/min, 펜 내리고 이동
    float feedRateTravel = 3000.0f; // mm/min, 펜 들고 이동

    // 펜 승강 (Z축 방식)
    float penUpZ = 5.0f;
    float penDownZ = 0.0f;
    float zFeedRate = 300.0f;

    int decimals = 3; // 좌표 소수 자리
};

// 캔버스 픽셀 좌표 폴리라인들(이미 순서 최적화된)을 G-code 문자열로 변환.
// bounds: 폴리라인 전체의 AABB (좌표 정규화 기준).
std::string WriteGCode(const std::vector<std::vector<PointF>>& orderedPaths,
                       const RectF& bounds,
                       const GCodeOptions& opt = {});

} // namespace sm
