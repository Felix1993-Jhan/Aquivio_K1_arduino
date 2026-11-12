# 建置、燒錄、測試

> 加入版本:0.0.4.0 / 最後修改:0.0.4.0(2026-11-12)
> 韌體 0.0.4.0(`main-k2port-v4`)。

## 1. 工具

| 工具 | 版本 | 位置 / 備註 |
|---|---|---|
| arduino-cli | 1.4.0 | `..\FirmwareTest_CS\arduino-cli.exe`(專案上一層,不在 PATH) |
| 核心 | arduino:avr 1.8.6 | FQBN `arduino:avr:mega:cpu=atmega2560` |
| 函式庫 | 無 | 本分支不使用 ArduinoJson(K1 優化版才用) |
| Python | 3.x + pyserial | 測試腳本 |

VS Code 會對 `avr/io.h`、`Arduino.h` 顯示紅字。那是 IDE 缺 include 路徑，以 arduino-cli 編譯結果為準。

## 2. 正式版 / 除錯版

| 版本 | 編譯旗標 | 差異 | 用途 |
|---|---|---|---|
| 正式版 | (無) | — | 上機 |
| 除錯版 | `-DK1_DEBUG_FLOW_GEN` | D22 輸出模擬流量計(水泵開著時約 6 Hz),以杜邦線接 A8 | 空板測試(沒有實體流量計) |

兩版都有 D23 心跳、`0xB5`、〔暫時〕D66 同步流量計。

## 3. 編譯與燒錄

在 `mcu/` 下執行:

```bash
# 查 COM port(每塊板子不同)
arduino-cli board list

# 正式版:編譯並燒錄
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 -u -p <COM> .

# 除錯版
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 \
  --build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN" -u -p <COM> .

# 只編譯不燒錄(看大小)
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 .
```

- **正式版 / 除錯版互相切換時加 `--clean`**,避免沿用上次的編譯結果
- 燒錄前先關掉上位機與任何序列埠工具(會佔住 COM port)
- 燒錄失敗出現 `stk500v2_getsync(): timeout`:先用 `board list` 確認板子還在
- 大小(0.0.4.0):正式版 Flash 6298 byte、RAM 1305 byte;除錯版 6374 / 1306 byte

## 4. 測試腳本

腳本在 `tools/portK2_tool/`,接線、適用版本與最後驗證結果見該資料夾的 `README.md`。上一層 `tools/` 是 K1 優化版(JSON 協定)的腳本，本分支不適用。

| 腳本 | 測什麼 | 韌體 |
|---|---|---|
| `k2p_uart_test.py` | 收包:標頭、CS 錯誤、逾時、黏包 | 正式 / 除錯 |
| `k2p_device_test.py` | `0x00~0x02` 開關與定時、無實體 ID、`0xB5`(Status 與實際腳位比對) | 正式 / 除錯 |
| `k2p_sensor_test.py` | `0x03` / `0x04`、不支援格式、變長封包對齊 | 除錯 + D22 → A8 |
| `k2p_pulse_test.py` | `0x06` 到量關閉與完成通知 | 除錯 + D22 → A8 |
| `k2p_freeflow_test.py` | `0x07` slot 開啟時間、UVC、N = 0、結束封包、N 上限;F9 隨機出杯 | 除錯 + D22 → A8 |

```bash
python -I -X utf8 tools/portK2_tool/k2p_uart_test.py --port <COM>
python -I -X utf8 tools/portK2_tool/k2p_freeflow_test.py --port <COM> --rounds 24 --seed 81398
```

規則:

- 每支腳本逐項印出 TX / 預期 / RX
- 韌體改了就全部重跑，驗證後更新腳本檔頭與 README 的「最後驗證」
- 腳本與它驗證的韌體放在同一個 commit
- 除了 `k2p_uart_test` 以外，腳本都會讓泵、閥實際動作，只在空板上跑
- 開 port 時板子會自動重開機，腳本開 port 後固定等 2.5 秒再送命令

## 5. 上機前

- 燒**正式版**,先關掉上位機
- 上位機要用 K2 的軟體(`aquivio-station`),K1 原本的上位機(JSON 協定)不能用
- 上位機的流量常數(`flowPulseConstant`)照 K2 設定;韌體只回 pulse 數

---

## 文件修改紀錄

| 版本 | 日期 | 改了什麼 |
|---|---|---|
| 0.0.4.0 | 2026-11-12 | 建立(`b74d5a3`) |
