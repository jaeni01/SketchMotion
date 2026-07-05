#pragma once
// KalmanFilter.h - AGV용 6상태 EKF (메카넘/홀로노믹)
// 상태 x = [x, y, theta, vx, vy, omega]  (위치 m, 각 rad, 속도는 바디 프레임)
// 측정 z = [x_m, y_m, theta_m]  (SM-Tag 자세, {floor})
//
// 설계 근거(DESIGN_V2 §7.1): 조셉 형식 갱신 + 대칭화, 각도 랩어라운드,
// NIS 반환으로 혁신 일관성 검정 가능.
#include <array>
#include <optional>

namespace sm {

struct AgvEkfParams {
    // 프로세스 잡음 (연속 시간 강도 → predict에서 dt 곱)
    double qPos = 1e-4;     // 위치 확산 (모델 외 슬립) [m^2/s]
    double qTheta = 1e-3;   // [rad^2/s]
    double qVel = 0.25;     // 바디 속도 랜덤워크 [(m/s)^2/s]
    double qOmega = 0.8;    // [(rad/s)^2/s]
    // 측정 잡음 (마커 자세)
    double rPos = 4e-6;     // (2mm)^2
    double rTheta = 3e-4;   // (~1도)^2
};

class AgvEkf {
public:
    static constexpr int N = 6;

    explicit AgvEkf(const AgvEkfParams& p = {});

    // 초기화: 첫 측정으로 위치·각 설정, 속도 0, 큰 초기 공분산
    void Reset(double x, double y, double theta);
    bool Initialized() const { return m_init; }

    void Predict(double dt);

    // 측정 갱신. 반환: NIS (혁신' S^-1 혁신, chi2(3) 기대). 실패(S 특이) 시 nullopt.
    std::optional<double> Update(double zx, double zy, double ztheta);

    // 지연 보상용: 현재 추정을 dt만큼 전진시킨 사본의 [x,y,theta,vx,vy,omega]
    std::array<double, N> PredictAhead(double dt) const;

    const std::array<double, N>& State() const { return m_x; }
    const std::array<double, N * N>& Cov() const { return m_P; }

    static double WrapAngle(double a); // (-pi, pi]

private:
    void SymmetrizeP();

    AgvEkfParams m_p;
    std::array<double, N> m_x{};
    std::array<double, N * N> m_P{};
    bool m_init = false;
};

} // namespace sm
