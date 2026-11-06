"""K1 水量測試 — 用除錯版韌體的模擬流量計,驗證「到量停水」與輸出

前提:
  - 燒錄除錯版:arduino-cli compile ... --build-property "compiler.cpp.extra_flags=-DK1_DEBUG_FLOW_GEN"
  - 杜邦線:D23 → A8(D23 在泵開著時輸出約 6 Hz 方波,泵關著時維持高準位)
  - 會讓泵 / 閥 / slot 輸出動作,空板測試用

用法(Git Bash):
    python -I -X utf8 tools/k1_volume_test.py --port COM28              # 預設 6 筆,seed 1
    python -I -X utf8 tools/k1_volume_test.py --port COM28 --orders 10 --seed 5
    python -I -X utf8 tools/k1_volume_test.py --dry-run                 # 只列出訂單

訂單(隨機):cold / warm;水量 250 / 330 / 500 ml(上位機格式字串 "0.25",少數用數字 0.25);
time:不帶 / 短(到量先到 → 應由流量停)/ 長(比出水時間長 → 應由時間停);0~2 個短 slot

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


def make_orders(n, rng):
    orders = []
    for _ in range(n):
        water = rng.choice(["cold", "warm"])
        ml = rng.choice(VOLUMES_ML)
        numeric = rng.random() < 0.2
        payload = {water: ml / 1000 if numeric else f"{ml / 1000:.2f}"}
        pulses = math.ceil(ml * 1000 / UL_PER_PULSE)
        vol_ms = pulses / GEN_HZ * 1000            # 到量大約需要的時間
        mode = rng.choice(["none", "short", "long"])
        if mode == "short":
            payload["time"] = rng.randrange(1000, 3001, 100)
        elif mode == "long":
            payload["time"] = int(vol_ms + rng.randrange(3000, 6001, 500)) // 100 * 100
        for s in rng.sample(range(1, 11), rng.randint(0, 2)):
            payload[f"slot{s}"] = str(rng.randrange(300, 2001, 100))
        orders.append({"payload": payload, "ml": ml, "mode": mode, "pulses": pulses})
    return orders


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
        print(f"  {i}. {json.dumps(o['payload'], separators=(',', ':'))}  預期 pulse {p}{'±' + str(tol) if tol else ''}")
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
                passed += ok
                water_s = f"{(t_use - t_req) / 1000:.1f} s" if t_req and t_use else "?"
                print(f"\n── {i}. {cmd}")
                print(f"  {req}")
                print(f"  pulse {pulse}(預期 {p_exp}{'±' + str(tol) if tol else ''})  usage {usage}"
                      f"(依 pulse 應為 {usage_str(pulse) if pulse is not None else '?'})  出水 {water_s}")
                print(f"  {fin}")
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
