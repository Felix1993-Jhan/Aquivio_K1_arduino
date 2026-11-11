#ifndef K1_TIMER_TASK_H
#define K1_TIMER_TASK_H

/* ═══════════════════════════════════════════════════════════════════════
 *  timer_task — 每 1 ms 的裝置計時工作(K2 移植版)
 * ═══════════════════════════════════════════════════════════════════════
 *  移植自 K2:Core/Src/stm32u0xx_it.c 的 timer1_SET_Count()、timer1_flowmeter() 的 Pulse 段落
 *           (K2 在 TIM1 中斷內)
 *           App/Src/timer_task.c 的 Tmr1_Interrupt_main()(K2 在主迴圈看 timer1_flag)
 *  K1 方案 A(D14):都由 tick_main() 每 1 ms 呼叫,函式本體照抄;
 *  變數只在主迴圈存取,K2 的 __disable_irq 快照交接省略。
 *
 *  〔P2〕Stop:0x00 時間到 → 關閉
 *  〔P4〕Pulse / PulseStop:0x06 脈衝扣到 0 → 關閉並主動送完成通知 40 71 30 06 [Type] 01 00 00 CS
 *        K1 以「流量總計數這次 − 上次」扣脈衝(不改共用的 flowmeter);0x04 歸零改呼叫 pulse_flow_clear()
 *        ⚠ 同 K2:0x02 不清 Pulse(殘值問題已重現,使用者決定照 K2 不修正,見討論稿 4.1)
 *  〔P5〕新增的流量脈衝同時餵給 freeflow 累積(freeFlow_Run == 1 時);freeflow 中 0x00 倒數停住(同 K2)
 *  異常偵測、DS18B20、MAX30102 不移植。
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

extern uint32_t Pulse, PulseStart, PulseStop;
extern uint8_t  PulseType;

void timer1_SET_Count(void);
void timer1_pulse_count(void);
void pulse_flow_clear(void);
void Tmr1_Interrupt_main(void);

#endif
