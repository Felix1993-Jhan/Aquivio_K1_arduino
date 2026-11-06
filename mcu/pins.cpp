#include "pins.h"

/* 腳位表 — 順序必須與 pins.h 的 ID enum 一致
 * 對照:專案根目錄 CLAUDE.md 腳位表 */
const pin_def_t PinMap[PIN_COUNT] = {
    { &PINH, _BV(PH5), PIN_OUT,       0 },   // slot1       D8
    { &PINH, _BV(PH6), PIN_OUT,       0 },   // slot2       D9
    { &PINB, _BV(PB4), PIN_OUT,       0 },   // slot3       D10
    { &PINB, _BV(PB5), PIN_OUT,       0 },   // slot4       D11
    { &PINB, _BV(PB6), PIN_OUT,       0 },   // slot5       D12
    { &PINE, _BV(PE5), PIN_OUT,       0 },   // slot6       D3
    { &PINH, _BV(PH4), PIN_OUT,       0 },   // slot7       D7
    { &PINH, _BV(PH3), PIN_OUT,       0 },   // slot8       D6
    { &PINE, _BV(PE3), PIN_OUT,       0 },   // slot9       D5
    { &PING, _BV(PG5), PIN_OUT,       0 },   // slot10      D4
    { &PINH, _BV(PH0), PIN_OUT,       0 },   // watervalve  D17
    { &PIND, _BV(PD3), PIN_OUT,       0 },   // bevvalve    D18(只在開機拉低,之後未使用)
    { &PINK, _BV(PK5), PIN_OUT,       0 },   // waterpump   A13
    { &PINK, _BV(PK6), PIN_OUT,       0 },   // warmvalve   A14
    { &PINK, _BV(PK7), PIN_OUT,       0 },   // coldvalve   A15
    { &PINK, _BV(PK0), PIN_IN_PULLUP, 0 },   // flowmeter   A8(FLOW_PIN_READ 同步)
    /* 〔新增 2026-11-06〕pressure A1:未來接水壓感測器(0~5 V,ADC)。
     * 硬體尚未改線(需割斷兩條線、串 10 kΩ 限流電阻),目前暫用一般輸入(無上拉,與 reset 後狀態相同)。
     * 改線後:註解掉 PIN_IN 這行、啟用 PIN_ANALOG 這行(PRESSURE_ADC_CH 同步) */
    { &PINF, _BV(PF1), PIN_IN,        0 },   // pressure    A1(暫用一般輸入)
//  { &PINF, _BV(PF1), PIN_ANALOG,    0 },   // pressure    A1(ADC,改線後啟用)
};

/* 初始化所有腳位:取代 setup() 內的 pinMode / digitalWrite(LOW)
 * 輸出腳「先定準位、再設 DDR」:順序相反時,切換瞬間可能短暫輸出 HIGH */
void pin_init_all(void)
{
    for (uint8_t i = 0; i < PIN_COUNT; i++) {
        const pin_def_t *d = &PinMap[i];
        if (d->mode == PIN_OUT) {
            if (d->init) PIN_PORT(d) |= d->mask; else PIN_PORT(d) &= ~d->mask;  // 先定準位
            PIN_DDR(d) |= d->mask;                                              // 再設輸出
        } else {
            PIN_DDR(d) &= ~d->mask;                                             // 輸入
            if (d->mode == PIN_IN_PULLUP) PIN_PORT(d) |= d->mask;               // 上拉
            else                          PIN_PORT(d) &= ~d->mask;              // 浮接(PIN_ANALOG 也不開上拉)
            if (d->mode == PIN_ANALOG) {                                        // 關數位輸入緩衝
                if      (d->pin == &PINF) DIDR0 |= d->mask;                     // PFn ↔ ADCnD
                else if (d->pin == &PINK) DIDR2 |= d->mask;                     // PKn ↔ ADC(8+n)D
            }
        }
    }
}

void pin_write_mask(uint32_t mask, uint8_t v)
{
    for (uint8_t i = 0; i < PIN_COUNT; i++)
        if ((mask & (1UL << i)) && PinMap[i].mode == PIN_OUT) pin_write(i, v);
}
