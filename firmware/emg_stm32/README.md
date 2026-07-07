# EMG 프론트엔드 펌웨어 (NUCLEO-F103RB)

SketchMotion의 **STM32 = EMG 취득 보드** 역할 (설계 §15). sEMG를 1kHz로 샘플링해 PC로 스트리밍한다. 신호 처리 본체는 PC의 `Core::EmgProcessor`(무의존 Core 원칙).

## 배선 (MyoWare 2.0 기준)

| MyoWare | NUCLEO-F103RB |
|---|---|
| ENV (엔벌로프 출력) | PA0 (ADC1_IN0) |
| +Vs | 3V3 |
| GND | GND |

- MyoWare의 ENV(정류·엔벌로프된 아날로그)를 쓰면 STM32는 ADC만 하면 됨. RAW를 쓰면 PC의 EmgProcessor 고역/정류가 처리.
- 2채널(제스처 방향 구분)은 ENV→PA0, 두 번째 ENV→PA1(ADC1_IN1) 스캔.

## PC 연결

F103RB는 USB 디바이스가 없다 → **ST-LINK 가상 COM(USART2, PA2/PA3, 115200)**으로 스트리밍한다. PC측 `bridges/emg_bridge`(얇은 시리얼↔TCP 어댑터)가 이를 9103 포트로 중계 → App의 BridgeClient가 소비.

## 빌드 (팀)

STM32CubeIDE 프로젝트로 감싼다. CubeMX 설정:
- 클럭: HSE + PLL 64MHz
- ADC1_IN0 (PA0), 12bit, 폴링 또는 DMA
- TIM2: 1kHz 업데이트 인터럽트 → `on_sample_timer()`
- USART2: 115200 8N1, RX 인터럽트 → `on_uart_rx(byte)`
- SysTick 1ms → `g_ms` 증가

`main.c`는 "무엇을 하는지"(샘플링·스트리밍·명령처리)를 고정하고, `MX_*_Init()`·HAL 래퍼(`adc_read_ch0`/`uart_write`)는 CubeMX 생성 코드가 채운다.

## 검증 없이 되는 것 (하드웨어 전)

- MockBridge(`MockBridge.exe emg`)가 동일 프로토콜(포트 9103)로 합성 근수축 신호를 방출 → App/EmgProcessor 파이프라인을 하드웨어 없이 검증.
