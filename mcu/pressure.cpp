#include <avr/io.h>
#include "pressure.h"
#include "pins.h"          // PRESSURE_ADC_CH

#define PRESSURE_PERIOD_MS  200    // 取樣週期

static uint16_t last_raw   = 0;
static uint8_t  valid      = 0;
static uint8_t  converting = 0;
static uint8_t  ms_cnt     = 0;

void pressure_init(void)
{
    ADMUX  = _BV(REFS0) | (PRESSURE_ADC_CH & 0x07);              // REFS1:0 = 01 → AVCC;ADLAR = 0 右對齊;MUX4:0
    ADCSRB = (PRESSURE_ADC_CH & 0x08) ? _BV(MUX5) : 0;           // 通道 8~15 才設 MUX5;不用自動觸發
    ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);   // 開 ADC、除頻 128、不開中斷
                                                                 // (用 = 整個覆蓋 Arduino init() 的設定)
}

void pressure_tick_1ms(void)
{
    if (converting && !(ADCSRA & _BV(ADSC))) {   // ADSC 自動清 0 = 轉換完成
        last_raw   = ADC;                        // 16-bit 讀取:編譯器先讀 ADCL 再讀 ADCH
        valid      = 1;
        converting = 0;
    }

    if (++ms_cnt >= PRESSURE_PERIOD_MS) {
        ms_cnt = 0;
        if (!converting) {
            ADCSRA |= _BV(ADSC);                 // 啟動一次轉換(約 104 µs,下一個 tick 已完成)
            converting = 1;
        }
    }
}

uint16_t pressure_raw(void)   { return last_raw; }
uint8_t  pressure_valid(void) { return valid; }
