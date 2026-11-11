#include <Arduino.h>
#include "uart_cmd.h"
#include "tx_queue.h"      // 回覆走 out(TX 佇列)
#include "device_ctrl.h"   // 〔P2〕handleSW、Status、Counter1ms

/* 緩衝大小:K2 為 256(DATA_SIZE)。K1 SRAM 只有 8 KB,改 64:
 * 最長的 0x07 LONG_PRESSURE = 7 + 3N byte,N ≤ 19 時 ≤ 64 */
#define DATA_SIZE  64

static uint8_t  RxBuffer[DATA_SIZE];
static uint8_t  TxBuffer[DATA_SIZE];
static uint16_t RxIndex = 0;
static uint16_t ur1Rx_Lenght = 8;          // CS 所在 index(總長 = ur1Rx_Lenght + 1),沿用 K2 名稱與慣例
static uint8_t  UartCounter1ms = 0;        // 只在主迴圈存取(tick_main / cmd_poll)→ 不需原子保護

/* ═══════════════════════════════════════════════════════════════════════
 *  底層工具 — Checksum 驗證 / 產生(照搬 K2 checkCommand / createCS)
 * ═══════════════════════════════════════════════════════════════════════ */

// 前三個為確認功能站號之類的碼,再將最後面的 CS 加進來,確認傳送的碼是否正常
static int checkCommand(void)
{
    int i, cs;
    if ((RxBuffer[0] == 0x40) && (RxBuffer[1] == 0x71) && (RxBuffer[2] == 0x30)) {
        cs = 0;
        for (i = 0; i < (ur1Rx_Lenght + 1); i++) cs += RxBuffer[i];
        return (((cs & 0xff) == 0) ? 1 : 0);
    }
    return 0;
}

// 最後的 CS 是全部加起來後需要等於 0 的一個數
static void createCS(void)
{
    uint16_t i;
    uint8_t  cs = 0;
    for (i = 0; i < ur1Rx_Lenght; i++) cs += TxBuffer[i];
    TxBuffer[ur1Rx_Lenght] = -cs;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  逐 byte 拼組封包(K2 HAL_UART_RxCpltCallback 的內容,去掉 RX_OK 與 HAL 呼叫)
 * ═══════════════════════════════════════════════════════════════════════ */
static const uint8_t HEADER[3] = { 0x40, 0x71, 0x30 };

static void rx_byte(uint8_t b)
{
    if (UartCounter1ms == 0) RxIndex = 0;                 // 上一包逾時未收完 → 從頭

    /* 〔K1 改,與 K2 不同〕標頭改為逐 byte 檢查(使用者 2026-11-11 決定):
     * K2 收滿 3 byte 才一次比對,不對就 3 byte 整組丟 → 前面多 1~2 byte 雜訊時,
     * 真正的 40 71 也被一起丟掉,整包對不齊(要等 20 ms 逾時後的下一包才恢復)。
     * 改為每收 1 byte 就比對該位置;不對就丟掉,但若這個 byte 是 0x40,當成新封包第 1 byte。
     * 標頭之後的雜訊仍由 CS 擋下。K2 原寫法保留於下方註解。 */
    if (RxIndex < 3 && b != HEADER[RxIndex]) {
        if (b == HEADER[0]) {                              // 例:40 40 71 30 …、40 71 40 71 30 …
            RxBuffer[0] = b;
            RxIndex = 1;
            UartCounter1ms = UART_RX_TIMEOUT;
        } else {
            RxIndex = 0;
        }
        return;
    }

    if (RxIndex == 0) UartCounter1ms = UART_RX_TIMEOUT;    // 第 1 byte:重設逾時
    if (RxIndex >= DATA_SIZE) {                            // 防止溢位
        RxIndex = 0;
        UartCounter1ms = UART_RX_TIMEOUT;
    }
    RxBuffer[RxIndex++] = b;

    /* K2 原寫法(收滿 3 byte 才比對,不對整組丟):
    if (RxIndex == 3) {
        // 讀完三個 byte 判斷是不是開頭確認碼,不是的話從頭開始讀
        if ((RxBuffer[0] != 0x40) || (RxBuffer[1] != 0x71) || (RxBuffer[2] != 0x30)) {
            RxIndex = 0;
            UartCounter1ms = UART_RX_TIMEOUT;
        }
    }
    */
    /* 〔P1〕固定 9 byte(ur1Rx_Lenght = 8)。K2 在 RxIndex == 5 時查 cmd_len_table 決定長度,
     * 查不到的 CMD 丟棄重收;變長命令(0x07、0x08)實作時再移植 */
}

/* ═══════════════════════════════════════════════════════════════════════
 *  CMD Handlers(K2 區塊 4)。0x00~0x02 的回覆 = 原封包(handler 不改 TxBuffer,同 K2)
 * ═══════════════════════════════════════════════════════════════════════ */

// [4..7] → 32-bit(LE)。⚠ 先轉 uint32_t 再移位:AVR 的 int 只有 16-bit,K2 原式直接 << 24 會溢位
static uint32_t rx_u32(void)
{
    return ((uint32_t)RxBuffer[7] << 24) | ((uint32_t)RxBuffer[6] << 16) |
           ((uint32_t)RxBuffer[5] << 8)  |  (uint32_t)RxBuffer[4];
}

static void tx_u32(uint32_t v)
{
    TxBuffer[4] = (uint8_t)v;
    TxBuffer[5] = (uint8_t)(v >> 8);
    TxBuffer[6] = (uint8_t)(v >> 16);
    TxBuffer[7] = (uint8_t)(v >> 24);
}

static void SWITCH_SET_CMD(void)
{
    if (isDeviceID(RxBuffer[4])) {      // 先判別是不是輸出裝置(K1 沒有實體的也算,Status 照翻)
        handleSW(0, RxBuffer[4]);
        // 資料 7 6 5 組成 24-bit 時間(ms);0 = 不自動關(同 K2)
        Counter1ms[RxBuffer[4]] = ((uint32_t)RxBuffer[7] << 16) | ((uint32_t)RxBuffer[6] << 8) | RxBuffer[5];
        /* 不移植:PressPump_OnHostStart(0x0A 水桶補壓)、ShortPulse_Register(0x08 異常偵測) */
    }
}

static void SWITCH_START_CMD(void)
{
    handleSW(1, rx_u32());              // 4-byte 遮罩;不移植 PressPump_OnHostStart
}

static void SWITCH_STOP_CMD(void)
{
    /* K2:if(freeFlow_Run) freeflow_Reset();  → P5 freeflow 加入時補上 */
    handleSW(2, rx_u32());              // 不移植 PressPump_OnHostStop
}

static void PING_CMD(void)
{
    TxBuffer[4] = VERSION0;
    TxBuffer[5] = VERSION1;
    TxBuffer[6] = VERSION2;
    TxBuffer[7] = VERSION3;
}

/* ⚠ K1 自訂(K2 沒有,討論稿 3.6):SUB 00 = Status、01 = 實際腳位,皆一次回全部 ID;其他 SUB 填 FF */
static void STATUS_QUERY_CMD(void)
{
    if      (RxBuffer[4] == 0x00) tx_u32(Status);
    else if (RxBuffer[4] == 0x01) tx_u32(device_pin_readback());
    else                          tx_u32(0xFFFFFFFFUL);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  主分派(K2 Ur1_Rx_main):收滿一包 → 驗證 CS → 依 CMD 處理 → 產生 CS → 回覆
 * ═══════════════════════════════════════════════════════════════════════ */
static void Ur1_Rx_main(void)
{
    int i_ur1;
    for (i_ur1 = 0; i_ur1 < ur1Rx_Lenght; i_ur1++) TxBuffer[i_ur1] = RxBuffer[i_ur1];

    if (checkCommand()) {
        /* 〔P1〕原為任何 CMD 都回 PING 內容;〔P2〕改為依 CMD 分派(同 K2),尚未實作的 CMD 維持 P1 行為。
         * P1 寫法保留供對照:
        TxBuffer[4] = VERSION0;
        TxBuffer[5] = VERSION1;
        TxBuffer[6] = VERSION2;
        TxBuffer[7] = VERSION3;
         */
        switch (RxBuffer[3]) {
            case SWITCH_SET:   SWITCH_SET_CMD();   break;
            case SWITCH_START: SWITCH_START_CMD(); break;
            case SWITCH_STOP:  SWITCH_STOP_CMD();  break;
            case PING:         PING_CMD();         break;
            case STATUS_QUERY: STATUS_QUERY_CMD(); break;   // ⚠ K1 自訂
            default:           PING_CMD();         break;   // 〔P1 暫時〕未實作的 CMD 回 PING 內容
        }
    } else {
        for (i_ur1 = 4; i_ur1 < ur1Rx_Lenght; i_ur1++) TxBuffer[i_ur1] = 0xff;
    }
    createCS();
    out.write(TxBuffer, ur1Rx_Lenght + 1);
    RxIndex = 0;
}

void uart_cmd_init(void)
{
    RxIndex = 0;
    ur1Rx_Lenght = 8;
    UartCounter1ms = 0;
}

void uart_tick_1ms(void)
{
    if (UartCounter1ms != 0) UartCounter1ms--;
}

void cmd_poll(void)
{
    while (Serial.available()) {
        rx_byte((uint8_t)Serial.read());
        if (RxIndex > ur1Rx_Lenght) Ur1_Rx_main();         // 收滿一包:當場處理,下一包的 byte 留在緩衝
    }
}
