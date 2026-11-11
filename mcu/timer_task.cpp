#include "timer_task.h"
#include "device_ctrl.h"
#include "flowmeter.h"     // 〔P4〕flow_get、flow_take
#include "uart_cmd.h"      // 〔P4〕uart_send_pulse_done

/* 〔P4〕SET_PULSE(0x06)狀態,同 K2 globals(Pulse / PulseStart / PulseStop / PulseType)
 * 只在主迴圈存取(uart_cmd、tick_main)→ 不需原子保護 */
uint32_t Pulse = 0;          // 剩餘脈衝數;0 = 不倒數
uint32_t PulseStart = 0;     // 0x06 打開的裝置遮罩(到量時關閉這些)
uint32_t PulseStop = 0;      // 到量 → 待關閉的遮罩(Tmr1_Interrupt_main 處理)
uint8_t  PulseType = 0;      // 0 = 常溫、1 = 冷水(完成通知回傳)

static uint32_t flow_last = 0;   // 上次讀到的流量總計數(只在主迴圈存取)

/* K2 stm32u0xx_it.c timer1_SET_Count():每個 ID 的 0x00 倒數,到 0 時若仍開著 → 記進 Stop
 * (K2 呼叫條件為 freeFlow_Run == 0,freeflow 於 P5 加入) */
void timer1_SET_Count(void)
{
    for (int i = 0; i < DEVICE_ID_COUNT; i++) {
        // 計算毫秒(依 ID 索引;非裝置的 Counter1ms 恆 0,無作用)
        if (Counter1ms[i] != 0) {
            Counter1ms[i]--;
            if (Counter1ms[i] == 0) {
                if (Status & DEV_BIT(i)) Stop |= DEV_BIT(i);
            }
        }
    }
}

/* 扣 d 個脈衝:同 K2 timer1_flowmeter() 內的 Pulse 段落(Pulse > 0 才扣,扣到 0 → PulseStop = PulseStart)。
 * K2 每次只扣 1;K1 一次扣 d 個,要扣的 ≥ 剩餘時直接歸 0 */
static void pulse_consume(uint32_t d)
{
    if (Pulse > 0 && d > 0) {
        if (d >= Pulse) {
            Pulse = 0;
            PulseStop = PulseStart;
        } else {
            Pulse -= d;
        }
    }
}

/* 〔K1,與 K2 不同〕K2 在流量計中斷內每確認 1 個脈衝就扣 Pulse;K1 的流量計模組(flowmeter,兩分支共用)不改,
 * 改為每 1 ms 讀總計數,以「這次 − 上次」得到新增脈衝數再扣(方案 A,在主迴圈)。
 * 無號相減在計數繞回時仍正確(同計時器 now − start);flow_cnt 滿值即停、不會繞回 */
void timer1_pulse_count(void)
{
    uint32_t now = flow_get();
    uint32_t d = now - flow_last;
    flow_last = now;
    pulse_consume(d);
}

/* 0x04 流量歸零:取代直接呼叫 flow_take()。歸零前「上次 tick 之後新增、尚未扣」的脈衝先補扣,
 * 再把 flow_last 設為 0,避免下一次相減錯誤 */
void pulse_flow_clear(void)
{
    uint32_t taken = flow_take();       // 原子讀取並歸零
    pulse_consume(taken - flow_last);
    flow_last = 0;
}

/* K2 timer_task.c Tmr1_Interrupt_main():處理 Set 給予的運行時間,計時到則關閉對應的 GPIO */
void Tmr1_Interrupt_main(void)
{
    uint32_t temp_main = Stop;          // K2 此處以 __disable_irq 保護;K1 同在主迴圈,不需要
    Stop = 0;
    if (temp_main != 0) handleSW(2, temp_main);
    /* 不移植:ShortPulse_Check(異常偵測) */

    /* 〔P4〕處理 SET_PULSE:脈衝扣到 0 → 關閉該模式打開的裝置,並主動送完成通知(同 K2) */
    temp_main = PulseStop;
    PulseStop = 0;
    if (temp_main != 0) {
        handleSW(2, temp_main);
        uart_send_pulse_done(PulseType);
    }
}
