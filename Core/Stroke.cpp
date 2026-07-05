#include "Stroke.h"
#include <numbers>

namespace sm {

RectF Stroke::Bounds() const {
    RectF r = BoundsOf({ m_points });
    r.Inflate(m_width * 0.5f);
    return r;
}

std::vector<std::vector<PointF>> Stroke::ToPolylines(int ellipseSegments) const {
    if (m_points.size() < 2)
        return {};

    switch (m_kind) {
    case StrokeKind::Freehand:
    case StrokeKind::Line:
        return { m_points };

    case StrokeKind::Rect: {
        const PointF a = m_points.front();
        const PointF b = m_points.back();
        return { { a, { b.x, a.y }, b, { a.x, b.y }, a } };
    }

    case StrokeKind::Ellipse: {
        const PointF a = m_points.front();
        const PointF b = m_points.back();
        const PointF center{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
        const float rx = std::abs(b.x - a.x) * 0.5f;
        const float ry = std::abs(b.y - a.y) * 0.5f;
        std::vector<PointF> pts;
        pts.reserve(static_cast<size_t>(ellipseSegments) + 1);
        for (int i = 0; i <= ellipseSegments; ++i) {
            const float t = static_cast<float>(i) / ellipseSegments
                          * 2.0f * std::numbers::pi_v<float>;
            pts.push_back({ center.x + rx * std::cos(t), center.y + ry * std::sin(t) });
        }
        return { pts };
    }
    }
    return {};
}

bool Stroke::HitTest(const PointF& p, float tolerance) const {
    const float threshold = m_width * 0.5f + tolerance;
    for (const auto& line : ToPolylines()) {
        if (line.size() == 1) {
            if (Distance(p, line[0]) <= threshold)
                return true;
            continue;
        }
        for (size_t i = 1; i < line.size(); ++i)
            if (DistanceToSegment(p, line[i - 1], line[i]) <= threshold)
                return true;
    }
    return false;
}

} // namespace sm
