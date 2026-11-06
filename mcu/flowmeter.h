#ifndef K1_FLOWMETER_H
#define K1_FLOWMETER_H

/* ═══════════════════════════════════════════════════════════════════════
 *  flowmeter — 流量計(A8 = PK0)脈衝計數
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由 mcu.ino 原樣搬來,行為不變:PCINT16 腳位變化中斷,計上升緣。
 *  步驟 3 會改為 1 ms 輪詢 + debounce、計數改 uint32_t 並加原子讀取。
 * ═══════════════════════════════════════════════════════════════════════ */

extern const double flowConstant;          // 每 pulse 的水量(單位未定,見架構計畫 9.3)
extern volatile int flowPulse;             // ISR 遞增;pump_water(0) 時歸零
extern volatile bool flowPulseOccurred;

void flow_init(void);                      // setup():開 PCINT16 腳位變化中斷

#endif
