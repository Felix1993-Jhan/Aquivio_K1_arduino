#ifndef K1_DEVICE_H
#define K1_DEVICE_H

/* ═══════════════════════════════════════════════════════════════════════
 *  device — 驅動元件控制:水路(水閥、泵、冷/熱閥)與 slot 開關,以及對應的訊息
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由 mcu.ino 原樣搬來,行為不變。底層腳位讀寫在 pins。
 *
 *  ▸ pump_water(1, temp)  開水路:水閥 → 泵 → 冷/熱閥(temp:0 = 冷、1 = 熱)
 *  ▸ pump_water(0, temp)  關水路,印 pulse: / xxx water usage:,流量計數歸零
 *  ▸ activateSlot / deactivateSlot  開關單一 slot(slotNumber 0~9)並印訊息
 * ═══════════════════════════════════════════════════════════════════════ */

void pump_water(int state, int water_temp, double desiredFlow = -1);   // desiredFlow 原程式即未使用
void activateSlot(int slotNumber, unsigned long duration);
void deactivateSlot(int slotNumber);

#endif
