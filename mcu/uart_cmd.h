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
 *  〔P2〕0x00 / 0x01 / 0x02 照搬 K2(回覆 = 原封包);0x05 PING;0xB5 狀態查詢(K1 自訂)。
 *        其他 CMD 仍為 P1 暫時行為。
 *  〔P3〕加入 K2 長度查表(查不到的 CMD 不回,同 K2);0x03 只有 ID18 Flow,其他 ID 回不支援格式;
 *        0x04 只清 ID18;0x08 / 0x09 / 0x0A / 0xB0 回不支援格式 9 byte(⚠ K1 自訂,討論稿 3.7);
 *        0x06 / 0x07 在 P4 / P5 前暫回 PING 內容。
 *  〔P4〕0x06 SET_PULSE 照搬 K2(Type 0 常溫、1 冷水;2 以上回不支援格式);到量主動送完成通知。
 *        0x07 在 P5 前仍暫回 PING 內容。
 * ═══════════════════════════════════════════════════════════════════════ */

#include <stdint.h>

/* 韌體版本(同 K2 排法:VERSION0 為最後一碼,回覆時在 [4])
 * v4.0 → 0.0.4.0 */
#define VERSION0   0x00
#define VERSION1   0x04
#define VERSION2   0x00
#define VERSION3   0x00

/* 命令碼(同 K2 Core/Inc/main.h) */
#define SWITCH_SET       0x00
#define SWITCH_START     0x01
#define SWITCH_STOP      0x02
#define SENSOR_READ      0x03
#define SENSOR_CLEAR     0x04
#define PING             0x05
#define SET_PULSE        0x06
#define LONG_PRESSURE    0x07
#define ERROR_REPORT     0x08   // K1 不支援(回 9 byte 不支援格式)
#define MAX30102_QUERY   0x09   // K1 不支援
#define WATER_MODE       0x0A   // K1 不支援
#define BOOTLOADER       0xB0   // K1 不支援

#define SENSOR_FLOW      18     // 0x03 / 0x04 的 ID:流量計(K1 唯一能讀的感測器)
#define STATUS_QUERY     0xB5   // ⚠ K1 自訂,K2 沒有(討論稿 3.6):SUB 00 = Status、01 = 實際腳位

#define UART_BAUD        115200
#define UART_RX_TIMEOUT  20     // ms:一包從第 1 byte 起 20 ms 內沒收完 → 丟棄重收(同 K2)

void uart_cmd_init(void);    // setup():清空接收狀態
void uart_tick_1ms(void);    // tick_main() 每 1 ms 呼叫:收包逾時倒數
void cmd_poll(void);         // loop():讀 RX,收滿一包就處理並回覆
void uart_send_pulse_done(uint8_t type);   // 〔P4〕0x06 完成通知(Tmr1_Interrupt_main 呼叫)

#endif
