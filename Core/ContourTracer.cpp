#include "ContourTracer.h"
#include <array>

namespace sm {

namespace {

// Moore 이웃 8방향, 시계 방향 (동쪽부터)
constexpr std::array<int, 8> kDx = { 1, 1, 0, -1, -1, -1, 0, 1 };
constexpr std::array<int, 8> kDy = { 0, 1, 1, 1, 0, -1, -1, -1 };

} // namespace

std::vector<std::vector<PointF>> TraceContours(const GrayImage& binary,
                                               const ContourOptions& opt) {
    std::vector<std::vector<PointF>> contours;
    if (binary.Empty())
        return contours;

    const int w = binary.Width();
    const int h = binary.Height();

    const auto isOn = [&](int x, int y) {
        return x >= 0 && x < w && y >= 0 && y < h && binary.At(x, y) == opt.onValue;
    };

    // 윤곽에 이미 포함된 픽셀 표시 (같은 윤곽을 반복 추적하지 않도록)
    std::vector<uint8_t> traced(static_cast<size_t>(w) * h, 0);
    const auto mark  = [&](int x, int y) { traced[static_cast<size_t>(y) * w + x] = 1; };
    const auto seen  = [&](int x, int y) { return traced[static_cast<size_t>(y) * w + x] != 0; };

    // 래스터 스캔: 왼쪽이 배경인 전경 픽셀 = 외곽 윤곽 시작 후보
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!isOn(x, y) || seen(x, y) || isOn(x - 1, y))
                continue;

            // Moore-neighbor tracing (Jacob's stopping criterion)
            std::vector<PointF> contour;
            const int startX = x, startY = y;
            int cx = startX, cy = startY;
            int backtrack = 4; // 진입 방향: 서쪽(배경)에서 들어옴

            const size_t hardLimit = static_cast<size_t>(w) * h * 4;
            size_t steps = 0;
            do {
                contour.push_back({ static_cast<float>(cx), static_cast<float>(cy) });
                mark(cx, cy);

                // backtrack 방향의 다음 칸부터 시계 방향으로 첫 전경 픽셀 탐색
                bool found = false;
                for (int i = 1; i <= 8; ++i) {
                    const int dir = (backtrack + i) % 8;
                    const int nx = cx + kDx[dir];
                    const int ny = cy + kDy[dir];
                    if (isOn(nx, ny)) {
                        // 새 backtrack = 현재 픽셀을 가리키는 방향
                        backtrack = (dir + 4) % 8;
                        cx = nx;
                        cy = ny;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    break; // 고립 픽셀
                ++steps;
            } while ((cx != startX || cy != startY) && steps < hardLimit);

            if (static_cast<int>(contour.size()) >= opt.minPoints)
                contours.push_back(std::move(contour));

            if (static_cast<int>(contours.size()) >= opt.maxContours)
                return contours;
        }
    }
    return contours;
}

} // namespace sm
