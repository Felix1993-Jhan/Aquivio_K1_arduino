#include <Arduino.h>
#include <util/atomic.h>
#include "flowmeter.h"
#include "pins.h"          // FLOW_PIN_READ

// Flowmeter variables
// const int flowmeterPin = A8;   // 〔步驟 1〕改用 pins.h 的 ID_FLOWMETER / FLOW_PIN_READ()
/* 〔步驟 2d〕對齊 old/mcu31v2:流量常數改為 0.0033(mcu31v2 註解「依實測校正」)。舊值保留供對照:
const double flowConstant = 0.0037;
*/
/* 〔步驟 4b〕不用浮點,改為 flowmeter.h 的 FLOW_UL_PER_PULSE(3300 µL = 0.0033 L)。舊定義保留供對照:
const double flowConstant = 0.0033;   // L per pulse(依實測校正,同 mcu31v2)
*/
/* 〔步驟 3〕計數改為 uint32_t + 原子存取。舊變數保留供對照:
volatile int flowPulse = 0;
*/
static volatile uint32_t flow_cnt = 0;   // 只有 flow_isr_1ms() 遞增;主迴圈經 flow_get / flow_take 存取
volatile bool flowPulseOccurred = false;

/* 〔步驟 3〕不再使用 PCINT 腳位變化中斷,改由 Timer5 每 1 ms 取樣(flow_isr_1ms)。
 * 原因:舊版每個邊緣都觸發、沒有 debounce,接點彈跳或雜訊會被多計(老闆反映不穩)。舊程式保留供對照:
void flow_init(void) {
    PCICR |= (1 << PCIE2);   // Enable pin change interrupt for A8 to A15
    PCMSK2 |= (1 << PCINT16);  // Enable pin change interrupt for A8 specifically
}

ISR(PCINT2_vect) {
    int currentFlowState = FLOW_PIN_READ() ? HIGH : LOW;   // 〔步驟 1〕直讀 PINK,取代 digitalRead
    static int lastFlowState = LOW;
    if (lastFlowState == LOW && currentFlowState == HIGH) {
        flowPulse++;
        flowPulseOccurred = true;
    }
    lastFlowState = currentFlowState;
}
*/

void flow_init(void) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { flow_cnt = 0; }
#ifdef K1_DEBUG_FLOW_GEN
    PORTA |= _BV(PA0);                         // D22 先定高準位(同流量計靜止),再設輸出
    DDRA  |= _BV(PA0);                         // 〔2026-11-09〕原 D23(PA1),D23 改當心跳
#endif
}

#ifdef K1_DEBUG_FLOW_GEN
/* 除錯用:D22(PA0)模擬流量計方波,只在 Timer5 ISR 內呼叫(在 flow_isr_1ms 開頭)
 * 寫 1 到 PINA = 翻轉,單一寫入;PORTA |= 為 SBI 單指令 → 都不是讀改寫,
 * 不會和主迴圈翻轉同一個 Port A 的 D23 心跳(PA1)互相蓋掉 */
static void flow_gen_1ms(void) {
    static uint8_t ms = 0, half = 83;
    if (PINK & _BV(PK5)) {                     // waterpump(A13 = PK5)開著 → 產生方波
        if (++ms >= half) {
            ms = 0;
            half = (half == 83) ? 84 : 83;     // 83 + 84 = 167 ms 一個週期 ≈ 6 Hz
            PINA = _BV(PA0);                   // 翻轉 D22
        }
    } else {                                   // 泵關著 → 回到高準位、重新計時
        ms = 0;
        PORTA |= _BV(PA0);                     // Port A 在 I/O 區,常數位址 → SBI 單指令(原子)
    }
}
#endif

/* 移植 K2 Core/Src/stm32u0xx_it.c timer1_flowmeter()(79~115 行):
 *   H = OFF:累計 off_ms 到 2 後,下一次取樣解除鎖定(latched = 0)
 *   L = ON :累計 on_ms 到 2 後,下一次取樣(第 3 ms)若尚未鎖定 → 計 1 pulse 並鎖定
 * 只在 ISR 內執行,static 變數不需原子保護 */
void flow_isr_1ms(void) {
#ifdef K1_DEBUG_FLOW_GEN
    flow_gen_1ms();                            // 除錯:先更新 D22 模擬波形(接到 A8)
#endif
    static uint8_t on_ms = 0, off_ms = 0, latched = 0;
    if (FLOW_PIN_READ()) {                     // H = OFF(上拉,未導通)
        on_ms = 0;
        if (off_ms < 2) off_ms++;
        else            latched = 0;
    } else {                                   // L = ON
        off_ms = 0;
        if (on_ms < 2) on_ms++;
        else if (!latched) {
            latched = 1;
            if (flow_cnt < 0xFFFFFFFFUL) flow_cnt++;
            flowPulseOccurred = true;
        }
    }
}

uint32_t flow_get(void) {
    uint32_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = flow_cnt; }
    return v;
}

uint32_t flow_take(void) {
    uint32_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = flow_cnt; flow_cnt = 0; }
    return v;
}
