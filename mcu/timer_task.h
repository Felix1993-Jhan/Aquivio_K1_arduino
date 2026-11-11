#ifndef K1_TIMER_TASK_H
#define K1_TIMER_TASK_H

/* ═══════════════════════════════════════════════════════════════════════
 *  timer_task — 每 1 ms 的裝置計時工作(K2 移植版)
 * ═══════════════════════════════════════════════════════════════════════
 *  移植自 K2:Core/Src/stm32u0xx_it.c 的 timer1_SET_Count()(K2 在 TIM1 中斷內)
 *           App/Src/timer_task.c 的 Tmr1_Interrupt_main()(K2 在主迴圈看 timer1_flag)
 *  K1 方案 A(D14):兩個都由 tick_main() 每 1 ms 呼叫,函式本體照抄;
 *  變數只在主迴圈存取,K2 的 __disable_irq 快照交接省略。
 *
 *  〔P2〕Tmr1_Interrupt_main 只處理 Stop(0x00 時間到 → 關閉)。
 *        PulseStop(0x06)於 P4 加入;異常偵測、DS18B20、MAX30102 不移植。
 * ═══════════════════════════════════════════════════════════════════════ */

void timer1_SET_Count(void);
void Tmr1_Interrupt_main(void);

#endif
