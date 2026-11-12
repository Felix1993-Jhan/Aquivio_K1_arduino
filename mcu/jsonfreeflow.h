#ifndef K1_JSONFREEFLOW_H
#define K1_JSONFREEFLOW_H

/* ═══════════════════════════════════════════════════════════════════════
 *  jsonfreeflow — JSON 協定版的 freeflow(按住出水,slot 依流量比例開啟)
 *  〔K1 新增,使用者 2026-11-12 決定〕原 K1 程式沒有;演算法移植自 K2 移植版 freeflow.cpp(K2 0x07)
 * ═══════════════════════════════════════════════════════════════════════
 *  命令(方案 B,同 K2 0x07 的意義):
 *    {"freeflow":"cold"|"warm", "slotN":每 pulse 開幾 ms}#      值可為數字或字串;上限 150,0 = 不用
 *    不帶 slot = 只出水(同 K2 沖洗 07 00 00)
 *  回覆:
 *    開始   start / Received actions: …(execute_actions 印)→ Freeflow cold requested
 *    進行中 每 200 ms:freeflow pulse:N(本次開始後累計 pulse 數;上位機依此決定何時停)
 *    stop#  stop → pulse:N / cold water usage:x.xxx(pump_water 印)→ finish: freeflow stopped by external stop → end
 *    值不是 cold / warm:Failed to parse JSON: freeflow(不啟動、不印 end,同 JSON 解析失敗)
 *    除錯版(K1_DEBUG_FLOW_GEN)stop 時在 finish 前多兩行統計(測試腳本用):
 *      freeflow stat slot ms:1=1234 3=2345   各 slot 實際開啟總 ms
 *      freeflow stat open:0=12345 1=4100     同時開著 0、1、2… 台的累計 ms
 *  行為:
 *    ▸ 開水泵 + 水閥、開 UVC(A9 / A10);結束時全關
 *    ▸ 每來 1 個 pulse:acc += add(上限 250);acc ≥ 100 → 開 100 ms、扣 100;扣完 < 100 才關(同 K2 移植版)
 *    ▸ acc 起點依 slot 編號輪流 17 / 50 / 83,錯開同時開啟(同 K2 移植版)
 *    ▸ 沒有逾時(同 K2):結束只靠上位機 stop#
 *    ▸ 進行中:stop#、stopslotN# 立刻處理;pressure# 立刻回;其他命令 / 新 JSON 暫存到 end 之後(同出杯)
 *  方案 A(配方換算,目前未呼叫):jff_add_from_recipe()
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

#define JFF_ADD_MAX     150   // 每 pulse 最大累積(ms),防止給到太大的數值(同 K2 ADD_MAX)
#define JFF_OPEN_MS     100   // 累積到 100 開一次 100 ms(同 K2 PUMP_100mSec)
#define JFF_ACC_MAX     250   // 最大累積量(同 K2 ACC_MAX)
#define JFF_REPORT_MS   200   // freeflow pulse 回報週期

bool jff_is_request(const char* actions);   // JSON 有 "freeflow" 鍵 → 由本模組處理
void jff_execute(const char* actions);      // 解析並開始(execute_actions 呼叫)
void jff_tick_1ms(void);                    // tick_main() 每 1 ms 呼叫
bool jff_busy(void);                        // freeflow 進行中
void jff_stop(void);                        // 進行中收到 stop:全關 → 用水量 → finish → end
void jff_stop_slot(int slotNumber);         // 進行中收到 stopslotN:立刻關該 slot,本次不再開(0~9)

/* 方案 A(目前未呼叫):配方的「整杯 slot 開幾 ms」與「水量 µL」→ 每 pulse 開幾 ms */
uint16_t jff_add_from_recipe(uint32_t slot_ms, uint32_t desired_ul);

#endif
