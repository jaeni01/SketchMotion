#include "MarkerDetector.h"
#include <algorithm>
#include <cmath>
#include "ContourTracer.h"
#include "Filters.h"
#include "FrameTransform.h"
#include "PathSimplify.h"

namespace sm {

namespace {

// 페이로드 좌표: (row, col) 0..3. 모서리 앵커 4셀.
constexpr int kAnchor[4][2] = { {0,0}, {0,3}, {3,3}, {3,0} }; // (0,0)만 흰색

bool IsAnchorCell(int r, int c) {
    for (const auto& a : kAnchor)
        if (a[0] == r && a[1] == c)
            return true;
    return false;
}

uint8_t ParityOf(uint8_t id) {
    return static_cast<uint8_t>((id ^ (id >> 4)) & 0xF);
}

// 4x4 비트 그리드를 시계방향 90도 회전
std::array<uint8_t, 16> Rotate90(const std::array<uint8_t, 16>& b) {
    std::array<uint8_t, 16> r{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            r[x * 4 + (3 - y)] = b[y * 4 + x];
    return r;
}

// 앵커 검사 + 12비트 추출 → id 복원 (패리티 불일치 시 nullopt)
std::optional<int> DecodeBits(std::array<uint8_t, 16> bits) {
    for (int rot = 0; rot < 4; ++rot) {
        if (bits[0] == 1 && bits[3] == 0 && bits[15] == 0 && bits[12] == 0) {
            uint16_t data = 0;
            int n = 0;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    if (!IsAnchorCell(r, c))
                        data |= static_cast<uint16_t>(bits[r * 4 + c] & 1) << n++;
            const uint8_t id = static_cast<uint8_t>(data & 0xFF);
            const uint8_t parity = static_cast<uint8_t>((data >> 8) & 0xF);
            if (parity == ParityOf(id))
                return id | (rot << 8); // 상위에 회전 수 전달
            return std::nullopt;
        }
        bits = Rotate90(bits);
    }
    return std::nullopt;
}

double PolygonArea(const std::vector<PointF>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const auto& u = p[i];
        const auto& v = p[(i + 1) % p.size()];
        a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
    }
    return a * 0.5;
}

} // namespace

std::array<uint8_t, 16> MakeTagBits(uint8_t id) {
    std::array<uint8_t, 16> bits{};
    bits[0] = 1; // (0,0) 흰 앵커, 나머지 앵커는 0 유지
    const uint16_t data = static_cast<uint16_t>(id) | (static_cast<uint16_t>(ParityOf(id)) << 8);
    int n = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!IsAnchorCell(r, c))
                bits[r * 4 + c] = (data >> n++) & 1;
    return bits;
}

GrayImage RenderTag(uint8_t id, int cellPx, int quietCells) {
    const auto bits = MakeTagBits(id);
    const int cells = 6 + 2 * quietCells;
    GrayImage img(cells * cellPx, cells * cellPx, 255);
    const auto fill = [&](int cx, int cy, uint8_t v) {
        for (int y = 0; y < cellPx; ++y)
            for (int x = 0; x < cellPx; ++x)
                img.Set((cx + quietCells) * cellPx + x, (cy + quietCells) * cellPx + y, v);
    };
    for (int cy = 0; cy < 6; ++cy)
        for (int cx = 0; cx < 6; ++cx) {
            const bool border = cx == 0 || cy == 0 || cx == 5 || cy == 5;
            uint8_t v = 0;
            if (!border)
                v = bits[(cy - 1) * 4 + (cx - 1)] ? 255 : 0;
            fill(cx, cy, border ? 0 : v);
        }
    return img;
}

std::vector<MarkerDetection> DetectMarkers(const GrayImage& gray,
                                           const MarkerDetectOptions& opt) {
    std::vector<MarkerDetection> out;
    if (gray.Empty())
        return out;

    const GrayImage binary = AdaptiveThresholdDark(gray, opt.adaptiveTile);

    ContourOptions copt;
    copt.minPoints = opt.minPerimeterPx;
    const auto contours = TraceContours(binary, copt);

    for (const auto& contour : contours) {
        // 폐곡선 → 사각형 근사
        std::vector<PointF> closed = contour;
        closed.push_back(contour.front());
        const double perim = PolylineLength(closed);
        auto poly = SimplifyRdp(closed, static_cast<float>(perim * 0.03));
        if (!poly.empty() && poly.front() == poly.back())
            poly.pop_back();
        if (poly.size() != 4)
            continue;
        const double area = std::abs(PolygonArea(poly));
        if (area < perim) // 지나치게 홀쭉한 후보 제거
            continue;

        // 시계방향으로 정렬 (이미지 좌표계: y 아래)
        if (PolygonArea(poly) < 0)
            std::reverse(poly.begin(), poly.end());

        // 코너 → 태그 셀 좌표계(0..6) 호모그래피
        const std::vector<PointF> tagCorners = { {0,0}, {6,0}, {6,6}, {0,6} };
        const std::vector<PointF> imgCorners(poly.begin(), poly.end());
        const auto h = Homography::Fit(tagCorners, imgCorners);
        if (!h)
            continue;

        // 셀 샘플링 (각 셀 중앙 3x3)
        const auto sampleCell = [&](double cx, double cy) -> double {
            int dark = 0, total = 0;
            for (int sy = -1; sy <= 1; ++sy)
                for (int sx = -1; sx <= 1; ++sx) {
                    const PointF p = h->Apply({ static_cast<float>(cx + 0.5 + sx * 0.22),
                                                static_cast<float>(cy + 0.5 + sy * 0.22) });
                    const int px = static_cast<int>(std::lround(p.x));
                    const int py = static_cast<int>(std::lround(p.y));
                    if (px < 0 || py < 0 || px >= binary.Width() || py >= binary.Height())
                        return 1.0; // 화면 밖 → 검정 취급 → 기각 유도
                    dark += binary.At(px, py) ? 1 : 0;
                    ++total;
                }
            return total ? static_cast<double>(dark) / total : 1.0;
        };

        // 테두리 링 검증 (20셀 중 85% 이상 검정)
        int borderDark = 0, borderTotal = 0;
        for (int i = 0; i < 6; ++i)
            for (const auto& [cx, cy] : { std::pair{i,0}, {i,5}, {0,i}, {5,i} }) {
                borderDark += sampleCell(cx, cy) > 0.5 ? 1 : 0;
                ++borderTotal;
            }
        if (borderDark < borderTotal * 0.85)
            continue;

        // 페이로드 4x4
        std::array<uint8_t, 16> bits{};
        bool ok = true;
        for (int r = 0; r < 4 && ok; ++r)
            for (int c = 0; c < 4; ++c) {
                const double darkRatio = sampleCell(c + 1, r + 1);
                if (darkRatio > 0.65) bits[r * 4 + c] = 0;
                else if (darkRatio < opt.maxCellDarkRatio) bits[r * 4 + c] = 1;
                else { ok = false; break; } // 애매한 셀 → 기각
            }
        if (!ok)
            continue;

        const auto decoded = DecodeBits(bits);
        if (!decoded)
            continue;

        MarkerDetection det;
        det.id = *decoded & 0xFF;
        const int rot = (*decoded >> 8) & 3;
        // 회전 수만큼 코너 순서를 돌려 corners[0]이 항상 흰 앵커 쪽이 되게
        for (int i = 0; i < 4; ++i)
            det.corners[i] = imgCorners[(i + rot) % 4];
        det.center = { (det.corners[0].x + det.corners[1].x + det.corners[2].x + det.corners[3].x) / 4.0f,
                       (det.corners[0].y + det.corners[1].y + det.corners[2].y + det.corners[3].y) / 4.0f };
        det.angle = std::atan2(det.corners[1].y - det.corners[0].y,
                               det.corners[1].x - det.corners[0].x);
        out.push_back(det);
    }
    return out;
}

} // namespace sm
