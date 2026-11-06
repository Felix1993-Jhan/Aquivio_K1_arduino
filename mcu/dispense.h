#ifndef K1_DISPENSE_H
#define K1_DISPENSE_H

/* ═══════════════════════════════════════════════════════════════════════
 *  dispense — 出杯流程(JSON 動作)
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由 mcu.ino 搬來;〔步驟 2d〕出杯邏輯對齊 old/mcu31v2。
 *  execute_actions() 仍是阻塞迴圈:
 *    開水 → 逐路開 slot(各等 500 ms)→ 監控「到量 AND 最短時間」/ slot 時間 → 全關 → "finish: ..." → "end"
 *  JSON:{"cold"|"warm":"公升(字串)", "time":毫秒(選用), "slotN":毫秒}
 *  步驟 5 會改為 1 ms tick 推進的狀態機。
 * ═══════════════════════════════════════════════════════════════════════ */

void execute_actions(const char* actions);   // actions = 去掉 '#' 的 JSON 字串

#endif
