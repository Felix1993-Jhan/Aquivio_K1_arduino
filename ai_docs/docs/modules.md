# 模組架構

> 加入版本:0.0.4.0 / 最後修改:0.0.4.0(2026-11-12)
> 韌體 0.0.4.0(`main-k2port-v4`)。程式都在 `mcu/`(Arduino IDE 只看 sketch 資料夾根目錄,模組不放子資料夾)。

## 1. 模組總覽

| 模組 | 職責 | 來源 |
|---|---|---|
| `mcu.ino` | `setup()` 初始化順序、`loop()` 主迴圈 | — |
| `pins` | 腳位表 `PinMap[]`、直接存取暫存器的讀寫函式 | 共用(`main-v4`) |
| `tick` | Timer5 1 ms 計時、`tick_main()` 每 1 ms 工作分派 | 共用;`tick_main` 內容本分支自有 |
| `flowmeter` | 流量計 1 ms 取樣 debounce、pulse 計數、除錯版模擬流量計 | 共用(debounce 移植自 K2) |
| `pressure` | A1 水壓 ADC 每 200 ms 取樣 | 共用(本分支未使用讀值) |
| `diagnostics` | 看門狗、D23 心跳;〔暫時〕D66 同步流量計 | 共用;D66 只在本分支 |
| `tx_queue` | TX 發送佇列(512 byte),回覆不等待 | 共用(K1 新增,K2 沒有) |
| `uart_cmd` | K2 協定:收包、長度查表、CS、命令分派與回覆 | 移植 K2 `uart_cmd.c` |
| `device_ctrl` | 裝置 ID 對照 `DeviceMap[]`、`Status`、`handleSW()` | 移植 K2 `device_ctrl.c` |
| `timer_task` | `0x00` 倒數、`0x06` 扣 pulse、到時 / 到量關閉 | 移植 K2 `timer_task.c` + `stm32u0xx_it.c` 的 `timer1_*` |
| `freeflow` | `0x07` slot 依流量開啟 | 移植 K2 `freeflow.c` + `timer1_longPressure_*` |

「共用」= 與 K1 優化版(`main-v4`)相同的模組。共用部分在 `main-v4` 修正後合併到本分支，不反向合併。

## 2. 執行模型

```
Timer5 ISR(每 1 ms)                主迴圈 loop()
├─ tick_cnt++                      ├─ wdt_reset()
├─ flow_isr_1ms()  讀 A8、計 pulse  ├─ tick_service()  補跑 tick_main(),次數 = tick_cnt 增量
└─ (除錯版)D22 翻轉                ├─ cmd_poll()      讀 Serial,收滿一包就處理並回覆
                                   └─ tx_poll()       TX 佇列搬到 Serial,不等待
```

**方案 A**:K2 在 TIM1 中斷內跑的 `timer1_*` 函式，本體照抄，但改由主迴圈的 `tick_main()` 呼叫。

- 好處：除了流量計數外，所有變數(`Status`、`Counter1ms`、`Pulse`、`channel7_CMD`…)都只在主迴圈存取
- 因此不需要 K2 的 `__disable_irq` 交接，也避開 8-bit AVR 讀寫多 byte 變數不是原子操作的問題
- 主迴圈短暫被拖慢時,`tick_service()` 會把累積的 tick 依序補跑，計時不會少

### 2.1 `tick_main()` 順序(每 1 ms)

| # | 呼叫 | 作用 |
|---|---|---|
| 1 | `diag_tick_1ms()` | D23 心跳 |
| 2 | `diag_flow_mirror_1ms()` | 〔暫時〕D66 跟隨 A8 |
| 3 | `uart_tick_1ms()` | 收包 20 ms 逾時倒數 |
| 4 | `timer1_SET_Count()` | `0x00` 倒數(只在非 freeflow 時) |
| 5 | `timer1_pulse_count()` | 讀流量增量:扣 `0x06` pulse;freeflow 中累積 slot `acc` |
| 6 | `timer1_longPressure_pumpCount()` | freeflow:開啟中的 slot 計時(只在 freeflow 時) |
| 7 | `Tmr1_Interrupt_main()` | 關閉倒數到 0 / 到量的裝置,送 `0x06` 完成通知 |
| 8 | `freeflow_punpON()` | freeflow:slot 開 / 關(只在 freeflow 時) |
| 9 | `pressure_tick_1ms()` | 水壓 ADC |

### 2.2 `setup()` 順序

`pin_init_all()`(最先，縮短高阻抗空窗)→ `setupWatchdog()` → `pressure_init()` → `flow_init()` → `tick_init()` → `Serial.begin(115200)` → `uart_cmd_init()`。開機不送任何資料。

## 3. 相依關係

```
mcu.ino ─┬─ pins
         ├─ tick ──┬─ diagnostics ── pins
         │         ├─ flowmeter
         │         ├─ uart_cmd(uart_tick_1ms)
         │         ├─ timer_task ──┬─ device_ctrl ── pins
         │         │               ├─ flowmeter
         │         │               ├─ uart_cmd(完成通知)
         │         │               └─ freeflow
         │         ├─ freeflow ── device_ctrl
         │         └─ pressure ── pins
         ├─ uart_cmd ──┬─ tx_queue
         │             ├─ device_ctrl
         │             ├─ flowmeter
         │             ├─ timer_task
         │             └─ freeflow
         └─ tx_queue
```

## 4. 各模組

### 4.1 `pins`

- `PinMap[]`:一張表定義所有腳位(port、方向、初始準位),索引為 `ID_xxx` enum
  - 只存 PINx 指標:AVR 同一 port 的 DDRx = PINx + 1、PORTx = PINx + 2
- `pin_init_all()`:輸出腳先定準位再設 DDR;ADC 腳關閉數位輸入緩衝(DIDR)
- `pin_write()` / `pin_read()` / `pin_toggle()`:取代 `digitalWrite` / `digitalRead`
- `FLOW_PIN_READ()`:ISR 用，直接讀 PINK,不查表;改腳位時要和 `PinMap` 同步修改
- 規則:`pin_write` 是讀改寫，只在主迴圈呼叫(見 [hardware.md 第 6 節](hardware.md#6-存取規則))

### 4.2 `tick`

- `tick_init()`:Timer5 CTC 1 kHz
- `tick_service()`:取一次 `tick_cnt` 快照，補跑到該值為止(每次執行時間有上限)
- `K1_DEBUG_TICK`:除錯用編譯開關，記錄最大積壓 tick 數

### 4.3 `flowmeter`

- `flow_isr_1ms()`:只在 Timer5 ISR 內呼叫;高 / 低各連續 3 ms 才確認，確認變低時計 1 pulse
- `flow_get()` / `flow_take()`:原子讀取 / 讀取並歸零
- `K1_DEBUG_FLOW_GEN`:除錯版 D22 模擬流量計(水泵開著時約 6 Hz)
- `FLOW_UL_PER_PULSE`(3300 µL):給 K1 優化版用;本分支韌體不換算 ml,沒有用到

### 4.4 `uart_cmd`

| 函式 | 作用 |
|---|---|
| `cmd_poll()` | `loop()` 呼叫:逐 byte 從 Serial 取出,收滿一包就呼叫 `Ur1_Rx_main()` |
| `rx_byte()` | 標頭逐 byte 比對、20 ms 逾時、第 5 byte 查長度表 |
| `Ur1_Rx_main()` | CS 驗證 → 依 CMD 分派 → 產生 CS → 寫進 TX 佇列 |
| `*_CMD()` | 各命令處理(同 K2 命名) |
| `uart_send_pulse_done()` | `0x06` 完成通知(獨立 9 byte 緩衝) |

- 收發緩衝各 64 byte(K2 256);最長封包為 `0x07` N = 19 的 64 byte
- K2 處理中以 `RX_OK` 鎖住並丟棄新資料;K1 在同一次 `cmd_poll()` 內處理完，後續 byte 留在 Serial 緩衝(64 byte),不需要鎖
- 版本號 `VERSION0~3` 定義在 `uart_cmd.h`

### 4.5 `device_ctrl`

- `DeviceMap[26]`:K2 ID → K1 `PinMap` 索引
  - `DEV_NOPIN`:輸出裝置但 K1 沒有實體
  - `DEV_NONE`:感測器 ID
- `handleSW(type, id)`(同 K2):
  - `0` = 單一 ID 開
  - `1` = 遮罩開(已開的不重複開)
  - `2` = 遮罩關，並清除倒數
- `Status`(bit n = ID n 開著)、`Stop`(倒數到 0 待關)、`Counter1ms[26]`(`0x00` 剩餘 ms)
- `device_pin_readback()`:讀實際腳位，給 `0xB5 01` 用(K1 新增)

### 4.6 `timer_task`

| 函式 | 作用 |
|---|---|
| `timer1_SET_Count()` | 每 ID 倒數,到 0 且仍開著 → 記進 `Stop` |
| `timer1_pulse_count()` | 讀流量總計數,與上次相減得新增 pulse 數 `d` → 扣 `Pulse`;freeflow 中呼叫 `timer1_longPressure_flowcount(d)` |
| `pulse_flow_clear()` | `0x04` 歸零:先補處理尚未處理的 pulse,再歸零並同步「上次」計數 |
| `Tmr1_Interrupt_main()` | 關閉 `Stop` 與 `PulseStop` 的裝置;到量時送完成通知 |

K2 在流量計中斷內每個 pulse 扣 1;K1 不改共用的 `flowmeter`,改用「總計數這次 − 上次」,結果相同。

### 4.7 `freeflow`

| 項目 | 內容 |
|---|---|
| 資料 | `channel7_CMD[19]`:`id`、`acc`(尚未開的 ms)、`pulse_add_time`(每 pulse ms)、`mSec_100count`(開啟計時) |
| 旗標 | `freeFlow_Run`(0 = 一般、1 = freeflow)、`pump0_9count`(前幾筆是 slot)、`temp_main_freeflow`(UVC 遮罩) |
| 常數 | `ADD_MAX` 150、`ACC_MAX` 250、`PUMP_100mSec` 100、`FREEFLOW_MAX` 19 |
| `sort_by_speed()` | 依 ID 由小到大排序(名稱沿用 K2,實際比的是 ID) |
| `freeflow_stagger_start()` | acc 起點輪流 17 / 50 / 83(K1 新增,K2 v0.0.0.11 也已採用) |
| `freeflow_punpON()` | `acc ≥ 100` 開 slot;計時滿 100 ms 扣 100,剩 < 100 才關 |
| `timer1_longPressure_flowcount(d)` | 每 pulse `acc += pulse_add_time`(上限 250) |
| `timer1_longPressure_pumpCount()` | 開啟中的 slot 每 1 ms 計時 |
| `freeflow_Reset()` | `0x02` 時清空全部 |

K2 的氣泡水模式(`freeFlow_Run = 2`)與短脈衝偵測未移植。

### 4.8 `tx_queue`(K1 新增)

- `out`:繼承 Arduino `Print`,`out.write()` 放進 512 byte 佇列後立刻返回
- `tx_poll()`:`loop()` 每圈趁 `Serial.availableForWrite()` 有空位時搬資料
- 佇列空且 Serial 有空位時直接送;佇列滿時才等待(不丟資料)
- 規則：所有輸出都走 `out`,只在主迴圈使用

### 4.9 `diagnostics`、`pressure`

見 [hardware.md 第 5 節](hardware.md#5-輸入與指示)。

## 5. 變數存取規則

| 變數 | 寫入者 | 讀取者 | 保護 |
|---|---|---|---|
| `tick_cnt` | Timer5 ISR | 主迴圈 | `ATOMIC_BLOCK`(16-bit) |
| 流量計數 | Timer5 ISR | 主迴圈 | `ATOMIC_BLOCK`(32-bit) |
| 其他全部 | 主迴圈 | 主迴圈 | 不需要 |

新增功能時:

- 計時工作放進 `tick_main()`
- 不在 ISR 寫輸出或共用變數
- ISR 一定要寫的話，主迴圈對應的讀寫要用 `ATOMIC_BLOCK` 包起來

---

## 文件修改紀錄

| 版本 | 日期 | 改了什麼 |
|---|---|---|
| 0.0.4.0 | 2026-11-12 | 建立(`b74d5a3`) |
