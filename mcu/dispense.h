#ifndef K1_DISPENSE_H
#define K1_DISPENSE_H

/* ═══════════════════════════════════════════════════════════════════════
 *  dispense — 出杯流程(JSON 動作)
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由 mcu.ino 搬來;〔步驟 2d〕出杯邏輯對齊 old/mcu31v2;
 *  〔步驟 4b〕解析層與執行層分開、內部全部整數(不用浮點、不用 String)。
 *
 *  解析層  pour_parse():JSON → pour_req_t(JSON 文件只在解析時存在,轉完即釋放)
 *  執行層  pour_run()  :只看 pour_req_t。仍是阻塞迴圈(步驟 5 改為 1 ms tick 狀態機):
 *    開水 → 逐路開 slot(各等 500 ms)→ 監控「到量 AND 最短時間」/ slot 時間 → 全關 → "finish: ..." → "end"
 *
 *  JSON(上位機 routes.cjs):{"cold"|"warm":"公升(字串)", "time":毫秒(選用), "slotN":"毫秒"}
 *  單位:水量一律 µL(目標量、累計量、每 pulse 水量 FLOW_UL_PER_PULSE)、時間一律 ms;
 *        輸出時才換成公升小數,格式同舊版
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>
#include "pins.h"          // SLOT_COUNT

#define WATER_NONE  0
#define WATER_COLD  1
#define WATER_WARM  2      // 程式的 warm = 常溫水

typedef struct {
    uint16_t slot_mask;               // bit i = JSON 有 "slot(i+1)"(同舊版 containsKey:值為 0 也會開)
    uint32_t slot_ms[SLOT_COUNT];     // 各 slot 開啟時間(ms,小數截掉同舊版);32-bit(架構計畫 5.1)
    uint8_t  water;                   // WATER_NONE / COLD / WARM(cold 優先,同舊版)
    uint32_t desired_ul;              // 目標水量(µL,"0.30" L → 300000);0 = 不看流量
    uint32_t min_time_ms;             // "time":最短出水時間(ms);0 = 不啟用;只在有水路時生效(同 mcu31v2)
} pour_req_t;

void execute_actions(const char* actions);   // actions = 去掉 '#' 的 JSON 字串

#endif
