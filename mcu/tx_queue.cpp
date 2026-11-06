#include <Arduino.h>
#include "tx_queue.h"

void nonBlockingPrint(const char* message) {
    static char printBuffer[64];  // Adjust size as needed
    static int printIndex = 0;

    if (message != NULL) {
        // New message to print
        strncpy(printBuffer, message, sizeof(printBuffer) - 1);
        printBuffer[sizeof(printBuffer) - 1] = '\0';  // Ensure null-termination
        printIndex = 0;
    }

    while (printIndex < strlen(printBuffer) && Serial.availableForWrite() > 0) {
        Serial.write(printBuffer[printIndex++]);
    }

    if (printIndex >= strlen(printBuffer)) {
        printBuffer[0] = '\0';  // Clear buffer after fully printed
        printIndex = 0;
    }
}
