#include <Arduino.h>
#include <avr/wdt.h>
#include "diagnostics.h"
#include "MemoryFree.h"

// Memory variables
unsigned long lastMemoryPrintTime = 0;
const unsigned long memoryPrintInterval = 30 * 1000; // Print every 30 seconds
unsigned long lastmcuAliveTime = 0;
const unsigned long mcuAliveInterval = 1 * 1000; // Print every 1000 ms (1 second)


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

/* 原 loop() 開頭的兩段(順序不變:先記憶體、再 mcuAlive) */
void diag_poll(void) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastMemoryPrintTime >= memoryPrintInterval) {
        checkMemory();
        lastMemoryPrintTime = currentMillis;
    }

    if (currentMillis - lastmcuAliveTime >= mcuAliveInterval) {
        Serial.print("mcuAlive \n");
        lastmcuAliveTime = currentMillis;
    }
}
