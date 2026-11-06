#ifndef K1_UART_CMD_H
#define K1_UART_CMD_H

/* ═══════════════════════════════════════════════════════════════════════
 *  uart_cmd — 字串分析 + 命令下達
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由 mcu.ino 原樣搬來,行為不變。
 *
 *  區塊 1  字串分析:serialEvent()(Arduino 核心在每次 loop() 之後呼叫)
 *          收 RX 字元進 inputString,遇到 '#' 設 stringComplete
 *  區塊 2  命令下達:cmd_poll()(原 loop() 中段)
 *          ping / startcoldwater / startwarmwater / stop / startslotN / stopslotN,
 *          其他字串一律交給 execute_actions() 當 JSON
 *
 *  步驟 4 會改為逐 byte 解析(架構計畫 5.4),修正滿 199 byte 卡死與黏包。
 * ═══════════════════════════════════════════════════════════════════════ */

void uart_cmd_init(void);   // setup():清空接收緩衝
void cmd_poll(void);        // loop():有完整命令就處理

#endif
