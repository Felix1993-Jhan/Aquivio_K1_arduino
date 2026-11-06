#ifndef K1_UART_CMD_H
#define K1_UART_CMD_H

/* ═══════════════════════════════════════════════════════════════════════
 *  uart_cmd — 字串分析 + 命令下達
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由 mcu.ino 搬來;〔步驟 4a〕改為逐 byte 解析(架構計畫 5.4)。
 *
 *  區塊 1  字串分析:cmd_poll()(loop() 呼叫)逐 byte 讀 RX,
 *          讀到 '#' 就處理一筆並清空;超過 255 字元丟棄到下一個 '#'(上限 2026-11-06 由 199 加大)
 *          → 修正舊版滿 199 byte 卡死、多筆命令黏在一起解析失敗
 *  區塊 2  命令下達:process_command()(判斷內容與舊版相同)
 *          ping / startcoldwater / startwarmwater / stop / startslotN / stopslotN,
 *          其他字串一律交給 execute_actions() 當 JSON
 *  舊的 serialEvent() 已移除(註解保留在 .cpp 檔尾)。
 * ═══════════════════════════════════════════════════════════════════════ */

void uart_cmd_init(void);   // setup():清空接收緩衝
void cmd_poll(void);        // loop():讀 RX,每讀到 '#' 處理一筆

#endif
