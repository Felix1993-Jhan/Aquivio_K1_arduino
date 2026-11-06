#include <Arduino.h>
#include "tx_queue.h"

/* ⚠ K1 新增,非原 K2 架構:TX 發送佇列(說明見 tx_queue.h 檔頭) */

static uint8_t  q[TX_QUEUE_SIZE];
static uint16_t q_head = 0;    // 下一個寫入位置(只有 write 改)
static uint16_t q_tail = 0;    // 下一個送出位置(只有 tx_poll 改);head == tail → 空

TxQueue out;

void tx_poll(void)
{
    while (q_head != q_tail && Serial.availableForWrite() > 0) {   // 有資料、Serial 有空位才送,不等待
        Serial.write(q[q_tail]);
        q_tail = (q_tail + 1) % TX_QUEUE_SIZE;
    }
}

size_t TxQueue::write(uint8_t c)
{
    /* 〔步驟 2c 修正〕佇列空、Serial 硬體緩衝有空位 → 直接送,不排隊(時序同舊版 Serial.print)。
     * 必須「佇列空」才能直送,否則會插隊到佇列裡的舊資料前面,順序就亂了 */
    if (q_head == q_tail && Serial.availableForWrite() > 0) {
        Serial.write(c);
        return 1;
    }

    uint16_t next = (q_head + 1) % TX_QUEUE_SIZE;
    while (next == q_tail) {      // 佇列滿:退回等待(同舊版 Serial.print 空等),不丟字串
        tx_poll();
    }
    q[q_head] = c;
    q_head = next;
    return 1;
}

void print_liters_ul(uint32_t ul, uint8_t digits)
{
    uint32_t div = 1;                                   // 最小位對應的微升數:3 位 → 1000 µL
    for (uint8_t i = digits; i < 6; i++) div *= 10;
    uint32_t scale = 1;                                 // 10^digits
    for (uint8_t i = 0; i < digits; i++) scale *= 10;

    uint32_t units = (ul + div / 2) / div;              // 標準四捨五入到最小位
    uint32_t frac  = units % scale;

    out.print(units / scale);                           // 整數部分(公升)
    out.print('.');
    for (uint32_t s = scale / 10; s > 1 && frac < s; s /= 10) out.print('0');   // 補前導 0
    out.print(frac);
}

/* 〔步驟 2c〕舊的 nonBlockingPrint 由上方 TX 佇列取代:
 * 原本 TX 緩衝滿時,新訊息會蓋掉未送完的舊訊息(Progress 可能被截斷);
 * 改為 out.print 放進佇列,不截斷、順序不變。舊程式保留供對照:
void nonBlockingPrint(const char* message) {
    static char printBuffer[64];  // Adjust size as needed
    static int printIndex = 0;

    if (message != NULL) {
        // New message to print
        strncpy(printBuffer, message, sizeof(printBuffer) - 1);
        printBuffer[sizeof(printBuffer) - 1] = '\0';  // Ensure null-termination
        printIndex = 0;
    }

    while (printIndex < strlen(printBuffer) && Serial.availableForWrite() > 0) {
        Serial.write(printBuffer[printIndex++]);
    }

    if (printIndex >= strlen(printBuffer)) {
        printBuffer[0] = '\0';  // Clear buffer after fully printed
        printIndex = 0;
    }
}
*/
