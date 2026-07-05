#pragma once
// Stroke.h - 드로잉 도메인 모델 (MFC 비의존)
#include <cstdint>
#include <vector>
#include "Geometry.h"

namespace sm {

enum class StrokeKind : uint8_t {
    Freehand = 0,   // points = 자유곡선 전체
    Line     = 1,   // points = {시작, 끝}
    Rect     = 2,   // points = {좌상단 앵커, 우하단 앵커}
    Ellipse  = 3,   // points = {AABB 좌상단, AABB 우하단}
};

struct ColorRGBA {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const ColorRGBA&) const = default;
};

class Stroke {
public:
    Stroke() = default;
    Stroke(uint64_t id, StrokeKind kind, ColorRGBA color, float width)
        : m_id(id), m_kind(kind), m_color(color), m_width(width) {}

    uint64_t Id() const { return m_id; }
    StrokeKind Kind() const { return m_kind; }
    ColorRGBA Color() const { return m_color; }
    float Width() const { return m_width; }
    const std::vector<PointF>& Points() const { return m_points; }

    void SetColor(ColorRGBA c) { m_color = c; }
    void SetWidth(float w) { m_width = w; }
    void AddPoint(const PointF& p) { m_points.push_back(p); }
    void SetPoints(std::vector<PointF> pts) { m_points = std::move(pts); }

    RectF Bounds() const;

    // 도형 종류와 무관하게 렌더링/경로 계획에 쓸 폴리라인으로 변환.
    // ellipseSegments: 타원 근사 분할 수
    std::vector<std::vector<PointF>> ToPolylines(int ellipseSegments = 48) const;

    // 히트 테스트(지우개용): 점 p가 스트로크 두께 + tolerance 안에 있는가
    bool HitTest(const PointF& p, float tolerance) const;

private:
    uint64_t m_id = 0;
    StrokeKind m_kind = StrokeKind::Freehand;
    ColorRGBA m_color{};
    float m_width = 2.0f;
    std::vector<PointF> m_points;
};

} // namespace sm
