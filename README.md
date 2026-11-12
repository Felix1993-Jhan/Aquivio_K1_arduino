# AQUIVIO K1 Firmware

**[中文](#中文) | [English](#english)**

## 兩個版本 / Two versions

K1 飲水機(Arduino Mega 2560)的韌體有兩個版本,各在自己的分支,文件也各自分開。
The K1 firmware (Arduino Mega 2560) has two versions, each on its own branch with its own documents.

| 版本 Version | 分支 Branch | 協定 Protocol | 技術文件 Docs |
|---|---|---|---|
| **K1 JSON 版 / K1 JSON**(本頁 this page) | `main-v4` | 文字命令 + JSON,`#` 結尾,9600 baud | [技術文件](ai_docs_k1json/技術文件.md) |
| K2 移植版 / K2 port | `main-k2port-v4` | K2 二進位封包 `40 71 30` + checksum,115200 baud | [技術文件](../main-k2port-v4/ai_docs/技術文件.md) · [README](../main-k2port-v4/README.md) |

---

## 中文

> 加入版本:v4.0 / 最後修改:v4.0(2026-11-12)

K1 JSON 版(分支 `main-v4`):沿用原 K1 上位機的文字 / JSON 命令,內部改寫為 1 ms tick、不阻塞的架構。

| 項目 | 內容 |
|------|------|
| MCU | ATmega2560(Arduino Mega 2560 R3,8-bit AVR,16 MHz)|
| 工具鏈 | arduino-cli 1.4.0 + arduino:avr 1.8.6 + ArduinoJson 7.1.0 |
| 對外介面 | USB(板上 16U2 轉 UART0)9600 baud,命令以 `#` 結尾 |
| 狀態 | 空板測試通過;**尚未上機驗證**;freeflow 格式〔暫定〕|

### 主要功能

- **出杯**:JSON 指定水量(冷水 / 常溫水)、最短時間、各 slot 開啟時間;到量停水
- **freeflow〔暫定〕**:按住出水、`stop#` 停止;slot 依實際流量按比例開啟,每 200 ms 回報累計 pulse
- **手動控制**:`startslotN`、`stopslotN`、`startcoldwater`、`startwarmwater`、`stop`
- **出杯中**:`stop`、`stopslotN`、`pressure` 立刻處理,其他命令暫存到結束後執行
- **感測**:流量計(1 ms 取樣 debounce,1 pulse = 3.3 ml)、A1 水壓 ADC(`pressure#`)
- **1 ms tick 架構**:Timer5 計時,工作在主迴圈補跑,不阻塞

命令與回覆細節見[技術文件](ai_docs_k1json/技術文件.md)。

### 目錄結構

```
mcu/              韌體(Arduino sketch,mcu.ino + 模組)
tools/            空板測試腳本(Python),說明見 tools/README.md
ai_docs_k1json/   技術文件
old/              公司倉舊版程式(main31、mcu31v2),對照用
```

### 建置與燒錄

在 `mcu/` 下執行:

```bash
# 正式版(上機)
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 -u -p <COM> .
# 除錯版(空板測試):D22 輸出模擬流量計,杜邦線接 A8
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 --build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN" -u -p <COM> .
```

- 正式版 / 除錯版互相切換時加 `--clean`;燒錄前先關掉上位機

---

## English

> Added in: v4.0 / Last modified: v4.0 (2026-11-12)

K1 JSON (branch `main-v4`): keeps the original K1 host's text / JSON commands; internally rewritten around a non-blocking 1 ms tick.

| Item | Value |
|------|------|
| MCU | ATmega2560 (Arduino Mega 2560 R3, 8-bit AVR, 16 MHz) |
| Toolchain | arduino-cli 1.4.0 + arduino:avr 1.8.6 + ArduinoJson 7.1.0 |
| Interface | USB (on-board 16U2 to UART0) 9600 baud, commands end with `#` |
| Status | Bench-tested; **not yet verified on the machine**; freeflow format is provisional |

### Features

- **Pour**: JSON gives water volume (cold / ambient), minimum time and per-slot open time; stops when the volume is reached
- **Freeflow (provisional)**: dispense while held, stop with `stop#`; slots open in proportion to the measured flow; cumulative pulse count reported every 200 ms
- **Manual control**: `startslotN`, `stopslotN`, `startcoldwater`, `startwarmwater`, `stop`
- **During a pour**: `stop`, `stopslotN`, `pressure` are handled at once; other commands run after it ends
- **Sensors**: flowmeter (1 ms sampling with debounce, 1 pulse = 3.3 ml), A1 pressure ADC (`pressure#`)
- **1 ms tick**: Timer5 counts every 1 ms; work runs in the main loop with catch-up, never blocking

Command and reply details: [technical document](ai_docs_k1json/技術文件.md) (in Chinese).

### Build and flash

Run inside `mcu/`; add `--build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN"` for the debug build (D22 outputs a simulated flowmeter signal, jumper to A8). Add `--clean` when switching between release and debug builds. Close the host software before flashing.

---

## 文件修改紀錄 / Document history

| 版本 / Version | 日期 / Date | 改了什麼 / Changes |
|---|---|---|
| v4.0 | 2026-11-12 | 建立:兩個版本總覽、K1 JSON 版說明(中英雙語),取代原本只有標題的 `# mcu` / Created: overview of both versions and the K1 JSON description (bilingual), replacing the title-only `# mcu` |
