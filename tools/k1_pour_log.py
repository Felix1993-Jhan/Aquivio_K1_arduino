"""K1 單筆出杯情境測試 — 內建情境自動判斷;也可送任意命令只看 log

適用韌體:情境的預期值以 d6ec6fa(步驟 5)為準;各情境的行為從哪一版開始見下表「起」欄
最後驗證:jsonfreeflow(2026-11-12)—— 除錯版(--flowgen)29 / 29 PASS;正式版預設情境(含 120 秒逾時)最後在 d6ec6fa

⚠ 會讓泵 / 閥 / slot 實際動作,空板測試用。

用法(Git Bash):
    python -I -X utf8 tools/k1_pour_log.py --port COM28                   # 正式版:跑全部適用情境(含 120 秒逾時)
    python -I -X utf8 tools/k1_pour_log.py --port COM28 --flowgen         # 除錯版 + D22 → A8:跑到量相關情境
    python -I -X utf8 tools/k1_pour_log.py --port COM28 --only zero_slot,zero_time
    python -I -X utf8 tools/k1_pour_log.py --list                         # 列出情境
    python -I -X utf8 tools/k1_pour_log.py --port COM28 --cmd '{"cold":"0","slot1":"2000"}'   # 只看 log,不判斷
    python -I -X utf8 tools/k1_pour_log.py --port COM28 --cmd '{"cold":0.1}' --stop-after 3

韌體版本:
    正式版  = 不帶 K1_DEBUG_FLOW_GEN;A8 沒有脈衝(到不了量)
    除錯版  = 編譯加 -DK1_DEBUG_FLOW_GEN,杜邦線 D22 → A8(泵開著時約 6 Hz,1 pulse = 3.3 ml)
    --flowgen 表示板子燒的是除錯版且已接線;不帶則視為正式版

情境(模式:正式 = 只在正式版跑、除錯 = 只在 --flowgen 跑、皆可):
    名稱             模式  起       內容 → 預期
    zero_slot        皆可  4001f2d  水量 "0" + slot1 2 s → 水開到 slot 關閉才停(約 2 s),finish = external stop
    zero_time        皆可  4001f2d  水量 "0" + time 3 s(上位機台幣 / 泰銖水量取整成 "0" 的情況)→ 約 3 s 停
    zero_both_time   皆可  4001f2d  水量 "0" + time 3 s + slot2 1 s → 以較長的 time 為準,約 3 s 停
    zero_both_slot   皆可  4001f2d  水量 "0" + time 1 s + slot2 2.5 s → 以較長的 slot 為準,約 2.5 s 停
    numeric          正式  4fc8663  {"cold":0.1}(數字格式)→ 讀成 0.10;2 秒後 stop
    numeric_flow     除錯  4fc8663  {"cold":0.1} → 第 31 個 pulse 到量停水,finish = flow pulse threshold
    finish_reprint   除錯  4001f2d  {"cold":"0.10","slot1":"500"} → 到量印一次用水量(31 pulse),
                                    收尾 FINISH 再印一次(pulse:0、0.000),行為 ③ 照舊
    timeout          正式  d456885  {"cold":"0.30","slot1":"500"},沒有脈衝 → 約 120 秒印
                                    ERROR: Execution timeout reached、finish = external stop、end
    「水量 0」的訂單不看流量計,只靠 slot / time 結束;同 mcu31v2(架構計畫 5.2)
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_harness import Board, Checks, first_t, show   # noqa: E402

FIN_EXT = "finish: stopped by system timeout or external stop"
FIN_FLOW = "finish: stopped by flow pulse threshold"
LATE_MS = 600          # 收尾時間的容許晚到(命令傳送 + 前面字串排隊)


def pulses(L):
    return [int(s[6:]) for _, s in L if s.startswith("pulse:")]


def usages(L):
    return [s.split(":")[1] for _, s in L if "water usage:" in s]


def finish(L):
    return [s for _, s in L if s.startswith("finish:")]


def check_zero(c, L, name, water, dur_ms):
    """水量 0:只在 FINISH 關水一次,關水時間 ≈ dur_ms"""
    req = f"{water.capitalize()} water flow requested: 0.00"
    c.check(f"{name} 讀成水量 0.00", any(s == req for _, s in L))
    p = pulses(L)
    c.check(f"{name} 水路只在收尾關一次", len(p) == 1, f"pulse 行 {len(p)} 次")
    t_off = first_t(L, lambda s: s.startswith("pulse:"))
    c.check(f"{name} 約 {dur_ms} ms 收尾", t_off is not None and dur_ms - 50 <= t_off <= dur_ms + LATE_MS,
            f"{t_off:.0f} ms" if t_off else "沒收到")
    c.check(f"{name} finish = external stop", finish(L) == [FIN_EXT], f"{finish(L)}")
    c.check(f"{name} 有 end", any(s == "end" for _, s in L))


def ck_zero_slot(c, L):
    check_zero(c, L, "zero_slot", "cold", 2000)
    t_s = first_t(L, lambda s: s == "Deactivated slot 1")
    t_p = first_t(L, lambda s: s.startswith("pulse:"))
    c.check("zero_slot slot1 關閉後才關水", t_s is not None and t_p is not None and t_s <= t_p)


def ck_zero_time(c, L):
    check_zero(c, L, "zero_time", "warm", 3000)
    c.check("zero_time 沒有 slot", not any(s.startswith("Activated") for _, s in L))


def ck_zero_both_time(c, L):
    check_zero(c, L, "zero_both_time", "cold", 3000)


def ck_zero_both_slot(c, L):
    check_zero(c, L, "zero_both_slot", "cold", 2500)


def ck_numeric(c, L):
    c.check("numeric 讀成 0.10", any(s == "Cold water flow requested: 0.10" for _, s in L))
    c.check("numeric Progress 目標 0.10", any(s.startswith("Progress") and s.endswith("Desired flow: 0.10")
                                              for _, s in L))
    c.check("numeric stop 後 end", any(s == "end" for _, s in L))


def ck_numeric_flow(c, L):
    c.check("numeric_flow 讀成 0.10", any(s == "Cold water flow requested: 0.10" for _, s in L))
    p = pulses(L)
    c.check("numeric_flow 第 31 pulse 到量", p[:1] == [31], f"pulse {p}")
    c.check("numeric_flow finish = flow", finish(L) == [FIN_FLOW], f"{finish(L)}")


def ck_finish_reprint(c, L):
    p, u = pulses(L), usages(L)
    c.check("finish_reprint 用水量印兩次(到量 31 → 收尾 0)", p == [31, 0], f"pulse {p}")
    c.check("finish_reprint 用水量 0.102 → 0.000", u == ["0.102", "0.000"], f"{u}")
    t_stop = first_t(L, lambda s: s.startswith("Stopping water pump"))
    t_p = first_t(L, lambda s: s.startswith("pulse:"))
    c.check("finish_reprint 先印 Stopping water pump", t_stop is not None and t_p is not None and t_stop <= t_p)
    c.check("finish_reprint finish = flow", finish(L) == [FIN_FLOW], f"{finish(L)}")


def ck_timeout(c, L):
    t_err = first_t(L, lambda s: s == "ERROR: Execution timeout reached")
    c.check("timeout 約 120 秒逾時", t_err is not None and 119_900 <= t_err <= 120_800,
            f"{t_err:.0f} ms" if t_err else "沒收到")
    c.check("timeout finish = external stop", finish(L) == [FIN_EXT], f"{finish(L)}")
    c.check("timeout 有 end", any(s == "end" for _, s in L))


# 名稱: (模式, 命令, 額外時間表, 最長秒數, 判斷函式)
SCENARIOS = {
    "zero_slot":      ("any",     b'{"cold":"0","slot1":"2000"}#',               [], 8,   ck_zero_slot),
    "zero_time":      ("any",     b'{"warm":"0","time":3000}#',                  [], 8,   ck_zero_time),
    "zero_both_time": ("any",     b'{"cold":"0","time":3000,"slot2":"1000"}#',   [], 8,   ck_zero_both_time),
    "zero_both_slot": ("any",     b'{"cold":"0","time":1000,"slot2":"2500"}#',   [], 8,   ck_zero_both_slot),
    "numeric":        ("release", b'{"cold":0.1}#',              [(2.0, b"stop#")], 4,   ck_numeric),
    "numeric_flow":   ("flowgen", b'{"cold":0.1}#',                              [], 15,  ck_numeric_flow),
    "finish_reprint": ("flowgen", b'{"cold":"0.10","slot1":"500"}#',             [], 15,  ck_finish_reprint),
    "timeout":        ("release", b'{"cold":"0.30","slot1":"500"}#',             [], 130, ck_timeout),
}


def main():
    ap = argparse.ArgumentParser(description="K1 單筆出杯情境測試")
    ap.add_argument("--port")
    ap.add_argument("--flowgen", action="store_true", help="板子是除錯版且已接 D22 → A8")
    ap.add_argument("--only", help="只跑指定情境,逗號分隔")
    ap.add_argument("--list", action="store_true", help="列出情境")
    ap.add_argument("--cmd", help="自由模式:送這筆命令(不用加 #),只印 log")
    ap.add_argument("--stop-after", type=float, help="自由模式:幾秒後送 stop#")
    ap.add_argument("--max", type=float, default=130, help="自由模式:最多收幾秒(預設 130,收到 end 提早結束)")
    args = ap.parse_args()

    mode = "flowgen" if args.flowgen else "release"
    if args.list:
        for name, (m, cmd, *_ ) in SCENARIOS.items():
            print(f"  {name:<15} {m:<8} {cmd.decode()}")
        return

    b = Board(args.port)
    try:
        if args.cmd:
            plan = [(0.0, args.cmd.encode() + b"#")]
            if args.stop_after:
                plan.append((args.stop_after, b"stop#"))
            show(b.run(plan, args.max, until_end=True), skip=())
            return

        names = args.only.split(",") if args.only else [n for n, v in SCENARIOS.items() if v[0] in ("any", mode)]
        c = Checks()
        for name in names:
            m, cmd, extra, max_s, fn = SCENARIOS[name]
            if m not in ("any", mode):
                print(f"\n── {name}:略過(需要{'--flowgen' if m == 'flowgen' else '正式版'})")
                continue
            print(f"\n── {name}  {cmd.decode()}")
            L = b.run([(0.0, cmd)] + extra, max_s, until_end=True)
            show([x for x in L if not x[1].startswith("Progress")])
            fn(c, L)
            b.run([(0, b"stop#")], 1.0)      # 下一個情境前確認全關
    finally:
        b.close()
    if not args.cmd:
        sys.exit(0 if c.summary() else 1)


if __name__ == "__main__":
    main()
