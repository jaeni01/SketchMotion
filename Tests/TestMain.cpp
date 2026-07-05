// TestMain.cpp - Core 라이브러리 단위테스트 (의존성 없는 assert 기반 러너)
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../Core/ArmKinematics.h"
#include "../Core/CanvasModel.h"
#include "../Core/ContourTracer.h"
#include "../Core/Filters.h"
#include "../Core/GCodeWriter.h"
#include "../Core/Geometry.h"
#include "../Core/PathPlanner.h"
#include "../Core/PathSimplify.h"
#include "../Core/Stroke.h"

namespace {

int g_failures = 0;
int g_checks = 0;

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

// ---------------------------------------------------------------- Geometry
void TestGeometry() {
    CHECK_NEAR(Distance({ 0, 0 }, { 3, 4 }), 5.0, 1e-6);
    CHECK_NEAR(DistanceToSegment({ 0, 5 }, { -10, 0 }, { 10, 0 }), 5.0, 1e-6);
    // 선분 밖의 점은 끝점까지 거리
    CHECK_NEAR(DistanceToSegment({ 13, 4 }, { -10, 0 }, { 10, 0 }), 5.0, 1e-6);

    const auto b = BoundsOf({ { { 1, 2 }, { 5, 8 } }, { { -3, 4 } } });
    CHECK_NEAR(b.left, -3.0, 1e-6);
    CHECK_NEAR(b.top, 2.0, 1e-6);
    CHECK_NEAR(b.right, 5.0, 1e-6);
    CHECK_NEAR(b.bottom, 8.0, 1e-6);

    CHECK_NEAR(PolylineLength({ { 0, 0 }, { 3, 0 }, { 3, 4 } }), 7.0, 1e-6);
}

// ------------------------------------------------------------------ Stroke
void TestStroke() {
    Stroke rect(1, StrokeKind::Rect, { 255, 0, 0, 255 }, 2.0f);
    rect.SetPoints({ { 0, 0 }, { 10, 6 } });
    const auto lines = rect.ToPolylines();
    CHECK(lines.size() == 1);
    CHECK(lines[0].size() == 5); // 닫힌 사각형
    CHECK(lines[0].front() == lines[0].back());

    Stroke ellipse(2, StrokeKind::Ellipse, {}, 1.0f);
    ellipse.SetPoints({ { -4, -3 }, { 4, 3 } });
    const auto el = ellipse.ToPolylines(64);
    CHECK(el.size() == 1);
    // 모든 점이 타원 방정식 근사 만족: (x/4)^2 + (y/3)^2 = 1
    for (const auto& p : el[0]) {
        const double v = (p.x / 4.0) * (p.x / 4.0) + (p.y / 3.0) * (p.y / 3.0);
        CHECK_NEAR(v, 1.0, 1e-4);
    }

    Stroke line(3, StrokeKind::Line, {}, 4.0f);
    line.SetPoints({ { 0, 0 }, { 100, 0 } });
    CHECK(line.HitTest({ 50, 1.5f }, 0.5f));   // 두께 안
    CHECK(!line.HitTest({ 50, 10 }, 0.5f));    // 두께 밖
}

// ------------------------------------------------------------- CanvasModel
void TestCanvasModelJsonRoundtrip() {
    CanvasModel m;
    m.SetCanvasSize(800, 600);

    Stroke s1(m.NextId(), StrokeKind::Freehand, { 10, 20, 30, 255 }, 3.5f);
    s1.SetPoints({ { 1.5f, 2.25f }, { 3, 4 }, { 5, 6 } });
    m.AddStroke(s1);

    Stroke s2(m.NextId(), StrokeKind::Ellipse, { 200, 100, 50, 128 }, 1.0f);
    s2.SetPoints({ { 10, 10 }, { 50, 30 } });
    m.AddStroke(s2);

    const std::string json = m.ToJson();
    const auto parsed = CanvasModel::FromJson(json);
    CHECK(parsed.has_value());
    if (!parsed) return;

    CHECK(parsed->Count() == 2);
    CHECK_NEAR(parsed->CanvasWidth(), 800.0, 1e-3);
    CHECK_NEAR(parsed->CanvasHeight(), 600.0, 1e-3);

    const Stroke* p1 = parsed->FindStroke(s1.Id());
    CHECK(p1 != nullptr);
    if (p1) {
        CHECK(p1->Kind() == StrokeKind::Freehand);
        CHECK((p1->Color() == ColorRGBA{ 10, 20, 30, 255 }));
        CHECK_NEAR(p1->Width(), 3.5, 1e-4);
        CHECK(p1->Points().size() == 3);
        CHECK_NEAR(p1->Points()[0].x, 1.5, 1e-4);
        CHECK_NEAR(p1->Points()[0].y, 2.25, 1e-4);
    }

    // NextId가 로드된 최대 id 이후로 이어지는지
    auto reloaded = *parsed;
    CHECK(reloaded.NextId() > s2.Id());

    // 손상된 입력 거부
    CHECK(!CanvasModel::FromJson("{ \"format\": \"other\" }").has_value());
    CHECK(!CanvasModel::FromJson("not json").has_value());
    CHECK(!CanvasModel::FromJson("").has_value());
}

void TestCanvasModelEdit() {
    CanvasModel m;
    Stroke s(m.NextId(), StrokeKind::Line, {}, 6.0f);
    s.SetPoints({ { 0, 0 }, { 100, 0 } });
    const uint64_t id = s.Id();
    m.AddStroke(std::move(s));

    CHECK(m.TopmostHit({ 50, 2 }, 1.0f) == std::optional<uint64_t>(id));
    CHECK(!m.TopmostHit({ 50, 50 }, 1.0f).has_value());
    CHECK(m.RemoveStroke(id));
    CHECK(!m.RemoveStroke(id));
    CHECK(m.Count() == 0);
}

// ----------------------------------------------------------------- Filters
void TestFilters() {
    // 4x4 BGRA: 왼쪽 절반 검정, 오른쪽 절반 흰색
    BgraImage img(4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            uint8_t* p = img.Pixel(x, y);
            const uint8_t v = (x >= 2) ? 255 : 0;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }

    const GrayImage gray = ToGrayscale(img);
    CHECK(gray.At(0, 0) == 0);
    CHECK(gray.At(3, 0) == 255);

    const GrayImage edges = SobelMagnitude(gray);
    CHECK(edges.At(1, 1) > 200); // 경계에서 강한 에지
    CHECK(edges.At(0, 1) == 0);  // 평탄 영역

    const GrayImage bin = Threshold(gray, 128);
    CHECK(bin.At(0, 0) == 0);
    CHECK(bin.At(3, 3) == 255);

    // Otsu: 50/200 두 클러스터 사이의 임계값을 찾아야 함
    GrayImage bimodal(10, 10, 50);
    for (int y = 0; y < 10; ++y)
        for (int x = 5; x < 10; ++x)
            bimodal.Set(x, y, 200);
    const uint8_t otsu = OtsuThreshold(bimodal);
    CHECK(otsu >= 50 && otsu < 200);

    const GrayImage inv = Invert(gray);
    CHECK(inv.At(0, 0) == 255);
    CHECK(inv.At(3, 0) == 0);

    // 블러: 균일 이미지가 변하지 않아야 함
    GrayImage flat(8, 8, 100);
    const GrayImage blurred = GaussianBlur5(flat);
    CHECK(blurred.At(4, 4) == 100);
    CHECK(blurred.At(0, 0) == 100);
}

// ----------------------------------------------------------- ContourTracer
void TestContourTracer() {
    // 20x20 이미지에 10x10 채운 사각형
    GrayImage img(20, 20, 0);
    for (int y = 5; y < 15; ++y)
        for (int x = 5; x < 15; ++x)
            img.Set(x, y, 255);

    ContourOptions opt;
    opt.minPoints = 8;
    const auto contours = TraceContours(img, opt);
    CHECK(contours.size() == 1);
    if (!contours.empty()) {
        // 10x10 사각형 둘레의 윤곽 픽셀 수는 36 (4*10 - 4)
        CHECK(contours[0].size() == 36);
        // 모든 윤곽점은 사각형 경계 위
        for (const auto& p : contours[0]) {
            CHECK(p.x >= 5 && p.x <= 14 && p.y >= 5 && p.y <= 14);
            const bool onBorder = p.x == 5 || p.x == 14 || p.y == 5 || p.y == 14;
            CHECK(onBorder);
        }
    }

    // 분리된 두 도형 → 두 윤곽
    GrayImage two(30, 12, 0);
    for (int y = 2; y < 10; ++y) {
        for (int x = 2; x < 10; ++x) two.Set(x, y, 255);
        for (int x = 18; x < 26; ++x) two.Set(x, y, 255);
    }
    CHECK(TraceContours(two, opt).size() == 2);

    // 빈 이미지 → 윤곽 없음
    CHECK(TraceContours(GrayImage(10, 10, 0), opt).empty());
}

// ------------------------------------------------------------ PathSimplify
void TestPathSimplify() {
    // 직선 위 점 100개는 양 끝 2개로 축소
    std::vector<PointF> line;
    for (int i = 0; i <= 100; ++i)
        line.push_back({ static_cast<float>(i), 0.0f });
    const auto simplified = SimplifyRdp(line, 0.5f);
    CHECK(simplified.size() == 2);
    CHECK(simplified.front() == line.front());
    CHECK(simplified.back() == line.back());

    // 계단형: 코너는 보존되어야 함
    const std::vector<PointF> corner = { { 0, 0 }, { 50, 0 }, { 50, 50 } };
    const auto keptCorner = SimplifyRdp(corner, 1.0f);
    CHECK(keptCorner.size() == 3);

    // 단순화 후에도 원본과의 최대 편차는 epsilon 이하
    std::vector<PointF> wave;
    for (int i = 0; i <= 200; ++i) {
        const float x = static_cast<float>(i);
        wave.push_back({ x, 20.0f * std::sin(x * 0.1f) });
    }
    const float eps = 2.0f;
    const auto sw = SimplifyRdp(wave, eps);
    CHECK(sw.size() < wave.size());
    for (const auto& p : wave) {
        float minDist = std::numeric_limits<float>::max();
        for (size_t i = 1; i < sw.size(); ++i)
            minDist = std::min(minDist, DistanceToSegment(p, sw[i - 1], sw[i]));
        CHECK(minDist <= eps + 1e-3f);
    }
}

// ------------------------------------------------------------- PathPlanner
void TestPathPlanner() {
    // 일부러 나쁜 순서: 멀리 갔다가 돌아오게 배치
    const std::vector<std::vector<PointF>> lines = {
        { { 100, 0 }, { 110, 0 } },
        { { 0, 0 }, { 10, 0 } },
        { { 50, 0 }, { 60, 0 } },
    };
    const float naive = TravelDistanceOf(lines, { 0, 0 });
    const auto planned = PlanDrawingOrder(lines, { 0, 0 });

    CHECK(planned.paths.size() == 3);
    CHECK(planned.stats.travelDistance < naive);
    // 최적 순서: (0,0)->10 draw, 40 travel, 10 draw, 40 travel, 10 draw
    CHECK_NEAR(planned.stats.travelDistance, 80.0, 1e-3);
    CHECK_NEAR(planned.stats.drawDistance, 30.0, 1e-3);

    // 방향 반전 확인: 끝점이 더 가까우면 뒤집어야 함
    const std::vector<std::vector<PointF>> rev = { { { 100, 0 }, { 5, 0 } } };
    const auto planned2 = PlanDrawingOrder(rev, { 0, 0 });
    CHECK(planned2.paths.size() == 1);
    CHECK_NEAR(planned2.paths[0].front().x, 5.0, 1e-3);
    CHECK_NEAR(planned2.stats.travelDistance, 5.0, 1e-3);

    CHECK(PlanDrawingOrder({}, { 0, 0 }).paths.empty());
}

// ------------------------------------------------------------- GCodeWriter
void TestGCodeWriter() {
    const std::vector<std::vector<PointF>> paths = {
        { { 0, 0 }, { 100, 0 }, { 100, 100 } },
    };
    const RectF bounds = { 0, 0, 100, 100 };
    GCodeOptions opt;
    opt.workWidthMm = 50.0f; // scale = 0.5

    const std::string g = WriteGCode(paths, bounds, opt);
    CHECK(g.find("G21") != std::string::npos);
    CHECK(g.find("G90") != std::string::npos);
    CHECK(g.find("M2") != std::string::npos);
    // flipY: 화면 (0,0)은 기계 (0, 50)
    CHECK(g.find("G0 X0.000 Y50.000") != std::string::npos);
    // (100,100) -> (50, 0)
    CHECK(g.find("G1 X50.000 Y0.000") != std::string::npos);
    // 펜 다운/업이 경로당 한 번씩
    CHECK(g.find("pen down") != std::string::npos);

    const std::string empty = WriteGCode({}, {}, opt);
    CHECK(empty.find("(empty drawing)") != std::string::npos);
}

// ----------------------------------------------------------- ArmKinematics
void TestArmKinematics() {
    ArmConfig cfg;
    cfg.base = { 0, 0 };
    cfg.l1 = 200.0f;
    cfg.l2 = 150.0f;

    // IK -> FK 왕복: 작업 영역 내 여러 점
    const std::vector<PointF> targets = {
        { 300, 50 }, { 100, 200 }, { -150, 180 }, { 60, -120 }, { 250, -100 },
    };
    for (const auto& t : targets) {
        CHECK(IsReachable(cfg, t));
        for (const bool elbowUp : { true, false }) {
            const auto q = InverseKinematics(cfg, t, elbowUp);
            CHECK(q.has_value());
            if (q) {
                const ArmPose pose = ForwardKinematics(cfg, *q);
                CHECK_NEAR(pose.wrist.x, t.x, 1e-2);
                CHECK_NEAR(pose.wrist.y, t.y, 1e-2);
                // 링크 길이 보존
                CHECK_NEAR(Distance(pose.shoulder, pose.elbow), cfg.l1, 1e-2);
                CHECK_NEAR(Distance(pose.elbow, pose.wrist), cfg.l2, 1e-2);
            }
        }
    }

    // 도달 불가: 너무 멀거나 너무 가까움
    CHECK(!InverseKinematics(cfg, { 500, 0 }).has_value());
    CHECK(!InverseKinematics(cfg, { 10, 0 }).has_value());
    CHECK(!IsReachable(cfg, { 500, 0 }));
    CHECK(!IsReachable(cfg, { 10, 0 }));

    // 경계(완전히 뻗은 팔)도 풀려야 함
    const auto qEdge = InverseKinematics(cfg, { 350, 0 });
    CHECK(qEdge.has_value());
    if (qEdge) {
        const ArmPose pose = ForwardKinematics(cfg, *qEdge);
        CHECK_NEAR(pose.wrist.x, 350.0, 1e-2);
        CHECK_NEAR(pose.wrist.y, 0.0, 1e-2);
    }
}

// -------------------------------------------- 통합: 이미지 → 벡터 → G-code
void TestVisionToMotionPipeline() {
    // 합성 이미지: 원 하나를 그린 뒤 전체 파이프라인 통과
    GrayImage img(100, 100, 0);
    const float cx = 50, cy = 50, r = 30;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) {
            const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            if (d <= r)
                img.Set(x, y, 255);
        }

    const auto contours = TraceContours(img);
    CHECK(contours.size() == 1);

    const auto simplified = SimplifyAll(contours, 1.5f);
    CHECK(simplified.size() == 1);
    CHECK(simplified[0].size() < contours[0].size());

    const auto planned = PlanDrawingOrder(simplified);
    const auto bounds = BoundsOf(planned.paths);
    const std::string g = WriteGCode(planned.paths, bounds);
    CHECK(g.find("G1") != std::string::npos);
    CHECK(g.size() > 200);
}

} // namespace

int main() {
    TestGeometry();
    TestStroke();
    TestCanvasModelJsonRoundtrip();
    TestCanvasModelEdit();
    TestFilters();
    TestContourTracer();
    TestPathSimplify();
    TestPathPlanner();
    TestGCodeWriter();
    TestArmKinematics();
    TestVisionToMotionPipeline();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
