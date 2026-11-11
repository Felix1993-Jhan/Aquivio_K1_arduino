#ifndef K1_DEVICE_CTRL_H
#define K1_DEVICE_CTRL_H

/* ═══════════════════════════════════════════════════════════════════════
 *  device_ctrl — 裝置 ID 對照表 + 開關統一入口(K2 移植版)
 * ═══════════════════════════════════════════════════════════════════════
 *  移植自 K2 App/Src/device_ctrl.c(v0.0.0.10)。對照表見 K2_aidata/K1_k2port_discussion.md 3.2。
 *
 *  ▸ DeviceMap[26]  索引 = K2 裝置 ID(0~25),內容 = K1 PinMap 索引
 *                   K2 存 GPIO port / pin / bit;K1 改存 PinMap 索引,bit 一律 = 1UL << ID(同 K2)
 *      DEV_NOPIN    輸出裝置但 K1 沒有實體(ID 11、16、17、25):Status 照翻,不輸出(D16)
 *      DEV_NONE     非輸出裝置(ID 18~24 感測器,同 K2 的空格)
 *  ▸ handleSW(type, id)  同 K2:0 = 單一 ID 開、1 = 遮罩開、2 = 遮罩關(並清倒數)
 *  ▸ ID10 水泵:K2 為 TIM3 PWM(實際只用 0 / 100%),K1 改為開關
 *
 *  Status / Counter1ms / Stop 只在主迴圈存取(uart_cmd、tick_main)→ 不需原子保護
 *  (K2 的 timer1_SET_Count 在 TIM1 中斷內,K1 依方案 A 改在 tick_main,見 timer_task)
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

#define DEVICE_ID_COUNT  26
#define DEV_NOPIN        0xFE
#define DEV_NONE         0xFF
#define DEV_BIT(id)      (1UL << (id))      // ⚠ 一定要 1UL:AVR 的 int 只有 16-bit

extern const uint8_t DeviceMap[DEVICE_ID_COUNT];
extern uint32_t Status;                       // bit n = ID n 目前開著
extern uint32_t Stop;                         // timer1_SET_Count 倒數到 0 的 ID,由 Tmr1_Interrupt_main 關閉
extern uint32_t Counter1ms[DEVICE_ID_COUNT];  // 0x00 SWITCH_SET 的剩餘時間(ms);0 = 不倒數

int      isDeviceID(uint8_t id);              // 輸出裝置(含 DEV_NOPIN)回 1
void     handleSW(int type, uint32_t id);
uint32_t device_pin_readback(void);           // 〔K1 新增〕實際腳位準位,bit 排法同 Status(0xB5 SUB 01)

#endif
