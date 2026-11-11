# K2 移植版測試腳本(`main-k2port-v4`)

K2 移植版改用 K2 UART 協定(`40 71 30` + CS、115200),上一層 `tools/` 的 K1 腳本(JSON + `#`、9600)不適用,測試另放這裡。討論與分階段見 `K2_aidata/K1_k2port_discussion.md`。

## 腳本

| 腳本 | 測什麼 | 韌體 | 接線 | 適用起點 | 最後驗證 |
|---|---|---|---|---|---|
| `k2p_uart_test.py` | 收包:標頭逐 byte 檢查(雜訊、重複 `40`、標頭斷一半)、CS 錯誤回 FF、半包逾時、黏包、回覆格式 | 正式 / 除錯 | — | P1 | P5:12 / 12 PASS |
| `k2p_device_test.py` | `0x00` / `0x01` / `0x02` 開關與定時關閉、K1 無實體 ID、感測器 ID 忽略、`0xB5` 狀態查詢(`Status` 與實際腳位比對);LED 步驟停留讓人看板子(`--hold`)。⚠ 輸出腳會動作 | 正式 / 除錯 | — | P2 | P2:71 / 71 PASS(`0x00` 1000 ms 實測 1012 ms,含序列埠往返);另逐一 ID 開關 3 輪各 26 / 26 PASS,使用者目視確認 LED 依 ID 順序亮 <br>P5:71 / 71 PASS |
| `k2p_sensor_test.py` | `0x03` 讀 Flow(ID18)與其他 ID 不支援格式、`0x04` 歸零、`0x08` / `0x09` / `0x0A` / `0xB0` 不支援格式、`0x08` / `0x07` 變長請求後對齊(`0x07` 用不支援的 Type 2,不動作) | **除錯** | D22 → A8 | P3 | P5:42 / 42 PASS |
| `k2p_pulse_test.py` | `0x06` SET_PULSE:冷水 / 常溫到量關閉與主動完成通知、時間與流量計數、不支援 Type、pulse 0 不自動停、出水中 `0x04` 歸零、`0x02` 提早停止;P7 重現殘留 Pulse 問題(K2 行為,討論稿 4.1) | **除錯** | D22 → A8 | P4 | P4:25 / 25 PASS(12 pulse = 1.82 秒;殘值 3.66 秒後自動關)<br>P5:25 / 25 PASS |
| `k2p_freeflow_test.py` | `0x07` freeflow:slot 開啟時間 ≈ pulse 數 × 時間值(以 `0xB5` 每約 4 ms 輪詢量測)、時間值上限 150、多組排序與 UVC、`N = 0` 停料只出水、結束封包(`0x02` bit0~17)重置、不支援 Type、`N` 上限 19;F9 隨機出杯(冷 / 常溫、4~6 個 slot 1:10 / 1:32、帶 UVC、杯量 250 / 330 / 500 ml 到量才停、每杯隨機按 1~3 次(中斷),`--rounds` / `--seed` / `--ml-per-pulse`) | **除錯** | D22 → A8 | P5 | P5:F1~F8 + F9 24 杯共 537 / 537 PASS(按 1 次 slot 平均 −2.8%、有中斷平均 −4.7%、最差 −16.5%;F1~F3 時間值 50 → 30%、100 → 59%、255 限為 150 → 89%) |

## 執行

```bash
python -I -X utf8 tools/portK2_tool/k2p_uart_test.py --port COM28
python -I -X utf8 tools/portK2_tool/k2p_device_test.py --port COM28 --hold 3   # 約 40 秒,看著板子 LED
python -I -X utf8 tools/portK2_tool/k2p_sensor_test.py --port COM28              # 除錯版 + D22 → A8
python -I -X utf8 tools/portK2_tool/k2p_pulse_test.py --port COM28               # 除錯版 + D22 → A8,約 30 秒
python -I -X utf8 tools/portK2_tool/k2p_freeflow_test.py --port COM28            # 除錯版 + D22 → A8;F9 預設 5 輪約 2.5 分鐘(20 輪約 7 分鐘)
```

開 port 時 Mega 會 auto-reset;本版開機不送 `ready`,腳本開 port 後固定等 2.5 秒再送。

## 維護規則

- 同上一層 `tools/README.md`:韌體改了全部重跑;行為刻意改變時舊判斷用註解保留並寫原因;腳本與韌體放同一個 commit;驗證後更新檔頭與上表「最後驗證」
- 每支腳本逐項印出 TX / 預期 / RX(hex),不只印總數
