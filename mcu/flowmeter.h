#ifndef K1_FLOWMETER_H
#define K1_FLOWMETER_H

/* ═══════════════════════════════════════════════════════════════════════
 *  flowmeter — 流量計(A8 = PK0)脈衝計數
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 3〕改為 1 ms 輪詢 + debounce(移植 K2 timer1_flowmeter(),架構計畫 4.2 / 4.3)
 *
 *  ▸ flow_isr_1ms()  由 Timer5 1 ms ISR 呼叫:直讀 PINK,高 / 低準位各需連續 3 次取樣(3 ms)一致,
 *                    確認為低準位(L = ON)的第一次才計 1 pulse;最高約 166 Hz(K2 實測約 8 Hz)
 *  ▸ 計數 uint32_t,滿值不再加;主迴圈讀取 / 歸零都以 ATOMIC_BLOCK 保護(AVR 讀 32-bit 要 4 次)
 *  ▸ 開機初始狀態同 K2(不特別處理)
 *
 *  舊版(〔步驟 2a〕前):PCINT16 腳位變化中斷、計上升緣、無 debounce、16-bit 非原子讀取
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

extern const double flowConstant;          // 每 pulse 的水量(公升;上位機 routes.cjs 以公升送水量)
/* 〔步驟 3〕改由 flow_get() / flow_take() 存取。舊宣告保留供對照:
extern volatile int flowPulse;             // ISR 遞增;pump_water(0) 時歸零
*/
extern volatile bool flowPulseOccurred;    // 有計到 pulse 時設 1(loop() 清除,原程式即無其他用途)

void     flow_init(void);                  // setup():清除計數(〔步驟 3〕不再開 PCINT16)
void     flow_isr_1ms(void);               // 只在 Timer5 ISR 內呼叫
uint32_t flow_get(void);                   // 目前累計的 pulse 數(原子讀取)
uint32_t flow_take(void);                  // 讀取並歸零(原子;停水回報用,不會漏掉讀與歸零之間的 pulse)

#endif
