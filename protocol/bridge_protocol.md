# Bridge Protocol v1 (확정)

> 소유: D(플랫폼). 변경은 ADR + 전원 합의로만. 구현 참조: `protocol/ProtoJson.h`(PC), `bridges/arm_bridge/arm_bridge.py`(RPi), `firmware/agv_esp32/agv_esp32.ino`(ESP32), `bridges/mock_bridge`(테스트).

## 1. 전송 계층

- TCP, **한 줄 = JSON 객체 하나** (JSON Lines), 개행 `\n`, UTF-8.
- PC = 클라이언트, 브리지 = 서버. 포트: **Arm 9101, AGV 9102, EMG 9103** (Mock도 동일 포트, 호스트만 127.0.0.1).
- EMG 브리지는 STM32 프론트엔드를 감싼다. 실물은 **USB CDC(가상 COM)**로 연결되고, PC측 얇은 어댑터(`bridges/emg_bridge`)가 시리얼↔TCP를 중계한다. Mock은 TCP 직결.
- 재접속: PC가 2초 간격 재시도. 브리지는 동시 접속 1개만 허용(새 접속이 오면 이전 접속 종료).
- 숫자는 IEEE double, 좌표 단위는 **frame에 따라 mm(paper/robot) 또는 m(floor)**. 픽셀 좌표 금지.

## 2. 메시지 골격

```jsonc
// 요청 (PC → 브리지)
{ "id": 17, "cmd": "draw_paths", "params": { ... } }
// 응답 (브리지 → PC, id 대응. 처리 완료가 아니라 '수락' 의미)
{ "id": 17, "ok": true, "result": { ... } }
{ "id": 17, "ok": false, "error": "soft limit exceeded" }
// 자발 이벤트 (브리지 → PC, id 없음)
{ "event": "progress", "data": { ... } }
```

## 3. 명령셋

### 공통
| cmd | params | result | 비고 |
|---|---|---|---|
| `hello` | `{}` | `{device, proto, version}` | device: `"mycobot280"` \| `"agv_mecanum"` \| `"emg_stm32"` \| `"mock_*"`, proto: 1 |
| `stop` | `{}` | `{}` | 감속 정지, 명령 큐 폐기 |
| `estop` | `{}` | `{}` | 즉시 정지. Arm: 서보 릴리즈. AGV: PWM 0 |
| `telemetry` | `{}` | 장치별 §5 | 폴링용 (1Hz 권장) |

### Arm (9101)
| cmd | params |
|---|---|
| `home` | `{}` — 홈 포즈 이동 |
| `draw_paths` | `{ "frame":"paper", "unit":"mm", "feed":20, "travel":60, "paths":[[{ "x":10.0,"y":20.0 },...],...] }` |

- 브리지는 각 폴리라인을 5mm 간격 보간, 소프트리밋 AABB 검사 후 실행. 위반 시 `ok:false` + 전체 거부(부분 실행 금지).
- Z는 브리지 설정값(pen_down_z, pen_up_z) 사용 — PC는 Z를 모른다(스프링 순응 전제).

### AGV (9102)
| cmd | params |
|---|---|
| `follow_path` | `{ "frame":"floor", "unit":"m", "v_max":0.25, "path":[{"x":0.70,"y":0.55},...] }` |
| `set_pose_estimate` | `{ "x":1.23, "y":0.45, "theta":1.571, "cov":[9개 row-major], "t_cam":123456.789 }` — 30–90Hz 스트림 |

- `theta`: {floor} x축 기준 CCW 라디안, AGV 진행방향 +x.
- `t_cam`: PC 단조시계(초). 브리지는 수신시각과의 차로 파이프라인 지연을 관찰만 한다(보상은 PC EKF가 예측으로 수행).

### EMG (9103) — sEMG 프론트엔드 (STM32)
| cmd | params |
|---|---|
| `start_stream` | `{ "rate":1000 }` — 지정 레이트로 `emg` 이벤트 스트림 시작 |
| `stop_stream` | `{}` |
| `calibrate_mvc` | `{ "seconds":3 }` — 최대수의수축 창을 열어 MVC 기준 등록. 완료 시 `mvc` 이벤트 |

- 원 신호 처리(정류·엔벌로프·칼만·데드존)는 **PC의 Core::EmgProcessor**가 수행한다(브리지/펌웨어는 raw 또는 경량 엔벌로프만 올림 — v2 무의존 Core 원칙). STM32는 ADC 샘플링 + 1차 정류만.
- 안전: EMG는 관측 전용(구동 없음). 다만 EMG→그리퍼 폐루프 사용 시 Arm/AGV의 `estop`이 최우선.

## 4. 이벤트

| event | data | 발신 |
|---|---|---|
| `progress` | `{ "phase":"draw"|"path", "seg":3, "of":12, "pct":41.2 }` | Arm/AGV 실행 중 1–2Hz |
| `done` | `{ "phase":"draw"|"path" }` | 완주 시 |
| `fault` | `{ "reason":"watchdog"|"softlimit"|"hw" , "detail":"..." }` | 이상 시 |
| `emg` | `{ "raw":[..], "t":123.456 }` 또는 `{ "raw":512.0, "t":.. }` | EMG 스트림 (레이트 만큼, 배치 허용) |
| `mvc` | `{ "envelope": 0.83 }` | `calibrate_mvc` 완료 시 (PC가 EmgProcessor.SetMvc에 사용) |

## 5. telemetry result

- Arm: `{ "joints":[j1..j6 deg], "moving":bool, "queued":int }`
- AGV: `{ "pose_age_ms":int, "wheel":[w1..w4 pwm], "state":"idle"|"follow"|"fault" }`
- EMG: `{ "streaming":bool, "rate":int, "mvc_set":bool }`

## 6. 안전 규칙 (강제)

1. **AGV 워치독**: `follow_path` 활성 중 `set_pose_estimate` 수신 간격이 **500ms**를 넘으면 즉시 정지 + `fault{reason:"watchdog"}`.
2. **연결 워치독**: TCP 끊김 감지 시 두 장치 모두 즉시 정지.
3. Arm 소프트리밋: 브리지 로컬 설정(AABB, mm). PC가 우회할 수 없다.
4. `estop`은 큐·상태와 무관하게 최우선 처리 (수신 스레드에서 직접 실행).

## 7. 좌표계 요약 (DESIGN_V2 §4)

`{cam}` 픽셀 → (K, 왜곡) → `{paper}` mm / `{floor}` m (호모그래피) → `{robot}` mm (Kabsch T_pr).
프로토콜에 등장하는 frame은 `paper`(Arm), `floor`(AGV) 둘뿐이다.

## 8. Mock 확장 (테스트 전용 — 실물 브리지에는 없음)

MockBridge(AGV 모드)는 내부에서 참 동역학을 시뮬레이션하며, 카메라가 없는 환경에서
PC의 EKF 루프를 닫기 위해 **가상 마커 관측 이벤트**를 추가로 발행한다:

```jsonc
{ "event": "mock_pose", "data": { "x":1.23, "y":0.45, "theta":1.571 } }  // 노이즈 포함, 30Hz
```

PC는 이를 SM-Tag 검출 결과와 동일하게 EKF `Update()`에 공급한다. 실물 전환 시 이 이벤트는
카메라 파이프라인으로 대체되고 코드 경로는 동일하다 (측정 출처만 교체).

## 9. 버전 규칙

- `hello.proto` 불일치 시 PC는 연결을 끊고 사용자에게 업데이트 안내.
- 필드 추가는 하위호환(수신자는 미지 필드 무시). 의미 변경/삭제는 proto 증가 + ADR.
