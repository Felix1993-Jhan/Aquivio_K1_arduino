#include <Arduino.h>
#include <ArduinoJson.h>
#include <avr/wdt.h>
#include "dispense.h"
#include "pins.h"          // slotCount
#include "device.h"        // pump_water、activateSlot、deactivateSlot
#include "flowmeter.h"     // flow_get、flowConstant
#include "diagnostics.h"   // checkMemory、checkSerialBuffers
#include "tx_queue.h"      // 〔步驟 2c〕out、tx_poll(取代 nonBlockingPrint)
#include "tick.h"          // tick_service(步驟 2b 過渡)

/* ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2d〕出杯邏輯對齊 old/mcu31v2/mcu31v2.ino(老闆電腦上的版本,目前已知最新)
 *    - 支援 JSON 的 "time"(毫秒):最短出水時間,須「到量 AND 時間到」才停水
 *    - 修正問題 ②:停水後 desiredFlow = 0、dispenseStopped = true,slot 結束後不再卡住
 *    - 新增輸出 "Stopping water pump (volume & time satisfied)..."、"finish: ..."
 *    - cold / warm 只讀字串(同 mcu31v2;上位機訂單送字串,數字會讀成 0)
 *  保留本專案的修正:TIMEOUT 120000UL、TX 佇列(out)、過渡用 tick_service / tx_poll
 *  被取代的 3fe056c 版本以註解保留在檔尾
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

static bool areAnySlotsActive(const unsigned long* slotStartTimes, const double* durations) {
    unsigned long now = millis();
    for (int i = 0; i < slotCount; i++) {
        if (slotStartTimes[i] != 0 && now - slotStartTimes[i] < (unsigned)durations[i]) {
            return true;
        }
    }
    return false;
}

// -------------------- Water command parse --------------------
// 讀取 cold/warm + time(毫秒),並在啟動後記錄開始時間
static void handleWaterFlow(JsonDocument& doc, double& desiredFlow, int& waterTemp) {
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

// -------------------- Activate slots from JSON --------------------
static void activateRequestedSlots(JsonDocument& doc, unsigned long* slotStartTimes, double* durations) {
    for (int i = slotCount; i > 0; i--) {
        String slotKey = "slot" + String(i);
        if (doc.containsKey(slotKey)) {
            durations[i-1] = doc[slotKey];
            slotStartTimes[i-1] = millis();
            activateSlot(i-1, durations[i-1]);
            /* 〔步驟 2c 過渡〕delay(500) 期間沒人呼叫 tx_poll(),TX 佇列送不出去(實測 start 晚 2 秒)。
             * 改成等 500 ms 的迴圈,期間照常送 TX、推進 tick;等待期間仍不檢查水量 / slot(同舊版)。
             * 步驟 5 改狀態機後移除。舊程式保留供對照:
            delay(500);
            */
            uint16_t t0 = tick_get();                          // 開始等待時的 tick(區域變數)
            while ((uint16_t)(tick_get() - t0) < 500) {        // 16-bit 相減,繞回時也正確
                tick_service();
                tx_poll();
            }
        }
    }
}

// -------------------- Main executor --------------------
void execute_actions(const char* actions) {
    out.println("start");
    out.print("Received actions: ");
    out.println(actions);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);
    if (error) {
        out.print("Failed to parse JSON: ");
        out.println(error.c_str());
        return;
    }

    double desiredFlow = 0;
    int waterTemp = 0;
    handleWaterFlow(doc, desiredFlow, waterTemp);

    unsigned long slotStartTimes[slotCount] = {0};
    double durations[slotCount] = {0};
    activateRequestedSlots(doc, slotStartTimes, durations);

    double accumulatedFlow = 0;
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

        // accumulatedFlow = flowPulse * flowConstant;   〔步驟 3〕改為原子讀取(舊版 16-bit 非原子,可能讀到撕裂值而提早停水)
        accumulatedFlow = flow_get() * flowConstant;

        // 條件評估
        bool anySlotActive = areAnySlotsActive(slotStartTimes, durations);

        bool reachVolume = (desiredFlow > 0) && (accumulatedFlow >= desiredFlow);
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
            desiredFlow = 0;       // 防重入
            dispenseStopped = true;
        }

        // slot 倒數
        for (int i = 0; i < slotCount; i++) {
            if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= (unsigned)durations[i]) {
                deactivateSlot(i);
                slotStartTimes[i] = 0;
            }
        }

        // 進度輸出(〔步驟 2c〕nonBlockingPrint 改為 out.print;String(x) 保留,步驟 4 移除)
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
        tick_service();   // 〔步驟 2b 過渡〕出杯阻塞期間仍推進 tick,避免累積超過 16-bit;步驟 5 改狀態機後移除
        tx_poll();        // 〔步驟 2c 過渡〕出杯阻塞期間仍送出 TX 佇列;步驟 5 改狀態機後移除

        // 判斷是否要繼續:任何 slot 在跑,或出水尚未同時滿足(到量 AND 時間)
        bool needMoreWater =
            anySlotActive ||
            (
                (desiredFlow > 0 && accumulatedFlow < desiredFlow) ||
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
