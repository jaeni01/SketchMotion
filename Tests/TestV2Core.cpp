// TestV2Core.cpp - v2 Core 모듈 테스트 (FrameTransform / EKF / PathTracker / MarkerDetector)
#include <cmath>
#include <cstdio>

#include "../Core/EmgProcessor.h"
#include "../Core/FrameTransform.h"
#include "../Core/KalmanFilter.h"
#include "../Core/MarkerDetector.h"
#include "../Core/PathTracker.h"

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        ++g_checks;                                                          \
        const double _a = (a), _b = (b);                                     \
        if (std::abs(_a - _b) > (eps)) {                                     \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", __FILE__, __LINE__,  \
                        #a, _a, #b, _b);                                     \
        }                                                                    \
    } while (0)

using namespace sm;

namespace {

// 결정론적 가우시안 (LCG + Box-Muller) - 테스트 재현성
struct Rng {
    uint64_t s = 88172645463325252ull;
    double Uniform() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<double>(s % 1000000007ull) / 1000000007.0;
    }
    double Gauss(double sigma) {
        const double u1 = std::max(1e-12, Uniform()), u2 = Uniform();
        return sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307 * u2);
    }
};

// ------------------------------------------------------------- Homography
void TestHomography() {
    // 단위 정사각 -> 원근 있는 사각형
    const std::vector<PointF> src = { {0,0}, {1,0}, {1,1}, {0,1} };
    const std::vector<PointF> dst = { {10,20}, {110,25}, {105,118}, {8,112} };
    const auto h = Homography::Fit(src, dst);
    CHECK(h.has_value());
    if (!h) return;
    for (size_t i = 0; i < 4; ++i) {
        const PointF p = h->Apply(src[i]);
        CHECK_NEAR(p.x, dst[i].x, 1e-3);
        CHECK_NEAR(p.y, dst[i].y, 1e-3);
    }
    // 내부점 왕복
    const auto inv = h->Inverse();
    CHECK(inv.has_value());
    if (inv) {
        const PointF mid = h->Apply({ 0.5f, 0.5f });
        const PointF back = inv->Apply(mid);
        CHECK_NEAR(back.x, 0.5, 1e-4);
        CHECK_NEAR(back.y, 0.5, 1e-4);
    }
    // 퇴화 (일직선) 거부
    CHECK(!Homography::Fit({ {0,0},{1,1},{2,2},{3,3} }, dst).has_value());
}

// ---------------------------------------------------------------- Kabsch
void TestRigid2D() {
    const double th = 0.7, tx = 12.0, ty = -5.0;
    RigidTransform2D truth; truth.theta = th; truth.tx = tx; truth.ty = ty;
    std::vector<PointF> src = { {0,0}, {100,0}, {100,60}, {0,60}, {50,30} };
    std::vector<PointF> dst;
    for (const auto& p : src) dst.push_back(truth.Apply(p));

    const auto fit = RigidTransform2D::Fit(src, dst);
    CHECK(fit.has_value());
    if (fit) {
        CHECK_NEAR(fit->theta, th, 1e-6);
        CHECK_NEAR(fit->tx, tx, 1e-3);
        CHECK_NEAR(fit->ty, ty, 1e-3);
        const auto inv = fit->Inverse();
        const PointF back = inv.Apply(dst[4]);
        CHECK_NEAR(back.x, 50.0, 1e-3);
        CHECK_NEAR(back.y, 30.0, 1e-3);
    }
}

// ------------------------------------------------------------------ EKF
void TestEkfConsistency() {
    // 등속 원운동 (필터 모델과 동일한 잡음으로 생성 -> NIS/NEES가 이론 구간에 있어야 함)
    AgvEkfParams p;
    AgvEkf ekf(p);
    Rng rng;

    const double dt = 1.0 / 60.0, speed = 0.2, radius = 0.6;
    const double w = speed / radius;
    double tvx = speed, tvy = 0, tom = w;                // 참 바디 속도
    double txp = radius, typ = 0, tth = 1.5707963;       // 참 자세

    double nisSum = 0, neesSum = 0;
    int nisCnt = 0;
    const int steps = 1500;
    for (int k = 0; k < steps; ++k) {
        // 참 상태 전파 + 프로세스 잡음 (필터의 Q와 동일 강도)
        tvx += rng.Gauss(std::sqrt(p.qVel * dt));
        tvy += rng.Gauss(std::sqrt(p.qVel * dt));
        tom += rng.Gauss(std::sqrt(p.qOmega * dt));
        const double c = std::cos(tth), s = std::sin(tth);
        txp += (tvx * c - tvy * s) * dt + rng.Gauss(std::sqrt(p.qPos * dt));
        typ += (tvx * s + tvy * c) * dt + rng.Gauss(std::sqrt(p.qPos * dt));
        tth = AgvEkf::WrapAngle(tth + tom * dt + rng.Gauss(std::sqrt(p.qTheta * dt)));

        // 측정
        const double zx = txp + rng.Gauss(std::sqrt(p.rPos));
        const double zy = typ + rng.Gauss(std::sqrt(p.rPos));
        const double zt = AgvEkf::WrapAngle(tth + rng.Gauss(std::sqrt(p.rTheta)));

        ekf.Predict(dt);
        const auto nis = ekf.Update(zx, zy, zt);
        if (k > 100 && nis) { nisSum += *nis; ++nisCnt; }   // 과도기 제외

        if (k > 100) {
            const auto& x = ekf.State();
            const auto& P = ekf.Cov();
            // NEES는 위치·각 3상태만 (속도는 약관측이라 몬테카를로 1런에선 변동 큼)
            const double e[3] = { txp - x[0], typ - x[1], AgvEkf::WrapAngle(tth - x[2]) };
            double p3[9], inv[9];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    p3[i * 3 + j] = P[i * 6 + j];
            const double det =
                p3[0] * (p3[4] * p3[8] - p3[5] * p3[7]) - p3[1] * (p3[3] * p3[8] - p3[5] * p3[6]) +
                p3[2] * (p3[3] * p3[7] - p3[4] * p3[6]);
            if (std::abs(det) > 1e-20) {
                inv[0] = (p3[4] * p3[8] - p3[5] * p3[7]) / det;
                inv[1] = (p3[2] * p3[7] - p3[1] * p3[8]) / det;
                inv[2] = (p3[1] * p3[5] - p3[2] * p3[4]) / det;
                inv[3] = (p3[5] * p3[6] - p3[3] * p3[8]) / det;
                inv[4] = (p3[0] * p3[8] - p3[2] * p3[6]) / det;
                inv[5] = (p3[2] * p3[3] - p3[0] * p3[5]) / det;
                inv[6] = (p3[3] * p3[7] - p3[4] * p3[6]) / det;
                inv[7] = (p3[1] * p3[6] - p3[0] * p3[7]) / det;
                inv[8] = (p3[0] * p3[4] - p3[1] * p3[3]) / det;
                double nees = 0;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        nees += e[i] * inv[i * 3 + j] * e[j];
                neesSum += nees;
            }
        }
    }
    const double nisMean = nisSum / nisCnt;
    const double neesMean = neesSum / nisCnt;
    // chi2(3) 평균 3. 시간평균 느슨한 구간 (조셉형/대칭화 포함 일관성 검증)
    CHECK(nisMean > 2.4 && nisMean < 3.6);
    CHECK(neesMean > 2.2 && neesMean < 4.0);

    // 지연 보상: PredictAhead가 상태의 바디 속도를 정확히 적분해야 함
    const auto ahead = ekf.PredictAhead(0.1);
    const auto& x = ekf.State();
    const double c2 = std::cos(x[2]), s2 = std::sin(x[2]);
    CHECK_NEAR(ahead[0], x[0] + (x[3] * c2 - x[4] * s2) * 0.1, 1e-9);
    CHECK_NEAR(ahead[1], x[1] + (x[3] * s2 + x[4] * c2) * 0.1, 1e-9);
    CHECK_NEAR(ahead[2], AgvEkf::WrapAngle(x[2] + x[5] * 0.1), 1e-9);
}

void TestEkfBasics() {
    AgvEkf ekf;
    CHECK(!ekf.Initialized());
    ekf.Update(1.0, 2.0, 0.5); // 첫 측정 = 초기화
    CHECK(ekf.Initialized());
    CHECK_NEAR(ekf.State()[0], 1.0, 1e-9);
    // 정지 측정 반복 -> 위치 공분산 감소
    const double p0 = ekf.Cov()[0];
    for (int i = 0; i < 30; ++i) {
        ekf.Predict(0.02);
        ekf.Update(1.0, 2.0, 0.5);
    }
    CHECK(ekf.Cov()[0] < p0);
    // 각도 랩: (-pi, pi] 관례 — 3pi는 크기 pi로 접힌다
    CHECK_NEAR(std::abs(AgvEkf::WrapAngle(3.0 * 3.14159265)), 3.14159265, 1e-6);
    CHECK_NEAR(AgvEkf::WrapAngle(-3.5 * 3.14159265), 0.5 * 3.14159265, 1e-6);
}

// ------------------------------------------------------------ PathTracker
void TestPathTracker() {
    TrackerParams tp;
    PathTracker t(tp);
    t.SetPath({ {0,0}, {1.0f,0} });

    // 경로 아래(y=-0.1)에서 시작, 헤딩 정렬 상태 -> vy 양수(경로로 복귀), vx 양수(전진)
    auto cmd = t.Step(0.2, -0.1, 0.0);
    CHECK(cmd.vx > 0);
    CHECK(cmd.vy > 0);
    CHECK_NEAR(cmd.crossTrackError, 0.1, 1e-6);

    // 헤딩이 90도 틀어져 있으면 omega가 보정 방향
    cmd = t.Step(0.2, 0.0, 1.5707963);
    CHECK(cmd.omega < 0);

    // 도달 판정
    cmd = t.Step(0.995, 0.0, 0.0);
    CHECK(cmd.done);

    // 시뮬 통합: 오프셋에서 시작해 경로 추종 시뮬레이션 -> 수렴 & 완주
    PathTracker t2(tp);
    t2.SetPath({ {0,0}, {0.8f,0}, {0.8f,0.5f} });
    double x = 0.0, y = -0.15, th = 0.6;
    bool done = false;
    double maxErrLate = 0;
    const double dt = 0.02;
    for (int i = 0; i < 3000 && !done; ++i) {
        const auto c = t2.Step(x, y, th);
        done = c.done;
        // 바디 -> 월드 적분 (이상 동역학)
        const double cc = std::cos(th), ss = std::sin(th);
        x += (c.vx * cc - c.vy * ss) * dt;
        y += (c.vx * ss + c.vy * cc) * dt;
        th = th + c.omega * dt;
        if (i > 400) maxErrLate = std::max(maxErrLate, c.crossTrackError);
    }
    CHECK(done);
    CHECK(maxErrLate < 0.05); // 수렴 후 5cm 이내 유지 (v2 DoD와 동일 기준)

    // 메카넘 믹싱 부호: 제자리 회전(omega>0) -> 좌/우 반대
    const auto wRot = MecanumWheelSpeeds(0, 0, 1.0, 0.10, 0.11, 0.03);
    CHECK(wRot[0] < 0 && wRot[1] > 0 && wRot[2] < 0 && wRot[3] > 0);
    // 순수 횡이동(vy>0) -> FL,RR 음수 / FR,RL 양수
    const auto wStrafe = MecanumWheelSpeeds(0, 0.2, 0, 0.10, 0.11, 0.03);
    CHECK(wStrafe[0] < 0 && wStrafe[1] > 0 && wStrafe[2] > 0 && wStrafe[3] < 0);
    // 직진 -> 4륜 동일
    const auto wFwd = MecanumWheelSpeeds(0.2, 0, 0, 0.10, 0.11, 0.03);
    CHECK_NEAR(wFwd[0], wFwd[1], 1e-12);
    CHECK_NEAR(wFwd[2], wFwd[3], 1e-12);
}

// --------------------------------------------------------- MarkerDetector
GrayImage RotateImage90(const GrayImage& src) {
    GrayImage out(src.Height(), src.Width());
    for (int y = 0; y < src.Height(); ++y)
        for (int x = 0; x < src.Width(); ++x)
            out.Set(src.Height() - 1 - y, x, src.At(x, y));
    return out;
}

// 태그를 큰 캔버스 임의 위치에 배치
GrayImage PlaceOnCanvas(const GrayImage& tag, int cw, int ch, int ox, int oy) {
    GrayImage canvas(cw, ch, 210); // 밝은 배경
    for (int y = 0; y < tag.Height(); ++y)
        for (int x = 0; x < tag.Width(); ++x)
            canvas.Set(ox + x, oy + y, tag.At(x, y));
    return canvas;
}

void TestMarkerRoundtrip() {
    for (const int idInt : { 1, 2, 3, 4, 7, 11, 42, 200 }) {
        const uint8_t id = static_cast<uint8_t>(idInt);
        const GrayImage tag = RenderTag(id, 12);
        const GrayImage scene = PlaceOnCanvas(tag, 400, 300, 60, 40);
        const auto dets = DetectMarkers(scene);
        CHECK(dets.size() == 1);
        if (dets.size() == 1)
            CHECK(dets[0].id == id);
    }
}

void TestMarkerRotation() {
    const uint8_t id = 5;
    GrayImage scene = PlaceOnCanvas(RenderTag(id, 12), 300, 300, 50, 50);
    for (int rot = 0; rot < 4; ++rot) {
        const auto dets = DetectMarkers(scene);
        CHECK(dets.size() == 1);
        if (dets.size() == 1)
            CHECK(dets[0].id == id);
        scene = RotateImage90(scene);
    }
}

void TestMarkerRejection() {
    // 패리티 파괴: 페이로드 셀 하나를 반전 -> 검출 자체가 기각되어야 함
    const uint8_t id = 9;
    GrayImage tag = RenderTag(id, 12);
    // (quiet 1셀 + 테두리 1셀) 이후 첫 페이로드 셀 = 셀 (2,2) 위치, 앵커(흰) -> 검정으로
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            tag.Set(2 * 12 + x, 2 * 12 + y, 0);
    const GrayImage scene = PlaceOnCanvas(tag, 300, 300, 40, 40);
    CHECK(DetectMarkers(scene).empty());

    // 무지 사각형(태그 아님) 기각
    GrayImage plain(200, 200, 220);
    for (int y = 60; y < 140; ++y)
        for (int x = 60; x < 140; ++x)
            plain.Set(x, y, 10);
    CHECK(DetectMarkers(plain).empty());
}

void TestMarkerNoise() {
    Rng rng;
    const uint8_t id = 3;
    GrayImage scene = PlaceOnCanvas(RenderTag(id, 14), 400, 300, 80, 60);
    // 가우시안 밝기 노이즈 + 점 노이즈
    for (int y = 0; y < scene.Height(); ++y)
        for (int x = 0; x < scene.Width(); ++x) {
            int v = scene.At(x, y) + static_cast<int>(rng.Gauss(10.0));
            if (rng.Uniform() < 0.002) v = rng.Uniform() < 0.5 ? 0 : 255;
            scene.Set(x, y, static_cast<uint8_t>(std::clamp(v, 0, 255)));
        }
    const auto dets = DetectMarkers(scene);
    CHECK(dets.size() == 1);
    if (dets.size() == 1)
        CHECK(dets[0].id == id);
}

// 마커 → 호모그래피 → 실세계 좌표 통합 (2막 파이프라인 축소판)
void TestMarkerToFloorPipeline() {
    // 바닥 4점 기준 마커로 호모그래피를 만들고, AGV 마커 중심을 {floor}로 변환
    const std::vector<PointF> px = { {50,40}, {350,42}, {348,240}, {52,238} };  // 픽셀
    const std::vector<PointF> fl = { {0,0}, {2.0f,0}, {2.0f,1.25f}, {0,1.25f} }; // m
    const auto h = Homography::Fit(px, fl);
    CHECK(h.has_value());
    if (!h) return;
    const PointF agvPx{ 200, 140 };
    const PointF agvFloor = h->Apply(agvPx);
    CHECK(agvFloor.x > 0.8f && agvFloor.x < 1.2f);
    CHECK(agvFloor.y > 0.5f && agvFloor.y < 0.85f);
}

// --------------------------------------------------------- EmgProcessor
// 합성 sEMG: 안정 시 저진폭 백색잡음, 수축 시 고진폭 버스트 (엔벌로프↑)
double SynthEmg(Rng& rng, double contraction /*0..1*/) {
    // 근수축 세기에 비례해 잡음 진폭이 커지는 근사 (sEMG의 진폭변조 특성)
    const double amp = 0.05 + contraction * 1.0;
    return rng.Gauss(amp);
}

void TestEmgProcessor() {
    EmgParams p;
    p.sampleRateHz = 1000.0;
    EmgProcessor emg(p);
    Rng rng;

    // 워밍업 (필터 정착) + MVC 캘리브레이션: 강수축 2초의 최대 엔벌로프
    double mvcMax = 0;
    for (int i = 0; i < 2000; ++i) {
        const auto o = emg.Push(SynthEmg(rng, 1.0));
        if (i > 500) mvcMax = std::max(mvcMax, o.envelope);
    }
    CHECK(mvcMax > 0.0);
    emg.SetMvc(mvcMax);

    // 이완 상태: 정규화·비례 낮고, 제스처 false
    double relaxNorm = 0;
    for (int i = 0; i < 1500; ++i) {
        const auto o = emg.Push(SynthEmg(rng, 0.0));
        if (i > 800) relaxNorm = o.normalized;
    }
    CHECK(relaxNorm < 0.2);

    // 중간 수축(0.5): 비례 출력이 이완보다 확실히 큼, 단조성
    double midProp = 0;
    for (int i = 0; i < 1500; ++i) {
        const auto o = emg.Push(SynthEmg(rng, 0.5));
        if (i > 800) midProp = o.proportional;
    }
    // 강수축(1.0): 비례 출력이 중간보다 큼
    double hiProp = 0; bool hiContract = false;
    for (int i = 0; i < 1500; ++i) {
        const auto o = emg.Push(SynthEmg(rng, 1.0));
        if (i > 800) { hiProp = o.proportional; hiContract = o.contracted; }
    }
    CHECK(midProp > 0.05);          // 데드존 넘김
    CHECK(hiProp > midProp);        // 비례성(단조 증가)
    CHECK(hiProp <= 1.0 + 1e-9);    // 클램프
    CHECK(hiContract);              // 강수축 시 제스처 on

    // 히스테리시스: on→off 임계가 달라 채터링 안 남
    // 수축 유지하다 서서히 이완 → contracted가 off로 '한 번' 전이
    int transitions = 0; bool prev = true;
    for (int i = 0; i < 4000; ++i) {
        const double c = 1.0 - (i / 4000.0); // 1.0 -> 0.0 느린 하강
        const auto o = emg.Push(SynthEmg(rng, c));
        if (o.contracted != prev) { ++transitions; prev = o.contracted; }
    }
    CHECK(transitions <= 2); // 히스테리시스로 왕복 채터링 없음 (이상적 1회)

    // 데드존: 아주 약한 신호는 비례 0
    EmgProcessor emg2(p);
    for (int i = 0; i < 500; ++i) emg2.Push(SynthEmg(rng, 1.0));
    emg2.SetMvc(mvcMax);
    double weakProp = 1.0;
    for (int i = 0; i < 1000; ++i) {
        const auto o = emg2.Push(SynthEmg(rng, 0.02)); // 데드존 이하
        if (i > 600) weakProp = o.proportional;
    }
    CHECK(weakProp == 0.0);

    // 리셋 후 상태 초기화
    emg2.Reset();
    const auto o = emg2.Push(0.0);
    CHECK(o.proportional == 0.0);
    CHECK(!o.contracted);
}

} // namespace

void RunV2CoreTests() {
    TestEmgProcessor();
    TestHomography();
    TestRigid2D();
    TestEkfBasics();
    TestEkfConsistency();
    TestPathTracker();
    TestMarkerRoundtrip();
    TestMarkerRotation();
    TestMarkerRejection();
    TestMarkerNoise();
    TestMarkerToFloorPipeline();
}
