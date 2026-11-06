"""K1 水量測試 — 用除錯版韌體的模擬流量計,驗證「到量停水」與輸出

前提:
  - 燒錄除錯版:arduino-cli compile ... --build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN"
  - 杜邦線:D23 → A8(D23 在泵開著時輸出約 6 Hz 方波,泵關著時維持高準位)
  - 會讓泵 / 閥 / slot 輸出動作,空板測試用

用法(Git Bash):
    python -I -X utf8 tools/k1_volume_test.py --port COM28              # 預設 6 筆,seed 1
    python -I -X utf8 tools/k1_volume_test.py --port COM28 --orders 10 --seed 5
    python -I -X utf8 tools/k1_volume_test.py --dry-run                 # 只列出訂單

訂單(隨機):cold / warm;總量 250 / 330 / 500 ml;0~10 個 slot,各隨機 1:32 或 1:10;
time:不帶 / 短(到量先到 → 應由流量停)/ 長(比出水時間長 → 應由時間停)

slot 與水量換算(測試用簡化模型,使用者 2026-11-06 指定):
  - 比例 1:N = 料佔總量 1/(N+1)(1:32 → 1/33、1:10 → 1/11)
  - 蠕動泵 5 ml/s → slot 毫秒 = 料 ml ÷ 5 × 1000(送字串、2 位小數,同上位機 toFixed(2))
  - 總量包含料(同上位機 routes.cjs translateOrder 會扣掉料):水 = 總量 − Σ 料,
    送 "cold":"0.xx"(2 位小數,少數用數字)
  註:上位機實際 slot 公式為 (volume × ratio ÷ 330) × 60000 × percentage% × waterCalibration,
      ratio 的意義不一定同上;此模型只用來產生合理的測試數值

核對(由流量停時):
  pulse 數 = ceil(水量 µL / 3300);usage = pulse × 3.3 ml,四捨五入到 0.001 L;
  finish = "stopped by flow pulse threshold"
由時間停時:pulse 約 = time × 6 Hz(±2);finish = "stopped by minimum time constraint"
"""

import argparse
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_serial_test import LineReader, find_port   # noqa: E402

import serial  # noqa: E402

UL_PER_PULSE = 3300
GEN_HZ = 1000 / 167           # 韌體 83 / 84 ms 交替翻轉(半週期)→ 週期 167 ms ≈ 5.99 Hz
VOLUMES_ML = (250, 330, 500)
RATIOS = (32, 10)             # 1:32、1:10
PUMP_ML_PER_S = 5.0           # 蠕動泵出料量


def make_orders(n, rng):
    orders = []
    for _ in range(n):
        water = rng.choice(["cold", "warm"])
        total = rng.choice(VOLUMES_ML)
        slots = {}                                     # slot 編號 → (比例 N, 料 ml, 送出的字串)
        for s in sorted(rng.sample(range(1, 11), rng.randint(0, 10))):
            ratio = rng.choice(RATIOS)
            syrup = total / (ratio + 1)
            slots[s] = (ratio, syrup, f"{syrup / PUMP_ML_PER_S * 1000:.2f}")
        water_ml = total - sum(v[1] for v in slots.values())
        water_l = round(water_ml / 1000, 2)            # 同上位機 toFixed(2)
        numeric = rng.random() < 0.2
        payload = {water: water_l if numeric else f"{water_l:.2f}"}
        pulses = math.ceil(round(water_l * 1_000_000) / UL_PER_PULSE)
        vol_ms = pulses / GEN_HZ * 1000                # 到量大約需要的時間
        mode = rng.choice(["none", "short", "long"])
        if mode == "short":
            payload["time"] = rng.randrange(1000, 3001, 100)
        elif mode == "long":
            payload["time"] = int(vol_ms + rng.randrange(3000, 6001, 500)) // 100 * 100
        for s, (_, _, ms) in slots.items():
            payload[f"slot{s}"] = ms
        orders.append({"payload": payload, "total": total, "water_ml": water_ml,
                       "slots": slots, "mode": mode, "pulses": pulses})
    return orders


def slot_expect(o):
    """各 slot 預期:印出的 duration(截掉小數)、實際開啟時間 ≈ max(設定, (N−k+1)×500)"""
    order = sorted(o["slots"], reverse=True)          # 韌體開啟順序:編號大 → 小
    n = len(order)
    out = {}
    for k, s in enumerate(order, start=1):
        dur = int(float(o["slots"][s][2]))
        out[s] = (dur, max(dur, (n - k + 1) * 500))
    return out


def expect(o):
    if o["mode"] == "long":
        p = round(o["payload"]["time"] / 1000 * GEN_HZ)
        return p, "finish: stopped by minimum time constraint", 2
    return o["pulses"], "finish: stopped by flow pulse threshold", 0


def usage_str(p):
    ml = (p * UL_PER_PULSE + 500) // 1000
    return f"{ml // 1000}.{ml % 1000:03d}"


def main():
    ap = argparse.ArgumentParser(description="K1 水量測試(除錯版模擬流量計,D23 → A8)")
    ap.add_argument("--port")
    ap.add_argument("--orders", type=int, default=6)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    orders = make_orders(args.orders, random.Random(args.seed))
    print(f"seed {args.seed},共 {len(orders)} 筆:")
    for i, o in enumerate(orders, 1):
        p, fin, tol = expect(o)
        sl = " ".join(f"s{s}=1:{r}" for s, (r, _, _) in o["slots"].items()) or "無 slot"
        print(f"  {i}. 總量 {o['total']} ml = 水 {o['water_ml']:.1f} + 料 {o['total'] - o['water_ml']:.1f}({sl})"
              f"  預期 pulse {p}{'±' + str(tol) if tol else ''}")
        print(f"     {json.dumps(o['payload'], separators=(',', ':'))}")
    if args.dry_run:
        return

    port = args.port or find_port()
    try:
        ser = serial.Serial(port, 9600, timeout=0.02)
    except serial.SerialException as e:
        sys.exit(f"開不了 {port}:{e}\n→ 請先關掉 Arduino IDE 的 Serial Monitor / PuTTY 等")

    passed = 0
    with ser:
        t0 = time.perf_counter()
        rd = LineReader(ser, t0)
        now = lambda: (time.perf_counter() - t0) * 1000.0  # noqa: E731
        rd.wait_for("ready", 6000)
        end = time.perf_counter() + 1.5
        while time.perf_counter() < end:
            rd.pump()
        try:
            for i, o in enumerate(orders, 1):
                idx = len(rd.lines)
                cmd = json.dumps(o["payload"], separators=(",", ":"))
                t_send = now()
                ser.write((cmd + "#").encode())
                deadline = time.perf_counter() + 130
                while time.perf_counter() < deadline:
                    rd.pump()
                    if any(s.strip() == "end" for _, s in rd.lines[idx:]):
                        break
                seg = [(t - t_send, s.strip()) for t, s in rd.lines[idx:]]
                req = next((s for _, s in seg if "water flow requested" in s), "?")
                pulse = next((int(s[6:]) for _, s in seg if s.startswith("pulse:")), None)
                usage = next((s.split(":")[1] for _, s in seg if "water usage:" in s), None)
                fin = next((s for _, s in seg if s.startswith("finish:")), None)
                t_req = next((t for t, s in seg if "water flow requested" in s), None)
                t_use = next((t for t, s in seg if "water usage:" in s), None)

                p_exp, fin_exp, tol = expect(o)
                ok = (pulse is not None and abs(pulse - p_exp) <= tol
                      and usage == usage_str(pulse) and fin == fin_exp)
                water_s = f"{(t_use - t_req) / 1000:.1f} s" if t_req and t_use else "?"
                print(f"\n── {i}. 總量 {o['total']} ml,水 {o['water_ml']:.1f} ml,{len(o['slots'])} 個 slot")
                print(f"  {cmd}")
                print(f"  {req}")
                print(f"  pulse {pulse}(預期 {p_exp}{'±' + str(tol) if tol else ''})  usage {usage}"
                      f"(依 pulse 應為 {usage_str(pulse) if pulse is not None else '?'})  出水 {water_s}")
                print(f"  {fin}")

                # slot:印出的 duration 與實際開啟時間(PC 收字串時間推算,誤差約 ±250 ms)
                # 最先開的 slot:它的 Activated 排在回傳 JSON / Received actions 之後,PC 收到時已晚數百 ms
                # → 改用「第二個 slot 的 Activated 時間 − 500 ms」推算(slot 每 500 ms 開一個,此時佇列已清空)
                on_times = {}
                for s in sorted(o["slots"], reverse=True):
                    on_times[s] = next((t for t, x in seg if x.startswith(f"Activated slot {s} ")), None)
                order_on = sorted(o["slots"], reverse=True)
                if len(order_on) >= 2 and on_times[order_on[1]] is not None:
                    on_times[order_on[0]] = on_times[order_on[1]] - 500
                for s, (dur, real_exp) in sorted(slot_expect(o).items(), reverse=True):
                    t_on = on_times[s]
                    d_txt = next((x.rsplit(":", 1)[1].strip() for _, x in seg
                                  if x.startswith(f"Activated slot {s} ")), None)
                    t_off = next((t for t, x in seg if x == f"Deactivated slot {s}" and t_on and t >= t_on), None)
                    real = (t_off - t_on) if t_on and t_off else None
                    s_ok = (d_txt == str(dur) and real is not None and abs(real - real_exp) <= 250)
                    ok = ok and s_ok
                    print(f"    slot{s:<2} 1:{o['slots'][s][0]:<2} 送 {o['slots'][s][2]:>8} → 印 {d_txt}(應 {dur})"
                          f"  實開 {real:.0f} ms(應 ≈ {real_exp}){'' if s_ok else '  ← 不符'}"
                          if real is not None else f"    slot{s} 找不到開關紀錄  ← 不符")
                passed += ok
                print(f"  → {'PASS' if ok else 'FAIL'}")
                end = time.perf_counter() + 1.5
                while time.perf_counter() < end:
                    rd.pump()
        finally:
            ser.write(b"stop#")
            time.sleep(0.5)
    print(f"\n結果:{passed} / {len(orders)} PASS")


if __name__ == "__main__":
    main()
