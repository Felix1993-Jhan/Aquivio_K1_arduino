#include <Arduino.h>
#include "uart_cmd.h"
#include "pins.h"          // slotCount、pin_write、ID_SLOT1
#include "device.h"        // pump_water
#include "dispense.h"      // execute_actions
#include "tx_queue.h"      // 〔步驟 2c〕輸出改走 out(TX 佇列),格式不變

/* 〔2026-11-06〕命令長度上限 200 → 256:上位機 10 個 slot 全開、每個帶 2 位小數(例 "120000.99")
 * 再加 time 時最長約 230 字元,超過舊上限 199 → 整筆被丟棄(舊版 mcu31v2 則永久卡死)。
 * 改 256 可收 255 字元,涵蓋最壞情況(slot 受 120 秒逾時限制,最多 6 位整數)。舊值保留供對照:
#define MAX_INPUT_LENGTH 200
*/
#define MAX_INPUT_LENGTH 256
static char inputString[MAX_INPUT_LENGTH];   // 一筆命令(不含 '#'),最多 255 字元 + '\0'
static unsigned int inputLength = 0;
static bool inputOverflow = false;           // 〔步驟 4a〕這一筆超長 → 丟棄到下一個 '#'
/* 〔步驟 4a〕不再需要「收完一整批」旗標。舊變數保留供對照:
boolean stringComplete = false;
*/

void uart_cmd_init(void) {
    memset(inputString, 0, MAX_INPUT_LENGTH);  // Initialize inputString with null terminators
}

/* ══════════════════════════════════════════════════════════
 *  ▌ 區塊 2  命令下達(原 loop() 中段的判斷,內容不變;〔步驟 4a〕抽成函式)
 * ══════════════════════════════════════════════════════════ */

static void process_command(char* cmd) {
    if (strcmp(cmd, "ping") != 0) {
        out.println(cmd);
    }

    // Processing commands
    if (strcmp(cmd, "ping") == 0) {
        out.println("pong\n");
    } else if (strcmp(cmd, "startcoldwater") == 0) {
        pump_water(1, 0);
    } else if (strcmp(cmd, "startwarmwater") == 0) {
        pump_water(1, 1);
    } else if (strcmp(cmd, "stop") == 0) {
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
            if (strcmp(cmd, command) == 0) {
                pin_write(ID_SLOT1 + i, 1);
                slotCommandProcessed = true;
                break;
            }
        }

        // Check for stop slot commands
        for (int i = 0; i < slotCount; i++) {
            char command[15];
            sprintf(command, "stopslot%d", i + 1);
            if (strcmp(cmd, command) == 0) {
                pin_write(ID_SLOT1 + i, 0);
                slotCommandProcessed = true;
                break;
            }
        }

        // If no slot commands processed, check other actions
        if (!slotCommandProcessed) {
            execute_actions(cmd);
        }
    }
}

/* ══════════════════════════════════════════════════════════
 *  ▌ 區塊 1  字串分析(〔步驟 4a〕逐 byte,架構計畫 5.4)
 *   讀到 '#' 就處理一筆(含回覆),再繼續讀下一個 byte → 多筆命令一筆一筆完成
 *   超過 255 字元(MAX_INPUT_LENGTH − 1):丟棄這一筆直到下一個 '#',之後自動恢復
 * ══════════════════════════════════════════════════════════ */

void cmd_poll(void) {
    while (Serial.available()) {
        char inChar = (char)Serial.read();

        if (inChar == '#') {
            if (!inputOverflow) {
                inputString[inputLength] = '\0';
                process_command(inputString);   // 出杯 JSON 會在這裡阻塞到 end(步驟 5 前)
            }
            inputLength = 0;
            inputOverflow = false;
        } else if (inputLength < MAX_INPUT_LENGTH - 1) {
            inputString[inputLength++] = inChar;
        } else {
            inputOverflow = true;               // 超長:這一筆作廢,等下一個 '#'
        }
    }
}

/* ══════════════════════════════════════════════════════════
 *  〔步驟 4a〕以下為被取代的舊寫法,保留供對照。
 *  換掉原因(架構計畫 5.4):
 *   1. 緩衝滿 199 byte 後 '#' 存不進去,但 stringComplete 已設 → 永久不清緩衝,
 *      連 stop# 都收不到,看門狗照餵所以不會重置
 *   2. 一次讀到多筆(ping#stop#)會整串當 JSON,解析失敗後全部遺失
 *  換成:cmd_poll() 逐 byte 解析,讀到 '#' 就處理一筆
 * ══════════════════════════════════════════════════════════
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

void cmd_poll(void) {
    if (stringComplete) {
        if (inputLength > 0 && inputString[inputLength - 1] == '#') {
            inputString[inputLength - 1] = '\0';  // Replace '#' with null terminator
            ...(判斷內容同上方 process_command)...
            inputLength = 0;  // Reset input length
            stringComplete = false;
        }
    }
}
*/
