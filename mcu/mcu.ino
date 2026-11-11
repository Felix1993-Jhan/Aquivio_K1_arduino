/* ═══════════════════════════════════════════════════════════════════════
 *  Aquivio K1 主程式 — K2 移植版(main-k2port-v4)
 * ═══════════════════════════════════════════════════════════════════════
 *  通訊改用 K2 UART 協定(40 71 30 + CS,115200),不保留原 K1 的 JSON / 文字命令。
 *  原 K1 版本(JSON + #、9600)見 main-v4;討論見 K2_aidata/K1_k2port_discussion.md。
 *
 *    pins         腳位表、暫存器讀寫
 *    flowmeter    流量計脈衝計數(1 ms 取樣 debounce,移植自 K2)
 *    uart_cmd     K2 UART 協定:收包、CS 驗證、命令分派、回覆
 *    tx_queue     TX 發送佇列(⚠ K1 新增,非原 K2 架構)
 *    tick         1 ms 計時(Timer5)與每 1 ms 的工作分派
 *    diagnostics  看門狗、D23 心跳
 *    pressure     A1 水壓 ADC(保留,可能不用)
 * ═══════════════════════════════════════════════════════════════════════ */

#include <avr/wdt.h>       // wdt_reset
#include "pins.h"
#include "flowmeter.h"
#include "uart_cmd.h"
#include "tx_queue.h"
#include "diagnostics.h"
#include "tick.h"
#include "pressure.h"

void setup() {
    pin_init_all();   // 最先執行,縮短開機後腳位 high-Z 的空窗
    setupWatchdog();
    pressure_init();
    flow_init();
    tick_init();

    Serial.begin(UART_BAUD);   // 〔K2 移植 P1〕115200(同 K2);開機不送 ready(K2 不主動送出)
    uart_cmd_init();
}

void loop() {
    wdt_reset();
    tick_service();   // 補跑累積的 1 ms tick
    cmd_poll();       // K2 封包:收滿一包就處理並回覆

    if (flowPulseOccurred) {
        flowPulseOccurred = false;   // 目前無其他用途(K2 freeflow 的 FlowChange 於 P5 處理)
    }

    tx_poll();        // ⚠ K1 新增:TX 佇列搬到 Serial,不等待(非原 K2 架構)
}
