#pragma once
// FrameTransform.h - 좌표계 변환: 평면 호모그래피(DLT) + 2D 강체 정합(Procrustes/Kabsch)
#include <array>
#include <optional>
#include <vector>
#include "Geometry.h"

namespace sm {

// 3x3 호모그래피 (row-major). 픽셀 평면 <-> 실세계 평면(mm/m) 매핑.
class Homography {
public:
    // 대응점 4개 이상으로 추정 (정확히 4개면 DLT 정해, 5+는 최소제곱).
    // 실패(퇴화 배치) 시 nullopt.
    static std::optional<Homography> Fit(const std::vector<PointF>& src,
                                         const std::vector<PointF>& dst);

    PointF Apply(const PointF& p) const;
    std::optional<Homography> Inverse() const;

    const std::array<double, 9>& H() const { return m_h; }
    static Homography FromArray(const std::array<double, 9>& h) { Homography r; r.m_h = h; return r; }

private:
    std::array<double, 9> m_h{ 1,0,0, 0,1,0, 0,0,1 };
};

// 2D 강체 변환 p' = R(theta) * p + t  (옵션: 균일 스케일)
struct RigidTransform2D {
    double theta = 0.0;
    double tx = 0.0, ty = 0.0;
    double scale = 1.0;

    PointF Apply(const PointF& p) const;
    RigidTransform2D Inverse() const;

    // 대응점 2개 이상으로 최소제곱 추정 (Procrustes; withScale=false면 순수 강체)
    static std::optional<RigidTransform2D> Fit(const std::vector<PointF>& src,
                                               const std::vector<PointF>& dst,
                                               bool withScale = false);
};

} // namespace sm
