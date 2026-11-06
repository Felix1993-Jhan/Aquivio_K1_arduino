#ifndef K1_TX_QUEUE_H
#define K1_TX_QUEUE_H

/* ═══════════════════════════════════════════════════════════════════════
 *  tx_queue — 字串回覆(TX 發送佇列)
 *  ⚠ K1 新增,非原 K2 架構(K2 沒有對應機制)。使用者 2026-11-05 同意,要求標明。
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2c〕架構計畫 6.1
 *
 *  為什麼要加:
 *    Arduino 的 Serial TX 已是中斷驅動(64 byte 環形緩衝,UDRE 中斷送出),
 *    但緩衝滿時 Serial.print 會在 write() 內空等(9600 baud 約每 byte 1 ms),
 *    主迴圈卡住 → slot 晚關。改成先放進自己的佇列、立刻返回,由 tx_poll() 慢慢搬。
 *
 *  ▸ out        取代 Serial 的輸出物件:out.print(...) / out.println(...),
 *               繼承 Print → 格式與 Serial.print 完全相同(print(x, 3) 也一樣)
 *  ▸ tx_poll()  loop() 每圈呼叫:趁 Serial.availableForWrite() 有空位搬資料,不等待
 *  ▸ 直送       佇列空、Serial 有空位時直接送,不排隊 → 平時時序同舊版
 *  ▸ 佇列滿     退回等待(同舊版 Serial.print 空等),不丟字串 → 輸出照舊
 *
 *  使用限制:
 *    - 所有輸出都走 out,混用 Serial.print 順序會亂(收資料的 Serial.read 等不受影響)
 *    - 只在主迴圈用,ISR 不輸出 → 佇列索引不需原子保護
 *    - watchdog 重置時,未送出的字串最多遺失約 TX_QUEUE_SIZE byte
 * ═══════════════════════════════════════════════════════════════════════ */

#include <Print.h>

#define TX_QUEUE_SIZE  512     // 實際最多存 511 byte(留一格分辨空 / 滿)

class TxQueue : public Print {
public:
    size_t write(uint8_t c) override;   // 放進佇列立刻返回;滿了才等待
    using Print::write;                 // 保留 write(const char*) 等多載
};

extern TxQueue out;

void tx_poll(void);

/* 〔步驟 2c〕舊的 nonBlockingPrint 由 out 取代(見 tx_queue.cpp 註解)
void nonBlockingPrint(const char* message);   // 有空位才送;新訊息會蓋掉未送完的舊訊息
*/

#endif
