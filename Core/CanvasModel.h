#pragma once
// CanvasModel.h - 문서 데이터 모델 + JSON 직렬화 (MFC 비의존)
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "Stroke.h"

namespace sm {

// 캔버스 = 스트로크의 순서 있는 컬렉션.
// UI 프레임워크를 모르며, App 계층(SketchDoc)이 소유한다.
class CanvasModel {
public:
    static constexpr int kFormatVersion = 1;

    uint64_t NextId() { return ++m_lastId; }

    const std::vector<Stroke>& Strokes() const { return m_strokes; }
    size_t Count() const { return m_strokes.size(); }

    void AddStroke(Stroke s);
    bool RemoveStroke(uint64_t id);                 // 없으면 false
    const Stroke* FindStroke(uint64_t id) const;
    void Clear();

    float CanvasWidth() const { return m_width; }
    float CanvasHeight() const { return m_height; }
    void SetCanvasSize(float w, float h) { m_width = w; m_height = h; }

    // 맨 위(나중에 그린)부터 히트 테스트. 없으면 nullopt.
    std::optional<uint64_t> TopmostHit(const PointF& p, float tolerance) const;

    // 모든 스트로크를 폴리라인으로 평탄화 (경로 계획 입력)
    std::vector<std::vector<PointF>> AllPolylines() const;

    // .skm(JSON) 직렬화
    std::string ToJson() const;
    static std::optional<CanvasModel> FromJson(const std::string& json);

private:
    std::vector<Stroke> m_strokes;
    uint64_t m_lastId = 0;
    float m_width = 1600.0f;
    float m_height = 1000.0f;
};

} // namespace sm
