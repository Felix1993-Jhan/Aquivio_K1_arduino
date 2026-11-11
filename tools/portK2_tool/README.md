# K2 移植版測試腳本(`main-k2port-v4`)

K2 移植版改用 K2 UART 協定(`40 71 30` + CS、115200),上一層 `tools/` 的 K1 腳本(JSON + `#`、9600)不適用,測試另放這裡。討論與分階段見 `K2_aidata/K1_k2port_discussion.md`。

## 腳本

| 腳本 | 測什麼 | 韌體 | 接線 | 適用起點 | 最後驗證 |
|---|---|---|---|---|---|
| `k2p_uart_test.py` | 收包:標頭逐 byte 檢查(雜訊、重複 `40`、標頭斷一半)、CS 錯誤回 FF、半包逾時、黏包、回覆格式 | 正式 / 除錯 | — | P1 | P1:12 / 12 PASS |

## 執行

```bash
python -I -X utf8 tools/portK2_tool/k2p_uart_test.py --port COM28
```

開 port 時 Mega 會 auto-reset;本版開機不送 `ready`,腳本開 port 後固定等 2.5 秒再送。

## 維護規則

- 同上一層 `tools/README.md`:韌體改了全部重跑;行為刻意改變時舊判斷用註解保留並寫原因;腳本與韌體放同一個 commit;驗證後更新檔頭與上表「最後驗證」
- 每支腳本逐項印出 TX / 預期 / RX(hex),不只印總數
