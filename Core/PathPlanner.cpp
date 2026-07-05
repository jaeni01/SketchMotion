#include "PathPlanner.h"
#include <algorithm>

namespace sm {

PlannedPath PlanDrawingOrder(const std::vector<std::vector<PointF>>& polylines,
                             const PointF& start) {
    PlannedPath result;

    std::vector<std::vector<PointF>> remaining;
    for (const auto& line : polylines)
        if (line.size() >= 2)
            remaining.push_back(line);

    PointF pen = start;
    while (!remaining.empty()) {
        size_t bestIdx = 0;
        bool bestReversed = false;
        float bestDist = std::numeric_limits<float>::max();

        for (size_t i = 0; i < remaining.size(); ++i) {
            const float dFront = Distance(pen, remaining[i].front());
            const float dBack = Distance(pen, remaining[i].back());
            if (dFront < bestDist) {
                bestDist = dFront;
                bestIdx = i;
                bestReversed = false;
            }
            if (dBack < bestDist) {
                bestDist = dBack;
                bestIdx = i;
                bestReversed = true;
            }
        }

        std::vector<PointF> chosen = std::move(remaining[bestIdx]);
        remaining.erase(remaining.begin() + bestIdx);
        if (bestReversed)
            std::reverse(chosen.begin(), chosen.end());

        result.stats.travelDistance += bestDist;
        result.stats.drawDistance += PolylineLength(chosen);
        pen = chosen.back();
        result.paths.push_back(std::move(chosen));
    }

    result.stats.pathCount = result.paths.size();
    return result;
}

float TravelDistanceOf(const std::vector<std::vector<PointF>>& polylines,
                       const PointF& start) {
    float travel = 0.0f;
    PointF pen = start;
    for (const auto& line : polylines) {
        if (line.size() < 2)
            continue;
        travel += Distance(pen, line.front());
        pen = line.back();
    }
    return travel;
}

} // namespace sm
