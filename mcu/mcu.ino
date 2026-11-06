/* ═══════════════════════════════════════════════════════════════════════
 *  Aquivio K1 主程式 — 只留 setup() / loop(),功能分在各模組
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由原單一 mcu.ino 拆出,函式內容原樣搬移、行為不變(原版見 git d456885)
 *
 *    pins         腳位表、暫存器讀寫
 *    device       驅動元件控制:水路、slot 開關
 *    flowmeter    流量計脈衝計數
 *    dispense     出杯流程(JSON 動作)
 *    uart_cmd     字串分析 + 命令下達
 *    tx_queue     字串回覆(TX 發送佇列,⚠ K1 新增)
 *    tick         1 ms 計時(Timer5)與每 1 ms 的工作分派
 *    diagnostics  看門狗、記憶體、mcuAlive、Serial 緩衝檢查
 *    pressure     A1 水壓(已寫好、尚未啟用)
 *    MemoryFree   剩餘 SRAM 計算
 * ═══════════════════════════════════════════════════════════════════════ */

#include <avr/wdt.h>       // wdt_reset
#include "pins.h"          // 〔步驟 1〕GPIO 腳位表與讀寫函式
#include "flowmeter.h"
#include "uart_cmd.h"
#include "tx_queue.h"
#include "diagnostics.h"
#include "tick.h"          // 〔步驟 2b〕1 ms tick

bool canPrint = true;      // 原程式恆為 true

void setup() {
    pin_init_all();   // 〔步驟 1〕最先執行,縮短開機後腳位 high-Z 的空窗(架構計畫 7.3、7.6)
    setupWatchdog();

    /* 〔步驟 1〕以下腳位初始化改由 pin_init_all() 處理:方向與初始準位相同
     * (slot / 閥 / 泵 = 輸出 LOW,流量計 = 輸入上拉),並移到 setupWatchdog() 之前。舊程式保留供對照:
    for (int i = 0; i < slotCount; i++) {
        pinMode(slotPins[i], OUTPUT);
        digitalWrite(slotPins[i], LOW);
    }

    pinMode(bevvalve, OUTPUT);
    pinMode(watervalve, OUTPUT);
    pinMode(coldvalve, OUTPUT);
    pinMode(warmvalve, OUTPUT);
    pinMode(waterpump, OUTPUT);
    digitalWrite(bevvalve, LOW);
    digitalWrite(watervalve, LOW);
    digitalWrite(coldvalve, LOW);
    digitalWrite(warmvalve, LOW);
    digitalWrite(waterpump, LOW);

    pinMode(flowmeterPin, INPUT_PULLUP);
    */
    flow_init();      // 〔步驟 3〕清除流量計數(原 PCICR / PCMSK2 兩行已移除,改由 Timer5 1 ms 取樣)
    tick_init();      // 〔步驟 2b〕Timer5 1 ms tick

    Serial.begin(9600);
    uart_cmd_init();  // 原 memset(inputString, ...)
    out.println("ready");   // 〔步驟 2c〕走 TX 佇列,loop() 的 tx_poll() 送出
}

void loop() {
    wdt_reset();

    if (!canPrint) return;

    tick_service();   // 〔步驟 2b〕補跑累積的 1 ms tick → 各模組計數、設旗標
    diag_poll();      // 原 loop() 開頭:每 30 s 記憶體檢查、每 1 s mcuAlive(看 tick 旗標)
    cmd_poll();       // 原 loop() 中段:有完整命令就處理

    if (flowPulseOccurred) {
        // Handle flow pulse if needed
        flowPulseOccurred = false;
    }

    // nonBlockingPrint(NULL);  // Continue any non-blocking print operations   〔步驟 2c〕由 tx_poll() 取代
    checkSerialBuffers();
    tx_poll();        // 〔步驟 2c〕⚠ K1 新增:TX 佇列搬到 Serial,不等待(非原 K2 架構)
}
