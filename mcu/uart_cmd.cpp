#include <Arduino.h>
#include "uart_cmd.h"
#include "pins.h"          // slotCount、pin_write、ID_SLOT1
#include "device.h"        // pump_water
#include "dispense.h"      // execute_actions

#define MAX_INPUT_LENGTH 200
char inputString[MAX_INPUT_LENGTH];
unsigned int inputLength = 0;
boolean stringComplete = false;

void uart_cmd_init(void) {
    memset(inputString, 0, MAX_INPUT_LENGTH);  // Initialize inputString with null terminators
}

/* ══════════════════════════════════════════════════════════
 *  ▌ 區塊 1  字串分析
 * ══════════════════════════════════════════════════════════ */

void serialEvent() {
    while (Serial.available()) {
        char inChar = (char)Serial.read();

        if (inputLength < MAX_INPUT_LENGTH - 1) {  // Save space for null terminator
            inputString[inputLength++] = inChar;
        }

        if (inChar == '#') {
            stringComplete = true;
        }
    }
}

/* ══════════════════════════════════════════════════════════
 *  ▌ 區塊 2  命令下達(原 loop() 中段,內容不變)
 * ══════════════════════════════════════════════════════════ */

void cmd_poll(void) {
    if (stringComplete) {
        if (inputLength > 0 && inputString[inputLength - 1] == '#') {
            inputString[inputLength - 1] = '\0';  // Replace '#' with null terminator
            if (strcmp(inputString, "ping") != 0) {
                Serial.println(inputString);
            }

            // Processing commands
            if (strcmp(inputString, "ping") == 0) {
                Serial.println("pong\n");
            } else if (strcmp(inputString, "startcoldwater") == 0) {
                pump_water(1, 0);
            } else if (strcmp(inputString, "startwarmwater") == 0) {
                pump_water(1, 1);
            } else if (strcmp(inputString, "stop") == 0) {
                pump_water(0, 0);
                for (int i = 0; i < slotCount; i++) {
                    pin_write(ID_SLOT1 + i, 0);
                }
            } else {
                boolean slotCommandProcessed = false;

                // Check for start slot commands
                for (int i = 0; i < slotCount; i++) {
                    char command[15];
                    sprintf(command, "startslot%d", i + 1);
                    if (strcmp(inputString, command) == 0) {
                        pin_write(ID_SLOT1 + i, 1);
                        slotCommandProcessed = true;
                        break;
                    }
                }

                // Check for stop slot commands
                for (int i = 0; i < slotCount; i++) {
                    char command[15];
                    sprintf(command, "stopslot%d", i + 1);
                    if (strcmp(inputString, command) == 0) {
                        pin_write(ID_SLOT1 + i, 0);
                        slotCommandProcessed = true;
                        break;
                    }
                }

                // If no slot commands processed, check other actions
                if (!slotCommandProcessed) {
                    execute_actions(inputString);
                }
            }

            inputLength = 0;  // Reset input length
            stringComplete = false;
        }
    }
}
