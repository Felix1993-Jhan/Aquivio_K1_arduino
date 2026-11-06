#include <Arduino.h>
#include "device.h"
#include "pins.h"
#include "flowmeter.h"     // flowPulse、flowConstant
#include "tx_queue.h"      // 〔步驟 2c〕輸出改走 out(TX 佇列),格式不變

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

        out.print("pulse:");
        out.print(flowPulse);
        out.print("\n");

        if(currentTemp == 1 ){
            out.print("warm water usage:");
            out.print(currentUsage,3);
            out.print("\n");
        }else{
            out.print("cold water usage:");
            out.print(currentUsage,3);
            out.print("\n");
        }

        flowPulse = 0;  // Reset the flow pulse count for the next measurement
        measuringMode = false;
    }
}

void activateSlot(int slotNumber, unsigned long duration) {
    pin_write(ID_SLOT1 + slotNumber, 1);
    out.print("Activated slot ");
    out.print(slotNumber + 1);
    out.print(" for duration: ");
    out.println(duration);
}

void deactivateSlot(int slotNumber) {
    pin_write(ID_SLOT1 + slotNumber, 0);
    out.print("Deactivated slot ");
    out.println(slotNumber + 1);
}
