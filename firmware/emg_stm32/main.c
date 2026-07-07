/*
 * main.c - SketchMotion EMG 프론트엔드 (NUCLEO-F103RB)
 *
 * 역할(설계 §15): 표면 근전(sEMG) 신호를 하드 리얼타임으로 샘플링해
 *   PC(Core::EmgProcessor)로 스트리밍하는 취득 보드. STM32의 교과서적 용도
 *   (ADC + 타이머 + USB CDC) 그대로. 신호 처리 본체는 PC에 있음(무의존 Core 원칙).
 *
 * 데이터 경로:
 *   MyoWare 2.0 (또는 유사 sEMG) ENV/RAW 출력 → PA0 (ADC1_IN0)
 *   TIM2 1kHz 인터럽트 → ADC 1샘플 → 링버퍼
 *   메인 루프 → USB CDC(가상 COM)로 JSON Lines: {"raw":<0..4095>,"t":<ms>}\n
 *   PC의 emg_bridge(시리얼↔TCP 어댑터)가 이를 9103 TCP로 중계.
 *
 * !! 실물 배선/클럭은 STM32CubeMX로 생성한 HAL 초기화로 대체 (아래는 골격) !!
 *   - 이 파일은 "무엇을 하는지"를 고정하고, MX_*_Init()은 CubeMX가 채운다.
 *   - F103RB는 USB가 없다(대신 ST-LINK VCP: USART2 @ PA2/PA3, 115200) → 실제로는
 *     USART2 VCP로 스트리밍한다. USB CDC 대신 uart_puts()를 사용.
 *
 * TODO(팀, Sprint):
 *   1) CubeMX: HSE/PLL 64MHz, ADC1_IN0(PA0), TIM2 1kHz, USART2 115200
 *   2) MyoWare ENV 핀 → PA0, GND/3V3 공통. RAW를 쓰려면 고역/정류를 PC에 맡김.
 *   3) 2채널이면 ADC1_IN0/IN1 스캔 + 배치 전송.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* CubeMX가 생성하는 HAL 핸들 (여기선 선언만; main 통합 시 실제 정의 사용) */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_ADC1_Init(void);
extern void MX_TIM2_Init(void);
extern void MX_USART2_UART_Init(void);
extern uint16_t adc_read_ch0(void);         /* HAL_ADC 폴링/DMA 래퍼 */
extern void uart_write(const uint8_t *buf, uint16_t len); /* USART2 송신 */
extern volatile uint32_t g_ms;              /* SysTick 1ms 카운터 */

/* ── 링버퍼 (ISR 생산 / 메인 소비) ── */
#define RB_SIZE 256
static volatile uint16_t rb_val[RB_SIZE];
static volatile uint32_t rb_ts[RB_SIZE];
static volatile uint16_t rb_head = 0, rb_tail = 0;

static volatile uint8_t g_streaming = 0;

/* TIM2 1kHz 인터럽트 핸들러 (CubeMX HAL 콜백에서 호출) */
void on_sample_timer(void) {
    if (!g_streaming) return;
    const uint16_t next = (uint16_t)((rb_head + 1) % RB_SIZE);
    if (next == rb_tail) return;           /* 오버런: 드롭 (PC가 지연 감지) */
    rb_val[rb_head] = adc_read_ch0();      /* 12bit 0..4095 */
    rb_ts[rb_head] = g_ms;
    rb_head = next;
}

/* ── 수신 명령 파싱 (라인 단위, 초경량) ── */
static char rx_line[128];
static uint16_t rx_len = 0;

static void send_line(const char *s) {
    uart_write((const uint8_t *)s, (uint16_t)strlen(s));
}

/* 아주 단순한 substring 매칭 (완전 JSON 파서는 PC측에만) */
static void handle_command(const char *line) {
    if (strstr(line, "\"start_stream\"")) {
        rb_head = rb_tail = 0;
        g_streaming = 1;
        send_line("{\"id\":0,\"ok\":true,\"result\":{}}\n");
    } else if (strstr(line, "\"stop_stream\"")) {
        g_streaming = 0;
        send_line("{\"id\":0,\"ok\":true,\"result\":{}}\n");
    } else if (strstr(line, "\"hello\"")) {
        send_line("{\"id\":0,\"ok\":true,\"result\":{\"device\":\"emg_stm32\",\"proto\":1,\"version\":\"emg-fw-0.1\"}}\n");
    } else if (strstr(line, "\"calibrate_mvc\"")) {
        /* MVC 계산은 PC가 스트림에서 수행 → 여기선 ack만 */
        send_line("{\"id\":0,\"ok\":true,\"result\":{\"note\":\"PC computes MVC from stream\"}}\n");
    } else {
        send_line("{\"id\":0,\"ok\":false,\"error\":\"unknown\"}\n");
    }
}

/* USART2 수신 콜백 (1바이트씩, CubeMX HAL RxCpltCallback에서 호출) */
void on_uart_rx(uint8_t byte) {
    if (byte == '\n') {
        rx_line[rx_len] = '\0';
        if (rx_len > 1) handle_command(rx_line);
        rx_len = 0;
    } else if (rx_len < sizeof(rx_line) - 1) {
        rx_line[rx_len++] = (char)byte;
    }
}

int main(void) {
    SystemClock_Config();
    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_TIM2_Init();
    MX_USART2_UART_Init();

    char out[48];
    for (;;) {
        /* 링버퍼 소비 → JSON Lines 송신 (배치로 묶으면 대역폭 절약) */
        while (rb_tail != rb_head) {
            const uint16_t v = rb_val[rb_tail];
            const uint32_t t = rb_ts[rb_tail];
            rb_tail = (uint16_t)((rb_tail + 1) % RB_SIZE);
            /* 이벤트 형식: {"event":"emg","data":{"raw":V,"t":T}} */
            int n = snprintf(out, sizeof(out),
                             "{\"event\":\"emg\",\"data\":{\"raw\":%u,\"t\":%lu}}\n",
                             (unsigned)v, (unsigned long)t);
            if (n > 0) uart_write((const uint8_t *)out, (uint16_t)n);
        }
    }
}
