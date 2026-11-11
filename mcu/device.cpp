#include <Arduino.h>
#include "device.h"
#include "pins.h"
#include "flowmeter.h"     // flow_take、FLOW_UL_PER_PULSE
#include "tx_queue.h"      // 〔步驟 2c〕輸出改走 out(TX 佇列),格式不變

int currentTemp = 0;
bool measuringMode = false;
/* 〔步驟 4b〕不用浮點:currentUsage 改為 pump_water 內的整數微升;另三個原程式宣告但未使用。舊宣告保留供對照:
double currentUsage;
double coldWaterUsage = 0;
double warmWaterUsage = 0;
*/
bool isMeasuring = false;   // 原程式宣告但未使用,原樣保留

void pump_water(int state, int water_temp) {
    /* 〔2026-11-10 腳位修正〕watervalve 移到實際接線 A11 後不再與出水連動(使用者決定:K1 上這顆沒有使用,
     * 設為一般輸出、開機拉低)。原 main31 / mcu31v2 驅動的 D17 沒接東西,機台實際上從未打開過它,
     * 不連動 = 維持機台現有行為。舊程式保留供對照:
    pin_write(ID_WATERVALVE, state == 1);
     */

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

        /* 〔步驟 3〕改為 flow_take() 原子讀取並歸零:舊版讀 2 次 flowPulse(計算、輸出)再歸零,
         * 中間被 ISR 插入時數值會不一致或漏掉 pulse。輸出格式不變。舊程式保留供對照:
        currentUsage = flowPulse * flowConstant;

        out.print("pulse:");
        out.print(flowPulse);
        out.print("\n");
        */
        uint32_t pulses = flow_take();
        /* 〔步驟 4b〕不用浮點:用水量改為整數微升,輸出時換成公升 3 位小數(標準四捨五入),格式同舊版。
         * 舊程式保留供對照:
        currentUsage = pulses * flowConstant;
        ...
            out.print(currentUsage,3);
        */
        uint32_t usage_ul = pulses * FLOW_UL_PER_PULSE;

        out.print("pulse:");
        out.print(pulses);
        out.print("\n");

        if(currentTemp == 1 ){
            out.print("warm water usage:");
            print_liters_ul(usage_ul, 3);
            out.print("\n");
        }else{
            out.print("cold water usage:");
            print_liters_ul(usage_ul, 3);
            out.print("\n");
        }

        // flowPulse = 0;  // Reset the flow pulse count for the next measurement   〔步驟 3〕已由 flow_take() 歸零
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
