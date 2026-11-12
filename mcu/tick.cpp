#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "tick.h"
#include "diagnostics.h"   // diag_tick_1ms
#include "flowmeter.h"     // 〔步驟 3〕flow_isr_1ms
#include "dispense.h"      // 〔步驟 5〕dispense_tick_1ms
#include "jsonfreeflow.h"  // 〔2026-11-12〕jff_tick_1ms
#include "pressure.h"      // 〔步驟 6〕pressure_tick_1ms

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
    diag_tick_1ms();       // 記憶體檢查 / mcuAlive 週期
    diag_flow_mirror_1ms();   // 〔暫時〕D66 與流量計 A8 同步(看 LED);停用時註解掉這行
    // 〔步驟 3〕流量計 debounce 在 ISR(flow_isr_1ms),這裡不需要
    dispense_tick_1ms();   // 〔步驟 5〕出杯狀態機(slot 倒數、到量 / 最短時間、逾時、Progress)
    jff_tick_1ms();        // 〔2026-11-12〕JSON freeflow:slot 依流量開關、每 200 ms 回報 pulse
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
