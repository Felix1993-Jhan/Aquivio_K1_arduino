#include <Arduino.h>
#include <avr/wdt.h>
#include "diagnostics.h"
#include "MemoryFree.h"

#include "tick.h"          // K1_DEBUG_TICK、tick_backlog_max
#include "pins.h"          // 〔2026-11-09〕pin_toggle(ID_HEARTBEAT)
#include "tx_queue.h"      // 〔步驟 2c〕輸出改走 out(TX 佇列),格式不變

/* 〔步驟 2b〕週期改由 1 ms tick 計數(diag_tick_1ms),不再用 millis() 比較。
 * 週期不變(記憶體 30 s、mcuAlive 1 s)。舊變數保留供對照:
// Memory variables
unsigned long lastMemoryPrintTime = 0;
const unsigned long memoryPrintInterval = 30 * 1000; // Print every 30 seconds
unsigned long lastmcuAliveTime = 0;
const unsigned long mcuAliveInterval = 1 * 1000; // Print every 1000 ms (1 second)
*/
#define MEMORY_CHECK_MS   30000u   // 每 30 秒檢查記憶體(uint16 上限 65535)
#define MCU_ALIVE_MS       1000u   // 每 1 秒送 mcuAlive
#define HEARTBEAT_MS       1000u   // 〔2026-11-09〕D23 心跳:每 1 秒翻轉一次(0.5 Hz)

static uint16_t memory_ms = 0, alive_ms = 0;          // 只在 tick_main(主迴圈)內修改
static uint16_t heartbeat_ms = 0;
static uint8_t  memory_due = 0, alive_due = 0;        // 週期到 → 設旗標,diag_poll 輸出後清除


void setupWatchdog() {
    cli();  // Disable interrupts
    wdt_reset();  // Reset Watchdog Timer
    MCUSR &= ~(1<<WDRF);  // Clear WDRF in MCUSR
    WDTCSR |= (1<<WDCE) | (1<<WDE);  // Set WDCE (Watchdog Change Enable)
    WDTCSR = (1<<WDE) | (1<<WDP3) | (1<<WDP0);  // Set WDE, and 8 seconds delay
    sei();  // Enable interrupts
}

void checkMemory() {
    static int lowestMemory = freeMemory();
    int currentMemory = freeMemory();

    if (currentMemory < lowestMemory) {
        lowestMemory = currentMemory;
        out.print("New lowest memory: ");
        out.print(lowestMemory);
        out.println(" bytes");
    }

    if (currentMemory < 100) {  // Arbitrary low memory threshold
        out.println("WARNING: Very low memory!");
    }
}

void checkSerialBuffers() {
    /* 〔步驟 5〕移除 RX 清空(架構計畫 9.1 #19):出杯不再阻塞,cmd_poll() 每圈都把 RX 讀出,
     * 不會累積到 60 byte;繼續清空反而會把出杯中送來的命令清掉。連帶不再印 WARNING。舊程式保留供對照:
    // Check RX buffer
    if (Serial.available() > 60) {  // Assuming a 64-byte RX buffer
        out.println("WARNING: RX buffer near capacity");
        while (Serial.available() > 0) {
            Serial.read();  // Clear the buffer
        }
    }
    */

    /* 〔步驟 2c〕移除 TX flush:Serial.flush() 會阻塞到 TX 全部送完(64 byte ≈ 67 ms),
     * 輸出改走 TX 佇列(out / tx_poll)後不需要。舊程式保留供對照:
    // Check TX buffer
    if (Serial.availableForWrite() < 32) {  // Less than half of TX buffer available
        // Serial.println("WARNING: TX buffer getting full");
        Serial.flush();  // Wait for all data to be transmitted
    }
    */
}

/* 〔步驟 2b〕每 1 ms 由 tick_main() 呼叫:只計數、設旗標,不輸出
 * (補跑多個 tick 時旗標只會設一次 → 不會連印多行,同舊版 millis() 的效果) */
void diag_tick_1ms(void) {
    if (++memory_ms >= MEMORY_CHECK_MS) { memory_ms = 0; memory_due = 1; }
    if (++alive_ms  >= MCU_ALIVE_MS)    { alive_ms  = 0; alive_due  = 1; }
    // 〔2026-11-09〕D23 心跳:待機、出杯中都翻轉(與 mcuAlive 只在待機送不同)。
    // 在主迴圈(tick_main)翻轉而非 ISR → 主迴圈卡住時心跳停止;補跑 tick 時仍準時。
    // pin_toggle = 寫 PINx 單次寫入,不是讀改寫,不會和 ISR 寫 Port A(除錯版 D22)衝突
    if (++heartbeat_ms >= HEARTBEAT_MS) { heartbeat_ms = 0; pin_toggle(ID_HEARTBEAT); }
}

/* 〔2026-11-11 新增,暫時〕D66(bevvalve,轉接板上沒接負載,只看 LED)與流量計 A8 同步:A8 高 → D66 高。
 * 跟原始腳位(未 debounce)。在主迴圈每 1 ms 做:D66 與水泵 / 閥同在 Port K,pin_write 為讀改寫,
 * 所有 Port K 寫入都在主迴圈才不會互相蓋掉(不放 Timer5 ISR)。
 * 要停用:把 tick_main 裡的 diag_flow_mirror_1ms() 呼叫註解掉
 * 〔main-v4,2026-11-12〕由 K2 移植版搬來(使用者要在 JSON 版也看流量計 LED),內容相同 */
void diag_flow_mirror_1ms(void) {
    pin_write(ID_BEVVALVE, FLOW_PIN_READ() != 0);
}

/* 原 loop() 開頭的兩段(順序不變:先記憶體、再 mcuAlive)
 * 只在 loop() 呼叫 → 出杯中 execute_actions 阻塞時不輸出;旗標保留到 end 之後才印一次,同 3fe056c */
void diag_poll(void) {
    /* 〔步驟 2b〕改為看 tick 設的旗標。舊程式保留供對照:
    unsigned long currentMillis = millis();
    if (currentMillis - lastMemoryPrintTime >= memoryPrintInterval) {
        checkMemory();
        lastMemoryPrintTime = currentMillis;
    }

    if (currentMillis - lastmcuAliveTime >= mcuAliveInterval) {
        Serial.print("mcuAlive \n");
        lastmcuAliveTime = currentMillis;
    }
    */
    if (memory_due) {
        memory_due = 0;
        checkMemory();
    }

    if (alive_due) {
        alive_due = 0;
        out.print("mcuAlive \n");
#ifdef K1_DEBUG_TICK
        out.print("tick backlog max: ");   // 除錯版才有,正式版不輸出
        out.println(tick_backlog_max());
#endif
    }
}
