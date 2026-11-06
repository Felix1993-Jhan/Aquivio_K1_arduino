"""K1 隨機訂單測試 — 出杯 JSON、手動出水,並在出杯中穿插其他命令

⚠ 會讓泵 / 閥 / slot 實際動作。執行前確認機台狀態。

用法(Git Bash):
    python -I -X utf8 tools/k1_order_test.py --dry-run           # 只列出會送的訂單,不開 port
    python -I -X utf8 tools/k1_order_test.py --port COM27        # 執行(預設 6 筆,seed 1)
    python -I -X utf8 tools/k1_order_test.py --orders 10 --seed 7

訂單種類(隨機):
    JSON    格式同上位機 routes.cjs:數值為字串,可帶 "time"(毫秒,最短出水時間)
            1~4 個 slot,每個 0.2~3.0 秒;水路:無 / cold / warm,約一半帶 time(1~5 秒)
            另有約 2 成「純水」訂單:只有水 + time、沒有 slot(台幣水量取整 → "0" 的情況)
            水量一律 "0" = 不看流量。若送 >0 的水量,流量計沒有脈衝時會卡到 120 秒逾時
            (--water 可指定,預設 0)
    手動    startcoldwater# / startwarmwater# → 隨機 1~10 秒 → stop#
出杯中穿插(隨機):ping# / stop# / startslotN# / stopslotN#
安全:插過 startslotN# 的訂單結束後補送 stop#;腳本結束或中斷時一定送 stop#

時間戳是 PC 收到每一行的時間,含 9600 baud 傳輸與 USB 延遲,slot 實開時間誤差約 ±50 ms。
"""

import argparse
import json
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_serial_test import LineReader, find_port   # noqa: E402

import serial  # noqa: E402

ORDER_GAP_S = 1.5          # 每筆訂單之間的間隔
END_TIMEOUT_MS = 130_000   # 等 end 的上限(韌體逾時 120 秒 + 餘裕)


# ─────────────────────────── 產生訂單 ───────────────────────────

def make_orders(n, rng, water_value):
    orders = []
    for _ in range(n):
        if rng.random() < 0.3:
            temp = rng.choice(["cold", "warm"])
            orders.append({
                "kind": "manual", "temp": temp,
                "hold_s": round(rng.uniform(1.0, 10.0), 1),
                "inject": ["ping"] if rng.random() < 0.5 else [],
            })
            continue

        # 格式同上位機 routes.cjs translateOrder:數值為字串、可帶 "time"(毫秒,最短出水時間)
        payload = {}
        if rng.random() < 0.2:
            # 純水訂單(台幣 / 泰銖時水量取 0 位小數 → "0",靠 time 控制出水時間)
            water = rng.choice(["cold", "warm"])
            payload[water] = f"{water_value:g}"
            payload["time"] = rng.randrange(1000, 5001, 100)
            orders.append({"kind": "json", "payload": payload, "inject": []})
            continue
        slots = sorted(rng.sample(range(1, 11), rng.randint(1, 4)))
        water = rng.choice([None, "cold", "warm"])
        if water:
            payload[water] = f"{water_value:g}"
            if rng.random() < 0.5:
                payload["time"] = rng.randrange(1000, 5001, 100)
        for s in slots:
            payload[f"slot{s}"] = str(rng.randrange(200, 3001, 100))

        # 出杯中穿插的命令:0~2 個,送出時間 = 出杯開始後隨機 0.3~3 秒
        pool = ["ping", "stop", f"startslot{rng.randint(1, 10)}", f"stopslot{rng.choice(slots)}"]
        inject = rng.sample(pool, rng.choice([0, 1, 1, 2]))
        orders.append({
            "kind": "json", "payload": payload,
            "inject": [(round(rng.uniform(0.3, 3.0), 1), c) for c in inject],
        })
    return orders


def describe(o):
    if o["kind"] == "manual":
        inj = "、中途 ping" if o["inject"] else ""
        return f"手動 start{o['temp']}water → {o['hold_s']} 秒 → stop{inj}"
    inj = ",".join(f"{c}@{t}s" for t, c in sorted(o["inject"])) or "無"
    return f"JSON {json.dumps(o['payload'], separators=(',', ':'))}  穿插:{inj}"


# ─────────────────────────── 執行 ───────────────────────────

class Runner:
    def __init__(self, ser):
        self.ser = ser
        self.t0 = time.perf_counter()
        self.rd = LineReader(ser, self.t0)

    def now(self):
        return (time.perf_counter() - self.t0) * 1000.0

    def send(self, cmd):
        self.ser.write((cmd + "#").encode())
        return self.now()

    def idle(self, seconds):
        end = self.now() + seconds * 1000.0
        while self.now() < end:
            self.rd.pump()

    def wait_line(self, pred, start_idx, timeout_ms):
        end = self.now() + timeout_ms
        while self.now() < end:
            self.rd.pump()
            for i in range(start_idx, len(self.rd.lines)):
                if pred(self.rd.lines[i][1].strip()):
                    return i
        return None


def run_json(r, o):
    idx0 = len(r.rd.lines)
    payload = json.dumps(o["payload"], separators=(",", ":"))
    t_send = r.send(payload)
    pending = sorted(o["inject"])
    sent_inject = []
    end_idx = None
    deadline = r.now() + END_TIMEOUT_MS
    while r.now() < deadline:
        r.rd.pump()
        if pending and r.now() - t_send >= pending[0][0] * 1000.0:
            _, c = pending.pop(0)
            sent_inject.append((r.send(c), c))
        end_idx = next((i for i in range(idx0, len(r.rd.lines))
                        if r.rd.lines[i][1].strip() == "end"), None)
        if end_idx is not None and not pending:
            break
    r.idle(1.2)                                   # end 後多收 1.2 秒:看排隊命令的反應與 mcuAlive
    return t_send, idx0, end_idx, sent_inject


def run_manual(r, o):
    idx0 = len(r.rd.lines)
    t_send = r.send(f"start{o['temp']}water")
    sent_inject = []
    if o["inject"]:
        r.idle(o["hold_s"] / 2)
        sent_inject.append((r.send("ping"), "ping"))
        r.idle(o["hold_s"] / 2)
    else:
        r.idle(o["hold_s"])
    t_stop = r.send("stop")
    r.idle(1.0)
    return t_send, t_stop, idx0, sent_inject


# ─────────────────────────── 報告 ───────────────────────────

def report_json(r, o, t_send, idx0, end_idx, sent_inject):
    lines = r.rd.lines
    seg = lines[idx0:end_idx + 1] if end_idx is not None else lines[idx0:]
    after = lines[end_idx + 1:] if end_idx is not None else []
    t_start = next((t for t, s in seg if s.strip() == "start"), None)
    t_end = lines[end_idx][0] if end_idx is not None else None

    print(f"  送出 → start {t_start - t_send:.0f} ms" if t_start else "  ⚠ 沒收到 start")
    if t_end is None:
        print("  ⚠ 沒收到 end(逾時)")
    else:
        print(f"  送出 → end   {t_end - t_send:.0f} ms")

    # 出水:Cold/Warm water flow requested → 之後第一個 water usage(停水時印)
    if "time" in o["payload"]:
        t_w = next((t for t, s in seg if "water flow requested" in s), None)
        t_u = next((t for t, s in seg if "water usage:" in s and t_w and t >= t_w), None)
        if t_w and t_u:
            print(f"  出水 {t_u - t_w:.0f} ms(time = {o['payload']['time']} ms,應 ≥ time)")
    for t, s in seg:
        if s.startswith("finish:") or s.startswith("Stopping water pump"):
            print(f"  {s.strip()}")

    # slot 實開時間:Activated → 之後第一個 Deactivated
    slots = sorted((int(k[4:]), int(float(v))) for k, v in o["payload"].items() if k.startswith("slot"))
    n = len(slots)
    # 〔步驟 5〕韌體開啟順序:時間由長到短(相同時編號大的先)、間隔 250 ms、準時關閉
    order = sorted(slots, key=lambda x: (-x[1], -x[0]))
    old_k = {num: k for k, (num, _) in enumerate(sorted(slots, key=lambda x: -x[0]), start=1)}
    print(f"  {'slot':>6} {'設定':>6} {'實開':>6} {'新版應':>6} {'舊版會開':>8}")
    for num, dur in order:
        t_on = next((t for t, s in seg if s.startswith(f"Activated slot {num} ")), None)
        t_off = next((t for t, s in seg if s.strip() == f"Deactivated slot {num}" and t_on and t >= t_on), None)
        old_predict = max(dur, (n - old_k[num] + 1) * 500)   # 舊版:編號大 → 小、各等 500 ms、期間不檢查
        real = f"{t_off - t_on:6.0f}" if t_on and t_off else "     ?"
        print(f"  {num:>6} {dur:>6} {real} {dur:>6} {old_predict:>8}")
    seen = [int(s.split()[2]) for _, s in seg if s.startswith("Activated slot ")]
    if slots:
        print(f"  開啟順序 {seen}(應 {[num for num, _ in order]})")

    alive_in = sum(1 for _, s in seg if s.strip() == "mcuAlive")
    print(f"  出杯中 mcuAlive:{alive_in} 次(預期 0)")
    t_alive = next((t for t, s in after if s.strip() == "mcuAlive"), None)
    if t_end and t_alive:
        print(f"  end → 第一個 mcuAlive:{t_alive - t_end:.0f} ms")

    for tag in ("ERROR", "Failed to parse", "WARNING", "New lowest memory"):
        for t, s in lines[idx0:]:
            if s.startswith(tag):
                print(f"  ! {s}")

    if sent_inject:
        print("  穿插命令:" + "、".join(f"{c}(出杯開始後 {t - t_send:.0f} ms)" for t, c in sent_inject))
        print("  end 之後收到:")
        for t, s in after:
            if s.strip() not in ("mcuAlive", ""):
                print(f"    +{t - t_end:6.0f} ms  {s}")


def report_manual(r, o, t_send, t_stop, idx0, sent_inject):
    seg = r.rd.lines[idx0:]
    print(f"  實際開水 {(t_stop - t_send) / 1000:.1f} 秒(start → stop 送出)")
    for t, s in seg:
        if s.strip() and s.strip() != "mcuAlive":
            print(f"    {t - t_send:7.0f} ms  {s}")
    alive = [t for t, s in seg if s.strip() == "mcuAlive" and t < t_stop]
    print(f"  開水期間 mcuAlive:{len(alive)} 次(手動出水不阻塞,預期約每秒一次)")


def main():
    ap = argparse.ArgumentParser(description="K1 隨機訂單測試(會讓機台動作)")
    ap.add_argument("--port")
    ap.add_argument("--orders", type=int, default=6)
    ap.add_argument("--seed", type=int, default=1, help="亂數種子,同一個 seed 產生相同訂單")
    ap.add_argument("--water", type=float, default=0, help="JSON 水量值(預設 0 = 不看流量)")
    ap.add_argument("--dry-run", action="store_true", help="只列出訂單,不開 port")
    args = ap.parse_args()

    orders = make_orders(args.orders, random.Random(args.seed), args.water)
    print(f"seed {args.seed},共 {len(orders)} 筆:")
    for i, o in enumerate(orders, 1):
        print(f"  {i}. {describe(o)}")
    if args.dry_run:
        return

    port = args.port or find_port()
    try:
        ser = serial.Serial(port, 9600, timeout=0.02)
    except serial.SerialException as e:
        sys.exit(f"開不了 {port}:{e}\n→ 請先關掉 Arduino IDE 的 Serial Monitor / PuTTY 等")

    with ser:
        r = Runner(ser)
        if r.wait_line(lambda s: s == "ready", 0, 6000) is None:
            print("⚠ 6 秒內沒收到 ready,再等 2 秒後照常測試")
            r.idle(2.0)
        r.idle(1.5)
        try:
            for i, o in enumerate(orders, 1):
                print(f"\n── {i}. {describe(o)}")
                if o["kind"] == "json":
                    res = run_json(r, o)
                    report_json(r, o, *res)
                    if any(c.startswith("startslot") for _, c in res[3]):
                        r.send("stop")                       # 排隊的 startslotN 在 end 後會打開 slot → 關掉
                        r.idle(0.5)
                        print("  (已補送 stop#,關掉排隊後被打開的 slot)")
                else:
                    report_manual(r, o, *run_manual(r, o))
                r.idle(ORDER_GAP_S)
        finally:
            r.send("stop")                                   # 無論如何最後全關
            r.idle(0.5)
            print("\n(結束:已送 stop#)")


if __name__ == "__main__":
    main()
