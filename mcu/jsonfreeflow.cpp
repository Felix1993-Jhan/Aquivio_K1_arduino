#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include "jsonfreeflow.h"
#include "pins.h"          // SLOT_COUNT、pin_write、ID_SLOT1、ID_CARBONATED、ID_SPOUT
#include "device.h"        // pump_water
#include "flowmeter.h"     // flow_get、FLOW_UL_PER_PULSE
#include "tx_queue.h"      // out

/* 〔K1 新增,使用者 2026-11-12 決定〕說明見 jsonfreeflow.h */

typedef struct {
    uint16_t add;      // 每 pulse 開幾 ms(上位機給);0 = 不用
    uint16_t acc;      // 尚未注入的時間量(ms),每 pulse 累積
    uint8_t  on_ms;    // 開啟中的計時(ms);0 = 未在計時(同 K2 mSec_100count)
} jff_slot_t;

static jff_slot_t slot[SLOT_COUNT];
static bool       running = false;
static int        waterTemp = 0;        // 0 = 冷水、1 = 常溫(同 pump_water)
static uint32_t   flow_start = 0;       // 開始時的流量計數(回報從這裡算起)
static uint32_t   flow_last = 0;        // 上一個 tick 的流量計數
static uint8_t    report_ms = 0;
static uint16_t   on_mask = 0;          // bit i = slot(i+1) 腳位開著

static void slot_pin(int i, bool on) {
    pin_write(ID_SLOT1 + i, on);
    if (on) on_mask |= (uint16_t)(1u << i);
    else    on_mask &= (uint16_t)~(1u << i);
}

#ifdef K1_DEBUG_FLOW_GEN
/* 除錯版統計(測試腳本用,正式版不編譯):每 1 ms 依 on_mask 累計,stop 時印出
 *   freeflow stat slot ms:1=1234 3=2345   各 slot 實際開啟總 ms(本次有設定的 slot)
 *   freeflow stat open:0=12345 1=4100     同時開著 0、1、2… 台的累計 ms */
static uint16_t st_used = 0;
static uint32_t st_slot_ms[SLOT_COUNT];
static uint32_t st_open_ms[SLOT_COUNT + 1];

static void stat_tick(void) {
    uint8_t n = 0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (on_mask & (1u << i)) { st_slot_ms[i]++; n++; }
    }
    st_open_ms[n]++;
}

static void stat_print(void) {
    out.print("freeflow stat slot ms:");
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (!(st_used & (1u << i))) continue;
        out.print(' '); out.print(i + 1); out.print('='); out.print(st_slot_ms[i]);
    }
    out.println();
    int last = 0;
    for (int k = 0; k <= SLOT_COUNT; k++) if (st_open_ms[k]) last = k;
    out.print("freeflow stat open:");
    for (int k = 0; k <= last; k++) {
        out.print(' '); out.print(k); out.print('='); out.print(st_open_ms[k]);
    }
    out.println();
}
#endif

static void uvc(bool on) {
    pin_write(ID_CARBONATED, on);       // A9(K2 移植版 ID12 UVC)
    pin_write(ID_SPOUT, on);            // A10(K2 移植版 ID13 UVC)
}

/* slot 值:數字或字串都接受,小數截掉;超過上限夾到 JFF_ADD_MAX */
static uint16_t json_add(JsonVariantConst v) {
    const char* s = v.as<const char*>();
    uint32_t x = (s != NULL) ? strtoul(s, NULL, 10) : v.as<uint32_t>();
    return (x > JFF_ADD_MAX) ? JFF_ADD_MAX : (uint16_t)x;
}

bool jff_is_request(const char* actions) {
    return strstr(actions, "\"freeflow\"") != NULL;
}

void jff_execute(const char* actions) {
    StaticJsonDocument<200> doc;        // 同 pour_parse(7.1.0 中即 JsonDocument)
    DeserializationError error = deserializeJson(doc, actions);
    if (error) {
        out.print("Failed to parse JSON: ");
        out.println(error.c_str());
        return;
    }

    const char* w = doc["freeflow"].as<const char*>();
    if      (w != NULL && strcmp(w, "cold") == 0) waterTemp = 0;
    else if (w != NULL && strcmp(w, "warm") == 0) waterTemp = 1;
    else { out.println("Failed to parse JSON: freeflow"); return; }

    /* acc 起點錯開(同 K2 移植版 freeflow_stagger_start):依 slot 編號(小 → 大)輪流 17 / 50 / 83 */
    static const uint8_t ACC_START[3] = { 17, 50, 83 };
    uint8_t k = 0;
    memset(slot, 0, sizeof(slot));
    for (int i = 0; i < SLOT_COUNT; i++) {
        char key[8];
        sprintf(key, "slot%d", i + 1);
        if (doc.containsKey(key)) slot[i].add = json_add(doc[key]);
        if (slot[i].add) slot[i].acc = ACC_START[k++ % 3];
    }

    pump_water(1, waterTemp);
    uvc(true);
    flow_start = flow_last = flow_get();
    report_ms = 0;
    on_mask = 0;
    running = true;
#ifdef K1_DEBUG_FLOW_GEN
    st_used = 0;
    for (int i = 0; i < SLOT_COUNT; i++) if (slot[i].add) st_used |= (uint16_t)(1u << i);
    memset(st_slot_ms, 0, sizeof(st_slot_ms));
    memset(st_open_ms, 0, sizeof(st_open_ms));
#endif
    out.print("Freeflow ");
    out.print(waterTemp ? "warm" : "cold");
    out.println(" requested");
}

/* 順序同 K2 移植版 tick_main:累積(timer1_pulse_count)→ 計時(pumpCount)→ 開 / 關(freeflow_punpON) */
void jff_tick_1ms(void) {
    if (!running) return;

    uint32_t now = flow_get();
    uint32_t d = now - flow_last;       // 這 1 ms 新增的 pulse(實際為 0 或 1)
    flow_last = now;

    for (int i = 0; i < SLOT_COUNT; i++) {
        jff_slot_t* s = &slot[i];
        if (d && s->add) {                                   // 每 pulse 累積,上限 ACC_MAX
            uint32_t a = s->acc + (uint32_t)s->add * d;
            s->acc = (a > JFF_ACC_MAX) ? JFF_ACC_MAX : (uint16_t)a;
        }
        if (s->on_ms && s->on_ms < JFF_OPEN_MS) s->on_ms++;  // 開啟中計時,上限 100
    }

    for (int i = 0; i < SLOT_COUNT; i++) {                   // 夠 100 且沒在計時 → 開
        if (slot[i].acc >= JFF_OPEN_MS && slot[i].on_ms == 0) {
            slot_pin(i, 1);
            slot[i].on_ms = 1;
        }
    }
    for (int i = 0; i < SLOT_COUNT; i++) {                   // 開滿 100 ms → 扣 100,不夠下一次才關
        if (slot[i].on_ms >= JFF_OPEN_MS) {
            slot[i].on_ms = 0;
            slot[i].acc -= JFF_OPEN_MS;
            if (slot[i].acc < JFF_OPEN_MS) slot_pin(i, 0);
        }
    }
#ifdef K1_DEBUG_FLOW_GEN
    stat_tick();
#endif

    if (++report_ms >= JFF_REPORT_MS) {
        report_ms = 0;
        out.print("freeflow pulse:");
        out.println(now - flow_start);
    }
}

bool jff_busy(void) {
    return running;
}

void jff_stop(void) {
    if (!running) return;
    for (int i = 0; i < SLOT_COUNT; i++) slot_pin(i, 0);
    uvc(false);
    pump_water(0, waterTemp);           // 關泵閥,印 pulse:N / 用水量(同出杯)
    memset(slot, 0, sizeof(slot));
    running = false;
#ifdef K1_DEBUG_FLOW_GEN
    stat_print();
#endif
    out.println("finish: freeflow stopped by external stop");
    out.println("end");
}

void jff_stop_slot(int slotNumber) {
    if (!running || slotNumber < 0 || slotNumber >= SLOT_COUNT) return;
    slot_pin(slotNumber, 0);
    slot[slotNumber].add = 0;
    slot[slotNumber].acc = 0;
    slot[slotNumber].on_ms = 0;
}

/* 方案 A(目前未呼叫,編譯器會移除):add = slot_ms × 每 pulse µL ÷ 水量 µL,四捨五入
 * 例:slot 2181 ms、0.30 L(300000 µL)→ 2181 × 3300 ÷ 300000 ≈ 24 ms / pulse */
uint16_t jff_add_from_recipe(uint32_t slot_ms, uint32_t desired_ul) {
    if (desired_ul == 0) return 0;
    if (slot_ms > 1000000UL) slot_ms = 1000000UL;            // × 3300 不超過 uint32
    uint32_t add = (slot_ms * FLOW_UL_PER_PULSE + desired_ul / 2) / desired_ul;
    return (add > JFF_ADD_MAX) ? JFF_ADD_MAX : (uint16_t)add;
}
