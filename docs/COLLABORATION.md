# 협업 툴 운영 문서 (Collaboration Tooling)

SketchMotion 프로젝트의 이슈 트래킹·문서화에 어떤 툴을 왜 골랐고, 어떻게 쓰고 있는지 기록한다.
(작성: 2026-07-05, Claude Code 세션에서 초기 세팅)

## 1. 툴 선정과 근거

보유 계정: Jira(KAN 보드), Confluence, Atlassian Home, Linear.

| 툴 | 결정 | 근거 |
|---|---|---|
| **Jira** (`KAN` / My_Pro) | ✅ **이슈 트래킹의 단일 소스 오브 트루스** | 이미 보드가 있고 Epic→Task 계층·워크플로(해야 할 일→진행 중→검토 중→완료)가 프로젝트 규모에 맞음 |
| **Linear** | ❌ 사용 안 함 | Jira와 역할이 100% 중복. 이슈 트래커를 두 개 운영하면 어느 쪽도 진실이 아니게 됨 — 현업에서 대표적인 안티패턴. 팀이 Linear로 통일하는 경우가 아니면 병행 금지 |
| **Confluence** | ✅ 프로젝트 개요/공유용 | 커넥터 재연결로 Confluence 스코프 승인 후 사용 (아래 §5 참고). **개발 문서(설계·사용법)는 리포 docs/에 코드와 함께 버전 관리**하고, Confluence에는 이해관계자 공유용 개요 페이지만 둔다 — 문서 이원화를 막기 위해 상세 내용은 리포를 단일 원본으로 유지 |
| **Atlassian Home** | 참고용 | 대시보드일 뿐 별도 운영 대상 아님 |

## 2. Jira 운영 규칙

- **계층**: Epic = 릴리스 단위, Task/Story = 작업 단위. 모든 이슈는 Epic 하위에 둔다.
- **라벨**: `core`(알고리즘) / `ui` / `vision` / `motion` / `infra` / `docs` / `hardware` — 요약 앞에도 `[라벨]` 접두어를 붙여 보드에서 한눈에 구분.
- **설명 작성 규칙**: 무엇을 했는지(불릿) + 겪은 이슈와 해결(예: "passive 설치는 승격 필수 — exit 5007") + 검증 방법. 커밋 해시를 함께 적는다 (예: `ed9077e`).
- **상태 전이**: 작업 착수 시 진행 중, 코드 리뷰/검증 대기면 검토 중, DoD 충족 시 완료. 완료 처리 전에 반드시 검증 증거(테스트 통과, 스크린샷)가 이슈에 적혀 있어야 한다.

## 3. 현재 등록된 이슈 (2026-07-05 기준)

### Epic
- [KAN-4](https://mnewpro.atlassian.net/browse/KAN-4) SketchMotion v1.0 — MFC Vision-to-Motion 드로잉 슈트 (진행 중 — 백로그 잔여)

### 완료 (v1.0 구현 이력)
- [KAN-5](https://mnewpro.atlassian.net/browse/KAN-5) [infra] 빌드 환경 구축 — VS2022 MFC 컴포넌트, 솔루션 구성
- [KAN-6](https://mnewpro.atlassian.net/browse/KAN-6) [core] 알고리즘 라이브러리 + 단위테스트 474 checks
- [KAN-7](https://mnewpro.atlassian.net/browse/KAN-7) [ui] MFC 앱 셸 + 드로잉 코어
- [KAN-8](https://mnewpro.atlassian.net/browse/KAN-8) [ui] Docking Pane 3종 + 다크 테마 + DPI
- [KAN-9](https://mnewpro.atlassian.net/browse/KAN-9) [vision] MF 웹캠 캡처 + 필터 + 트레이싱
- [KAN-10](https://mnewpro.atlassian.net/browse/KAN-10) [motion] 경로 최적화 + G-code + 로봇팔 시뮬
- [KAN-11](https://mnewpro.atlassian.net/browse/KAN-11) [docs] README/ARCHITECTURE/스크린샷

### 백로그 (v1.1+)
- [KAN-12](https://mnewpro.atlassian.net/browse/KAN-12) [vision] 실제 웹캠 실기 검증
- [KAN-13](https://mnewpro.atlassian.net/browse/KAN-13) [motion] GRBL 시리얼 스트리밍 (하드웨어 필요)
- [KAN-14](https://mnewpro.atlassian.net/browse/KAN-14) [vision] Suzuki-Abe 내부 윤곽 지원
- [KAN-15](https://mnewpro.atlassian.net/browse/KAN-15) [docs] GitHub 공개 + 릴리스 패키징

## 4. 문서 배치 원칙

| 문서 종류 | 위치 | 이유 |
|---|---|---|
| 설계/아키텍처 | `docs/ARCHITECTURE.md` (리포) | 코드와 함께 변경·리뷰되어야 함 |
| 사용법/소개 | `README.md` (리포) | GitHub 첫 화면 |
| 작업 이력·계획 | Jira (KAN) | 상태·기간·계층 관리는 트래커의 일 |
| 협업 운영 규칙 | 본 문서 | 툴 선택 근거가 유실되지 않도록 |
| 프로젝트 개요(공유용) | Confluence [SD 스페이스: SketchMotion v1.0](https://mnewpro.atlassian.net/wiki/spaces/SD/pages/164042) | 이해관계자에게 보여줄 요약 — 상세는 리포로 링크 |
| 회의록·기획 초안 | (미래) Confluence | 코드와 수명이 다른 문서가 생기면 |

## 5. Confluence 연동 (해결 완료)

최초에는 403 "The app is not installed on this instance" 발생 — 커넥터의 승인 스코프가 Jira(read/write:jira-work)뿐이었기 때문.
**해결(2026-07-05)**: claude.ai 커넥터 설정에서 Atlassian을 Disconnect → 재연결하면서 Confluence 권한(read/write:page, space, search 등)을 승인. Atlassian admin 쪽은 애초에 문제 없었음(계정에 Confluence Premium 접근 보유).
이후 `getConfluenceSpaces` → `createConfluencePage` 흐름으로 [SketchMotion v1.0 개요 페이지](https://mnewpro.atlassian.net/wiki/spaces/SD/pages/164042) 생성 완료 (SD "Software Development" 스페이스).

## 6. 커밋 ↔ 이슈 연결 규칙 (앞으로)

- 커밋 메시지에 이슈 키를 포함: `KAN-14: Suzuki-Abe contour tracing` — GitHub 연동 시 Jira 개발 패널에 자동 링크됨.
- PR 단위 = 이슈 단위를 원칙으로 한다.
