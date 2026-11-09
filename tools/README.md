# K1 測試腳本

在空板(Mega 2560)上以序列埠對 `mcu/` 韌體做實機測試。每支腳本的預期值對應某一版韌體，檔頭與下表都寫明「從哪版起適用」與「最後在哪版驗證過」。

## 腳本

| 腳本 | 測什麼 | 韌體 | 接線 | 適用起點 | 最後驗證 |
|---|---|---|---|---|---|
| `k1_serial_test.py` | `mcuAlive` 間隔、`ping` → `pong`(不動泵閥) | 正式 / 除錯 | — | `09802ba`(步驟 2b) | `d6ec6fa`:平均 1000.0 ms、pong 12 ms |
| `k1_cmd_parse_test.py` | 黏包、超長丟棄、255 字元上限、待機 `stop` | 正式 / 除錯 | — | `02c0475`(4a);255 上限 `3627cb6` | `d6ec6fa`:8 / 8 PASS |
| `k1_pour_cmd_test.py` | 出杯中 `stop` / `stopslotN` 立刻、其他命令 `end` 後執行、下一杯 JSON 暫存 | 正式 / 除錯 | — | `d6ec6fa`(步驟 5) | `d6ec6fa`:11 / 11 PASS |
| `k1_pour_log.py` | 單筆出杯情境：水量 0 + slot / time、數字格式水量、120 秒逾時、到量後再印用水量;也可送任意命令只看 log | 正式;到量情境用除錯 + `--flowgen` | 到量情境 D23 → A8 | 各情境見檔頭 | `d6ec6fa`:正式 28 / 28、除錯 7 / 7 PASS |
| `k1_order_test.py` | 隨機訂單、slot 開啟順序與時間、出杯中穿插命令 | 正式 | — | `4001f2d`(2d);預期值 `d6ec6fa` 改版 | `d6ec6fa`:seed 3 |
| `k1_volume_test.py` | 模擬流量計到量停水、配比換算、slot 關閉時間 | 除錯 | D23 → A8 | `4fc8663`(H4);預期值 `d6ec6fa` 改版 | `d6ec6fa`:seed 11 6 / 6 PASS |
| `k1_pressure_test.py` | `pressure#`:開機尚無資料回 `null`、待機讀值、出杯中立刻回 | 正式 / 除錯 | A1 接待測電壓(空板目前 5 V → 10 kΩ → A1,讀 1023) | 步驟 6 | 步驟 6:7 / 7 PASS(`--expect 1023`) |
| `k1_harness.py` | 共用模組(開 port、時間表送命令、PASS / FAIL),本身不是測試 | — | — | — | — |

- **正式版**:不帶 `K1_DEBUG_FLOW_GEN` 編譯。A8 沒有脈衝，出水到不了量。表中標「正式」的測試，也可以用除錯版拔掉 D23 → A8 杜邦線來跑(A8 同樣沒有脈衝)
- **除錯版**:編譯加 `--build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN"`,D23 在泵開著時輸出約 6 Hz 方波模擬流量計，以杜邦線接 A8。空板測試期間燒錄一律用除錯版(使用者 2026-11-09);原始碼預設仍為關閉
- 除錯版接著杜邦線時，只有 `k1_pour_log` 的 `timeout` 情境需要先拔線(其他「正式」測試不看流量，或在到量前就 `stop`)

## 執行

```bash
python -I -X utf8 tools/k1_cmd_parse_test.py --port COM28
python -I -X utf8 tools/k1_pour_cmd_test.py --port COM28
python -I -X utf8 tools/k1_pour_log.py --port COM28              # 正式版(或除錯版拔線),約 2.5 分鐘(含逾時)
python -I -X utf8 tools/k1_pour_log.py --port COM28 --flowgen    # 除錯版
python -I -X utf8 tools/k1_pressure_test.py --port COM28 --expect 1023
python -I -X utf8 tools/k1_serial_test.py --port COM28
python -I -X utf8 tools/k1_order_test.py --port COM28 --seed 3
python -I -X utf8 tools/k1_volume_test.py --port COM28 --seed 11  # 除錯版
```

除了 `k1_serial_test.py`,其他都會讓泵 / 閥 / slot 實際動作，只在空板上跑。開 port 時 Mega 會 auto-reset,腳本收到 `ready` 後才送命令。

## 維護規則

- 韌體改了以後，上表的腳本全部重跑，確認原本正常的沒被改壞
- 新的測項加進同範圍的腳本;新範圍(例：步驟 6 的水壓)另開一支
- 行為刻意改變、預期值跟著改時：舊的判斷用註解保留，開頭寫原因與 commit;腳本和韌體放同一個 commit
- 每次驗證後更新檔頭與上表的「最後驗證」
