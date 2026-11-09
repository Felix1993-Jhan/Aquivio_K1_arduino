"""K1 命令解析測試 — 逐 byte 解析(黏包、超長、長度上限、恢復),全部在待機狀態下送

適用韌體:步驟 4a(02c0475)起;T4 的 255 字元上限從 3627cb6 起(之前上限 199)
          正式版、除錯版皆可,不需接線
最後驗證:d6ec6fa(步驟 5,正式版)全部 PASS

用法(Git Bash):
    python -I -X utf8 tools/k1_cmd_parse_test.py --port COM28

測項(對應架構計畫 5.4):
    T1  ping#ping#ping# 一次寫入        → 3 個 pong(舊版 mcu31v2:整串當 JSON,全部失效)
    T2  300 字元 + # + ping#            → 超長那筆丟棄、不被當 JSON;後面的 ping 照常回 pong
                                          (舊版:滿 199 byte 後永久卡死)
    T3  startslot3#stopslot3#           → 依序回 startslot3、stopslot3(會讓 slot3 短暫動作)
    T4  255 字元 + #                     → 收下(不丟棄),當 JSON 解析 → Failed to parse
    T5  待機時 stop#                     → 回 stop、pulse:、用水量;不印 end、不印 Deactivated(照舊)
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_harness import Board, Checks, show   # noqa: E402


def lines_of(L):
    return [s for _, s in L if s != "mcuAlive"]


def main():
    ap = argparse.ArgumentParser(description="K1 命令解析測試(待機狀態)")
    ap.add_argument("--port")
    args = ap.parse_args()

    b = Board(args.port)
    c = Checks()
    try:
        print("\n[T1] ping#ping#ping#")
        L = lines_of(b.run([(0, b"ping#ping#ping#")], 1.0))
        c.check("T1 三筆 ping 黏在一起 → 3 個 pong", L.count("pong") == 3, f"收到 {L}")

        print("\n[T2] 300 字元 + # + ping#")
        L = lines_of(b.run([(0, b"x" * 300 + b"#ping#")], 1.0))
        c.check("T2 超長那筆不被當 JSON", not any(s == "start" or "Received" in s for s in L))
        c.check("T2 超長後 ping 照常回 pong", L.count("pong") == 1, f"收到 {L}")

        print("\n[T3] startslot3#stopslot3#")
        L = lines_of(b.run([(0, b"startslot3#stopslot3#")], 1.0))
        c.check("T3 兩筆依序執行", L == ["startslot3", "stopslot3"], f"收到 {L}")

        print("\n[T4] 255 字元 + #")
        L = lines_of(b.run([(0, b"y" * 255 + b"#")], 1.5))
        c.check("T4 255 字元收下(回傳原字串)", "y" * 255 in L)
        c.check("T4 當 JSON 解析 → Failed to parse", any(s.startswith("Failed to parse") for s in L))

        print("\n[T5] 待機時 stop#")
        L = b.run([(0, b"stop#")], 1.0)
        show(L)
        L = lines_of(L)
        c.check("T5 回 stop、pulse:、用水量",
                L[:1] == ["stop"] and any(s.startswith("pulse:") for s in L)
                and any("water usage:" in s for s in L), f"收到 {L}")
        c.check("T5 不印 end、不印 Deactivated",
                not any(s == "end" or s.startswith("Deactivated") for s in L))
    finally:
        b.close()
    sys.exit(0 if c.summary() else 1)


if __name__ == "__main__":
    main()
