#include <string.h>
#include "freeflow.h"
#include "device_ctrl.h"   // handleSW、DEV_BIT

freeflow_def channel7_CMD[FREEFLOW_MAX];   // 各 slot 資料
uint8_t      freeFlow_Run = 0;
uint8_t      pump0_9count = 0;
uint32_t     temp_main_freeflow = 0;

/* ══════════════════════════════════════════════════════════
 *  工具(K2 freeflow.c 區塊 1)
 * ══════════════════════════════════════════════════════════ */

// bubble sort by ID(小的 ID 在前面)
void sort_by_speed(freeflow_def *data, int count)
{
    freeflow_def temp;
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            if (data[j].id > data[j + 1].id) {
                temp = data[j];
                data[j] = data[j + 1];
                data[j + 1] = temp;
            }
        }
    }
}

/* 清空所有 slot 結構 + 旗標。K2 另清 FlowChange(K1 改用脈衝差值,沒有此旗標)、
 * sperkingCycleCount(氣泡水)、shortPulseMask / pendingRead(短脈衝偵測),皆不移植 */
void freeflow_Reset(void)
{
    memset(channel7_CMD, 0, sizeof(channel7_CMD));
    freeFlow_Run = 0;
    pump0_9count = 0;
    temp_main_freeflow = 0;
}

/* 〔K1 新增,使用者 2026-11-11 決定,試驗中〕slot 的 acc 起點錯開(K2 一律從 0 開始)。
 * 原因:同比例的 slot 會在同一個 pulse 一起跨過 100 → 同時開,出水口有「小噴水」感。
 * 做法:LONG_PRESSURE_CMD 排序(ID 小 → 大)後呼叫;時間值不是 0 的 slot 依序輪流給 17 / 50 / 83。
 *   ▸ 錯開:起點不同 → 跨過 100 的 pulse 不同(同比例的 slot 會一直維持相位差)
 *   ▸ 劑量:平均預先加 50,抵掉放開時 freeflow_Reset 清掉的殘量(平均約 50)
 * 每次 0x07 都重設(同一杯放開再按也會重新加)。K2 原本為 LONG_PRESSURE_CMD 裡的 acc = 0(保留,先清 0 再由此覆蓋) */
void freeflow_stagger_start(void)
{
    static const uint8_t ACC_START[3] = { 17, 50, 83 };
    uint8_t k = 0;
    for (int i = 0; i < pump0_9count; i++) {
        if (channel7_CMD[i].pulse_add_time) channel7_CMD[i].acc = ACC_START[k++ % 3];
    }
}

/* ══════════════════════════════════════════════════════════
 *  常溫水 / 冷水模式(K2 freeflow.c 區塊 2)
 *  K2 在主迴圈每圈呼叫;K1 由 tick_main 每 1 ms 呼叫(狀態每 1 ms 才變,結果相同)
 * ══════════════════════════════════════════════════════════ */
void freeflow_punpON(void)
{
    // 運行 slot 並且開始計時
    for (int i = 0; i < pump0_9count; i++) {
        /* 〔K1 修正,使用者 2026-11-11 決定〕K2 開啟條件為 acc > 100,關閉條件為 acc < 100:
         * 扣完剛好 = 100 時兩邊都不成立 → slot 開著但計時停在 0,要等下一個 pulse 才重新計時,
         * 多開「到下一個 pulse 之前」的時間(模擬:add 50 / 週期 80 ms 時多 58%;水流停止則一直開到 0x02)。
         * 改為 ≥ 100:扣完還夠開 100 ms 就繼續開。K2 原寫法:
        if (channel7_CMD[i].acc > PUMP_100mSec) {
         */
        if (channel7_CMD[i].acc >= PUMP_100mSec) {
            if (channel7_CMD[i].mSec_100count == 0) {          // 判斷是否已經在運行了
                handleSW(1, DEV_BIT(channel7_CMD[i].id));      // ⚠ K2 為 1 << id,AVR int 16-bit 改 DEV_BIT
                channel7_CMD[i].mSec_100count++;
            }
        }
    }

    for (int i = 0; i < pump0_9count; i++) {
        if (channel7_CMD[i].mSec_100count >= PUMP_100mSec) {   // 運行了 100 ms:清空 count、扣除 100
            channel7_CMD[i].mSec_100count = 0;
            channel7_CMD[i].acc -= PUMP_100mSec;
            if (channel7_CMD[i].acc < PUMP_100mSec) {          // 累積值還夠 100 則繼續開著
                handleSW(2, DEV_BIT(channel7_CMD[i].id));
            }
        }
    }
}

/* K2 timer1_longPressure_flowcount():每次 pulse 累積該 slot 需要開啟的時間,上限 ACC_MAX。
 * K1:K2 由 FlowChange 旗標觸發、每次加 1 個 pulse;K1 傳入這 1 ms 新增的 pulse 數 d(實際為 0 或 1),
 * 一次加 d 倍後再限制上限(上限是取最小值,與逐次加再限制結果相同) */
void timer1_longPressure_flowcount(uint32_t d)
{
    for (int i = 0; i < pump0_9count; i++) {
        uint32_t a = channel7_CMD[i].acc + (uint32_t)channel7_CMD[i].pulse_add_time * d;
        channel7_CMD[i].acc = (a > ACC_MAX) ? ACC_MAX : (uint16_t)a;   // 防止加到爆炸
    }
}

// K2 timer1_longPressure_pumpCount():開啟中的 slot 每 1 ms 計時,上限 100
void timer1_longPressure_pumpCount(void)
{
    for (int i = 0; i < pump0_9count; i++) {
        if (channel7_CMD[i].mSec_100count) channel7_CMD[i].mSec_100count++;
        if (channel7_CMD[i].mSec_100count >= PUMP_100mSec) channel7_CMD[i].mSec_100count = PUMP_100mSec;
    }
}
