#pragma once
// Commands.h - Command 패턴 undo/redo
#include <memory>
#include <vector>
#include "../Core/CanvasModel.h"

namespace sm {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute(CanvasModel& model) = 0;
    virtual void Undo(CanvasModel& model) = 0;
};

// 스트로크 1개 추가
class AddStrokeCommand : public ICommand {
public:
    explicit AddStrokeCommand(Stroke s) : m_stroke(std::move(s)) {}
    void Execute(CanvasModel& model) override { model.AddStroke(m_stroke); }
    void Undo(CanvasModel& model) override { model.RemoveStroke(m_stroke.Id()); }
private:
    Stroke m_stroke;
};

// 여러 스트로크 일괄 추가 (카메라 트레이싱 결과 등)
class AddStrokesCommand : public ICommand {
public:
    explicit AddStrokesCommand(std::vector<Stroke> strokes)
        : m_strokes(std::move(strokes)) {}
    void Execute(CanvasModel& model) override {
        for (const auto& s : m_strokes)
            model.AddStroke(s);
    }
    void Undo(CanvasModel& model) override {
        for (const auto& s : m_strokes)
            model.RemoveStroke(s.Id());
    }
private:
    std::vector<Stroke> m_strokes;
};

// 스트로크 제거 (지우개)
class RemoveStrokeCommand : public ICommand {
public:
    explicit RemoveStrokeCommand(Stroke s) : m_stroke(std::move(s)) {}
    void Execute(CanvasModel& model) override { model.RemoveStroke(m_stroke.Id()); }
    void Undo(CanvasModel& model) override { model.AddStroke(m_stroke); }
private:
    Stroke m_stroke; // 복원용 전체 사본 보관
};

class UndoRedoManager {
public:
    void Execute(std::unique_ptr<ICommand> cmd, CanvasModel& model) {
        cmd->Execute(model);
        m_undo.push_back(std::move(cmd));
        m_redo.clear();
    }
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }

    void Undo(CanvasModel& model) {
        if (m_undo.empty()) return;
        auto cmd = std::move(m_undo.back());
        m_undo.pop_back();
        cmd->Undo(model);
        m_redo.push_back(std::move(cmd));
    }
    void Redo(CanvasModel& model) {
        if (m_redo.empty()) return;
        auto cmd = std::move(m_redo.back());
        m_redo.pop_back();
        cmd->Execute(model);
        m_undo.push_back(std::move(cmd));
    }
    void Clear() {
        m_undo.clear();
        m_redo.clear();
    }

private:
    std::vector<std::unique_ptr<ICommand>> m_undo;
    std::vector<std::unique_ptr<ICommand>> m_redo;
};

} // namespace sm
