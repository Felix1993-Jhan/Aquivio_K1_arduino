#ifndef K1_DIAGNOSTICS_H
#define K1_DIAGNOSTICS_H

/* ═══════════════════════════════════════════════════════════════════════
 *  diagnostics — 看門狗、D23 心跳
 * ═══════════════════════════════════════════════════════════════════════
 *  K2 移植版:port 上只能有 K2 二進位封包,原 K1 的文字輸出(mcuAlive、記憶體警告)
 *  與 Serial 緩衝檢查已移除(原版見 main-v4)。
 *
 *  ▸ setupWatchdog()  8 秒看門狗
 *  ▸ diag_tick_1ms()  每 1 ms 由 tick_main() 呼叫:D23 心跳每 1 秒翻轉(0.5 Hz)
 *  ▸ diag_flow_mirror_1ms()  〔暫時〕每 1 ms 由 tick_main() 呼叫:D66 與流量計 A8 同步(看 LED 用)
 * ═══════════════════════════════════════════════════════════════════════ */

void setupWatchdog();
void diag_tick_1ms(void);
void diag_flow_mirror_1ms(void);

#endif
