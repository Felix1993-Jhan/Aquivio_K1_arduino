#ifndef K1_TICK_H
#define K1_TICK_H

/* ═══════════════════════════════════════════════════════════════════════
 *  tick — 1 ms 計時(Timer5 比較中斷)與每 1 ms 的工作分派
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2b〕架構計畫第 3、4.1 節
 *
 *  ▸ ISR(TIMER5_COMPA_vect):只做 tick_cnt++ 與流量計取樣 debounce(〔步驟 3〕flow_isr_1ms)
 *  ▸ tick_service():loop() 呼叫,把累積未處理的 tick 逐一補跑 tick_main()
 *      主迴圈被卡住時,時間在 ISR 內繼續累加,回來時補齊,不會少算
 *  ▸ tick_main():每個 tick 依序呼叫各模組的 xxx_tick_1ms()
 *      只做計數、設旗標,不輸出字串(輸出由各模組的 poll 函式在 loop() 做)
 *  ▸ tick_cnt 為 16-bit(約 65 秒才繞回),讀取以 ATOMIC_BLOCK 保護
 *
 *  Timer0 留給 Arduino millis() / delay(),不動。
 *  Timer5 的 OC5A~C(D46/D45/D44)為空腳,CTC 模式不接腳位。
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

/* 除錯用:取消註解後記錄「最大未處理 tick 數」,diag_poll 每秒印一次(正式版不開) */
// #define K1_DEBUG_TICK

void     tick_init(void);       // setup():Timer5 CTC,16 MHz / 64 / 250 = 1 kHz
uint16_t tick_get(void);        // 目前的 tick 計數(原子讀取)
void     tick_service(void);    // loop():補跑累積的 tick

#ifdef K1_DEBUG_TICK
uint16_t tick_backlog_max(void);
#endif

#endif
