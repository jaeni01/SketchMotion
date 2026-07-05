# SketchMotion 아키텍처

## 1. 설계 목표

이전 프로젝트(SketchTrade)의 실패 원인 — 기능 과욕(멀티플레이어 + 마켓플레이스 + 지갑), 계층 없는 구조, 시연 불가능 — 을 반성하고 다음 원칙으로 재설계했다.

1. **하나의 스토리**: "그린 것(또는 카메라로 본 것)을 기계가 그릴 수 있는 경로로 만든다." 모든 기능은 이 파이프라인에 복무한다.
2. **알고리즘과 UI의 완전한 분리**: 알고리즘은 창 핸들 없이 테스트할 수 있어야 한다.
3. **외부 라이브러리 0개**: 영상처리·기구학·경로계획 전부 직접 구현. 빌드는 VS + MFC 컴포넌트만으로 끝난다.

## 2. 계층 구조

```
┌────────────────────────────────────────────────────┐
│ App (MFC exe)                                      │
│  SketchMotionApp ─ CWinAppEx, GDI+ 수명 관리       │
│  MainFrame ─ CFrameWndEx, 도킹 패널, 상태바, 테마  │
│  SketchDoc ─ CanvasModel 소유, undo/redo, .skm IO  │
│  SketchView ─ GDI+ 더블버퍼 렌더링, 도구 입력      │
│  ToolPane / CameraPane / RobotSimPane              │
│  CameraCapture ─ Media Foundation 워커 스레드      │
├────────────────────────────────────────────────────┤
│ Core (정적 라이브러리, MFC/Win32 헤더 미포함)      │
│  Geometry, Stroke, CanvasModel(+JSON)              │
│  ImageBuffer, Filters, ContourTracer               │
│  PathSimplify, PathPlanner, GCodeWriter            │
│  ArmKinematics                                     │
├────────────────────────────────────────────────────┤
│ Tests (콘솔, 의존성 없는 assert 러너, 474 checks)  │
└────────────────────────────────────────────────────┘
```

의존 방향은 App → Core 단방향. Core는 `<afxwin.h>`도 `<windows.h>`도 모른다.

## 3. 핵심 설계 결정

### 3.1 Document/View + Command 패턴
- `CSketchDoc`이 `sm::CanvasModel`(순수 C++ 데이터)과 `UndoRedoManager`를 소유한다.
- **모든 편집은 `ExecuteCommand()` 단일 경로**로만 이루어진다. 뷰가 모델을 직접 수정하지 않으므로 undo 일관성이 구조적으로 보장된다.
- 지우개는 삭제된 `Stroke` 전체 사본을 커맨드에 보관해 undo 시 복원한다.

### 3.2 스레딩: 캡처 스레드 → UI 마샬링
- Media Foundation `IMFSourceReader`는 워커 스레드에서 동기 `ReadSample` 루프를 돈다.
- 프레임은 힙에 `BgraImage`로 복사 후 `PostMessage(WM_APP_CAMERA_FRAME, 0, ptr)`로 UI 스레드에 넘긴다. **소유권은 메시지와 함께 이동**하고, 수신측이 `unique_ptr`로 회수한다.
- 워커 스레드는 MFC 객체를 절대 만지지 않는다 (MFC 스레드 선호도 규칙).
- 종료는 `atomic<bool>` 플래그 + `join()`. 스트라이드 부호로 top-down/bottom-up 프레임을 모두 처리한다.

### 3.3 Vision 파이프라인 (전부 직접 구현)
```
BGRA → Grayscale(BT.601) → GaussianBlur(1-4-6-4-1 분리형)
     → Sobel magnitude → Threshold(Otsu 기반) 
     → Moore-neighbor 윤곽 추적 (Jacob's stopping criterion)
     → Ramer-Douglas-Peucker 단순화 (명시적 스택, 재귀 없음)
     → 캔버스 스케일링 → AddStrokesCommand (undo 가능)
```
- 윤곽 추적은 "왼쪽이 배경인 전경 픽셀"을 시작점으로 래스터 스캔하고, traced 마스크로 중복 추적을 막는다.
- RDP는 긴 윤곽에서의 스택 오버플로를 피하기 위해 재귀 대신 명시적 스택을 쓴다.

### 3.4 Motion 파이프라인
- **경로 순서 최적화**: 탐욕적 nearest-neighbor + 경로 방향 반전 허용. 펜업 이동거리를 통계로 산출해 미최적화 대비 절감률을 사용자에게 보여준다.
- **G-code**: 캔버스 px → 작업영역 mm 스케일링, y축 반전(화면↓ → 기계↑), G0/G1 + Z 승강. GRBL 계열 펜 플로터에서 그대로 사용 가능한 형식.
- **2링크 IK**: 코사인 법칙 해석해. elbow-up/down 두 해 중 선택, 도달성 검사(도넛 영역), IK→FK 왕복 오차를 단위테스트로 검증(< 1e-2).
- 시뮬레이터는 세그먼트를 (시작, 끝, 펜상태, 소요시간)으로 펼친 뒤 30ms 타이머로 진행한다. 드로잉 속도와 펜업 이동 속도를 다르게 둬서 실제 플로터의 거동을 흉내낸다.

### 3.5 직렬화 (.skm)
- 사람이 읽을 수 있는 JSON. 포맷을 우리가 완전히 통제하므로 파서는 이 스키마 전용 미니 파서(~150줄)로 충분하며, 알 수 없는 키는 건너뛰어 전방 호환성을 갖는다.
- 손상 입력은 `std::optional` 실패로 처리 — 예외/크래시 없음.

### 3.6 UI
- `CFrameWndEx` + `CDockablePane` 3종(도구/카메라/로봇). 장비 소프트웨어형 레이아웃.
- Office2007 ObsidianBlack 비주얼 매니저 + `DWMWA_USE_IMMERSIVE_DARK_MODE` 다크 타이틀바.
- Per-Monitor V2 DPI 매니페스트.
- 뷰 렌더링은 GDI+ 더블 버퍼(`OnEraseBkgnd` 차단 + 메모리 DC BitBlt). 줌은 커서 기준 앵커 줌.

## 4. 테스트 전략

Tests.exe는 외부 프레임워크 없이 `CHECK`/`CHECK_NEAR` 매크로로 474개 검증을 수행한다.

| 대상 | 검증 내용 |
|---|---|
| RDP | 직선 축소, 코너 보존, **최대 편차 ≤ ε 불변식** |
| 윤곽 추적 | 사각형 둘레 픽셀 수 정확 일치(36), 분리 도형 2개, 빈 이미지 |
| IK | 5개 목표점 × elbow up/down IK→FK 왕복, 링크 길이 보존, 도달 불가 거부 |
| 경로 계획 | 최적 순서의 이동거리 수치 검증, 방향 반전 동작 |
| G-code | 좌표 매핑/ y반전 수치 검증, 빈 입력 처리 |
| 직렬화 | 왕복 무손실, 손상 입력 3종 거부 |
| 통합 | 합성 원 이미지 → 윤곽 → 단순화 → 경로 → G-code 전체 파이프라인 |

## 5. 알려진 한계 / 다음 단계

- 윤곽 추적은 외곽선만 추출한다 (구멍/내부 윤곽은 미지원 — 필요 시 Suzuki-Abe로 확장).
- 로봇팔은 2링크 평면 모델이다. 3링크/손목 자세까지 확장하려면 IK를 자코비안 기반으로 교체.
- 시리얼(COM) 포트로 GRBL에 직접 스트리밍하는 기능은 G-code 파일 내보내기로 대체했다 (하드웨어 없이 검증 불가한 코드를 넣지 않는다는 원칙).
