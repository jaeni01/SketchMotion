#pragma once
// SketchDoc.h - 문서: CanvasModel 소유 + undo/redo + .skm 입출력
#include "framework.h"
#include "Commands.h"
#include "../Core/CanvasModel.h"

class CSketchDoc : public CDocument {
    DECLARE_DYNCREATE(CSketchDoc)
protected:
    CSketchDoc() = default;

public:
    sm::CanvasModel& Model() { return m_model; }
    const sm::CanvasModel& Model() const { return m_model; }

    // 모든 편집은 이 경로로만 수행한다 (undo 일관성)
    void ExecuteCommand(std::unique_ptr<sm::ICommand> cmd);

    BOOL OnNewDocument() override;
    BOOL OnOpenDocument(LPCTSTR lpszPathName) override;
    BOOL OnSaveDocument(LPCTSTR lpszPathName) override;

protected:
    afx_msg void OnEditUndo();
    afx_msg void OnEditRedo();
    afx_msg void OnUpdateEditUndo(CCmdUI* pCmdUI);
    afx_msg void OnUpdateEditRedo(CCmdUI* pCmdUI);
    DECLARE_MESSAGE_MAP()

private:
    void NotifyChanged();

    sm::CanvasModel m_model;
    sm::UndoRedoManager m_undoRedo;
};
