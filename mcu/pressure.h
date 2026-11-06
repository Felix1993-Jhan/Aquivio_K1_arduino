#ifndef K1_PRESSURE_H
#define K1_PRESSURE_H

/* ═══════════════════════════════════════════════════════════════════════
 *  pressure — 水壓感測器(A1 = ADC1)讀取
 *  〔新增 2026-11-06〕原 K1 程式與 K2 架構計畫都沒有此功能
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  ▸ ADC 直接存取暫存器,不用 analogRead
 *      參考電壓 AVCC(5 V)、右對齊 10-bit、除頻 128 → ADC 時脈 125 kHz
 *      一次轉換 13 個 ADC 週期 ≈ 104 µs(啟用後第一次 25 週期 ≈ 200 µs)
 *  ▸ 非阻塞:每 200 ms 啟動一次轉換,下一個 tick 檢查完成再取值,不空等
 *  ▸ 只在主迴圈呼叫,不開 ADC 中斷
 *
 *  ⚠ 目前尚未接上主程式:
 *    - 硬體尚未改線,PinMap[ID_PRESSURE] 暫為 PIN_IN;啟用前改為 PIN_ANALOG(見 pins.cpp)
 *    - pressure_tick_1ms() 要等步驟 2 的 1 ms tick 完成後,從 tick_main() 呼叫
 *    - 上位機指令:上位機主動查詢(例 `pressure#` → `pressure:512`),倒水中照 5.3 先收不執行
 *    - 輸出為原始 ADC 值 0~1023;換算成水壓需要感測器規格(待查)
 *    - mV ≈ raw × 5000 / 1024,但實際參考 = 板上 AVCC,USB 供電時未必剛好 5.00 V
 *
 *  ⚠ 硬體:參考用 AVCC 時,AREF 腳不可外接電壓(內部會與 AVCC 相連)
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

void     pressure_init(void);       // setup():設定 ADC(在 pin_init_all() 之後)
void     pressure_tick_1ms(void);   // 每 1 ms 呼叫一次
uint16_t pressure_raw(void);        // 最近一次 ADC 值 0~1023
uint8_t  pressure_valid(void);      // 開機後是否已取得第一筆(0 = 尚無資料)

#endif
