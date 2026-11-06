#include <Arduino.h>
#include <ArduinoJson.h>
#include <avr/wdt.h>
#include "dispense.h"
#include "pins.h"          // slotCount
#include "device.h"        // pump_water、activateSlot、deactivateSlot
#include "flowmeter.h"     // flowPulse、flowConstant
#include "diagnostics.h"   // checkMemory、checkSerialBuffers
#include "tx_queue.h"      // nonBlockingPrint

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
        Serial.print("Cold water flow requested: ");
    } else if (doc.containsKey("warm")) {
        desiredFlow = doc["warm"];
        pump_water(1, 1, desiredFlow);
        waterTemp = 1;
        Serial.print("Warm water flow requested: ");
    } else {
        Serial.println("No water flow requested");
        return;
    }
    Serial.println(desiredFlow);
}

static void activateRequestedSlots(JsonDocument& doc, unsigned long* slotStartTimes, double* durations) {
    for (int i = slotCount; i > 0; i--) {
        String slotKey = "slot" + String(i);
        if (doc.containsKey(slotKey)) {
            durations[i-1] = doc[slotKey];
            slotStartTimes[i-1] = millis();
            activateSlot(i-1, durations[i-1]);
            delay(500);
        }
    }
}

void execute_actions(const char* actions) {
    Serial.println("start");
    Serial.print("Received actions: ");
    Serial.println(actions);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, actions);

    if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
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
            Serial.println("Desired flow reached, stopping water pump");
            pump_water(0, waterTemp);
        }

        for (int i = 0; i < slotCount; i++) {
            if (slotStartTimes[i] != 0 && millis() - slotStartTimes[i] >= (unsigned)durations[i]) {
                deactivateSlot(i);
                slotStartTimes[i] = 0;
            }
        }

        if (millis() - lastProgressLog >= 1000) {
            nonBlockingPrint("Progress - Accumulated flow: ");
            nonBlockingPrint(String(accumulatedFlow).c_str());
            nonBlockingPrint(" / Desired flow: ");
            nonBlockingPrint(String(desiredFlow).c_str());
            nonBlockingPrint("\n");
            lastProgressLog = millis();
        }

        checkMemory();
        checkSerialBuffers();
        delay(10);
        wdt_reset();

        if (millis() - startTime >= TIMEOUT) {
            Serial.println("ERROR: Execution timeout reached");
            break;
        }
    }

    // Ensure everything is turned off
    pump_water(0, waterTemp);
    for (int i = 0; i < slotCount; i++) {
        deactivateSlot(i);
    }

    Serial.println("end");
}
