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
