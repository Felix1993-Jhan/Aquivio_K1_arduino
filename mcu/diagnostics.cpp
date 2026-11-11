#include <Arduino.h>
#include <avr/wdt.h>
#include "diagnostics.h"
#include "pins.h"          // pin_toggle(ID_HEARTBEAT)

#define HEARTBEAT_MS  1000u   // D23 心跳:每 1 秒翻轉一次(0.5 Hz)

static uint16_t heartbeat_ms = 0;   // 只在 tick_main(主迴圈)內修改

void setupWatchdog() {
    cli();  // Disable interrupts
    wdt_reset();  // Reset Watchdog Timer
    MCUSR &= ~(1<<WDRF);  // Clear WDRF in MCUSR
    WDTCSR |= (1<<WDCE) | (1<<WDE);  // Set WDCE (Watchdog Change Enable)
    WDTCSR = (1<<WDE) | (1<<WDP3) | (1<<WDP0);  // Set WDE, and 8 seconds delay
    sei();  // Enable interrupts
}

/* 在主迴圈(tick_main)翻轉而非 ISR → 主迴圈卡住時心跳停止;補跑 tick 時仍準時。
 * pin_toggle = 寫 PINx 單次寫入,不是讀改寫,不會和 ISR 寫 Port A(除錯版 D22)衝突 */
void diag_tick_1ms(void) {
    if (++heartbeat_ms >= HEARTBEAT_MS) { heartbeat_ms = 0; pin_toggle(ID_HEARTBEAT); }
}

/* 〔2026-11-11 新增,暫時〕D66(bevvalve,轉接板上沒接負載,只看 LED)與流量計 A8 同步:A8 高 → D66 高。
 * 跟原始腳位(未 debounce)。在主迴圈每 1 ms 做:D66 與水泵 / 閥同在 Port K,pin_write 為讀改寫,
 * 所有 Port K 寫入都在主迴圈才不會互相蓋掉(不放 Timer5 ISR)。
 * 要停用:把 tick_main 裡的 diag_flow_mirror_1ms() 呼叫註解掉 */
void diag_flow_mirror_1ms(void) {
    pin_write(ID_BEVVALVE, FLOW_PIN_READ() != 0);
}
