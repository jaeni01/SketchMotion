#pragma once
// EmgProcessor.h - 표면 근전(sEMG) 신호 처리: 엔벌로프 + 1D 칼만 + 비례/제스처 매핑
//
// 파이프라인 (BCI 아카이브 08번 EMG 데모 v1~v2를 Core 알고리즘으로):
//   raw ADC → DC 제거(고역) → 정류 → 저역 엔벌로프 → 1D 칼만 상태추정
//           → MVC 정규화 → 데드존 → 비례 출력(0..1) + 임계 제스처(수축/이완)
//
// 이력서 한 줄(08번): "sEMG를 실시간 디코딩해 로봇 그리퍼를 비례 제어 —
//   전처리 + 상태추정(칼만) + 제어(P/데드존/EMA)."
// IBVS 프로젝트에서 쓴 데드존·EMA 스택을 생체신호로 이식한 것. MFC/HW 무의존.
#include <cstdint>

namespace sm {

struct EmgParams {
    double sampleRateHz = 1000.0; // ADC 샘플레이트
    double hpCutoffHz = 20.0;     // DC/모션 드리프트 제거 고역 (1차)
    double lpCutoffHz = 4.0;      // 엔벌로프 저역 (1차, 정류 후)
    // 1D 칼만 (엔벌로프 상태추정): 상수속도 아님, 랜덤워크 레벨 모델
    double qEnv = 4.0e-3;         // 프로세스 잡음 (엔벌로프 변화 허용) [/s]
    double rEnv = 2.0e-3;         // 측정 잡음 (엔벌로프 관측 분산)
    // 매핑
    double deadzone = 0.08;       // MVC 정규화 후 데드존 (0..1)
    double onThreshold = 0.30;    // 제스처 '수축' 진입 (히스테리시스 상단)
    double offThreshold = 0.18;   // 제스처 '이완' 복귀 (히스테리시스 하단)
};

struct EmgOutput {
    double envelope = 0.0;    // 칼만 추정 엔벌로프 (원 단위, MVC 정규화 전)
    double normalized = 0.0;  // MVC 정규화 (0..1, 클램프)
    double proportional = 0.0;// 데드존 적용 후 비례 출력 (0..1)
    bool contracted = false;  // 히스테리시스 제스처 상태
};

class EmgProcessor {
public:
    explicit EmgProcessor(const EmgParams& p = {});

    void Reset();

    // 원 ADC 샘플 1개 투입 → 처리된 출력. sampleRateHz 주기로 호출 가정.
    EmgOutput Push(double rawSample);

    // MVC(최대 수의 수축) 캘리브레이션: 현재 엔벌로프를 100% 기준으로 등록.
    // 캘리브레이션 창 동안의 최대 엔벌로프를 넘겨 호출한다.
    void SetMvc(double mvcEnvelope);
    double Mvc() const { return m_mvc; }

    // 현재 칼만 엔벌로프 (정규화 전)
    double Envelope() const { return m_env; }

private:
    EmgParams m_p;
    // 1차 IIR 계수 (초기화에서 계산)
    double m_hpAlpha = 0.0;  // 고역
    double m_lpAlpha = 0.0;  // 저역
    // 필터 상태
    double m_hpPrevIn = 0.0, m_hpPrevOut = 0.0;
    double m_lpState = 0.0;
    // 칼만 (스칼라)
    double m_env = 0.0, m_envP = 1.0;
    double m_mvc = 1.0;
    bool m_contracted = false;
    bool m_init = false;
};

} // namespace sm
