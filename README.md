# AQUIVIO K1 Firmware — K2 移植版 / K2 Port

**[中文](#中文) | [English](#english)**

---

## 中文

> 加入版本:0.0.4.0 / 最後修改:0.0.4.0(2026-11-12)

AQUIVIO K1 飲水機的 Arduino Mega 2560 韌體，**K2 移植版**(分支 `main-k2port-v4`):通訊改用 K2 的 UART 協定，上位機可沿用 K2 的命令與裝置 ID。

| 項目 | 內容 |
|------|------|
| 韌體版本 | **0.0.4.0**(定義在 `mcu/uart_cmd.h` 的 `VERSION0~3`,`0x05` PING 回傳)|
| MCU | ATmega2560(Arduino Mega 2560 R3,8-bit AVR,16 MHz,256 KB Flash,8 KB SRAM)|
| 工具鏈 | arduino-cli 1.4.0 + arduino:avr 1.8.6 |
| 對外介面 | USB(板上 16U2 轉 UART0)115200 bps,固定標頭 `40 71 30` + checksum(同 K2)|
| 狀態 | 功能完成、空板測試通過;**尚未上機驗證** |

### 系統定位

K1 由**上位機**(觸控螢幕軟體)與 **Arduino Mega 2560(本專案)**組成。上位機負責介面、配方與水量換算;Mega 2560 負責泵、閥、UVC 的開關與流量計計數，兩者以 USB 序列埠溝通。

本分支與 K1 的另一個版本並存:

| 版本 | 分支 | 協定 |
|------|------|------|
| K1 優化版 | `main-v4` | 原 K1 協定(JSON + `#`、9600),搭配 K1 上位機 |
| **K2 移植版(本分支)** | `main-k2port-v4` | K2 UART 協定(115200),搭配 K2 上位機 |

### 主要功能

- **19 個輸出裝置 ID(同 K2)**,K1 有實體的 15 個:10 個 slot(營養液泵)、水泵、常溫 / 冷水閥、UVC × 2(ID12 / 13);沒有實體的 ID(11、16、17、25)只記狀態
- **感測器**:流量計(1 ms 取樣 debounce,同 K2)
- **出水模式**:定時開關(`0x00`)/ 流量脈衝計量(`0x06`)/ 常壓連續出水 freeflow(`0x07`,常溫・冷水,slot 依流量開啟)
- **1 ms tick 架構**:Timer5 每 1 ms 計時，工作在主迴圈補跑，不阻塞
- **K1 自訂 `0xB5` 狀態查詢**:讀輸出狀態與實際腳位(測試用)

### UART 命令

| 代碼 | 名稱 | K1 |
|:---:|------|------|
| `0x00` | SWITCH_SET | ✅ 開啟單一裝置指定時間 |
| `0x01` / `0x02` | SWITCH_START / STOP | ✅ 以位元遮罩批次開 / 關(`0x02` 同時結束 freeflow)|
| `0x03` / `0x04` | SENSOR_READ / CLEAR | ✅ 只有 ID18 流量;其他 ID 回不支援格式 |
| `0x05` | PING | ✅ 回 `0.0.4.0` |
| `0x06` | SET_PULSE | ✅ 依流量脈衝數出水(常溫 / 冷水)|
| `0x07` | LONG_PRESSURE | ✅ freeflow(常溫 / 冷水),UVC 一律開 |
| `0x08` / `0x09` / `0x0A` / `0xB0` | ERROR_REPORT / MAX30102 / WATER_MODE / BOOTLOADER | ❌ 回不支援格式(K1 沒有對應硬體)|
| `0xB5` | STATUS_QUERY | ⚠ K1 自訂,K2 沒有 |

封包格式與各命令細節見 [protocol.md](ai_docs/docs/protocol.md);與 K2 的差異見 [k2_diff.md](ai_docs/docs/k2_diff.md)。

### 目錄結構

```
mcu/                 韌體(Arduino sketch,mcu.ino + 10 個模組)
tools/portK2_tool/   本分支的實機測試腳本(Python)
tools/               K1 優化版(main-v4)的測試腳本,本分支不適用
old/                 公司倉舊版程式(main31、mcu31v2),對照用
ai_docs/             技術文件
```

`mcu/*.hex` 是公司倉 2024 年的匯出檔，不是本版韌體。

### 建置與燒錄

在 `mcu/` 下執行(arduino-cli 不在 PATH,位於 `..\FirmwareTest_CS\arduino-cli.exe`):

```bash
# 正式版
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 -u -p <COM> .
# 除錯版:D22 輸出模擬流量計(杜邦線接 A8)
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 --build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN" -u -p <COM> .
```

- COM port 每塊板子不同，用 `arduino-cli board list` 查
- 正式版 / 除錯版互相切換時加 `--clean`
- 燒錄前先關掉上位機(會佔住 COM port)
- 開 port 時板子會自動重開機(16U2 的 DTR 接 RESET),開機約 2 秒後才收得到命令

完整流程與測試見 [tooling.md](ai_docs/docs/tooling.md)。

### 文件

| 要找什麼 | 文件 |
|------|------|
| **總覽與索引(從這裡開始)** | [ai_docs/技術文件.md](ai_docs/技術文件.md) |
| 功能狀態、版本紀錄 | [changelog.md](ai_docs/docs/changelog.md) |
| UART 命令格式 | [protocol.md](ai_docs/docs/protocol.md) |
| 腳位 / Timer / ADC | [hardware.md](ai_docs/docs/hardware.md) |
| 模組架構 | [modules.md](ai_docs/docs/modules.md) |
| 與 K2 韌體的差異 | [k2_diff.md](ai_docs/docs/k2_diff.md) |
| 建置、燒錄、測試 | [tooling.md](ai_docs/docs/tooling.md) |

### 版本

| 版本 | 重點 |
|------|------|
| 0.0.4.0 | K2 移植版:K2 UART 協定、`0x00~0x07`、freeflow(slot 錯開開啟、UVC 一律開)、`0xB5` 狀態查詢 |

完整紀錄見 [changelog.md](ai_docs/docs/changelog.md)。

---

## English

> Added in: 0.0.4.0 / Last modified: 0.0.4.0 (2026-11-12)

Arduino Mega 2560 firmware for the AQUIVIO K1 water dispenser — **K2 port** (branch `main-k2port-v4`). Communication uses the K2 UART protocol, so the host can reuse K2 commands and device IDs.

| Item | Value |
|------|------|
| Firmware version | **0.0.4.0** (`VERSION0~3` in `mcu/uart_cmd.h`, returned by `0x05` PING) |
| MCU | ATmega2560 (Arduino Mega 2560 R3, 8-bit AVR, 16 MHz, 256 KB Flash, 8 KB SRAM) |
| Toolchain | arduino-cli 1.4.0 + arduino:avr 1.8.6 |
| Interface | USB (on-board 16U2 to UART0) 115200 bps, fixed header `40 71 30` + checksum (same as K2) |
| Status | Feature complete, bench-tested; **not yet verified on the machine** |

### System role

K1 consists of the **host** (touch-screen software) and the **Arduino Mega 2560 (this project)**. The host handles the UI, recipes and volume conversion; the Mega 2560 switches pumps, valves and UVC and counts flowmeter pulses. They talk over the USB serial port.

This branch coexists with the other K1 version:

| Version | Branch | Protocol |
|------|------|------|
| K1 optimized | `main-v4` | Original K1 protocol (JSON + `#`, 9600), for the K1 host |
| **K2 port (this branch)** | `main-k2port-v4` | K2 UART protocol (115200), for the K2 host |

### Features

- **19 output device IDs (same as K2)**, 15 of them with K1 hardware: 10 slots (nutrient pumps), water pump, ambient / cold valves, UVC × 2 (ID12 / 13); the others (11, 16, 17, 25) only track status
- **Sensor**: flowmeter (1 ms sampling with debounce, same as K2)
- **Dispense modes**: timed switch (`0x00`) / flow-pulse metering (`0x06`) / continuous freeflow (`0x07`, ambient and cold; slots open according to flow)
- **1 ms tick**: Timer5 counts every 1 ms; work runs in the main loop with catch-up, never blocking
- **K1-specific `0xB5` status query**: reads output status and actual pin levels (for testing)

### UART commands

| Code | Name | K1 |
|:---:|------|------|
| `0x00` | SWITCH_SET | ✅ Turn one device on for a given time |
| `0x01` / `0x02` | SWITCH_START / STOP | ✅ Bit-mask on / off (`0x02` also ends freeflow) |
| `0x03` / `0x04` | SENSOR_READ / CLEAR | ✅ ID18 flow only; other IDs return the unsupported format |
| `0x05` | PING | ✅ Returns `0.0.4.0` |
| `0x06` | SET_PULSE | ✅ Dispense by flow pulse count (ambient / cold) |
| `0x07` | LONG_PRESSURE | ✅ Freeflow (ambient / cold); UVC always on |
| `0x08` / `0x09` / `0x0A` / `0xB0` | ERROR_REPORT / MAX30102 / WATER_MODE / BOOTLOADER | ❌ Unsupported-format reply (no such hardware on K1) |
| `0xB5` | STATUS_QUERY | ⚠ K1-specific, not in K2 |

Packet format and command details: [protocol.md](ai_docs/docs/protocol.md). Differences from K2: [k2_diff.md](ai_docs/docs/k2_diff.md).

### Repository layout

```
mcu/                 Firmware (Arduino sketch: mcu.ino + 10 modules)
tools/portK2_tool/   Bench test scripts for this branch (Python)
tools/               Test scripts for the K1 optimized version (main-v4); not for this branch
old/                 Old company-repo code (main31, mcu31v2) for reference
ai_docs/             Technical documents
```

`mcu/*.hex` are 2024 exports from the company repo, not this firmware.

### Build and flash

Run inside `mcu/` (arduino-cli is not on PATH; it is at `..\FirmwareTest_CS\arduino-cli.exe`):

```bash
# Release build
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 -u -p <COM> .
# Debug build: D22 outputs a simulated flowmeter signal (jumper to A8)
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 --build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN" -u -p <COM> .
```

- The COM port differs per board; check with `arduino-cli board list`
- Add `--clean` when switching between release and debug builds
- Close the host software before flashing (it holds the COM port)
- Opening the port resets the board (16U2 DTR is wired to RESET); commands are accepted about 2 s after boot

Full procedure and tests: [tooling.md](ai_docs/docs/tooling.md).

### Documentation

Technical documents are in Chinese.

| Topic | Document |
|------|------|
| **Overview and index (start here)** | [ai_docs/技術文件.md](ai_docs/技術文件.md) |
| Feature status, version history | [changelog.md](ai_docs/docs/changelog.md) |
| UART command format | [protocol.md](ai_docs/docs/protocol.md) |
| Pins / timer / ADC | [hardware.md](ai_docs/docs/hardware.md) |
| Module architecture | [modules.md](ai_docs/docs/modules.md) |
| Differences from the K2 firmware | [k2_diff.md](ai_docs/docs/k2_diff.md) |
| Build, flash, test | [tooling.md](ai_docs/docs/tooling.md) |

### Versions

| Version | Highlights |
|------|------|
| 0.0.4.0 | K2 port: K2 UART protocol, `0x00~0x07`, freeflow (staggered slot start, UVC always on), `0xB5` status query |

Full history: [changelog.md](ai_docs/docs/changelog.md).

---

## 文件修改紀錄 / Document history

| 版本 / Version | 日期 / Date | 改了什麼 / Changes |
|---|---|---|
| 0.0.4.0 | 2026-11-12 | 建立:K2 移植版說明(中英雙語),取代原本只有標題的 `# mcu` / Created: K2 port description (bilingual), replacing the title-only `# mcu` |
