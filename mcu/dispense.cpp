#include <Arduino.h>
#include <ArduinoJson.h>
#include <avr/wdt.h>
#include "dispense.h"
#include "pins.h"          // slotCount
#include "device.h"        // pump_water、activateSlot、deactivateSlot
#include "flowmeter.h"     // flow_get、FLOW_UL_PER_PULSE
#include "diagnostics.h"   // checkMemory、checkSerialBuffers
#include "tx_queue.h"      // 〔步驟 2c〕out、tx_poll;〔步驟 4b〕print_liters_ul
#include "tick.h"          // tick_service(步驟 2b 過渡)

/* ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2d〕出杯邏輯對齊 old/mcu31v2/mcu31v2.ino(老闆電腦上的版本,目前已知最新)
 *    - 支援 JSON 的 "time"(毫秒):最短出水時間,須「到量 AND 時間到」才停水
 *    - 修正問題 ②:停水後 desired = 0、dispenseStopped = true,slot 結束後不再卡住
 *    - 輸出 "Stopping water pump (volume & time satisfied)..."、"finish: ..."
 *    - cold / warm 只讀字串(同 mcu31v2;上位機訂單送字串,數字會讀成 0)
 *  〔步驟 4b〕解析層(pour_parse)與執行層(pour_run)分開;內部全部整數、不用 String:
 *    - 水量一律 µL(目標量、累計量 = pulse × FLOW_UL_PER_PULSE)、時間一律 ms
 *    - slot 時間 32-bit 比較(舊版 (unsigned) 在 AVR 為 16-bit,> 65.5 秒會在錯誤時間關閉,架構計畫 5.1)
 *    - 輸出換成公升小數、位數同舊版(標準四捨五入,剛好 .5 時可能與舊版浮點差 1 個最小位)
 *  保留本專案的修正:TIMEOUT 120000UL、TX 佇列(out)、過渡用 tick_service / tx_poll
 *  被取代的版本(2d 浮點版、3fe056c 版)以註解保留在檔尾
 * ═══════════════════════════════════════════════════════════════════════ */

// -------------------- Min-Time guard (for cold/warm) --------------------
static unsigned long dispenseStartMs = 0;    // 本次出水開始時間
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

static bool areAnySlotsActive(const unsigned long* slotStartTimes, const uint32_t* durations) {
    unsigned long now = millis();
    for (int i = 0; i < slotCount; i++) {
        if (slotStartTimes[i] != 0 && now - slotStartTimes[i] < durations[i]) {   // 〔步驟 4b〕32-bit 比較
            return true;
        }
    }
    return false;
}

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

/* slot 毫秒:上位機送字串("2181.82"),也接受數字;小數截掉(同舊版 (unsigned)durations 的截斷) */
static uint32_t json_ms(JsonVariantConst v) {
    const char* s = v.as<const char*>();
    if (s != NULL) return parse_fixed(s, 0);
    return v.as<uint32_t>();
}

static bool pour_parse(const char* actions, pour_req_t* r) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);
    if (error) {
        out.print("Failed to parse JSON: ");
        out.println(error.c_str());
        return false;
    }

    memset(r, 0, sizeof(*r));

    // 水路:cold 優先(同舊版);水量只讀字串(同 mcu31v2:數字 → as<const char*>() 為 NULL → 0)
    if (doc.containsKey("cold")) {
        r->water = WATER_COLD;
        r->desired_ul = parse_fixed(doc["cold"].as<const char*>(), 6);   // 公升 → µL
    } else if (doc.containsKey("warm")) {
        r->water = WATER_WARM;
        r->desired_ul = parse_fixed(doc["warm"].as<const char*>(), 6);
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
 *  ▌ 執行層:只看 pour_req_t(流程同 2d / mcu31v2,數值改整數)
 * ══════════════════════════════════════════════════════════ */

static void pour_run(const pour_req_t* r) {
    uint32_t desired_ul = 0;                       // 目標水量(µL)
    int waterTemp = 0;

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
            /* 〔步驟 2c 過渡〕delay(500) 改成等 500 ms 的迴圈,期間照常送 TX、推進 tick;
             * 等待期間仍不檢查水量 / slot(同舊版)。步驟 5 改狀態機後移除 */
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
    /* 〔修正 2026-11-06〕120 * 1000 以 AVR 16-bit int 計算會溢位 → 0xFFFFD4C0 ms ≈ 49.7 天,
     * 2 分鐘逾時從未生效(mcu31v2 同樣有此問題)。改為 120000UL,真正 120 秒逾時。舊寫法保留供對照:
    const unsigned long TIMEOUT = 120 * 1000; // 2 min safety
    */
    const unsigned long TIMEOUT = 120000UL;  // 2 min safety

    // 以 for(;;) 執行,並在迴圈內決定是否繼續或收尾,避免邊界跳出
    for (;;) {
        // Timeout 防護
        if (millis() - startTime >= TIMEOUT) {
            out.println("ERROR: Execution timeout reached");
            break;
        }

        accumulated_ul = flow_get() * FLOW_UL_PER_PULSE;   // 〔步驟 3〕原子讀取;〔步驟 4b〕整數 µL

        // 條件評估
        bool anySlotActive = areAnySlotsActive(slotStartTimes, durations);

        bool reachVolume = (desired_ul > 0) && (accumulated_ul >= desired_ul);   // 同單位(µL)直接比
        if (reachVolume && !volumeReachedOnce) { volumeReachedOnce = true; volumeReachedMs = millis(); }

        unsigned long elapsed = millis() - dispenseStartMs;
        bool reachTime = (!timeGuardEnabled) || (elapsed >= minTimeMs);
        if (reachTime && !timeReachedOnce) { timeReachedOnce = true; timeReachedMs = millis(); }

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

        // slot 倒數(〔步驟 4b〕32-bit 比較)
        for (int i = 0; i < slotCount; i++) {
            if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= durations[i]) {
                deactivateSlot(i);
                slotStartTimes[i] = 0;
            }
        }

        // 進度輸出(〔步驟 4b〕String(x) 改為 print_liters_ul,公升 2 位,格式同舊版)
        if (millis() - lastProgressLog >= 1000) {
            out.print("Progress - Accumulated flow: ");
            print_liters_ul(accumulated_ul, 2);
            out.print(" / Desired flow: ");
            print_liters_ul(desired_ul, 2);
            out.print("\n");
            lastProgressLog = millis();
        }

        checkMemory();
        checkSerialBuffers();
        delay(10);
        wdt_reset();
        tick_service();   // 〔步驟 2b 過渡〕出杯阻塞期間仍推進 tick,避免累積超過 16-bit;步驟 5 改狀態機後移除
        tx_poll();        // 〔步驟 2c 過渡〕出杯阻塞期間仍送出 TX 佇列;步驟 5 改狀態機後移除

        // 判斷是否要繼續:任何 slot 在跑,或出水尚未同時滿足(到量 AND 時間)
        bool needMoreWater =
            anySlotActive ||
            (
                (desired_ul > 0 && accumulated_ul < desired_ul) ||
                (timeGuardEnabled && (elapsed < minTimeMs))
            );

        if (!needMoreWater) break;  // 條件滿足 → 收尾
    }

    // 保險關
    pump_water(0, waterTemp);
    for (int i = 0; i < slotCount; i++) deactivateSlot(i);

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
}

// -------------------- Main executor --------------------
void execute_actions(const char* actions) {
    out.println("start");
    out.print("Received actions: ");
    out.println(actions);

    pour_req_t req;
    if (!pour_parse(actions, &req)) return;   // 解析失敗:已印 "Failed to parse JSON: ...",不送 end(同舊版)
    pour_run(&req);
}

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
