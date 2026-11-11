# K2 移植版測試腳本(`main-k2port-v4`)

K2 移植版改用 K2 UART 協定(`40 71 30` + CS、115200),上一層 `tools/` 的 K1 腳本(JSON + `#`、9600)不適用,測試另放這裡。討論與分階段見 `K2_aidata/K1_k2port_discussion.md`。

## 腳本

| 腳本 | 測什麼 | 韌體 | 接線 | 適用起點 | 最後驗證 |
|---|---|---|---|---|---|
| `k2p_uart_test.py` | 收包:標頭逐 byte 檢查(雜訊、重複 `40`、標頭斷一半)、CS 錯誤回 FF、半包逾時、黏包、回覆格式 | 正式 / 除錯 | — | P1 | P3:12 / 12 PASS |
| `k2p_device_test.py` | `0x00` / `0x01` / `0x02` 開關與定時關閉、K1 無實體 ID、感測器 ID 忽略、`0xB5` 狀態查詢(`Status` 與實際腳位比對);LED 步驟停留讓人看板子(`--hold`)。⚠ 輸出腳會動作 | 正式 / 除錯 | — | P2 | P2:71 / 71 PASS(`0x00` 1000 ms 實測 1012 ms,含序列埠往返);另逐一 ID 開關 3 輪各 26 / 26 PASS,使用者目視確認 LED 依 ID 順序亮 <br>P3:71 / 71 PASS(1014 ms) |
| `k2p_sensor_test.py` | `0x03` 讀 Flow(ID18)與其他 ID 不支援格式、`0x04` 歸零、`0x08` / `0x09` / `0x0A` / `0xB0` 不支援格式、`0x08` / `0x07` 變長請求後對齊、`0x06` / `0x07` 暫回 PING | **除錯** | D22 → A8 | P3 | P3:43 / 43 PASS(水泵開 3 秒 = 19 pulse) |

## 執行

```bash
python -I -X utf8 tools/portK2_tool/k2p_uart_test.py --port COM28
python -I -X utf8 tools/portK2_tool/k2p_device_test.py --port COM28 --hold 3   # 約 40 秒,看著板子 LED
python -I -X utf8 tools/portK2_tool/k2p_sensor_test.py --port COM28              # 除錯版 + D22 → A8
```

開 port 時 Mega 會 auto-reset;本版開機不送 `ready`,腳本開 port 後固定等 2.5 秒再送。

## 維護規則

- 同上一層 `tools/README.md`:韌體改了全部重跑;行為刻意改變時舊判斷用註解保留並寫原因;腳本與韌體放同一個 commit;驗證後更新檔頭與上表「最後驗證」
- 每支腳本逐項印出 TX / 預期 / RX(hex),不只印總數
