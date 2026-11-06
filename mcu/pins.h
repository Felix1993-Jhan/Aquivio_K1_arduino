#ifndef K1_PINS_H
#define K1_PINS_H

/* ═══════════════════════════════════════════════════════════════════════
 *  pins — K1 所有 GPIO 的腳位表與讀寫函式(直接存取暫存器)
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  ▸ PinMap[] — 一張表定義所有腳位(port、方向、初始準位),索引 = ID_xxx
 *      對照來源:專案根目錄 CLAUDE.md 腳位表(main-v4,沿用 main31)
 *  ▸ 只存 PINx 指標:AVR 每個 port 的 PINx / DDRx / PORTx 位址連續,
 *      DDRx = PINx + 1、PORTx = PINx + 2(A~L 皆同)
 *  ▸ 取代 Arduino 的 pinMode / digitalWrite / digitalRead
 *      (架構計畫 K2_aidata/K1_1ms_tick_architecture.md 第 7 節)
 *
 *  ⚠ 非原子:pin_write 是讀改寫(LDS → ORI/ANDI → STS),
 *    Port H/J/K/L 在擴充 I/O 區,查表寫入時 A~G 也一樣。
 *    規則:輸出腳只在主迴圈寫,ISR 不寫輸出。
 *    若日後 ISR 必須寫輸出,主迴圈的 pin_write 要用 ATOMIC_BLOCK 包起來。
 * ═══════════════════════════════════════════════════════════════════════ */

#include <avr/io.h>
#include <stdint.h>

/* PIN_ANALOG:ADC 輸入。輸入、不開上拉,並關閉該腳的數位輸入緩衝(DIDRx),
 * 減少類比訊號在數位閘上造成的漏電與干擾。只支援 Port F(ADC0~7)/ Port K(ADC8~15) */
typedef enum { PIN_OUT, PIN_IN, PIN_IN_PULLUP, PIN_ANALOG } pin_mode_t;

typedef struct {
    volatile uint8_t *pin;   // PINx;DDRx = pin+1、PORTx = pin+2
    uint8_t mask;            // _BV(bit)
    uint8_t mode;            // pin_mode_t
    uint8_t init;            // PIN_OUT 的初始準位
} pin_def_t;

#define PIN_DDR(d)   (*((d)->pin + 1))
#define PIN_PORT(d)  (*((d)->pin + 2))

/* slot1~slot10 必須連續:呼叫端用 ID_SLOT1 + i 對應 slot(i+1) */
enum {
    ID_SLOT1, ID_SLOT2, ID_SLOT3, ID_SLOT4, ID_SLOT5,
    ID_SLOT6, ID_SLOT7, ID_SLOT8, ID_SLOT9, ID_SLOT10,
    ID_WATERVALVE, ID_BEVVALVE, ID_WATERPUMP, ID_WARMVALVE, ID_COLDVALVE,
    ID_FLOWMETER,
    ID_PRESSURE,      // 〔新增 2026-11-06〕A1:目前一般輸入,未來改 ADC 接水壓感測器;原 K1 程式沒有
    PIN_COUNT
};
static_assert(ID_SLOT10 - ID_SLOT1 == 9, "slot ID 必須連續");

#define SLOT_COUNT  (ID_SLOT10 - ID_SLOT1 + 1)

/* 流量計腳位:ISR 熱路徑不查表,直接讀暫存器。
 * 與 PinMap[ID_FLOWMETER] 必須一致,改腳位時兩處同步修改 */
#define FLOW_PIN_READ()  (PINK & _BV(PK0))   // A8 = PK0

/* 水壓感測器的 ADC 通道:與 PinMap[ID_PRESSURE] 必須一致,改腳位時兩處同步修改 */
#define PRESSURE_ADC_CH  1                    // A1 = PF1 = ADC1

extern const pin_def_t PinMap[PIN_COUNT];

void pin_init_all(void);                         // setup() 第一行呼叫
void pin_write_mask(uint32_t mask, uint8_t v);   // 多腳同動(bit i = ID i)

static inline void pin_write(uint8_t id, uint8_t v)
{
    const pin_def_t *d = &PinMap[id];
    if (v) PIN_PORT(d) |= d->mask; else PIN_PORT(d) &= ~d->mask;   // 讀改寫,見檔頭規則
}

static inline uint8_t pin_read(uint8_t id)                          // 輸出腳也可讀回實際準位
{
    return (*PinMap[id].pin & PinMap[id].mask) ? 1 : 0;
}

static inline void pin_toggle(uint8_t id)                           // 寫 1 到 PINx = 翻轉
{
    *PinMap[id].pin = PinMap[id].mask;                              // 單次寫入,H/J/K/L 也是原子
}

#endif
