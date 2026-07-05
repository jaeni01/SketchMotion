#pragma once
// MarkerDetector.h - SM-Tag: 자체 구현 정사각 fiducial 마커
//
// 구조 (6x6 셀): 바깥 1셀 링 = 검정 테두리, 안쪽 4x4 = 페이로드 16셀.
// 페이로드 모서리 4셀은 방향 앵커: (0,0)=흰, 나머지 3모서리=검정.
// 남은 12셀 = ID 8비트 + 패리티 4비트 (p = (id ^ id>>4) & 0xF).
// -> 4방향 회전 복원 + 오검출 기각이 구조적으로 보장된다.
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "Geometry.h"
#include "ImageBuffer.h"

namespace sm {

struct MarkerDetection {
    int id = -1;
    // 이미지 좌표. corners[0] = 방향 앵커(흰 모서리) 쪽 외곽 코너, 이후 시계방향.
    std::array<PointF, 4> corners{};
    PointF center{};
    float angle = 0.0f; // 이미지 좌표계에서 마커 +x축 방향 [rad]
};

struct MarkerDetectOptions {
    int minPerimeterPx = 40;    // 이보다 작은 후보 무시
    float maxCellDarkRatio = 0.35f; // 페이로드 '흰' 판정 여유
    int adaptiveTile = 24;
};

// 페이로드 16셀 비트 생성 (row-major, 1=흰). id: 0..255
std::array<uint8_t, 16> MakeTagBits(uint8_t id);

// 마커 렌더링: 6x6 셀 + quiet zone(흰 여백) 1셀, 셀당 cellPx 픽셀
GrayImage RenderTag(uint8_t id, int cellPx, int quietCells = 1);

// 그레이 이미지에서 마커 검출
std::vector<MarkerDetection> DetectMarkers(const GrayImage& gray,
                                           const MarkerDetectOptions& opt = {});

} // namespace sm
