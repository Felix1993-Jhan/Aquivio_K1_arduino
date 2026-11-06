#include <Arduino.h>
#include "flowmeter.h"
#include "pins.h"          // FLOW_PIN_READ

// Flowmeter variables
// const int flowmeterPin = A8;   // 〔步驟 1〕改用 pins.h 的 ID_FLOWMETER / FLOW_PIN_READ()
/* 〔步驟 2d〕對齊 old/mcu31v2:流量常數改為 0.0033(mcu31v2 註解「依實測校正」)。舊值保留供對照:
const double flowConstant = 0.0037;
*/
const double flowConstant = 0.0033;   // L per pulse(依實測校正,同 mcu31v2)
volatile int flowPulse = 0;
volatile bool flowPulseOccurred = false;

void flow_init(void) {
    PCICR |= (1 << PCIE2);   // Enable pin change interrupt for A8 to A15
    PCMSK2 |= (1 << PCINT16);  // Enable pin change interrupt for A8 specifically
}

ISR(PCINT2_vect) {
    int currentFlowState = FLOW_PIN_READ() ? HIGH : LOW;   // 〔步驟 1〕直讀 PINK,取代 digitalRead
    static int lastFlowState = LOW;
    if (lastFlowState == LOW && currentFlowState == HIGH) {
        flowPulse++;
        flowPulseOccurred = true;
    }
    lastFlowState = currentFlowState;
}
