#include "GCodeWriter.h"
#include <cstdio>
#include <sstream>

namespace sm {

namespace {

std::string Num(float v, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

} // namespace

std::string WriteGCode(const std::vector<std::vector<PointF>>& orderedPaths,
                       const RectF& bounds,
                       const GCodeOptions& opt) {
    std::ostringstream os;
    os << "; SketchMotion G-code export\n";
    os << "; paths=" << orderedPaths.size() << "\n";
    os << "G21 ; mm units\n";
    os << "G90 ; absolute positioning\n";

    if (bounds.IsEmpty() || orderedPaths.empty()) {
        os << "; (empty drawing)\n";
        os << "M2\n";
        return os.str();
    }

    const float scale = opt.workWidthMm / bounds.Width();
    const auto mapPoint = [&](const PointF& p) -> PointF {
        const float mx = (p.x - bounds.left) * scale;
        float my = (p.y - bounds.top) * scale;
        if (opt.flipY)
            my = bounds.Height() * scale - my;
        return { mx, my };
    };

    const std::string penUp   = "G1 Z" + Num(opt.penUpZ, opt.decimals)   + " F" + Num(opt.zFeedRate, 0);
    const std::string penDown = "G1 Z" + Num(opt.penDownZ, opt.decimals) + " F" + Num(opt.zFeedRate, 0);

    os << penUp << " ; pen up\n";
    os << "G0 X0 Y0\n";

    for (const auto& path : orderedPaths) {
        if (path.size() < 2)
            continue;
        const PointF first = mapPoint(path.front());
        os << "G0 X" << Num(first.x, opt.decimals)
           << " Y" << Num(first.y, opt.decimals)
           << " F" << Num(opt.feedRateTravel, 0) << "\n";
        os << penDown << " ; pen down\n";
        for (size_t i = 1; i < path.size(); ++i) {
            const PointF p = mapPoint(path[i]);
            os << "G1 X" << Num(p.x, opt.decimals)
               << " Y" << Num(p.y, opt.decimals)
               << " F" << Num(opt.feedRateDraw, 0) << "\n";
        }
        os << penUp << " ; pen up\n";
    }

    os << "G0 X0 Y0 ; home\n";
    os << "M2 ; end\n";
    return os.str();
}

} // namespace sm
