#ifndef K1_UART_CMD_H
#define K1_UART_CMD_H

/* ═══════════════════════════════════════════════════════════════════════
 *  uart_cmd — K2 UART 協定(K2 移植版,main-k2port-v4)
 * ═══════════════════════════════════════════════════════════════════════
 *  移植自 K2 App/Src/uart_cmd.c(v0.0.0.10);原 K1 文字 / JSON 命令見 main-v4。
 *
 *  封包:[0x40] [0x71] [0x30] [CMD] [Data...] [CS],CS 使全包加總 & 0xFF = 0
 *
 *  與 K2 的差異:
 *    ▸ K2 在 UART 中斷逐 byte 收(HAL_UART_RxCpltCallback);K1 由 Arduino Serial 中斷收進
 *      64 byte 環形緩衝,cmd_poll() 在 loop() 逐 byte 取出 → 同一套逐 byte 流程
 *    ▸ K2 處理中以 RX_OK 鎖住、丟棄新資料;K1 收滿一包就在同一次 cmd_poll() 內處理完,
 *      後續 byte 留在 Serial 緩衝,不需要鎖、也不會丟
 *    ▸ 收包逾時 UartCounter1ms 由 tick_main() 每 1 ms 遞減(K2 在 TIM1 中斷內遞減)
 *    ▸ 標頭逐 byte 檢查(K2 收滿 3 byte 才比對、不對整組丟):前面有雜訊時不會整包對不齊
 *      (使用者 2026-11-11 決定,見 uart_cmd.cpp rx_byte)
 *
 *  〔P1〕只收 9 byte 固定長度;任何 CMD 收到且 CS 正確 → 回 PING 內容(版本 4 byte)
 *        40 71 30 [CMD] V0 V1 V2 V3 CS。CS 錯誤 → [4..7] 填 0xFF(同 K2)。
 *        各命令實作後逐一換成正式處理;長度查表(cmd_len_table)於變長命令實作時加入。
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

/* 韌體版本(同 K2 排法:VERSION0 為最後一碼,回覆時在 [4])
 * v4.0 → 0.0.4.0 */
#define VERSION0   0x00
#define VERSION1   0x04
#define VERSION2   0x00
#define VERSION3   0x00

#define UART_BAUD        115200
#define UART_RX_TIMEOUT  20     // ms:一包從第 1 byte 起 20 ms 內沒收完 → 丟棄重收(同 K2)

void uart_cmd_init(void);    // setup():清空接收狀態
void uart_tick_1ms(void);    // tick_main() 每 1 ms 呼叫:收包逾時倒數
void cmd_poll(void);         // loop():讀 RX,收滿一包就處理並回覆

#endif
