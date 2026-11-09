#include "pins.h"

/* 〔步驟 1〕腳位改由下方 PinMap[] 統一定義,讀寫改用 pin_write() / FLOW_PIN_READ()
 * (直接存取暫存器,取代 pinMode / digitalWrite / digitalRead,架構計畫第 7 節)。
 * slot(i+1) 對應 ID_SLOT1 + i。舊的 Arduino 腳位編號保留供對照(〔步驟 2a〕由 mcu.ino 移來):
const int slotPins[] = {8,9,10,11,12,3,7,6,5,4};
const int slotCount = sizeof(slotPins) / sizeof(slotPins[0]);
const int bevvalve = 18;
const int watervalve = 17;
const int coldvalve = A15;
const int warmvalve = A14;
const int waterpump = A13;
const int flowmeterPin = A8;
*/

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
    /* 〔新增 2026-11-06〕pressure A1:水壓感測器(0~5 V,ADC);K1 轉接板需割斷兩條線、串 10 kΩ 限流電阻。
     * 〔步驟 6〕改為 ADC(關數位輸入緩衝 DIDR0);PRESSURE_ADC_CH 同步為 1。
     * 原「暫用一般輸入」(無上拉,與 reset 後狀態相同)保留供對照:
    { &PINF, _BV(PF1), PIN_IN,        0 },   // pressure    A1(暫用一般輸入)
     */
    { &PINF, _BV(PF1), PIN_ANALOG,    0 },   // pressure    A1(ADC1)
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
