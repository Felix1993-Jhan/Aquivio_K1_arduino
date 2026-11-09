"""K1 出杯中命令測試 — 停止類立刻生效,其他暫存到 end 後依序執行(架構計畫 5.3、9.1 #27~#29)

適用韌體:步驟 5(d6ec6fa)起(之前出杯阻塞,出杯中收不到命令)
          正式版、除錯版皆可,不需接線(除錯版接 D23 → A8 時,A 的 0.30 L 約 15 秒才到量,仍在 6 秒 stop 之後)
最後驗證:d6ec6fa(步驟 5,正式版)

⚠ 會讓泵 / 閥 / slot 實際動作,空板測試用。

用法(Git Bash):
    python -I -X utf8 tools/k1_pour_cmd_test.py --port COM28

測項:
    A  出杯 {"cold":"0.30","slot1":"8000","slot2":"6000","slot5":"3000"}
         2.0 s ping#、2.5 s startslot3#  → 暫存,end 之後才回 pong、startslot3
         4.0 s stopslot1#                → 立刻回傳(只關腳位,不印 Deactivated slot 1)
         6.0 s stop#                     → 立刻收尾:stop → 用水量 → Deactivated ×10 → finish → end
         出杯中不送 mcuAlive;slot 開啟順序 = 時間由長到短(1 → 2 → 5)
    B  出杯 {"slot3":"1500"},0.5 s 再送 {"slot4":"800"}
         → 第二杯暫存,第一杯 end 之後才開始(start / end 各 2 次)
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_harness import Board, Checks, first_t, show   # noqa: E402

FAST_MS = 300          # 「立刻」:送出到收到回傳的上限(含 9600 baud 與 USB 延遲)


def main():
    ap = argparse.ArgumentParser(description="K1 出杯中命令測試(步驟 5)")
    ap.add_argument("--port")
    args = ap.parse_args()

    b = Board(args.port)
    c = Checks()
    try:
        print("\n══ A 出杯中:ping、startslot3 → 暫存;stopslot1、stop → 立刻")
        L = b.run([(0.0, b'{"cold":"0.30","slot1":"8000","slot2":"6000","slot5":"3000"}#'),
                   (2.0, b"ping#"), (2.5, b"startslot3#"),
                   (4.0, b"stopslot1#"), (6.0, b"stop#")], 9)
        show(L)
        t_end = first_t(L, lambda s: s == "end")
        t_stop = first_t(L, lambda s: s == "stop")
        t_ss1 = first_t(L, lambda s: s == "stopslot1")
        t_pong = first_t(L, lambda s: s == "pong")
        t_ss3 = first_t(L, lambda s: s == "startslot3")
        seen = [int(s.split()[2]) for _, s in L if s.startswith("Activated slot ")]
        fin = [s for _, s in L if s.startswith("finish:")]

        c.check("A 開啟順序 = 時間由長到短", seen == [1, 2, 5], f"{seen}")
        c.check("A stopslot1 立刻回傳", t_ss1 is not None and t_ss1 - 4000 <= FAST_MS,
                f"{t_ss1 - 4000:.0f} ms" if t_ss1 else "沒收到")
        c.check("A stopslot1 只關腳位(stop 前沒有 Deactivated slot 1)",
                t_stop is not None and first_t([(t, s) for t, s in L if t < t_stop],
                                               lambda s: s == "Deactivated slot 1") is None)
        c.check("A stop 立刻回傳", t_stop is not None and t_stop - 6000 <= FAST_MS,
                f"{t_stop - 6000:.0f} ms" if t_stop else "沒收到")
        c.check("A stop 後 1 秒內 end", t_end is not None and t_stop is not None and t_end - t_stop <= 1000,
                f"{t_end - t_stop:.0f} ms" if t_end and t_stop else "沒收到")
        c.check("A finish = external stop", fin == ["finish: stopped by system timeout or external stop"], f"{fin}")
        c.check("A 出杯中沒有 mcuAlive",
                t_end is not None and not any(s == "mcuAlive" and t < t_end for t, s in L))
        c.check("A ping 在 end 之後才回 pong", t_pong is not None and t_end is not None and t_pong > t_end)
        c.check("A startslot3 在 end 之後才執行", t_ss3 is not None and t_end is not None and t_ss3 > t_end)

        b.run([(0, b"stop#")], 1.0)          # 關掉 end 後被打開的 slot3

        print("\n══ B 出杯中送下一杯 JSON → end 後接著出下一杯")
        L = b.run([(0.0, b'{"slot3":"1500"}#'), (0.5, b'{"slot4":"800"}#')], 4)
        show(L)
        starts = [t for t, s in L if s == "start"]
        ends = [t for t, s in L if s == "end"]
        c.check("B start、end 各 2 次", len(starts) == 2 and len(ends) == 2, f"start {len(starts)}、end {len(ends)}")
        c.check("B 第二杯在第一杯 end 之後開始", len(starts) == 2 and len(ends) >= 1 and starts[1] > ends[0])
    finally:
        b.close()
    sys.exit(0 if c.summary() else 1)


if __name__ == "__main__":
    main()
