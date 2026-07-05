#include "SketchDoc.h"
#include <fstream>
#include <sstream>

IMPLEMENT_DYNCREATE(CSketchDoc, CDocument)

BEGIN_MESSAGE_MAP(CSketchDoc, CDocument)
    ON_COMMAND(ID_EDIT_UNDO, &CSketchDoc::OnEditUndo)
    ON_COMMAND(ID_EDIT_REDO, &CSketchDoc::OnEditRedo)
    ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, &CSketchDoc::OnUpdateEditUndo)
    ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, &CSketchDoc::OnUpdateEditRedo)
END_MESSAGE_MAP()

void CSketchDoc::ExecuteCommand(std::unique_ptr<sm::ICommand> cmd) {
    m_undoRedo.Execute(std::move(cmd), m_model);
    SetModifiedFlag(TRUE);
    NotifyChanged();
}

void CSketchDoc::NotifyChanged() {
    UpdateAllViews(nullptr);
}

BOOL CSketchDoc::OnNewDocument() {
    if (!CDocument::OnNewDocument())
        return FALSE;
    m_model.Clear();
    m_undoRedo.Clear();
    return TRUE;
}

BOOL CSketchDoc::OnOpenDocument(LPCTSTR lpszPathName) {
    std::ifstream in(lpszPathName, std::ios::binary);
    if (!in)
        return FALSE;
    std::ostringstream buf;
    buf << in.rdbuf();

    auto parsed = sm::CanvasModel::FromJson(buf.str());
    if (!parsed) {
        AfxMessageBox(L"Failed to parse .skm file (unknown or corrupted format).",
                      MB_ICONERROR);
        return FALSE;
    }
    m_model = std::move(*parsed);
    m_undoRedo.Clear();
    SetModifiedFlag(FALSE);
    return TRUE;
}

BOOL CSketchDoc::OnSaveDocument(LPCTSTR lpszPathName) {
    std::ofstream out(lpszPathName, std::ios::binary | std::ios::trunc);
    if (!out) {
        AfxMessageBox(L"Failed to open file for writing.", MB_ICONERROR);
        return FALSE;
    }
    out << m_model.ToJson();
    if (!out.good())
        return FALSE;
    SetModifiedFlag(FALSE);
    return TRUE;
}

void CSketchDoc::OnEditUndo() {
    m_undoRedo.Undo(m_model);
    SetModifiedFlag(TRUE);
    NotifyChanged();
}

void CSketchDoc::OnEditRedo() {
    m_undoRedo.Redo(m_model);
    SetModifiedFlag(TRUE);
    NotifyChanged();
}

void CSketchDoc::OnUpdateEditUndo(CCmdUI* pCmdUI) {
    pCmdUI->Enable(m_undoRedo.CanUndo());
}

void CSketchDoc::OnUpdateEditRedo(CCmdUI* pCmdUI) {
    pCmdUI->Enable(m_undoRedo.CanRedo());
}
