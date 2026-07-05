#include "KalmanFilter.h"
#include <cmath>

namespace sm {

namespace {
constexpr int N = AgvEkf::N;

// row-major NxN 곱 헬퍼 (작은 고정 크기라 단순 3중 루프)
void MatMulNN(const double* A, const double* B, double* C) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            double s = 0;
            for (int k = 0; k < N; ++k)
                s += A[i * N + k] * B[k * N + j];
            C[i * N + j] = s;
        }
}

bool Invert3x3(const double m[9], double out[9]) {
    const double det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-15)
        return false;
    out[0] = (m[4] * m[8] - m[5] * m[7]) / det;
    out[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    out[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    out[3] = (m[5] * m[6] - m[3] * m[8]) / det;
    out[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    out[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    out[6] = (m[3] * m[7] - m[4] * m[6]) / det;
    out[7] = (m[1] * m[6] - m[0] * m[7]) / det;
    out[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return true;
}
} // namespace

double AgvEkf::WrapAngle(double a) {
    while (a > 3.14159265358979323846) a -= 2 * 3.14159265358979323846;
    while (a <= -3.14159265358979323846) a += 2 * 3.14159265358979323846;
    return a;
}

AgvEkf::AgvEkf(const AgvEkfParams& p) : m_p(p) {}

void AgvEkf::Reset(double x, double y, double theta) {
    m_x = { x, y, WrapAngle(theta), 0, 0, 0 };
    m_P.fill(0.0);
    const double p0[N] = { 1e-4, 1e-4, 1e-3, 0.25, 0.25, 1.0 };
    for (int i = 0; i < N; ++i)
        m_P[i * N + i] = p0[i];
    m_init = true;
}

void AgvEkf::SymmetrizeP() {
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            const double v = 0.5 * (m_P[i * N + j] + m_P[j * N + i]);
            m_P[i * N + j] = v;
            m_P[j * N + i] = v;
        }
}

void AgvEkf::Predict(double dt) {
    if (!m_init || dt <= 0)
        return;
    const double th = m_x[2], vx = m_x[3], vy = m_x[4], om = m_x[5];
    const double c = std::cos(th), s = std::sin(th);

    // 상태 전파 (바디 속도 -> 월드)
    m_x[0] += (vx * c - vy * s) * dt;
    m_x[1] += (vx * s + vy * c) * dt;
    m_x[2] = WrapAngle(th + om * dt);

    // 야코비안 F
    std::array<double, N * N> F{};
    for (int i = 0; i < N; ++i)
        F[i * N + i] = 1.0;
    F[0 * N + 2] = (-vx * s - vy * c) * dt;
    F[0 * N + 3] = c * dt;
    F[0 * N + 4] = -s * dt;
    F[1 * N + 2] = (vx * c - vy * s) * dt;
    F[1 * N + 3] = s * dt;
    F[1 * N + 4] = c * dt;
    F[2 * N + 5] = dt;

    // P = F P F' + Q dt
    std::array<double, N * N> T{}, P2{};
    MatMulNN(F.data(), m_P.data(), T.data());
    // F' 곱: P2 = T * F'
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            double sum = 0;
            for (int k = 0; k < N; ++k)
                sum += T[i * N + k] * F[j * N + k];
            P2[i * N + j] = sum;
        }
    m_P = P2;
    const double q[N] = { m_p.qPos, m_p.qPos, m_p.qTheta, m_p.qVel, m_p.qVel, m_p.qOmega };
    for (int i = 0; i < N; ++i)
        m_P[i * N + i] += q[i] * dt;
    SymmetrizeP();
}

std::optional<double> AgvEkf::Update(double zx, double zy, double ztheta) {
    if (!m_init) {
        Reset(zx, zy, ztheta);
        return std::nullopt;
    }
    // H = [I3 | 0], 혁신 (각도 랩)
    const double inno[3] = { zx - m_x[0], zy - m_x[1], WrapAngle(ztheta - m_x[2]) };

    // S = H P H' + R  (= P 좌상단 3x3 + R)
    double S[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            S[i * 3 + j] = m_P[i * N + j];
    S[0] += m_p.rPos; S[4] += m_p.rPos; S[8] += m_p.rTheta;

    double Sinv[9];
    if (!Invert3x3(S, Sinv))
        return std::nullopt;

    // K = P H' S^-1  (N x 3) ; P H' = P의 앞 3열
    double K[N * 3];
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k)
                s += m_P[i * N + k] * Sinv[k * 3 + j];
            K[i * 3 + j] = s;
        }

    // 상태 갱신
    for (int i = 0; i < N; ++i)
        m_x[i] += K[i * 3 + 0] * inno[0] + K[i * 3 + 1] * inno[1] + K[i * 3 + 2] * inno[2];
    m_x[2] = WrapAngle(m_x[2]);

    // 조셉 형식: P = (I-KH) P (I-KH)' + K R K'
    std::array<double, N * N> IKH{};
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            IKH[i * N + j] = (i == j ? 1.0 : 0.0) - (j < 3 ? K[i * 3 + j] : 0.0);
    std::array<double, N * N> T{}, P2{};
    MatMulNN(IKH.data(), m_P.data(), T.data());
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            double s = 0;
            for (int k = 0; k < N; ++k)
                s += T[i * N + k] * IKH[j * N + k];
            P2[i * N + j] = s;
        }
    const double r[3] = { m_p.rPos, m_p.rPos, m_p.rTheta };
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k)
                s += K[i * 3 + k] * r[k] * K[j * 3 + k];
            P2[i * N + j] += s;
        }
    m_P = P2;
    SymmetrizeP();

    // NIS
    double nis = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            nis += inno[i] * Sinv[i * 3 + j] * inno[j];
    return nis;
}

std::array<double, AgvEkf::N> AgvEkf::PredictAhead(double dt) const {
    auto x = m_x;
    const double c = std::cos(x[2]), s = std::sin(x[2]);
    x[0] += (x[3] * c - x[4] * s) * dt;
    x[1] += (x[3] * s + x[4] * c) * dt;
    x[2] = WrapAngle(x[2] + x[5] * dt);
    return x;
}

} // namespace sm
