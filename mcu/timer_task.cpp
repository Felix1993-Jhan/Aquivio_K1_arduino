#include "timer_task.h"
#include "device_ctrl.h"

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

/* K2 timer_task.c Tmr1_Interrupt_main():處理 Set 給予的運行時間,計時到則關閉對應的 GPIO */
void Tmr1_Interrupt_main(void)
{
    uint32_t temp_main = Stop;          // K2 此處以 __disable_irq 保護;K1 同在主迴圈,不需要
    Stop = 0;
    if (temp_main != 0) handleSW(2, temp_main);
    /* 不移植:ShortPulse_Check(異常偵測)。PulseStop(0x06 完成回覆)於 P4 加入 */
}
