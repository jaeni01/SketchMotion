#pragma once
// Geometry.h - 기본 기하 타입 (MFC 비의존)
#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

namespace sm {

struct PointF {
    float x = 0.0f;
    float y = 0.0f;

    PointF() = default;
    PointF(float px, float py) : x(px), y(py) {}

    PointF operator+(const PointF& o) const { return { x + o.x, y + o.y }; }
    PointF operator-(const PointF& o) const { return { x - o.x, y - o.y }; }
    PointF operator*(float s) const { return { x * s, y * s }; }
    bool operator==(const PointF& o) const { return x == o.x && y == o.y; }
};

inline float Dot(const PointF& a, const PointF& b) { return a.x * b.x + a.y * b.y; }
inline float LengthSq(const PointF& v) { return v.x * v.x + v.y * v.y; }
inline float Length(const PointF& v) { return std::sqrt(LengthSq(v)); }
inline float Distance(const PointF& a, const PointF& b) { return Length(b - a); }

// 점 p에서 선분 [a,b]까지의 수직 거리
inline float DistanceToSegment(const PointF& p, const PointF& a, const PointF& b) {
    const PointF ab = b - a;
    const float lenSq = LengthSq(ab);
    if (lenSq <= std::numeric_limits<float>::epsilon())
        return Distance(p, a);
    float t = Dot(p - a, ab) / lenSq;
    t = std::clamp(t, 0.0f, 1.0f);
    return Distance(p, a + ab * t);
}

struct RectF {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
    bool IsEmpty() const { return right <= left || bottom <= top; }

    void ExpandToInclude(const PointF& p) {
        left = std::min(left, p.x);
        top = std::min(top, p.y);
        right = std::max(right, p.x);
        bottom = std::max(bottom, p.y);
    }

    void Inflate(float margin) {
        left -= margin; top -= margin;
        right += margin; bottom += margin;
    }

    bool Contains(const PointF& p) const {
        return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
    }
};

// 폴리라인 집합에서 AABB 계산. 비어 있으면 IsEmpty()인 RectF 반환.
RectF BoundsOf(const std::vector<std::vector<PointF>>& polylines);

// 폴리라인 총 길이
float PolylineLength(const std::vector<PointF>& pts);

} // namespace sm
