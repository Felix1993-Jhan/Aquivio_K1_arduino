#include <Arduino.h>
#include "uart_cmd.h"
#include "tx_queue.h"      // 回覆走 out(TX 佇列)
#include "device_ctrl.h"   // 〔P2〕handleSW、Status、Counter1ms
#include "flowmeter.h"     // 〔P3〕flow_get、flow_take
#include "timer_task.h"    // 〔P4〕Pulse、PulseStart、PulseType、pulse_flow_clear
#include "freeflow.h"      // 〔P5〕channel7_CMD、freeFlow_Run、freeflow_Reset、sort_by_speed

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
/* 〔P3〕加參數 cs_idx(回覆的 CS 位置):K2 回覆長度一律 = 請求長度(ur1Rx_Lenght);
 * K1 的不支援格式(3.7)固定 9 byte,與 0x08 請求的 7 / 16 byte 不同,所以分開。K2 原寫法:
static void createCS(void)
{
    uint16_t i;
    uint8_t  cs = 0;
    for (i = 0; i < ur1Rx_Lenght; i++) cs += TxBuffer[i];
    TxBuffer[ur1Rx_Lenght] = -cs;
}
 */
static void createCS(uint16_t cs_idx)
{
    uint16_t i;
    uint8_t  cs = 0;
    for (i = 0; i < cs_idx; i++) cs += TxBuffer[i];
    TxBuffer[cs_idx] = -cs;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  cmd_id 長度計算 — 查表驅動(〔P3〕照搬 K2 cmd_len_table + lookup_cmd_len)
 *  回傳 = ur1Rx_Lenght(CS 所在 index,總長 = +1)
 * ═══════════════════════════════════════════════════════════════════════ */
typedef uint8_t (*len_calc_fn)(uint8_t sub);

// 一般命令:固定 9 bytes 含 CS
static uint8_t fixed_9(uint8_t sub) { (void)sub; return 8; }

// LONG_PRESSURE (0x07):sub 是裝置數量,每裝置 3 bytes(id + val_lo + val_hi)
/* 〔P5,K1 修正〕K2 回傳 uint8_t 的 6 + 3N,N ≥ 84 時溢位繞回小數字(例:N = 86 → 8,被當成 9 byte 收下,
 * 之後 LONG_PRESSURE_CMD 迴圈寫 86 組,超出 channel7_CMD)。N 超過 FREEFLOW_MAX 回 0xFF(≥ DATA_SIZE)→ 收包端丟棄。
 * K2 原寫法:
static uint8_t long_pressure_len(uint8_t sub) { return 6 + (sub * 3); }
 */
static uint8_t long_pressure_len(uint8_t sub) { return (sub > FREEFLOW_MAX) ? 0xFF : 6 + (sub * 3); }

// ERROR_REPORT (0x08):只有 SET (sub=0x01) 是 16 bytes,其他 sub 都 7 bytes
static uint8_t error_report_len(uint8_t sub) { return (sub == 0x01) ? 15 : 6; }

typedef struct {
    uint8_t     cmd_id;
    len_calc_fn calc;
} cmd_len_entry_t;

static const cmd_len_entry_t cmd_len_table[] = {
    { SWITCH_SET,     fixed_9            },   // 0x00
    { SWITCH_START,   fixed_9            },   // 0x01
    { SWITCH_STOP,    fixed_9            },   // 0x02
    { SENSOR_READ,    fixed_9            },   // 0x03
    { SENSOR_CLEAR,   fixed_9            },   // 0x04
    { PING,           fixed_9            },   // 0x05
    { SET_PULSE,      fixed_9            },   // 0x06
    { LONG_PRESSURE,  long_pressure_len  },   // 0x07 依裝置數變長
    { ERROR_REPORT,   error_report_len   },   // 0x08 依 sub-command 分兩種(K1 不支援,仍需收完整包)
    { MAX30102_QUERY, fixed_9            },   // 0x09
    { WATER_MODE,     fixed_9            },   // 0x0A
    { BOOTLOADER,     fixed_9            },   // 0xB0
    { STATUS_QUERY,   fixed_9            },   // 0xB5 ⚠ K1 自訂
};
#define CMD_LEN_COUNT (sizeof(cmd_len_table) / sizeof(cmd_len_table[0]))

// 查表:cmd_id 對到就填 out_len 回傳 1;找不到(未知命令)回 0
static int lookup_cmd_len(uint8_t cmd_id, uint8_t sub, uint16_t *out_len)
{
    for (uint8_t i = 0; i < CMD_LEN_COUNT; i++) {
        if (cmd_len_table[i].cmd_id == cmd_id) {
            *out_len = cmd_len_table[i].calc(sub);
            return 1;
        }
    }
    return 0;
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
    /* 〔P1〕原為固定 9 byte(ur1Rx_Lenght = 8);〔P3〕改為 K2 的查表決定長度 */
    if (RxIndex == 5) {
        if (!lookup_cmd_len(RxBuffer[3], RxBuffer[4], &ur1Rx_Lenght)
            || ur1Rx_Lenght >= DATA_SIZE) {                // 〔K1 加〕超過緩衝(0x07 N > FREEFLOW_MAX)也丟棄
            // 未知 cmd_id → 重新讀取(同 K2:不回覆)
            RxIndex = 0;
            ur1Rx_Lenght = 8;
            UartCounter1ms = UART_RX_TIMEOUT;
        }
    }
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
    if (freeFlow_Run) freeflow_Reset(); // 〔P5〕放開按鍵時,判斷是否有運行過長壓,有則清空(同 K2,不看遮罩)
    handleSW(2, rx_u32());              // 不移植 PressPump_OnHostStop
}

static void PING_CMD(void)
{
    TxBuffer[4] = VERSION0;
    TxBuffer[5] = VERSION1;
    TxBuffer[6] = VERSION2;
    TxBuffer[7] = VERSION3;
}

/* K2 SENSOR_READ_CMD:[4] = ID echo,[5..7] = 讀值 24-bit LE。
 * K1 只有 ID18 Flow;其他 ID(K2 的裝置電流、壓力、溫度、漏水、0xF0 板版、未定義)回不支援格式(討論稿 3.7) */
static void SENSOR_READ_CMD(void)
{
    uint8_t id = RxBuffer[4];
    TxBuffer[4] = id;
    if (id == SENSOR_FLOW) {
        uint32_t v = flow_get();            // K1 流量在 Timer5 ISR 計數,flow_get 為原子讀取
        TxBuffer[5] = (uint8_t)v;
        TxBuffer[6] = (uint8_t)(v >> 8);
        TxBuffer[7] = (uint8_t)(v >> 16);   // 只回低 24 bit(同 K2)
    } else {
        TxBuffer[5] = 0xFF;
        TxBuffer[6] = id;
        TxBuffer[7] = 0xFF;
    }
}

/* K2 SENSOR_CLEAR_CMD:只有 ID18 Flow 歸零,其他 ID 不動作;回覆 = 原封包(K2 也只清 Flow)
 * 不移植 ErrorReport_ResetFlowSnapshot(異常偵測) */
static void SENSOR_CLEAR_CMD(void)
{
    TxBuffer[4] = RxBuffer[4];
    /* 〔P4〕改呼叫 pulse_flow_clear():歸零時同步 0x06 扣脈衝用的「上次」計數。P3 原寫法:
    if (RxBuffer[4] == SENSOR_FLOW) (void)flow_take();   // 原子讀取並歸零
     */
    if (RxBuffer[4] == SENSOR_FLOW) pulse_flow_clear();
}

/* K2 pulse_start_by_type():由 Type 決定該啟動哪些裝置
 *   0 = 常溫水(水泵 ID10 + 常溫繼電器 ID14)、1 = 冷水(ID10 + 冷水繼電器 ID15)
 *   K2 的 2 = 氣泡水(ID16)K1 沒有,由呼叫端回不支援格式 */
static uint32_t pulse_start_by_type(uint8_t type)
{
    switch (type) {
        case 0:  return DEV_BIT(10) | DEV_BIT(14);   // 常溫水
        case 1:  return DEV_BIT(10) | DEV_BIT(15);   // 冷水
        default: return 0;
    }
}

/* ⚠ K1 自訂(討論稿 3.7):K1 沒有的功能,回 9 byte:[4] = 代碼,[5..7] = FF 代碼 FF
 * 〔P5〕移到 SET_PULSE_CMD 前面,0x06 / 0x07 的不支援 Type 共用 */
static void UNSUPPORTED_CMD(uint8_t code)
{
    TxBuffer[4] = code;
    TxBuffer[5] = 0xFF;
    TxBuffer[6] = code;
    TxBuffer[7] = 0xFF;
}

/* K2 SET_PULSE_CMD:依流量脈衝數出水。[4] = Type、[5..7] = 脈衝數 24-bit LE;回覆 = 原封包,
 * 脈衝扣到 0 時由 Tmr1_Interrupt_main 關閉並主動送完成通知。
 * ⚠ K1:Type 2(氣泡水)及其他未定義 Type → 不支援格式 [Type] FF [Type] FF,不動作(討論稿 3.7)
 * 不移植 PressPump_OnHostStart(水桶補壓) */
static void SET_PULSE_CMD(void)
{
    uint8_t type = RxBuffer[4];
    if (type > 1) {
        UNSUPPORTED_CMD(type);          // 〔P5〕原為逐一填 [4..7],內容相同,改共用
        return;
    }
    Pulse = ((uint32_t)RxBuffer[7] << 16) | ((uint32_t)RxBuffer[6] << 8) | RxBuffer[5];
    PulseType = type;
    PulseStart = pulse_start_by_type(type);
    handleSW(1, PulseStart);            // 做 Start 的動作
}

/* K2 LONG_PRESSURE_CMD(〔P5〕):freeflow,slot 依流量開啟。
 *   封包:[4] = N(組數)、每組 3 byte [ID][時間 L][時間 H](每 pulse 該 slot 開幾 ms)、CS 前一 byte = Type
 *   N > 0:記錄各 slot,ID 11~13(UVC)直接開;N = 0:停料、只出水(slot + 水泵 + 閥全關,再依 Type 開水泵 + 閥)
 *   最後依 Type 開水泵 + 閥;回覆 = 原封包(7 + 3N byte)
 * ⚠ K1:Type 2(氣泡水,K2 走模式 2)及其他未定義 Type → 不支援格式 9 byte,不動作;回傳 0 讓呼叫端改 9 byte
 * 不移植 PressPump_OnHostStart(水桶補壓)、ShortPulse_Register(氣泡水短脈衝) */
static int LONG_PRESSURE_CMD(void)
{
    uint8_t type = RxBuffer[ur1Rx_Lenght - 1];      // Type 永遠在 CS 前一 byte
    if (type > 1) {
        UNSUPPORTED_CMD(type);
        return 0;
    }
    PulseStart   = pulse_start_by_type(type);
    freeFlow_Run = 1;                               // K2:(type == 2) ? 2 : 1;K1 沒有氣泡水
    if (RxBuffer[4]) {
        temp_main_freeflow = 0;
        pump0_9count = 0;                           // 確認使用了多少 slot
        for (uint8_t i = 0; i < RxBuffer[4]; i++) {
            // 將傳遞下來的資料記錄下來,並將之前的累加量清空
            uint8_t base = 5 + (i * 3);             // 3 筆資料一組
            uint8_t id   = RxBuffer[base];
            uint16_t val = (uint16_t)RxBuffer[base + 1] | ((uint16_t)RxBuffer[base + 2] << 8);
            channel7_CMD[i].acc = 0;
            channel7_CMD[i].id  = id;
            if (id > 10 && id < 14) temp_main_freeflow |= DEV_BIT(id);    // UVC 範圍直接啟動 UVC
            channel7_CMD[i].pulse_add_time = (val > ADD_MAX) ? ADD_MAX : val;   // 防止軟體給到太大的數值
            if (id < 10) pump0_9count++;            // 計算有幾個 slot
        }
        sort_by_speed(channel7_CMD, RxBuffer[4]);   // 小的 ID 在前面
        freeflow_stagger_start();                   // 〔K1 新增〕slot 的 acc 起點錯開 17 / 50 / 83(見 freeflow.cpp)
    } else {
        // 數值設定為 00 則表示要做 delay 關水:清空 slot 資料
        for (uint8_t i = 0; i < 10; i++) {
            channel7_CMD[i].acc = 0;
            channel7_CMD[i].mSec_100count = 0;
            channel7_CMD[i].pulse_add_time = 0;
        }
        handleSW(2, 0x1C7FFUL);                     // 關閉前面的 10 個 slot 與 3 個 relay + 水泵
    }
    handleSW(1, temp_main_freeflow);                // 開啟 UVC(N = 0 時為上一包的,同 K2)
    handleSW(1, PulseStart);                        // 做 Start 的動作,直接開啟出水
    return 1;
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
    uint16_t tx_cs = ur1Rx_Lenght;          // 〔P3〕回覆的 CS 位置:預設 = 請求長度(同 K2);9 byte 回覆設為 8
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
            case SWITCH_SET:     SWITCH_SET_CMD();   break;
            case SWITCH_START:   SWITCH_START_CMD(); break;
            case SWITCH_STOP:    SWITCH_STOP_CMD();  break;
            case SENSOR_READ:    SENSOR_READ_CMD();  break;
            case SENSOR_CLEAR:   SENSOR_CLEAR_CMD(); break;
            case PING:           PING_CMD();         break;
            /* 〔P3〕原為 case SET_PULSE: 與 LONG_PRESSURE 同樣暫回 PING 內容;〔P4〕實作 */
            case SET_PULSE:      SET_PULSE_CMD();    break;
            /* 〔P1 暫時〕P5 實作前回 PING 內容(9 byte):
            case LONG_PRESSURE:  PING_CMD(); tx_cs = 8; break;
             */
            case LONG_PRESSURE:  if (!LONG_PRESSURE_CMD()) tx_cs = 8; break;   // 〔P5〕不支援的 Type 回 9 byte
            case ERROR_REPORT:                                       // ⚠ K1 不支援(討論稿 3.7):回 9 byte
            case MAX30102_QUERY:
            case WATER_MODE:
            case BOOTLOADER:     UNSUPPORTED_CMD(RxBuffer[3]); tx_cs = 8; break;
            case STATUS_QUERY:   STATUS_QUERY_CMD(); break;          // ⚠ K1 自訂
            /* 〔P2〕原 default: PING_CMD();(未實作的 CMD 回 PING 內容)。〔P3〕查不到長度表的 CMD
             * 在 rx_byte 就丟棄(同 K2),不會進到這裡 */
        }
    } else {
        for (i_ur1 = 4; i_ur1 < ur1Rx_Lenght; i_ur1++) TxBuffer[i_ur1] = 0xff;
    }
    createCS(tx_cs);
    out.write(TxBuffer, tx_cs + 1);
    RxIndex = 0;
}

/* 〔P4〕0x06 完成通知(MCU 主動送):40 71 30 06 [Type] 01 00 00 CS(同 K2 Tmr1_Interrupt_main)
 * ⚠ K1 修正:K2 用 createCS() 以「上一包請求的 ur1Rx_Lenght」算 CS 位置,上一包若是 0x07 / 0x08(非 9 byte)
 *   CS 會算錯;K1 用獨立緩衝、固定 9 byte 計算。不動 TxBuffer,避免影響命令處理 */
void uart_send_pulse_done(uint8_t type)
{
    uint8_t b[9] = { 0x40, 0x71, 0x30, SET_PULSE, type, 0x01, 0x00, 0x00, 0 };
    uint8_t cs = 0;
    for (uint8_t i = 0; i < 8; i++) cs += b[i];
    b[8] = -cs;
    out.write(b, 9);
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
