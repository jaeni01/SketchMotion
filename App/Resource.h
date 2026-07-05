#pragma once
// Resource.h - 리소스 ID 정의

#define IDR_MAINFRAME                   128

// 도구
#define ID_TOOL_PEN                     32771
#define ID_TOOL_LINE                    32772
#define ID_TOOL_RECT                    32773
#define ID_TOOL_ELLIPSE                 32774
#define ID_TOOL_ERASER                  32775

// 파일/모션/비전
#define ID_FILE_EXPORT_PNG              32781
#define ID_MOTION_EXPORT_GCODE          32782
#define ID_MOTION_SIMULATE              32783
#define ID_VISION_TRACE                 32784
#define ID_VISION_LOAD_TEST_IMAGE       32785

// 뷰(패널 토글)
#define ID_VIEW_TOOL_PANE               32791
#define ID_VIEW_CAMERA_PANE             32792
#define ID_VIEW_ROBOT_PANE              32793

// 카메라 패널 컨트롤
#define ID_CAM_START                    32801
#define ID_CAM_STOP                     32802
#define ID_CAM_FILTER_COMBO             32803
#define ID_CAM_TRACE                    32804

// 로봇 패널 컨트롤
#define ID_ROBOT_PLAY                   32811
#define ID_ROBOT_STOP                   32812

// 도구 패널 컨트롤
#define ID_TOOLPANE_COLOR               32821
#define ID_TOOLPANE_WIDTH_COMBO         32822
#define ID_TOOLPANE_FIRST_TOOL          32831   // +0..+4 = 도구 버튼 5개

// 상태바
#define ID_INDICATOR_TOOL               59142
#define ID_INDICATOR_POS                59143
#define ID_INDICATOR_ZOOM               59144
#define ID_INDICATOR_STROKES            59145
