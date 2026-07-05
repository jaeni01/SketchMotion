#include "FrameTransform.h"
#include <cmath>

namespace sm {

namespace {

// 부분 피벗 가우스 소거로 A x = b 풀기 (n<=8). 특이 시 false.
bool SolveLinear(std::vector<double>& A, std::vector<double>& b, int n) {
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int r = col + 1; r < n; ++r)
            if (std::abs(A[r * n + col]) > std::abs(A[pivot * n + col]))
                pivot = r;
        if (std::abs(A[pivot * n + col]) < 1e-12)
            return false;
        if (pivot != col) {
            for (int c = 0; c < n; ++c)
                std::swap(A[col * n + c], A[pivot * n + c]);
            std::swap(b[col], b[pivot]);
        }
        const double d = A[col * n + col];
        for (int r = col + 1; r < n; ++r) {
            const double f = A[r * n + col] / d;
            if (f == 0.0) continue;
            for (int c = col; c < n; ++c)
                A[r * n + c] -= f * A[col * n + c];
            b[r] -= f * b[col];
        }
    }
    for (int col = n - 1; col >= 0; --col) {
        double s = b[col];
        for (int c = col + 1; c < n; ++c)
            s -= A[col * n + c] * b[c];
        b[col] = s / A[col * n + col];
    }
    return true;
}

} // namespace

std::optional<Homography> Homography::Fit(const std::vector<PointF>& src,
                                          const std::vector<PointF>& dst) {
    const size_t n = src.size();
    if (n < 4 || dst.size() != n)
        return std::nullopt;

    // DLT: 각 대응점이 2개 식 제공. h33=1 고정, 미지수 8개.
    // 5+점이면 정규방정식 (AtA)h = At b 로 최소제곱.
    std::vector<double> AtA(64, 0.0), Atb(8, 0.0);
    for (size_t i = 0; i < n; ++i) {
        const double x = src[i].x, y = src[i].y;
        const double u = dst[i].x, v = dst[i].y;
        const double r1[8] = { x, y, 1, 0, 0, 0, -u * x, -u * y };
        const double r2[8] = { 0, 0, 0, x, y, 1, -v * x, -v * y };
        for (int a = 0; a < 8; ++a) {
            for (int c = 0; c < 8; ++c)
                AtA[a * 8 + c] += r1[a] * r1[c] + r2[a] * r2[c];
            Atb[a] += r1[a] * u + r2[a] * v;
        }
    }
    if (!SolveLinear(AtA, Atb, 8))
        return std::nullopt;

    Homography h;
    for (int i = 0; i < 8; ++i)
        h.m_h[i] = Atb[i];
    h.m_h[8] = 1.0;
    return h;
}

PointF Homography::Apply(const PointF& p) const {
    const double w = m_h[6] * p.x + m_h[7] * p.y + m_h[8];
    const double u = (m_h[0] * p.x + m_h[1] * p.y + m_h[2]) / w;
    const double v = (m_h[3] * p.x + m_h[4] * p.y + m_h[5]) / w;
    return { static_cast<float>(u), static_cast<float>(v) };
}

std::optional<Homography> Homography::Inverse() const {
    // 3x3 역행렬 (여인수)
    const auto& h = m_h;
    const double det =
        h[0] * (h[4] * h[8] - h[5] * h[7]) -
        h[1] * (h[3] * h[8] - h[5] * h[6]) +
        h[2] * (h[3] * h[7] - h[4] * h[6]);
    if (std::abs(det) < 1e-14)
        return std::nullopt;
    std::array<double, 9> inv{
        (h[4] * h[8] - h[5] * h[7]) / det, (h[2] * h[7] - h[1] * h[8]) / det, (h[1] * h[5] - h[2] * h[4]) / det,
        (h[5] * h[6] - h[3] * h[8]) / det, (h[0] * h[8] - h[2] * h[6]) / det, (h[2] * h[3] - h[0] * h[5]) / det,
        (h[3] * h[7] - h[4] * h[6]) / det, (h[1] * h[6] - h[0] * h[7]) / det, (h[0] * h[4] - h[1] * h[3]) / det,
    };
    // h33 = 1 로 정규화
    if (std::abs(inv[8]) < 1e-14)
        return std::nullopt;
    for (auto& v : inv)
        v /= inv[8];
    return FromArray(inv);
}

PointF RigidTransform2D::Apply(const PointF& p) const {
    const double c = std::cos(theta), s = std::sin(theta);
    return { static_cast<float>(scale * (c * p.x - s * p.y) + tx),
             static_cast<float>(scale * (s * p.x + c * p.y) + ty) };
}

RigidTransform2D RigidTransform2D::Inverse() const {
    RigidTransform2D r;
    r.scale = 1.0 / scale;
    r.theta = -theta;
    const double c = std::cos(r.theta), s = std::sin(r.theta);
    r.tx = -r.scale * (c * tx - s * ty);
    r.ty = -r.scale * (s * tx + c * ty);
    return r;
}

std::optional<RigidTransform2D> RigidTransform2D::Fit(const std::vector<PointF>& src,
                                                      const std::vector<PointF>& dst,
                                                      bool withScale) {
    const size_t n = src.size();
    if (n < 2 || dst.size() != n)
        return std::nullopt;

    // 중심화 후 2D Procrustes: theta = atan2(Σ cross, Σ dot)
    double sx = 0, sy = 0, dx = 0, dy = 0;
    for (size_t i = 0; i < n; ++i) {
        sx += src[i].x; sy += src[i].y;
        dx += dst[i].x; dy += dst[i].y;
    }
    sx /= n; sy /= n; dx /= n; dy /= n;

    double sumDot = 0, sumCross = 0, sumSrcSq = 0;
    for (size_t i = 0; i < n; ++i) {
        const double ax = src[i].x - sx, ay = src[i].y - sy;
        const double bx = dst[i].x - dx, by = dst[i].y - dy;
        sumDot += ax * bx + ay * by;
        sumCross += ax * by - ay * bx;
        sumSrcSq += ax * ax + ay * ay;
    }
    if (sumSrcSq < 1e-12)
        return std::nullopt;

    RigidTransform2D t;
    t.theta = std::atan2(sumCross, sumDot);
    t.scale = withScale ? std::sqrt(sumDot * sumDot + sumCross * sumCross) / sumSrcSq : 1.0;
    const double c = std::cos(t.theta), s = std::sin(t.theta);
    t.tx = dx - t.scale * (c * sx - s * sy);
    t.ty = dy - t.scale * (s * sx + c * sy);
    return t;
}

} // namespace sm
