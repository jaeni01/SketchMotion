#pragma once
// SketchMotionApp.h - 애플리케이션 클래스
#include "framework.h"

class CSketchMotionApp : public CWinAppEx {
public:
    BOOL InitInstance() override;
    int ExitInstance() override;

private:
    ULONG_PTR m_gdiplusToken = 0;
};

extern CSketchMotionApp theApp;
