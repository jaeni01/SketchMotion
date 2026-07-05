#include "PathTracker.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {
double Wrap(double a) {
    while (a > 3.14159265358979323846) a -= 2 * 3.14159265358979323846;
    while (a <= -3.14159265358979323846) a += 2 * 3.14159265358979323846;
    return a;
}
} // namespace

PathTracker::PathTracker(const TrackerParams& p) : m_p(p) {}

void PathTracker::SetPath(std::vector<PointF> path) {
    m_path = std::move(path);
    m_cum.assign(m_path.size(), 0.0);
    for (size_t i = 1; i < m_path.size(); ++i)
        m_cum[i] = m_cum[i - 1] + Distance(m_path[i - 1], m_path[i]);
    m_total = m_cum.empty() ? 0.0 : m_cum.back();
    m_along = 0;
}

void PathTracker::Nearest(double x, double y, PointF& closest, PointF& tangent, double& along) const {
    const PointF p{ static_cast<float>(x), static_cast<float>(y) };
    double best = 1e18;
    closest = m_path.front();
    tangent = { 1, 0 };
    along = 0;
    for (size_t i = 1; i < m_path.size(); ++i) {
        const PointF a = m_path[i - 1], b = m_path[i];
        const PointF ab = b - a;
        const float lenSq = LengthSq(ab);
        float t = lenSq > 1e-12f ? Dot(p - a, ab) / lenSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const PointF c = a + ab * t;
        const double d = Distance(p, c);
        if (d < best) {
            best = d;
            closest = c;
            const float len = std::sqrt(lenSq);
            tangent = len > 1e-6f ? PointF{ ab.x / len, ab.y / len } : PointF{ 1, 0 };
            along = m_cum[i - 1] + t * len;
        }
    }
}

PointF PathTracker::PointAt(double s) const {
    s = std::clamp(s, 0.0, m_total);
    for (size_t i = 1; i < m_path.size(); ++i) {
        if (s <= m_cum[i] || i == m_path.size() - 1) {
            const double segLen = m_cum[i] - m_cum[i - 1];
            const double t = segLen > 1e-12 ? (s - m_cum[i - 1]) / segLen : 0.0;
            const PointF a = m_path[i - 1], b = m_path[i];
            return { static_cast<float>(a.x + (b.x - a.x) * t),
                     static_cast<float>(a.y + (b.y - a.y) * t) };
        }
    }
    return m_path.back();
}

PointF PathTracker::TangentAt(double s) const {
    s = std::clamp(s, 0.0, m_total);
    for (size_t i = 1; i < m_path.size(); ++i) {
        if (s <= m_cum[i] || i == m_path.size() - 1) {
            const PointF ab = m_path[i] - m_path[i - 1];
            const float len = Length(ab);
            return len > 1e-6f ? PointF{ ab.x / len, ab.y / len } : PointF{ 1, 0 };
        }
    }
    return { 1, 0 };
}

TrackerCmd PathTracker::Step(double x, double y, double theta) {
    TrackerCmd cmd;
    if (!HasPath())
        return cmd;

    PointF closest, tangentNear;
    double along = 0;
    Nearest(x, y, closest, tangentNear, along);
    m_along = std::max(m_along, along); // 역행 방지 (진행 단조)
    cmd.crossTrackError = std::hypot(closest.x - x, closest.y - y);

    const PointF goal = m_path.back();
    const double dGoal = Distance({ static_cast<float>(x), static_cast<float>(y) }, goal);
    if (dGoal < m_p.arriveRadius && m_along > m_total - 3.0 * m_p.lookahead) {
        cmd.done = true;
        return cmd;
    }

    // 캐럿: 경로상 진행점 + lookahead. 코너에서 목표점이 먼저 돌아나가므로
    // 접선 피드포워드식의 코너 평형 데드락이 발생하지 않는다.
    const double carrotS = std::min(m_total, m_along + m_p.lookahead);
    const PointF carrot = PointAt(carrotS);

    // 월드 프레임 속도 = 캐럿 오차 P (종점에선 캐럿=goal이라 자연 감속)
    double wx = m_p.kPos * (carrot.x - x);
    double wy = m_p.kPos * (carrot.y - y);
    const double mag = std::hypot(wx, wy);
    if (mag > m_p.vMax) {
        wx *= m_p.vMax / mag;
        wy *= m_p.vMax / mag;
    }

    // 헤딩: 캐럿 지점의 경로 접선을 향해 독립 P 제어 (홀로노믹이라 위치와 분리)
    const PointF tan = TangentAt(carrotS);
    const double thetaRef = std::atan2(tan.y, tan.x);
    cmd.omega = std::clamp(m_p.kTheta * Wrap(thetaRef - theta), -m_p.omegaMax, m_p.omegaMax);

    // 월드 -> 바디 프레임
    const double c = std::cos(theta), s = std::sin(theta);
    cmd.vx = c * wx + s * wy;
    cmd.vy = -s * wx + c * wy;
    return cmd;
}

double PathTracker::Progress() const {
    return m_total > 1e-9 ? std::min(1.0, m_along / m_total) : 0.0;
}

std::array<double, 4> MecanumWheelSpeeds(double vx, double vy, double omega,
                                         double L, double W, double r) {
    const double k = (L + W) * omega;
    return {
        (vx - vy - k) / r,  // FL
        (vx + vy + k) / r,  // FR
        (vx + vy - k) / r,  // RL
        (vx - vy + k) / r,  // RR
    };
}

} // namespace sm
