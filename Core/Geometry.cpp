#include "Geometry.h"

namespace sm {

RectF BoundsOf(const std::vector<std::vector<PointF>>& polylines) {
    RectF r;
    bool first = true;
    for (const auto& line : polylines) {
        for (const auto& p : line) {
            if (first) {
                r = { p.x, p.y, p.x, p.y };
                first = false;
            } else {
                r.ExpandToInclude(p);
            }
        }
    }
    return r;
}

float PolylineLength(const std::vector<PointF>& pts) {
    float total = 0.0f;
    for (size_t i = 1; i < pts.size(); ++i)
        total += Distance(pts[i - 1], pts[i]);
    return total;
}

} // namespace sm
