#pragma once
// PathPlanner.h - 펜 플로터식 경로 계획: 폴리라인 순회 순서 최적화
#include <vector>
#include "Geometry.h"

namespace sm {

struct PlanStats {
    float drawDistance = 0.0f;    // 펜 내리고 이동한 거리
    float travelDistance = 0.0f;  // 펜 들고 이동한 거리 (최소화 대상)
    size_t pathCount = 0;
};

struct PlannedPath {
    std::vector<std::vector<PointF>> paths; // 순회 순서대로, 필요 시 방향 반전됨
    PlanStats stats;
};

// 탐욕적 nearest-neighbor: 현재 펜 위치에서 가장 가까운 끝점을 가진
// 폴리라인을 다음으로 선택하고, 필요하면 방향을 뒤집는다.
// start: 펜의 시작 위치 (보통 원점)
PlannedPath PlanDrawingOrder(const std::vector<std::vector<PointF>>& polylines,
                             const PointF& start = { 0.0f, 0.0f });

// 주어진 순서 그대로의 pen-up 이동 거리 (최적화 효과 비교용)
float TravelDistanceOf(const std::vector<std::vector<PointF>>& polylines,
                       const PointF& start = { 0.0f, 0.0f });

} // namespace sm
