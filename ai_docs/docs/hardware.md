# 硬體:腳位、計時器、ADC

> 韌體 0.0.4.0(`main-k2port-v4`)。腳位依 K1 轉接板實際接線(使用者確認 2026-11-10);Arduino 官方腳位圖 `A000067-full-pinout.pdf`。〔[沿革](#沿革)〕
> 程式:`mcu/pins.cpp`(`PinMap[]`)、`mcu/device_ctrl.cpp`(`DeviceMap[]`)、`mcu/tick.cpp`、`mcu/flowmeter.cpp`、`mcu/pressure.cpp`、`mcu/diagnostics.cpp`。

## 1. MCU 與資源

| 項目 | 內容 |
|---|---|
| 板子 | Arduino Mega 2560 R3 |
| MCU | ATmega2560,8-bit AVR,16 MHz 石英 |
| Flash | 256 KB(bootloader 佔 8 KB);本版正式版 6.3 KB |
| SRAM | 8 KB;本版全域變數 1305 byte(15%) |
| USB | 板上 ATmega16U2 轉 UART0;DTR 經電容接 RESET(開 port 即重開機) |
| 看門狗 | 8 秒;主迴圈每圈 `wdt_reset()` |

## 2. 腳位表

| 功能 | Arduino 編號 | AVR 埠 | 設定 | 裝置 ID |
|---|---|---|---|---|
| slot1~5 | D8 / D9 / D10 / D11 / D12 | PH5 / PH6 / PB4 / PB5 / PB6 | 輸出 | 0~4 |
| slot6~10 | D3 / D7 / D6 / D5 / D4 | PE5 / PH4 / PH3 / PE3 / PG5 | 輸出 | 5~9 |
| waterpump 水泵 | A13(D67) | PK5 | 輸出 | 10 |
| carbonated | A9(D63) | PK1 | 輸出 | 12(UVC1) |
| spout | A10(D64) | PK2 | 輸出 | 13(UVC2) |
| warmvalve 常溫閥 | A14(D68) | PK6 | 輸出 | 14 |
| coldvalve 冷水閥 | A15(D69) | PK7 | 輸出 | 15 |
| watervalve | A11(D65) | PK3 | 輸出,開機拉低,不使用 | — |
| bevvalve | A12(D66) | PK4 | 輸出,開機拉低;〔暫時〕與流量計同步(見 5.3) | — |
| flowmeter 流量計 | A8(D62) | PK0 | 輸入 + 上拉 | 18(`0x03`) |
| pressure 水壓 | A1(D55) | PF1 | ADC(無上拉、關數位輸入緩衝) | — |
| heartbeat 心跳 | D23 | PA1 | 輸出 | — |
| 模擬流量計(除錯版) | D22 | PA0 | 輸出(只在除錯版) | — |
| Serial | D0 / D1 | PE0 / PE1 | UART0 | — |

- 「warm」是常溫水(機台只有常溫與冷水)
- carbonated / spout 依使用者指定對應 K2 的 UVC1 / UVC2;目前實機尚未接 UVC
- 所有輸出腳開機時「先定低準位、再設輸出」。上電到 `setup()` 第一行之間約有 bootloader 時間的高阻抗空窗,韌體無法消除

## 3. 裝置 ID 對照

`DeviceMap[26]` 以 K2 裝置 ID 為索引,內容為 K1 `PinMap` 索引。

| ID | K2 名稱 | K1 |
|:---:|---|---|
| 0~9 | PUMP0~9 | slot1~10 |
| 10 | PUMP10 水泵(K2 為 TIM3 PWM) | waterpump A13(開關) |
| 11 | UVC0 | 無實體(`DEV_NOPIN`) |
| 12 | UVC1 | carbonated A9 |
| 13 | UVC2 | spout A10 |
| 14 | RELAY0 常溫 | warmvalve A14 |
| 15 | RELAY1 冷水 | coldvalve A15 |
| 16 | RELAY2 氣泡水 | 無實體 |
| 17 | CLEAN | 無實體 |
| 18 | FLOW(感測器) | flowmeter A8 |
| 19~24 | 壓力、溫度、漏水(感測器) | 無(`DEV_NONE`,`0x03` 回不支援格式) |
| 25 | CONTAINER | 無實體 |

- **無實體的輸出 ID**:`0x00~0x02` 照常切換 `Status`,沒有腳位輸出;可用 `0xB5 00` 看出上位機下過的命令
- **遮罩位元**:一律用 `DEV_BIT(id) = 1UL << id`。AVR 的 `int` 只有 16-bit,寫成 `1 << id` 在 id ≥ 15 時會溢位

## 4. 1 ms tick(Timer5)

| 項目 | 內容 |
|---|---|
| 模式 | CTC,TOP = `OCR5A` = 249 |
| 頻率 | 16 MHz / 64 / 250 = 1000 Hz |
| ISR 內容 | `tick_cnt++`、流量計取樣(只讀 PINK);除錯版另翻轉 D22 |
| 其他工作 | 主迴圈 `tick_service()` 依 `tick_cnt` 補跑 `tick_main()`,每 1 ms 一次 |

Arduino 的 `init()` 預設把 Timer5 設成 8-bit PWM,所以 `TCCR5A` / `TCCR5B` 要整個重寫。Timer0(`millis()`)不受影響。

## 5. 輸入與指示

### 5.1 流量計(A8)

- 上拉輸入，水流時流量計把線拉低(L = ON)
- 每 1 ms 在 ISR 取樣(移植 K2 `timer1_flowmeter()`):
  - 高、低準位都要連續 3 次取樣一致才確認
  - 確認為低準位的那一次計 1 pulse
- 可計數的最高頻率約 166 Hz(K2 實測水流約 8 Hz)
- 計數為 32-bit,到滿值後停止，不會繞回
- 主迴圈以 `ATOMIC_BLOCK` 讀取或歸零(AVR 讀 32-bit 要分 4 次)
- 每 pulse 的水量由上位機設定，韌體只回 pulse 數

### 5.2 水壓(A1 ADC)

- 參考電壓 AVCC,除頻 128,10-bit 右對齊
- 每 200 ms 啟動一次轉換，下一個 tick 讀回;不用 `analogRead`
- K2 協定不讀這個值(`0x03` ID20 回不支援格式);程式保留
- K1 轉接板若要接感測器，需要實體改線：割斷兩條線，串 10 kΩ 限流電阻

### 5.3 指示與除錯腳

| 腳位 | 行為 | 版本 |
|---|---|---|
| D23 心跳 | 主迴圈每 1 秒翻轉(0.5 Hz);主迴圈卡住即停 | 正式 / 除錯 |
| D22 模擬流量計 | 水泵(PK5)開著時輸出約 6 Hz 方波(83 / 84 ms 交替翻轉),泵關時維持高準位;以杜邦線接 A8 | 只有除錯版(`-DK1_DEBUG_FLOW_GEN`) |
| D66(bevvalve)〔暫時〕 | 每 1 ms 跟隨 A8 原始準位(未 debounce),看板上 LED 判斷流量計訊號;轉接板上沒接負載 | 正式 / 除錯;去留待定 |

D66 停用方式:把 `tick.cpp` `tick_main()` 裡的 `diag_flow_mirror_1ms()` 註解掉。

## 6. 存取規則

- **輸出腳只在主迴圈寫**
  - `pin_write` 是讀改寫(讀 PORTx → 改 1 bit → 寫回)
  - Port H / J / K / L 在擴充 I/O 區，這個讀改寫不是原子操作
  - 若 ISR 也寫同一個 port,可能蓋掉主迴圈剛寫的值。水泵、閥、UVC、D66 都在 Port K
- **ISR 只讀不寫**
  - 流量計 ISR 只讀 PINK
  - 除錯版的 D22 翻轉用單次寫入:寫 PINA 翻轉、`PORTA |=` 編成 SBI 單一指令，不是讀改寫
- **翻轉用 `pin_toggle`**:寫 1 到 PINx 是單次寫入，各 port 都是原子操作

## 7. 空腳位

Mega 2560 共 70 支 I/O,本版使用 23 支(`PinMap` 20 支 + Serial 2 支 + 除錯版 D22)。空腳位清單見專案根目錄 `CLAUDE.md`。

---

## 沿革

- 2026-11-12 建立(0.0.4.0,`b74d5a3`)。腳位整理自 `CLAUDE.md`,裝置 ID 整理自 `K2_aidata/K1_k2port_discussion.md` 3.2
