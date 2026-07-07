# SketchMotion 코딩 규약 (Conventions)

> 4인 팀의 단일 코딩 표준. 재니가 정리한 "소스/헤더 분리 실무 원칙"(For_Being_Better_ProGrammer 시리즈 + 실무 교과서)에서 **이 프로젝트에 실제로 필요한 규칙만 채취해** 우리 구조에 맞춰 확정한 것. 원칙의 근거·배경은 원문 참조, 여기는 "우리가 지킨다"의 강제 목록.

## 0. 한 줄 철학 (원문에서)

> **헤더는 복도가 아니라 출입구다.** 꼭 필요한 것만 통과시킨다.
> 초보는 *파일* 단위로, 고수는 *경계(boundary)* 단위로 생각한다. 우리의 경계 = **Core(무의존 계약) / App(MFC) / bridges·firmware(원격) / protocol(공유 DTO)**.

## 1. 계층과 의존 방향 (강제)

의존은 **한 방향**으로만 흐른다. 아래 계층은 위 계층을 절대 include하지 않는다.

```
UI (MFC 패널·뷰)  →  제어 (Orchestrator·Reporter)  →  I/O (Capture·Bridge·Vision)  →  Core
                                                                     protocol/ (App·bridge 공유 계약)
```

- **Core는 windows.h·afxwin.h·소켓·OpenCV를 모른다.** 순수 C++만. (이미 준수 — `tools/check_selfcontained.ps1`로 검증)
- 하위가 상위 이벤트를 알려야 하면 **콜백/PostMessage 마샬링**으로 역전한다 (드라이버가 비즈니스 로직을 include하지 않음). App은 `WM_APP_*` 메시지로 워커→UI를 잇는다.

## 2. 헤더에 넣는 것 / 넣지 않는 것

**헤더(.h)**: 타입 선언, 함수/메서드 프로토타입, `constexpr`·`enum class`, 1~3줄 인라인, 그 헤더가 쓰는 다른 헤더의 include.
**소스(.cpp)**: 함수 몸체, 파일 전용 헬퍼는 **익명 namespace**(= C++판 `static`), 파일 전용 상수.

금지: 헤더에 함수 정의(짧은 인라인 제외), 비-inline 전역 변수 정의, 내부 헬퍼 선언 노출.

## 3. 자기충족성 (황금률 — CI 게이트)

**어떤 헤더든 그것 하나만 include해도 컴파일돼야 한다.** 사용자가 include 순서를 외우게 만들지 않는다.
- 강제: `tools/check_selfcontained.ps1` (ci.ps1 1.5단계). Core 헤더 각각을 독립 TU로 컴파일해 통과 확인. 현재 14/14 통과.

## 4. 모듈 응집도 — "한 모듈은 한 가지 일"

`utils.h` 같은 쓰레기통 금지. 분리 기준: **"그리고(and)" 없이 한 문장으로 설명되는가?**
- ✅ `KalmanFilter`("AGV 자세를 추정한다"), `MarkerDetector`("SM-Tag를 검출한다"), `PathTracker`("경로를 따라간다").
- 새 모듈을 만들 때 이 문장 테스트를 통과하지 못하면 쪼갠다.

## 5. 팬아웃 최소화

헤더가 include하는 다른 헤더 수(fan-out)를 줄인다. 포인터/참조만 쓰는 타입은 **전방 선언**, 실제 include는 .cpp로 내린다. (Core 헤더는 대부분 `Geometry.h`·표준 라이브러리에만 의존 — 유지.)

## 6. 네이밍 (프로젝트 확정값)

| 대상 | 규칙 | 예 |
|---|---|---|
| 파일 | 타입명과 일치 (PascalCase.h/.cpp) | `PathTracker.cpp` |
| Core 네임스페이스 | `namespace sm` | `sm::AgvEkf` |
| C++ 멤버 변수 | `m_` 접두 (MFC/Microsoft 스타일) | `m_missionActive` |
| 상수 | `k` 접두 | `kMargin`, `WM_APP_BRIDGE` |
| 브리지 프로토콜 필드 | snake_case (JSON 관례) | `set_pose_estimate`, `v_max` |
| ESP32/Arduino | Arduino 관례 유지 | `controlTask`, `driveBody` |

- **static 대신 익명 namespace** (.cpp 내부): 원문 4편 권장, C++ 관용.
- 공개 함수엔 의미가 드러나는 이름. 접두 없는 자유함수는 "내부용" 신호 → 익명 namespace로.

## 7. 우리가 의도적으로 **안** 따르는 것 (원문 대비)

원문은 학습 교과서라 범용적이다. 우리 규모(단일 조직 데스크톱 + 임베디드)에 과한 것은 뺀다.
- **PImpl / opaque pointer / ABI·SemVer**: 공유 라이브러리 배포가 없으므로 불필요. (원문 C 교과서 파트) — v3에서 SDK로 배포하게 되면 재도입.
- **`#pragma once` + include guard 벨트-앤-서스펜더**: `#pragma once` 단독 사용 (MSVC/Clang/GCC 전부 지원, 데스크톱 대상).
- **C++20 modules**: MFC/레거시 호환 위해 헤더 유지 (설계서 §8.4 기조와 동일).

## 8. 리뷰 체크리스트 (PR 셀프체크)

- [ ] 새 헤더가 자기충족적인가? (`check_selfcontained.ps1` 통과)
- [ ] Core에 windows/소켓/OpenCV 의존이 새로 들어가지 않았는가?
- [ ] 새 모듈이 "그리고" 없이 한 문장으로 설명되는가?
- [ ] 의존 방향이 UI→제어→I/O→Core를 거스르지 않는가?
- [ ] 헤더에 함수 몸체(짧은 인라인 외)나 전역 정의를 넣지 않았는가?
