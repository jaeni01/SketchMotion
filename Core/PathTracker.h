#pragma once
// PathTracker.h - 홀로노믹(메카넘) 경로추종: 캐럿(lookahead) 목표점 P 추종, 헤딩 독립 제어
// 최근접점+접선 피드포워드 방식은 코너 통과 후 평형 데드락이 생겨 캐럿 방식을 채택 (테스트로 재현·검증됨)
// ESP32 펌웨어(agv_esp32.ino)에 동일 로직이 포팅된다 - 여기 단위테스트가 원본.
#include <array>
#include <vector>
#include "Geometry.h"

namespace sm {

struct TrackerParams {
    double lookahead = 0.15;  // 캐럿(목표점) 선행 거리 [m] — 코너 데드락 방지의 핵심
    double kPos = 1.8;        // 캐럿 오차 P 이득 [1/s]
    double kTheta = 2.5;      // 헤딩 오차 P 이득 [1/s]
    double vMax = 0.30;       // 속도 상한 [m/s]
    double omegaMax = 1.8;    // [rad/s]
    double arriveRadius = 0.03; // 최종점 도달 판정 [m]
};

struct TrackerCmd {
    double vx = 0, vy = 0, omega = 0; // 바디 프레임
    bool done = false;
    double crossTrackError = 0;       // 진단용 [m]
};

class PathTracker {
public:
    explicit PathTracker(const TrackerParams& p = {});

    // path: {floor} 좌표(m). 각 웨이포인트에서 다음 웨이포인트를 향해 헤딩 정렬.
    void SetPath(std::vector<PointF> path);
    bool HasPath() const { return m_path.size() >= 2; }

    // 현재 추정 자세 → 속도 명령 (바디 프레임)
    TrackerCmd Step(double x, double y, double theta);

    // 진행 상황 (0..1)
    double Progress() const;

private:
    // 경로상 최근접점과 그 지점의 누적 거리/접선 계산
    void Nearest(double x, double y, PointF& closest, PointF& tangent, double& along) const;
    // 누적 거리 s 지점의 경로 위 점/접선
    PointF PointAt(double s) const;
    PointF TangentAt(double s) const;

    TrackerParams m_p;
    std::vector<PointF> m_path;
    std::vector<double> m_cum;  // 누적 거리
    double m_total = 0;
    double m_along = 0;         // 단조 진행 (역행 방지)
};

// 메카넘 역기구학 (X 배치): (vx,vy,omega) -> 바퀴 각속도 4개 [rad/s]
// 순서: FL, FR, RL, RR. L=반휠베이스, W=반트레드, r=바퀴반지름 [m]
std::array<double, 4> MecanumWheelSpeeds(double vx, double vy, double omega,
                                         double L, double W, double r);

} // namespace sm
