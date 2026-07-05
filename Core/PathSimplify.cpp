#include "PathSimplify.h"

namespace sm {

namespace {

// 재귀 대신 명시적 스택 사용 (긴 윤곽에서 스택 오버플로 방지)
void RdpKeepFlags(const std::vector<PointF>& pts, float epsilon,
                  std::vector<uint8_t>& keep) {
    std::vector<std::pair<size_t, size_t>> stack;
    stack.emplace_back(0, pts.size() - 1);

    while (!stack.empty()) {
        const auto [first, last] = stack.back();
        stack.pop_back();
        if (last <= first + 1)
            continue;

        float maxDist = 0.0f;
        size_t maxIdx = first;
        for (size_t i = first + 1; i < last; ++i) {
            const float d = DistanceToSegment(pts[i], pts[first], pts[last]);
            if (d > maxDist) {
                maxDist = d;
                maxIdx = i;
            }
        }
        if (maxDist > epsilon) {
            keep[maxIdx] = 1;
            stack.emplace_back(first, maxIdx);
            stack.emplace_back(maxIdx, last);
        }
    }
}

} // namespace

std::vector<PointF> SimplifyRdp(const std::vector<PointF>& pts, float epsilon) {
    if (pts.size() <= 2 || epsilon <= 0.0f)
        return pts;

    std::vector<uint8_t> keep(pts.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    RdpKeepFlags(pts, epsilon, keep);

    std::vector<PointF> out;
    out.reserve(pts.size() / 2);
    for (size_t i = 0; i < pts.size(); ++i)
        if (keep[i])
            out.push_back(pts[i]);
    return out;
}

std::vector<std::vector<PointF>> SimplifyAll(const std::vector<std::vector<PointF>>& lines,
                                             float epsilon) {
    std::vector<std::vector<PointF>> out;
    out.reserve(lines.size());
    for (const auto& line : lines) {
        auto s = SimplifyRdp(line, epsilon);
        if (s.size() >= 2)
            out.push_back(std::move(s));
    }
    return out;
}

} // namespace sm
