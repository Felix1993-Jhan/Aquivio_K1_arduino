#ifndef K1_DIAGNOSTICS_H
#define K1_DIAGNOSTICS_H

/* ═══════════════════════════════════════════════════════════════════════
 *  diagnostics — 系統監控:看門狗、記憶體、mcuAlive、Serial 緩衝檢查
 * ═══════════════════════════════════════════════════════════════════════
 *  〔步驟 2a〕由 mcu.ino 原樣搬來,行為不變。
 *
 *  ▸ diag_tick_1ms()  〔步驟 2b〕每 1 ms 由 tick_main() 呼叫:計數到 30 s / 1 s 設旗標
 *  ▸ diag_poll()      原 loop() 開頭兩段:旗標到 → checkMemory / 送 "mcuAlive \n"
 *                     (只在 loop() 呼叫 → 出杯中 execute_actions 阻塞時不送,同 3fe056c)
 *  checkSerialBuffers 的 flush 在 2c 移除。
 * ═══════════════════════════════════════════════════════════════════════ */

void setupWatchdog();         // 8 秒看門狗
void checkMemory();           // 剩餘 SRAM 創新低或低於 100 byte 時印警告
void checkSerialBuffers();    // RX > 60 byte 清空;TX 可寫 < 32 byte 時 flush(阻塞)
void diag_tick_1ms(void);
void diag_poll(void);

#endif
