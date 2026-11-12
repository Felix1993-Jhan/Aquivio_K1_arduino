"""K1 水壓測試 — pressure# 命令(A1 = ADC1,10-bit 原始值 0~1023)

適用韌體:步驟 6 起;正式版、除錯版皆可
接線:A1 接到要量的電壓。空板目前為 5 V → 10 kΩ → A1,讀值應為 1023(--expect 1023)
      A1 浮接時讀值會亂跳,不要用 --expect
最後驗證:jsonfreeflow(2026-11-12,除錯版)7 / 7 PASS(--expect 1023)

⚠ T3 會開 slot1 約 2 秒,空板測試用。

用法(Git Bash):
    python -I -X utf8 tools/k1_pressure_test.py --port COM28 --expect 1023
    python -I -X utf8 tools/k1_pressure_test.py --port COM28                  # 不核對數值,只看格式與時機

測項(架構計畫 7.7 P1~P6):
    T1  收到 ready 立刻問(開機 200 ms 內還沒有第一筆)→ 回 pressure、pressure:null
    T2  待機連問 10 次 → 每次先回 pressure 再回 pressure:<0~1023>;有 --expect 時核對 ±tol
    T3  出杯中問({"slot1":"2000"} 開始後 1 秒)→ 立刻回覆(不暫存到 end 後);出杯照常約 2 秒收尾
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_harness import Board, Checks, first_t, show   # noqa: E402

FAST_MS = 300
VALUE = re.compile(r"^pressure:(\d+)$")


def values(L):
    return [int(m.group(1)) for _, s in L if (m := VALUE.match(s))]


def main():
    ap = argparse.ArgumentParser(description="K1 水壓 pressure# 測試")
    ap.add_argument("--port")
    ap.add_argument("--expect", type=int, help="預期 ADC 值(例:5 V → 1023)")
    ap.add_argument("--tol", type=int, default=2, help="--expect 的容許誤差(預設 ±2)")
    args = ap.parse_args()

    b = Board(args.port, settle_s=0)
    c = Checks()
    try:
        print("\n[T1] 收到 ready 立刻問")
        L = [x for x in b.run([(0, b"pressure#")], 0.5) if x[1] != "mcuAlive"]
        show(L)
        c.check("T1 尚無資料 → pressure:null", [s for _, s in L if s.startswith("pressure")]
                == ["pressure", "pressure:null"], f"{[s for _, s in L]}")
        b.idle(1.0)

        print("\n[T2] 待機連問 10 次")
        L = b.run([(k * 0.3, b"pressure#") for k in range(10)], 3.3)
        got = [s for _, s in L if s.startswith("pressure")]
        v = values(L)
        pairs_ok = len(got) == 20 and all(got[2 * k] == "pressure" and VALUE.match(got[2 * k + 1])
                                          for k in range(10))
        c.check("T2 每次先回 pressure 再回值", pairs_ok, f"{len(got)} 行")
        c.check("T2 值在 0~1023", len(v) == 10 and all(0 <= x <= 1023 for x in v), f"{v}")
        if args.expect is not None:
            c.check(f"T2 值 = {args.expect} ±{args.tol}", v and all(abs(x - args.expect) <= args.tol for x in v),
                    f"最小 {min(v) if v else '-'}、最大 {max(v) if v else '-'}")

        print("\n[T3] 出杯中問")
        L = b.run([(0, b'{"slot1":"2000"}#'), (1.0, b"pressure#")], 4, until_end=True)
        show([x for x in L if not x[1].startswith("Deactivated slot")])
        t_val = first_t(L, lambda s: bool(VALUE.match(s)))
        t_end = first_t(L, lambda s: s == "end")
        c.check("T3 出杯中立刻回覆", t_val is not None and t_val - 1000 <= FAST_MS,
                f"{t_val - 1000:.0f} ms" if t_val else "沒收到")
        c.check("T3 回覆在 end 之前(沒有暫存)", t_val is not None and t_end is not None and t_val < t_end)
        t_off = first_t(L, lambda s: s.startswith("pulse:"))
        c.check("T3 出杯照常約 2 秒收尾", t_off is not None and 1950 <= t_off <= 2600,
                f"{t_off:.0f} ms" if t_off else "沒收到")
    finally:
        b.close()
    sys.exit(0 if c.summary() else 1)


if __name__ == "__main__":
    main()
