#include <Arduino.h>
#include <ArduinoJson.h>
#include "dispense.h"
#include "pins.h"          // slotCount、pin_write、ID_SLOT1
#include "device.h"        // pump_water、activateSlot、deactivateSlot
#include "flowmeter.h"     // flow_get、FLOW_UL_PER_PULSE
#include "diagnostics.h"   // checkMemory
#include "tx_queue.h"      // out、print_liters_ul
#include "jsonfreeflow.h"  // 〔2026-11-12〕JSON freeflow(按住出水)

/* ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2d〕出杯邏輯對齊 old/mcu31v2/mcu31v2.ino(老闆電腦上的版本,目前已知最新)
 *    - 支援 JSON 的 "time"(毫秒):最短出水時間,須「到量 AND 時間到」才停水
 *    - 修正問題 ②:停水後 desired = 0、dispenseStopped = true,slot 結束後不再卡住
 *    - 輸出 "Stopping water pump (volume & time satisfied)..."、"finish: ..."
 *    - cold / warm:〔H4〕字串、數字都接受(mcu31v2 只讀字串,數字會讀成 0)
 *  〔步驟 4b〕解析層(pour_parse)與執行層分開;內部全部整數、不用 String:
 *    - 水量一律 µL(目標量、累計量 = pulse × FLOW_UL_PER_PULSE)、時間一律 ms
 *    - slot 時間 32-bit(舊版 (unsigned) 在 AVR 為 16-bit,> 65.5 秒會在錯誤時間關閉,架構計畫 5.1)
 *    - 輸出換成公升小數、位數同舊版(標準四捨五入,剛好 .5 時可能與舊版浮點差 1 個最小位)
 *  〔步驟 5〕執行層改為 1 ms tick 狀態機(dispense_tick_1ms),不再阻塞 loop():
 *    - 不再使用 delay(500) / delay(10) / millis();時間全部由 tick 計數(slot 倒數同 K2 Counter1ms)
 *    - slot 依設定時間由長到短開(相同時編號大的先),間隔 250 ms(9.1 #26)
 *    - STARTING(開 slot 期間)也每 1 ms 檢查 slot 時間與到量(9.2 b)
 *    - 120 秒逾時、Progress 仍從 slot 全開完起算(同舊版 startTime)
 *    - 輸出字串與順序同舊版
 *  被取代的版本(4b 阻塞版、2d 浮點版、3fe056c 版)以註解保留在檔尾
 * ═══════════════════════════════════════════════════════════════════════ */

// -------------------- Min-Time guard (for cold/warm) --------------------
// 〔步驟 5〕時間點改用 water_ms(開水後經過 ms,+1 避免 0 被當成「未發生」),原為 millis()
static unsigned long minTimeMs = 0;          // 應需時間(毫秒);0=不啟用
static bool timeGuardEnabled = false;        // 是否啟用時間下限
static bool stoppedByFlow = false;           // 這次是否由 flow 停
static bool stoppedByTime = false;           // 這次是否由 time 停(代表先到量、等時間)
static bool dispenseStopped = false;         // 是否已正常停水

// 先後判斷(誰先達成)
static bool volumeReachedOnce = false;
static bool timeReachedOnce = false;
static unsigned long volumeReachedMs = 0;
static unsigned long timeReachedMs = 0;

/* ══════════════════════════════════════════════════════════
 *  ▌ 解析層:JSON → pour_req_t(〔步驟 4b〕)
 * ══════════════════════════════════════════════════════════ */

/* 十進位字串 → 放大 10^decimals 的整數(不用浮點):
 *   parse_fixed("0.30", 6)    → 300000(公升 → µL)
 *   parse_fixed("2181.82", 0) → 2181(毫秒,小數截掉)
 * 規則比照 atof / String::toFloat 的寬鬆讀法:略過前導空白、讀到非數字就停;
 * 超過 decimals 的小數位捨去;負數 → 0(舊版負值也等同不看流量);uint32 上限:6 位小數時約 4294 L */
static uint32_t parse_fixed(const char* s, uint8_t decimals) {
    if (s == NULL) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;
    else if (*s == '-') return 0;

    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); s++; }

    uint8_t n = 0;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9' && n < decimals) { v = v * 10 + (uint32_t)(*s - '0'); n++; s++; }
    }
    while (n < decimals) { v *= 10; n++; }
    return v;
}

/* 〔H4〕水量(公升)→ µL,字串與數字都接受:
 *   字串(上位機訂單 routes.cjs,"0.30"):parse_fixed 直接轉整數,不經過浮點
 *   數字(管理介面「100ml cold / warm」按鈕 main.cjs,{"cold":0.1}):ArduinoJson 解析時已存成浮點,
 *     只在這裡從浮點換一次整數(×1,000,000、四捨五入),之後全部整數
 * mcu31v2 只讀字串,數字會變 0 → 該按鈕幾乎不出水;此處改為兩者都接受(行為差異檔第 11 節) */
static uint32_t json_liters_to_ul(JsonVariantConst v) {
    const char* s = v.as<const char*>();
    if (s != NULL) return parse_fixed(s, 6);

    float f = v.as<float>();                    // 不是數字(null 等)→ 0
    if (!(f > 0.0f)) return 0;                  // 負數、0 → 0(同舊版:視為不看流量)
    if (f >= 4294.0f) return 4294000000UL;      // uint32 上限(約 4294 L),避免轉型溢位
    return (uint32_t)(f * 1000000.0f + 0.5f);
}

/* slot 毫秒:上位機送字串("2181.82"),也接受數字;小數截掉(同舊版 (unsigned)durations 的截斷) */
static uint32_t json_ms(JsonVariantConst v) {
    const char* s = v.as<const char*>();
    if (s != NULL) return parse_fixed(s, 0);
    return v.as<uint32_t>();
}

static bool pour_parse(const char* actions, pour_req_t* r) {
    /* 編譯警告 StaticJsonDocument deprecated:7.1.0 中它就是 JsonDocument(200 不限制大小),行為相同,
     * 使用者 2026-11-06 決定先不改;日後升級 ArduinoJson 大版本時改為 JsonDocument doc; */
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);
    if (error) {
        out.print("Failed to parse JSON: ");
        out.println(error.c_str());
        return false;
    }

    memset(r, 0, sizeof(*r));

    // 水路:cold 優先(同舊版)
    /* 〔H4〕改為字串、數字都接受(json_liters_to_ul)。原寫法只讀字串(同 mcu31v2,數字 → NULL → 0),保留供對照:
    if (doc.containsKey("cold")) {
        r->water = WATER_COLD;
        r->desired_ul = parse_fixed(doc["cold"].as<const char*>(), 6);   // 公升 → µL
    } else if (doc.containsKey("warm")) {
        r->water = WATER_WARM;
        r->desired_ul = parse_fixed(doc["warm"].as<const char*>(), 6);
    }
    */
    if (doc.containsKey("cold")) {
        r->water = WATER_COLD;
        r->desired_ul = json_liters_to_ul(doc["cold"]);   // 公升 → µL
    } else if (doc.containsKey("warm")) {
        r->water = WATER_WARM;
        r->desired_ul = json_liters_to_ul(doc["warm"]);
    }

    // time:只在有水路時讀(同 mcu31v2:沒有 cold/warm 時 handleWaterFlow 提早 return,不讀 time)
    if (r->water != WATER_NONE && doc.containsKey("time")) {
        r->min_time_ms = (unsigned long)doc["time"].as<unsigned long>();
    }

    // slot:有鍵就開(同舊版 containsKey),時間轉一次成 32-bit 整數
    for (int i = 1; i <= slotCount; i++) {
        char key[8];
        sprintf(key, "slot%d", i);
        if (doc.containsKey(key)) {
            r->slot_mask |= (uint16_t)(1u << (i - 1));
            r->slot_ms[i - 1] = json_ms(doc[key]);
        }
    }
    return true;   // doc 在此離開作用域 → JSON 佔用的 heap 釋放,出杯期間不再佔用
}

/* ══════════════════════════════════════════════════════════
 *  ▌ 執行層:1 ms tick 狀態機(〔步驟 5〕)
 * ══════════════════════════════════════════════════════════ */

#define SLOT_OPEN_INTERVAL_MS  250       // 開 slot 間隔(原 delay(500),9.1 #26)
#define POUR_TIMEOUT_MS        120000UL  // 同舊版 TIMEOUT(已修正 16-bit 溢位),從 slot 全開完起算
#define PROGRESS_INTERVAL_MS   1000      // Progress 每秒一次(從 slot 全開完起算,同舊版)
#define MEMCHECK_INTERVAL_MS   10        // 原出杯迴圈每圈(delay(10))呼叫一次 checkMemory

enum { DS_IDLE, DS_STARTING, DS_RUNNING };

static uint8_t  ds_state = DS_IDLE;
static pour_req_t ds_req;                        // 本杯動作(pour_parse 的輸出)
static uint32_t desired_ul = 0;                  // 目標水量(µL);停水後設 0(防重入,同舊版)
static int      waterTemp = 0;
static uint32_t accumulated_ul = 0;              // 已出水量(µL)

static uint32_t slot_left[SLOT_COUNT];           // 各 slot 剩餘開啟時間(ms),每 tick 減 1(同 K2 Counter1ms)
static uint16_t slot_on = 0;                     // bit i = slot(i+1) 開著
static uint16_t slot_cancel = 0;                 // bit i = 出杯中收到 stopslot(i+1) 時尚未開 → 不開
static uint8_t  open_order[SLOT_COUNT];          // 開啟順序(0~9):時間由長到短,相同時編號大的先
static uint8_t  open_count = 0, open_next = 0;
static uint16_t open_wait_ms = 0;                // 距上一個 slot 開啟的 ms

static uint32_t water_ms = 0;                    // 開水後經過 ms(原 millis() − dispenseStartMs)
static uint32_t run_ms = 0;                      // slot 全開完後經過 ms(原 millis() − startTime)
static uint16_t progress_ms = 0;
static uint8_t  memcheck_ms = 0;

static void open_slot(uint8_t i) {
    slot_left[i] = ds_req.slot_ms[i];
    slot_on |= (uint16_t)(1u << i);
    activateSlot(i, slot_left[i]);
}

/* STARTING:開下一個 slot(跳過出杯中被 stopslotN 取消的);全部開完 → RUNNING */
static void open_next_slot(void) {
    while (open_next < open_count && (slot_cancel & (1u << open_order[open_next]))) open_next++;
    if (open_next < open_count) open_slot(open_order[open_next++]);
}

static void pour_finish(void) {
    // 保險關
    pump_water(0, waterTemp);
    for (int i = 0; i < slotCount; i++) deactivateSlot(i);
    slot_on = 0;

    // finish 回報
    if (dispenseStopped) {
        if (stoppedByTime) {
            out.println("finish: stopped by minimum time constraint");
        } else if (stoppedByFlow) {
            out.println("finish: stopped by flow pulse threshold");
        } else {
            out.println("finish: stopped by (volume & time) unspecified");
        }
    } else {
        out.println("finish: stopped by system timeout or external stop");
    }

    out.println("end");
    ds_state = DS_IDLE;
}

static void pour_start(const pour_req_t* r) {
    ds_req = *r;
    desired_ul = 0;
    waterTemp = 0;

    // -------------------- Water(原 handleWaterFlow)--------------------
    // reset flags
    timeGuardEnabled   = false;
    minTimeMs          = 0;
    stoppedByFlow      = false;
    stoppedByTime      = false;
    dispenseStopped    = false;
    volumeReachedOnce  = false;
    timeReachedOnce    = false;
    volumeReachedMs    = 0;
    timeReachedMs      = 0;

    if (r->water == WATER_COLD) {
        desired_ul = r->desired_ul;
        pump_water(1, 0);
        waterTemp = 0;
        out.print("Cold water flow requested: ");
    } else if (r->water == WATER_WARM) {
        desired_ul = r->desired_ul;
        pump_water(1, 1);
        waterTemp = 1;
        out.print("Warm water flow requested: ");
    } else {
        out.println("No water flow requested");
    }

    if (r->water != WATER_NONE) {
        minTimeMs = r->min_time_ms;
        timeGuardEnabled = (minTimeMs > 0);
        print_liters_ul(desired_ul, 2);            // 同舊版 println(desiredFlow):公升 2 位
        out.println();
    }
    water_ms = 0;                                  // 原 dispenseStartMs = millis()

    // -------------------- Slots(原 activateRequestedSlots)--------------------
    // 開啟順序:時間由長到短(插入排序,嚴格 < 才往前 → 時間相同時保留編號大的在前)
    open_count = 0;
    for (int i = slotCount - 1; i >= 0; i--) {
        if (!(r->slot_mask & (1u << i))) continue;
        uint8_t k = open_count++;
        while (k > 0 && r->slot_ms[open_order[k - 1]] < r->slot_ms[i]) {
            open_order[k] = open_order[k - 1];
            k--;
        }
        open_order[k] = (uint8_t)i;
    }
    open_next = 0;
    slot_on = 0;
    slot_cancel = 0;
    open_wait_ms = 0;
    run_ms = 0;
    progress_ms = 0;
    memcheck_ms = 0;
    accumulated_ul = 0;

    ds_state = DS_STARTING;
    open_next_slot();                              // 第一個 slot 立刻開(同舊版)
    if (open_next >= open_count) ds_state = DS_RUNNING;
}

void dispense_tick_1ms(void) {
    if (ds_state == DS_IDLE) return;

    water_ms++;

    // Timeout 防護(RUNNING;同舊版迴圈開頭先檢查)
    if (ds_state == DS_RUNNING && ++run_ms >= POUR_TIMEOUT_MS) {
        out.println("ERROR: Execution timeout reached");
        pour_finish();
        return;
    }

    accumulated_ul = flow_get() * FLOW_UL_PER_PULSE;   // 〔步驟 3〕原子讀取;〔步驟 4b〕整數 µL

    // 條件評估(STARTING / RUNNING 皆檢查)
    bool reachVolume = (desired_ul > 0) && (accumulated_ul >= desired_ul);   // 同單位(µL)直接比
    if (reachVolume && !volumeReachedOnce) { volumeReachedOnce = true; volumeReachedMs = water_ms; }

    bool reachTime = (!timeGuardEnabled) || (water_ms >= minTimeMs);
    if (reachTime && !timeReachedOnce) { timeReachedOnce = true; timeReachedMs = water_ms; }

    // 正常停水條件:同時成立(到量 AND 時間)
    if (!dispenseStopped && reachVolume && reachTime) {
        if (timeGuardEnabled) {
            // 先到量後等時間 → by time;先時間後到量 → by flow
            if (volumeReachedMs > 0 && timeReachedMs > 0) {
                stoppedByTime = (volumeReachedMs < timeReachedMs);
                stoppedByFlow = (timeReachedMs <= volumeReachedMs);
            } else if (volumeReachedMs > 0) {
                stoppedByFlow = true;
            } else {
                stoppedByFlow = true;
            }
        } else {
            stoppedByFlow = true;
        }

        out.print("Stopping water pump (volume & time satisfied). ");
        if (timeGuardEnabled) { out.print("min time = "); out.print(minTimeMs); out.print(" ms. "); }
        out.println();

        pump_water(0, waterTemp);
        desired_ul = 0;        // 防重入
        dispenseStopped = true;
    }

    // slot 倒數(同 K2 timer1_SET_Count:每 ms 減 1,到 0 就關)
    for (int i = 0; i < slotCount; i++) {
        if (!(slot_on & (1u << i))) continue;
        if (slot_left[i] > 0) slot_left[i]--;
        if (slot_left[i] == 0) {
            deactivateSlot(i);
            slot_on &= (uint16_t)~(1u << i);
        }
    }

    // STARTING:每 250 ms 開下一個 slot(在倒數之後開 → 新開的 slot 從下一個 tick 起算)
    if (ds_state == DS_STARTING) {
        if (++open_wait_ms >= SLOT_OPEN_INTERVAL_MS) {
            open_wait_ms = 0;
            open_next_slot();
        }
        if (open_next >= open_count) ds_state = DS_RUNNING;   // 之後才開始算逾時與 Progress(同舊版)
    } else {
        // 進度輸出(公升 2 位,格式同舊版)
        if (++progress_ms >= PROGRESS_INTERVAL_MS) {
            progress_ms = 0;
            out.print("Progress - Accumulated flow: ");
            print_liters_ul(accumulated_ul, 2);
            out.print(" / Desired flow: ");
            print_liters_ul(desired_ul, 2);
            out.print("\n");
        }

        // 判斷是否要繼續:任何 slot 在跑,或出水尚未同時滿足(到量 AND 時間)
        bool needMoreWater =
            (slot_on != 0) ||
            (
                (desired_ul > 0 && accumulated_ul < desired_ul) ||
                (timeGuardEnabled && (water_ms < minTimeMs))
            );

        if (!needMoreWater) {  // 條件滿足 → 收尾
            pour_finish();
            return;
        }
    }

    if (++memcheck_ms >= MEMCHECK_INTERVAL_MS) {
        memcheck_ms = 0;
        checkMemory();
    }
}

/* 〔jsonfreeflow,2026-11-12〕freeflow 進行中也算出杯中:命令暫存、不送 mcuAlive、stop / stopslotN 轉給 freeflow。
 * 原寫法(只看出杯狀態機)保留供對照:
bool dispense_busy(void) {
    return ds_state != DS_IDLE;
}

void dispense_abort(void) {
    if (ds_state != DS_IDLE) pour_finish();   // finish: stopped by system timeout or external stop(若水還沒停)
}
 */
bool dispense_busy(void) {
    return ds_state != DS_IDLE || jff_busy();
}

void dispense_abort(void) {
    if (ds_state != DS_IDLE) pour_finish();   // finish: stopped by system timeout or external stop(若水還沒停)
    jff_stop();                               // 〔jsonfreeflow〕freeflow 進行中才有動作
}

void dispense_stop_slot(int slotNumber) {
    if (jff_busy()) { jff_stop_slot(slotNumber); return; }   // 〔jsonfreeflow〕
    if (ds_state == DS_IDLE || slotNumber < 0 || slotNumber >= slotCount) return;
    uint16_t bit = (uint16_t)(1u << slotNumber);
    if (slot_on & bit) {
        pin_write(ID_SLOT1 + slotNumber, 0);  // 同待機時的 stopslotN:只關腳位,不印 Deactivated
        slot_on &= (uint16_t)~bit;
    } else {
        slot_cancel |= bit;                    // 尚未開 → 取消,不開
    }
}

// -------------------- Main executor --------------------
void execute_actions(const char* actions) {
    out.println("start");
    out.print("Received actions: ");
    out.println(actions);

    if (jff_is_request(actions)) {            // 〔jsonfreeflow,2026-11-12〕有 "freeflow" 鍵 → 按住出水模式
        jff_execute(actions);
        return;
    }

    pour_req_t req;
    if (!pour_parse(actions, &req)) return;   // 解析失敗:已印 "Failed to parse JSON: ...",不送 end(同舊版)
    pour_start(&req);                         // 〔步驟 5〕開始後立刻返回,之後由 dispense_tick_1ms() 推進
}

/* ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 5〕以下為被取代的 4b / H4 阻塞版 pour_run,保留供對照。
 *  換掉原因:出杯期間阻塞 loop()(收不到 stop、slot 開啟期間不檢查);改為 1 ms tick 狀態機。
 *  (原函式內的區塊註解改寫為 // 以免與外層註解衝突)
 * ═══════════════════════════════════════════════════════════════════════
static bool areAnySlotsActive(const unsigned long* slotStartTimes, const uint32_t* durations) {
    unsigned long now = millis();
    for (int i = 0; i < slotCount; i++) {
        if (slotStartTimes[i] != 0 && now - slotStartTimes[i] < durations[i]) {   // 〔步驟 4b〕32-bit 比較
            return true;
        }
    }
    return false;
}

static void pour_run(const pour_req_t* r) {
    uint32_t desired_ul = 0;                       // 目標水量(µL)
    int waterTemp = 0;

    // -------------------- Water(原 handleWaterFlow)--------------------
    // reset flags(同上)
    if (r->water == WATER_COLD) {
        desired_ul = r->desired_ul;
        pump_water(1, 0);
        waterTemp = 0;
        out.print("Cold water flow requested: ");
    } else if (r->water == WATER_WARM) {
        desired_ul = r->desired_ul;
        pump_water(1, 1);
        waterTemp = 1;
        out.print("Warm water flow requested: ");
    } else {
        out.println("No water flow requested");
    }

    if (r->water != WATER_NONE) {
        minTimeMs = r->min_time_ms;
        timeGuardEnabled = (minTimeMs > 0);
        dispenseStartMs = millis();

        print_liters_ul(desired_ul, 2);            // 同舊版 println(desiredFlow):公升 2 位
        out.println();
    }

    // -------------------- Slots(原 activateRequestedSlots)--------------------
    unsigned long slotStartTimes[slotCount] = {0};
    uint32_t durations[slotCount] = {0};           // 〔步驟 4b〕原 double,改 32-bit 整數 ms
    for (int i = slotCount; i > 0; i--) {
        if (r->slot_mask & (1u << (i - 1))) {
            durations[i-1] = r->slot_ms[i-1];
            slotStartTimes[i-1] = millis();
            activateSlot(i-1, durations[i-1]);
            // 〔步驟 2c 過渡〕delay(500) 改成等 500 ms 的迴圈,期間照常送 TX、推進 tick;
            // 等待期間仍不檢查水量 / slot(同舊版)。步驟 5 改狀態機後移除
            uint16_t t0 = tick_get();
            while ((uint16_t)(tick_get() - t0) < 500) {
                tick_service();
                tx_poll();
            }
        }
    }

    // -------------------- Main loop --------------------
    uint32_t accumulated_ul = 0;                   // 〔步驟 4b〕原 double accumulatedFlow(公升)
    unsigned long lastProgressLog = millis();
    unsigned long startTime = millis();
    // 〔修正 2026-11-06〕120 * 1000 以 AVR 16-bit int 計算會溢位 → 0xFFFFD4C0 ms ≈ 49.7 天,
    // 2 分鐘逾時從未生效(mcu31v2 同樣有此問題)。改為 120000UL,真正 120 秒逾時。舊寫法:
    // const unsigned long TIMEOUT = 120 * 1000; // 2 min safety
    const unsigned long TIMEOUT = 120000UL;  // 2 min safety

    for (;;) {
        if (millis() - startTime >= TIMEOUT) {
            out.println("ERROR: Execution timeout reached");
            break;
        }

        accumulated_ul = flow_get() * FLOW_UL_PER_PULSE;
        bool anySlotActive = areAnySlotsActive(slotStartTimes, durations);

        bool reachVolume = (desired_ul > 0) && (accumulated_ul >= desired_ul);
        if (reachVolume && !volumeReachedOnce) { volumeReachedOnce = true; volumeReachedMs = millis(); }

        unsigned long elapsed = millis() - dispenseStartMs;
        bool reachTime = (!timeGuardEnabled) || (elapsed >= minTimeMs);
        if (reachTime && !timeReachedOnce) { timeReachedOnce = true; timeReachedMs = millis(); }

        if (!dispenseStopped && reachVolume && reachTime) {
            (stoppedByTime / stoppedByFlow 判斷、Stopping water pump 輸出,同上)
            pump_water(0, waterTemp);
            desired_ul = 0;        // 防重入
            dispenseStopped = true;
        }

        for (int i = 0; i < slotCount; i++) {
            if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= durations[i]) {
                deactivateSlot(i);
                slotStartTimes[i] = 0;
            }
        }

        if (millis() - lastProgressLog >= 1000) {
            (Progress 輸出,同上)
            lastProgressLog = millis();
        }

        checkMemory();
        checkSerialBuffers();
        delay(10);
        wdt_reset();
        tick_service();   // 〔步驟 2b 過渡〕
        tx_poll();        // 〔步驟 2c 過渡〕

        bool needMoreWater =
            anySlotActive ||
            (
                (desired_ul > 0 && accumulated_ul < desired_ul) ||
                (timeGuardEnabled && (elapsed < minTimeMs))
            );

        if (!needMoreWater) break;
    }

    (保險關、finish 回報、end,同上 pour_finish)
}

void execute_actions(const char* actions) {
    (start / Received actions 輸出,同上)
    pour_req_t req;
    if (!pour_parse(actions, &req)) return;
    pour_run(&req);
}
*/

/* ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 4b〕以下為被取代的 2d 版本(浮點 / String),保留供對照。
 *  換掉原因:使用者 2026-11-06 決定內部不用浮點(單位統一為 ml / ms / µL);
 *  解析與執行分開;順帶移除 String(heap 碎片化,問題 ⑦)與 16-bit slot 比較(架構計畫 5.1)
 * ═══════════════════════════════════════════════════════════════════════
static bool areAnySlotsActive(const unsigned long* slotStartTimes, const double* durations) {
    ...
        if (slotStartTimes[i] != 0 && now - slotStartTimes[i] < (unsigned)durations[i]) {
    ...
}

static void handleWaterFlow(JsonDocument& doc, double& desiredFlow, int& waterTemp) {
    // reset flags(同上)
    if (doc.containsKey("cold")) {
        desiredFlow = String((const char*)doc["cold"]).toFloat();
        pump_water(1, 0, desiredFlow);
        waterTemp = 0;
        out.print("Cold water flow requested: ");
    } else if (doc.containsKey("warm")) {
        desiredFlow = String((const char*)doc["warm"]).toFloat();
        pump_water(1, 1, desiredFlow);
        waterTemp = 1;
        out.print("Warm water flow requested: ");
    } else {
        out.println("No water flow requested");
        return;
    }

    if (doc.containsKey("time")) {
        minTimeMs = (unsigned long)doc["time"].as<unsigned long>();
        timeGuardEnabled = (minTimeMs > 0);
    }
    dispenseStartMs = millis();

    out.println(desiredFlow);
}

static void activateRequestedSlots(JsonDocument& doc, unsigned long* slotStartTimes, double* durations) {
    for (int i = slotCount; i > 0; i--) {
        String slotKey = "slot" + String(i);
        if (doc.containsKey(slotKey)) {
            durations[i-1] = doc[slotKey];
            slotStartTimes[i-1] = millis();
            activateSlot(i-1, durations[i-1]);
            (等待 500 ms 迴圈,同上)
        }
    }
}

void execute_actions(const char* actions) {
    (start / Received actions / deserializeJson / Failed to parse JSON,同上)
    double desiredFlow = 0;
    int waterTemp = 0;
    handleWaterFlow(doc, desiredFlow, waterTemp);

    unsigned long slotStartTimes[slotCount] = {0};
    double durations[slotCount] = {0};
    activateRequestedSlots(doc, slotStartTimes, durations);

    double accumulatedFlow = 0;
    ...
        accumulatedFlow = flow_get() * flowConstant;
        bool reachVolume = (desiredFlow > 0) && (accumulatedFlow >= desiredFlow);
        ...
            desiredFlow = 0;       // 防重入
        ...
            if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= (unsigned)durations[i]) {
        ...
            out.print(String(accumulatedFlow).c_str());
            out.print(" / Desired flow: ");
            out.print(String(desiredFlow).c_str());
        ...
        bool needMoreWater =
            anySlotActive ||
            ((desiredFlow > 0 && accumulatedFlow < desiredFlow) || (timeGuardEnabled && (elapsed < minTimeMs)));
    ...(其餘同上)
}
*/

/* ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2d〕以下為被取代的 3fe056c(main31)版本,保留供對照。
 *  換掉原因:現場已知最新韌體為 mcu31v2(支援 "time"、已修正 ②),新韌體以它為行為基準。
 *  差異:cold/warm 原以 doc["cold"] 直接取數值(字串、數字都可);無 time;
 *        到量時 pump_water(0) 歸零計數但 desiredFlow 不變 → slot 結束後迴圈條件重新成立(②)
 * ═══════════════════════════════════════════════════════════════════════
static void handleWaterFlow(JsonDocument& doc, double& desiredFlow, int& waterTemp) {
    if (doc.containsKey("cold")) {
        desiredFlow = doc["cold"];
        pump_water(1, 0, desiredFlow);
        waterTemp = 0;
        out.print("Cold water flow requested: ");
    } else if (doc.containsKey("warm")) {
        desiredFlow = doc["warm"];
        pump_water(1, 1, desiredFlow);
        waterTemp = 1;
        out.print("Warm water flow requested: ");
    } else {
        out.println("No water flow requested");
        return;
    }
    out.println(desiredFlow);
}

    // (execute_actions 內,activateRequestedSlots 之後)
    while ((areAnySlotsActive(slotStartTimes, durations) || accumulatedFlow < desiredFlow) && (millis() - startTime < TIMEOUT)) {
        accumulatedFlow = flowPulse * flowConstant;

        if (accumulatedFlow >= desiredFlow && desiredFlow > 0) {
            out.println("Desired flow reached, stopping water pump");
            pump_water(0, waterTemp);
        }

        for (int i = 0; i < slotCount; i++) {
            if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= (unsigned)durations[i]) {
                deactivateSlot(i);
                slotStartTimes[i] = 0;
            }
        }

        if (millis() - lastProgressLog >= 1000) {
            out.print("Progress - Accumulated flow: ");
            out.print(String(accumulatedFlow).c_str());
            out.print(" / Desired flow: ");
            out.print(String(desiredFlow).c_str());
            out.print("\n");
            lastProgressLog = millis();
        }

        checkMemory();
        checkSerialBuffers();
        delay(10);
        wdt_reset();
        tick_service();
        tx_poll();

        if (millis() - startTime >= TIMEOUT) {
            out.println("ERROR: Execution timeout reached");
            break;
        }
    }

    // Ensure everything is turned off
    pump_water(0, waterTemp);
    for (int i = 0; i < slotCount; i++) {
        deactivateSlot(i);
    }

    out.println("end");
*/
