#include <Arduino.h>
#include <ArduinoJson.h>
#include <avr/wdt.h>
#include "dispense.h"
#include "pins.h"          // slotCount
#include "device.h"        // pump_water、activateSlot、deactivateSlot
#include "flowmeter.h"     // flowPulse、flowConstant
#include "diagnostics.h"   // checkMemory、checkSerialBuffers
#include "tx_queue.h"      // 〔步驟 2c〕out、tx_poll(取代 nonBlockingPrint)
#include "tick.h"          // tick_service(步驟 2b 過渡)

static bool areAnySlotsActive(const unsigned long* slotStartTimes, const double* durations) {
    unsigned long currentTime = millis();
    for (int i = 0; i < slotCount; i++) {
        if (slotStartTimes[i] != 0 && currentTime - slotStartTimes[i] < (unsigned)durations[i]) {
            return true;
        }
    }
    return false;
}

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
     * 2 分鐘逾時從未生效。改為 120000UL(unsigned long 常數),真正 120 秒逾時。舊寫法保留供對照:
    const unsigned long TIMEOUT = 120 * 1000;  // 2 minutes timeout
    */
    const unsigned long TIMEOUT = 120000UL;  // 2 minutes timeout

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
            /* 〔步驟 2c〕nonBlockingPrint 改為 out.print(TX 佇列):內容相同,TX 滿時不再被截斷。
             * String(x) 保留(步驟 4 移除 String)。舊程式保留供對照:
            nonBlockingPrint("Progress - Accumulated flow: ");
            nonBlockingPrint(String(accumulatedFlow).c_str());
            nonBlockingPrint(" / Desired flow: ");
            nonBlockingPrint(String(desiredFlow).c_str());
            nonBlockingPrint("\n");
            */
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
}
