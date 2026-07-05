#pragma once
// PathSimplify.h - Ramer-Douglas-Peucker 폴리라인 단순화
#include <vector>
#include "Geometry.h"

namespace sm {

// epsilon: 허용 오차(px). 원본 순서를 유지하며 양 끝점은 항상 보존한다.
std::vector<PointF> SimplifyRdp(const std::vector<PointF>& pts, float epsilon);

std::vector<std::vector<PointF>> SimplifyAll(const std::vector<std::vector<PointF>>& lines,
                                             float epsilon);

} // namespace sm
