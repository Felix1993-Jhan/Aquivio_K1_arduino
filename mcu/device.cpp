#include <Arduino.h>
#include "device.h"
#include "pins.h"
#include "flowmeter.h"     // flowPulse、flowConstant

int currentTemp = 0;
bool measuringMode = false;
double currentUsage;
// 以下三個原程式宣告但未使用,原樣保留
double coldWaterUsage = 0;
double warmWaterUsage = 0;
bool isMeasuring = false;

void pump_water(int state, int water_temp, double desiredFlow) {
    pin_write(ID_WATERVALVE, state == 1);

    if (state == 1) {
        pin_write(ID_WATERPUMP, 1);
        if(water_temp == 1) {
            pin_write(ID_WARMVALVE, 1);
            pin_write(ID_COLDVALVE, 0);
            currentTemp = 1;
        } else {
            pin_write(ID_WARMVALVE, 0);
            pin_write(ID_COLDVALVE, 1);
            currentTemp = 0;
        }
        measuringMode = true;

    } else {
        pin_write(ID_WATERPUMP, 0);
        pin_write(ID_WARMVALVE, 0);
        pin_write(ID_COLDVALVE, 0);

        currentUsage = flowPulse * flowConstant;

        Serial.print("pulse:");
        Serial.print(flowPulse);
        Serial.print("\n");

        if(currentTemp == 1 ){
            Serial.print("warm water usage:");
            Serial.print(currentUsage,3);
            Serial.print("\n");
        }else{
            Serial.print("cold water usage:");
            Serial.print(currentUsage,3);
            Serial.print("\n");
        }

        flowPulse = 0;  // Reset the flow pulse count for the next measurement
        measuringMode = false;
    }
}

void activateSlot(int slotNumber, unsigned long duration) {
    pin_write(ID_SLOT1 + slotNumber, 1);
    Serial.print("Activated slot ");
    Serial.print(slotNumber + 1);
    Serial.print(" for duration: ");
    Serial.println(duration);
}

void deactivateSlot(int slotNumber) {
    pin_write(ID_SLOT1 + slotNumber, 0);
    Serial.print("Deactivated slot ");
    Serial.println(slotNumber + 1);
}
