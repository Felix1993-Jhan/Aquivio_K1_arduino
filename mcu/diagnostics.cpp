#include <Arduino.h>
#include <avr/wdt.h>
#include "diagnostics.h"
#include "MemoryFree.h"

#include "tick.h"          // K1_DEBUG_TICK、tick_backlog_max

/* 〔步驟 2b〕週期改由 1 ms tick 計數(diag_tick_1ms),不再用 millis() 比較。
 * 週期不變(記憶體 30 s、mcuAlive 1 s)。舊變數保留供對照:
// Memory variables
unsigned long lastMemoryPrintTime = 0;
const unsigned long memoryPrintInterval = 30 * 1000; // Print every 30 seconds
unsigned long lastmcuAliveTime = 0;
const unsigned long mcuAliveInterval = 1 * 1000; // Print every 1000 ms (1 second)
*/
#define MEMORY_CHECK_MS   30000u   // 每 30 秒檢查記憶體(uint16 上限 65535)
#define MCU_ALIVE_MS       1000u   // 每 1 秒送 mcuAlive

static uint16_t memory_ms = 0, alive_ms = 0;          // 只在 tick_main(主迴圈)內修改
static uint8_t  memory_due = 0, alive_due = 0;        // 週期到 → 設旗標,diag_poll 輸出後清除


void setupWatchdog() {
    cli();  // Disable interrupts
    wdt_reset();  // Reset Watchdog Timer
    MCUSR &= ~(1<<WDRF);  // Clear WDRF in MCUSR
    WDTCSR |= (1<<WDCE) | (1<<WDE);  // Set WDCE (Watchdog Change Enable)
    WDTCSR = (1<<WDE) | (1<<WDP3) | (1<<WDP0);  // Set WDE, and 8 seconds delay
    sei();  // Enable interrupts
}

void checkMemory() {
    static int lowestMemory = freeMemory();
    int currentMemory = freeMemory();

    if (currentMemory < lowestMemory) {
        lowestMemory = currentMemory;
        Serial.print("New lowest memory: ");
        Serial.print(lowestMemory);
        Serial.println(" bytes");
    }

    if (currentMemory < 100) {  // Arbitrary low memory threshold
        Serial.println("WARNING: Very low memory!");
    }
}

void checkSerialBuffers() {
    // Check RX buffer
    if (Serial.available() > 60) {  // Assuming a 64-byte RX buffer
        Serial.println("WARNING: RX buffer near capacity");
        while (Serial.available() > 0) {
            Serial.read();  // Clear the buffer
        }
    }

    // Check TX buffer
    if (Serial.availableForWrite() < 32) {  // Less than half of TX buffer available
        // Serial.println("WARNING: TX buffer getting full");
        Serial.flush();  // Wait for all data to be transmitted
    }
}

/* 〔步驟 2b〕每 1 ms 由 tick_main() 呼叫:只計數、設旗標,不輸出
 * (補跑多個 tick 時旗標只會設一次 → 不會連印多行,同舊版 millis() 的效果) */
void diag_tick_1ms(void) {
    if (++memory_ms >= MEMORY_CHECK_MS) { memory_ms = 0; memory_due = 1; }
    if (++alive_ms  >= MCU_ALIVE_MS)    { alive_ms  = 0; alive_due  = 1; }
}

/* 原 loop() 開頭的兩段(順序不變:先記憶體、再 mcuAlive)
 * 只在 loop() 呼叫 → 出杯中 execute_actions 阻塞時不輸出;旗標保留到 end 之後才印一次,同 3fe056c */
void diag_poll(void) {
    /* 〔步驟 2b〕改為看 tick 設的旗標。舊程式保留供對照:
    unsigned long currentMillis = millis();
    if (currentMillis - lastMemoryPrintTime >= memoryPrintInterval) {
        checkMemory();
        lastMemoryPrintTime = currentMillis;
    }

    if (currentMillis - lastmcuAliveTime >= mcuAliveInterval) {
        Serial.print("mcuAlive \n");
        lastmcuAliveTime = currentMillis;
    }
    */
    if (memory_due) {
        memory_due = 0;
        checkMemory();
    }

    if (alive_due) {
        alive_due = 0;
        Serial.print("mcuAlive \n");
#ifdef K1_DEBUG_TICK
        Serial.print("tick backlog max: ");   // 除錯版才有,正式版不輸出
        Serial.println(tick_backlog_max());
#endif
    }
}
