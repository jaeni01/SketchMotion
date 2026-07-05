#pragma once
// framework.h - MFC 공통 include (모든 App 소스가 가장 먼저 포함)

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#define NOMINMAX

#include <afxwin.h>
#include <afxext.h>
#include <afxcontrolbars.h>   // CFrameWndEx, CDockablePane, CMFCToolBar 계열

#include <objidl.h>
#include <gdiplus.h>

#include <memory>
#include <vector>
