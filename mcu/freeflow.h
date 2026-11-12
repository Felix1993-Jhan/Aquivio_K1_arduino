#ifndef K1_FREEFLOW_H
#define K1_FREEFLOW_H

/* ═══════════════════════════════════════════════════════════════════════
 *  freeflow — 0x07 LONG_PRESSURE 常溫水 / 冷水的 slot 依流量開啟控制(K2 移植版 P5)
 * ═══════════════════════════════════════════════════════════════════════
 *  移植自 K2:App/Src/freeflow.c(sort_by_speed、freeflow_Reset、freeflow_punpON)
 *           Core/Src/stm32u0xx_it.c 的 timer1_longPressure_flowcount / pumpCount(K2 在 TIM1 中斷內)
 *  K1 方案 A:都由 tick_main() 每 1 ms 呼叫,變數只在主迴圈存取。
 *
 *  ▸ 每來 1 個流量 pulse:acc += pulse_add_time(上限 ACC_MAX)
 *  ▸ acc ≥ 100 → 開 slot 100 ms,扣 100;扣完 < 100 才關
 *    ⚠ K1 修正:K2 開啟條件為 acc > 100,扣完剛好 = 100 時卡開(見 freeflow.cpp)
 *  ▸ 〔K1 新增,試驗中〕收到 0x07 時 slot 的 acc 起點依 ID 順序輪流 17 / 50 / 83(K2 為 0),錯開同時開啟
 *  ▸ 不移植:氣泡水模式 2(freeflow_punpON2、timer1_longPressure_*2、SPRKINGCYCLE),K1 沒有氣泡水
 *           短脈衝偵測(ShortPulse_*,異常偵測)
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

/* K1:K2 為 COUNTER_SIZE 18 且不檢查 N(N > 18 會寫出陣列外)。
 * K1 改為收包緩衝能收的最大 N:(DATA_SIZE 64 − 7) / 3 = 19,N 超過時收包端丟棄 */
#define FREEFLOW_MAX   19

#define ADD_MAX        150   // 0.150 Sec,每 pulse 最大累積(防止軟體給到太大的數值)
#define PUMP_100mSec   100   // 累積到 0.1 Sec 開一次
#define ACC_MAX        250   // 0.25 Sec,最大累積量

/* 此結構的數值皆定義為「每 pulse,該 slot 所需要的開啟時間」(同 K2 main.h freeflow_def) */
typedef struct {
    uint8_t  id;               // 標明是哪個 ID
    uint16_t acc;              // 尚未注入的時間量(ms),每次 pulse 流過時累積
    uint16_t pulse_add_time;   // 每 pulse 該 slot 需要的時間(ms),由軟體給予
    uint16_t mSec_100count;    // 開啟中的計時(ms);0 = 未在計時
} freeflow_def;

extern freeflow_def channel7_CMD[FREEFLOW_MAX];
extern uint8_t      freeFlow_Run;         // 0 = 停、1 = 常溫 / 冷水(K2 的 2 = 氣泡水 K1 沒有)
extern uint8_t      pump0_9count;         // channel7_CMD 前面幾筆是 slot(ID < 10)
extern uint32_t     temp_main_freeflow;   // 0x07 帶的 UVC(ID 11~13)遮罩

void sort_by_speed(freeflow_def *data, int count);
void freeflow_Reset(void);
void freeflow_stagger_start(void);   // 〔K1 新增,試驗中〕acc 起點 17 / 50 / 83 輪流
void freeflow_punpON(void);
void timer1_longPressure_flowcount(uint32_t d);
void timer1_longPressure_pumpCount(void);

#endif
