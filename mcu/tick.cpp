#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "tick.h"
#include "diagnostics.h"   // diag_tick_1ms
#include "flowmeter.h"     // 〔步驟 3〕flow_isr_1ms
#include "pressure.h"      // 〔步驟 6〕pressure_tick_1ms
#include "uart_cmd.h"      // 〔K2 移植 P1〕uart_tick_1ms
#include "timer_task.h"    // 〔K2 移植 P2〕timer1_SET_Count、Tmr1_Interrupt_main

static volatile uint16_t tick_cnt = 0;   // 只有 ISR 寫
static uint16_t tick_done = 0;           // 只有主迴圈寫

#ifdef K1_DEBUG_TICK
static uint16_t backlog_max = 0;
uint16_t tick_backlog_max(void) { return backlog_max; }
#endif

/* 1 ms tick:CTC 模式(TOP = OCR5A),16 MHz / 64 / 250 = 1000 Hz
 * Arduino init() 預設把 Timer5 設成 8-bit PWM,所以 TCCR5A / TCCR5B 必須用 = 整個覆蓋 */
void tick_init(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        TCCR5A = 0;
        TCCR5B = (1 << WGM52) | (1 << CS51) | (1 << CS50);   // CTC、除頻 64
        OCR5A  = 249;
        TCNT5  = 0;
        TIFR5  = (1 << OCF5A);                               // 清掉可能殘留的比較旗標
        TIMSK5 = (1 << OCIE5A);                              // 開比較 A 中斷
    }
}

ISR(TIMER5_COMPA_vect)
{
    tick_cnt++;
    flow_isr_1ms();        // 〔步驟 3〕流量計 1 ms 取樣 + debounce(只讀 PINK,不寫輸出)
}

uint16_t tick_get(void)
{
    uint16_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = tick_cnt; }   // 16-bit 在 AVR 要讀 2 次
    return v;
}

/* 每 1 ms 的工作:依序呼叫各模組(之後的步驟在這裡加) */
static void tick_main(void)
{
    diag_tick_1ms();       // D23 心跳
    // 〔步驟 3〕流量計 debounce 在 ISR(flow_isr_1ms),這裡不需要
    uart_tick_1ms();       // 〔K2 移植 P1〕收包逾時倒數(K2 在 TIM1 中斷內)
    timer1_SET_Count();    // 〔K2 移植 P2〕0x00 倒數(K2 在 TIM1 中斷內;方案 A 改在這裡)
    timer1_pulse_count();  // 〔K2 移植 P4〕0x06 扣脈衝(K2 在流量計中斷內;K1 以總計數差值)
    Tmr1_Interrupt_main(); // 〔K2 移植 P2〕倒數到 0 的裝置關閉(K2 在主迴圈看 timer1_flag)
    pressure_tick_1ms();   // 〔步驟 6〕水壓:每 200 ms 啟動一次 ADC 轉換,完成後存進 last_raw
}

void tick_service(void)
{
    uint16_t now = tick_get();             // 取一次快照:補到這個值為止,本次執行時間有上限
#ifdef K1_DEBUG_TICK
    uint16_t backlog = now - tick_done;    // 16-bit 相減,繞回時仍正確
    if (backlog > backlog_max) backlog_max = backlog;
#endif
    while (tick_done != now) {
        tick_done++;
        tick_main();
    }
}
