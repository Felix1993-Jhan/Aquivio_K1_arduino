#include "device_ctrl.h"
#include "pins.h"

/* 索引 = K2 ID。對照 K1_k2port_discussion.md 3.2 */
const uint8_t DeviceMap[DEVICE_ID_COUNT] = {
    ID_SLOT1,  ID_SLOT2,  ID_SLOT3,  ID_SLOT4,  ID_SLOT5,     //  0~4:Pump0~4  → slot1~5
    ID_SLOT6,  ID_SLOT7,  ID_SLOT8,  ID_SLOT9,  ID_SLOT10,    //  5~9:Pump5~9  → slot6~10
    ID_WATERPUMP,                                             // 10:Pump10 水泵(K2 PWM → K1 開關)
    DEV_NOPIN,                                                // 11:UVC0(K1 無)
    ID_CARBONATED,                                            // 12:UVC1 → carbonated A9
    ID_SPOUT,                                                 // 13:UVC2 → spout A10
    ID_WARMVALVE,                                             // 14:Relay0 常溫 → warmvalve A14
    ID_COLDVALVE,                                             // 15:Relay1 冷水 → coldvalve A15
    DEV_NOPIN,                                                // 16:Relay2 氣泡水(K1 無)
    DEV_NOPIN,                                                // 17:Clean(K1 無)
    DEV_NONE, DEV_NONE, DEV_NONE, DEV_NONE,                   // 18~21:Flow、壓力 ×2、溫度 0(非裝置)
    DEV_NONE, DEV_NONE, DEV_NONE,                             // 22~24:溫度 1、2、Leak(非裝置)
    DEV_NOPIN,                                                // 25:Container(K1 無)
};

uint32_t Status = 0;
uint32_t Stop = 0;
uint32_t Counter1ms[DEVICE_ID_COUNT];

// 判別 ID 是否為輸出裝置(K2:DeviceMap[id].port != 0;K1 沒有實體的輸出也算,Status 照翻)
int isDeviceID(uint8_t id)
{
    return (id < DEVICE_ID_COUNT) && (DeviceMap[id] != DEV_NONE);
}

static void dev_on(uint8_t i)
{
    if (DeviceMap[i] != DEV_NOPIN) pin_write(DeviceMap[i], 1);
    Status |= DEV_BIT(i);
}

static void dev_off(uint8_t i)
{
    if (DeviceMap[i] != DEV_NOPIN) pin_write(DeviceMap[i], 0);
    Status &= ~DEV_BIT(i);
    Counter1ms[i] = 0;
}

//type = 0, control SW using time (id = device sensor ID 0~25)
//type = 1, open SW  (id = bit mask)
//type = 2, stop SW  (id = bit mask)
void handleSW(int type, uint32_t id)
{
    if (type == 0) {                    // 設定計時開關(id = 單一裝置 ID);倒數值由呼叫端填 Counter1ms
        if (isDeviceID(id)) dev_on(id);
    } else if (type == 1) {             // 批次啟動(id = 位元遮罩)
        if (id != 0) {
            for (uint8_t i = 0; i < DEVICE_ID_COUNT; i++)
                if ((id & DEV_BIT(i)) && isDeviceID(i) && !(Status & DEV_BIT(i))) dev_on(i);
        }
    } else if (type == 2) {             // 批次停止(id = 位元遮罩)
        if (id != 0) {
            for (uint8_t i = 0; i < DEVICE_ID_COUNT; i++)
                if ((id & DEV_BIT(i)) && (Status & DEV_BIT(i))) dev_off(i);
            Stop = 0;                   // 同 K2
        }
    }
}

uint32_t device_pin_readback(void)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < DEVICE_ID_COUNT; i++) {
        uint8_t p = DeviceMap[i];
        if (p != DEV_NOPIN && p != DEV_NONE && pin_read(p)) v |= DEV_BIT(i);
    }
    return v;
}
