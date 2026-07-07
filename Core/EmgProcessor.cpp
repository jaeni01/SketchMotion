#include "EmgProcessor.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {
// 1차 저역 IIR의 스무딩 계수 (RC 근사): alpha = dt / (RC + dt)
double LowpassAlpha(double cutoffHz, double dt) {
    if (cutoffHz <= 0.0) return 1.0;
    const double rc = 1.0 / (2.0 * 3.14159265358979323846 * cutoffHz);
    return dt / (rc + dt);
}
// 1차 고역의 계수 (DC 제거): alpha = RC / (RC + dt)
double HighpassAlpha(double cutoffHz, double dt) {
    if (cutoffHz <= 0.0) return 1.0;
    const double rc = 1.0 / (2.0 * 3.14159265358979323846 * cutoffHz);
    return rc / (rc + dt);
}
} // namespace

EmgProcessor::EmgProcessor(const EmgParams& p) : m_p(p) {
    const double dt = 1.0 / std::max(1.0, m_p.sampleRateHz);
    m_hpAlpha = HighpassAlpha(m_p.hpCutoffHz, dt);
    m_lpAlpha = LowpassAlpha(m_p.lpCutoffHz, dt);
    Reset();
}

void EmgProcessor::Reset() {
    m_hpPrevIn = m_hpPrevOut = 0.0;
    m_lpState = 0.0;
    m_env = 0.0;
    m_envP = 1.0;
    m_contracted = false;
    m_init = false;
}

void EmgProcessor::SetMvc(double mvcEnvelope) {
    // 0 나눗셈·비정상 값 방지
    m_mvc = std::max(1e-6, mvcEnvelope);
}

EmgOutput EmgProcessor::Push(double rawSample) {
    // (1) 고역 필터로 DC/모션 드리프트 제거 (표준 1차 HPF 차분식)
    if (!m_init) {
        m_hpPrevIn = rawSample;
        m_hpPrevOut = 0.0;
        m_init = true;
    }
    const double hp = m_hpAlpha * (m_hpPrevOut + rawSample - m_hpPrevIn);
    m_hpPrevIn = rawSample;
    m_hpPrevOut = hp;

    // (2) 정류 (절댓값) → (3) 저역 엔벌로프 (EMA)
    const double rect = std::abs(hp);
    m_lpState += m_lpAlpha * (rect - m_lpState);

    // (4) 1D 칼만: 엔벌로프 레벨을 랜덤워크로 추정 (측정 = LPF 엔벌로프)
    //     예측: x 유지, P += Q ; 갱신: K = P/(P+R)
    const double dt = 1.0 / std::max(1.0, m_p.sampleRateHz);
    m_envP += m_p.qEnv * dt;
    const double K = m_envP / (m_envP + m_p.rEnv);
    m_env += K * (m_lpState - m_env);
    m_envP = (1.0 - K) * m_envP;
    if (m_env < 0.0) m_env = 0.0;

    // (5) MVC 정규화 → 데드존 → 비례
    EmgOutput out;
    out.envelope = m_env;
    out.normalized = std::clamp(m_env / m_mvc, 0.0, 1.0);
    if (out.normalized <= m_p.deadzone) {
        out.proportional = 0.0;
    } else {
        // 데드존 위를 0..1로 재스케일 (IBVS 데드존 스택 재사용)
        out.proportional = std::clamp(
            (out.normalized - m_p.deadzone) / (1.0 - m_p.deadzone), 0.0, 1.0);
    }

    // (6) 히스테리시스 제스처 (수축/이완 채터링 방지)
    if (!m_contracted && out.normalized >= m_p.onThreshold)
        m_contracted = true;
    else if (m_contracted && out.normalized <= m_p.offThreshold)
        m_contracted = false;
    out.contracted = m_contracted;

    return out;
}

} // namespace sm
