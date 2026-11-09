"""K1 測試腳本共用:開 port 等 ready、依時間表送命令並收行、PASS / FAIL 記錄

給 k1_cmd_parse_test.py、k1_pour_cmd_test.py、k1_pour_log.py 共用(本身不是測試)。
序列埠讀行與找埠沿用 k1_serial_test.py 的 LineReader / find_port。
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_serial_test import LineReader, find_port   # noqa: E402

import serial  # noqa: E402


class Board:
    """開 COM port(Mega 會 auto-reset)→ 等 ready → 再等 1.5 秒讓開機訊息送完"""

    def __init__(self, port=None, baud=9600):
        port = port or find_port()
        try:
            self.ser = serial.Serial(port, baud, timeout=0.02)
        except serial.SerialException as e:
            sys.exit(f"開不了 {port}:{e}\n→ 請先關掉 Arduino IDE 的 Serial Monitor / PuTTY 等")
        self.t0 = time.perf_counter()
        self.rd = LineReader(self.ser, self.t0)
        if self.rd.wait_for("ready", 6000) is None:
            print("⚠ 6 秒內沒收到 ready(板子可能沒重開,或韌體不同)")
        self.idle(1.5)

    def now(self):
        return (time.perf_counter() - self.t0) * 1000.0

    def idle(self, seconds):
        end = time.perf_counter() + seconds
        while time.perf_counter() < end:
            self.rd.pump()

    def run(self, plan, max_s, until_end=False, tail_s=0.5):
        """plan = [(秒, bytes), …],從呼叫時起算依序送出。
        until_end:時間表送完後收到 end 就提早結束。
        回傳 [(相對呼叫時的 ms, 去頭尾空白的字串)],不含空行。"""
        i = len(self.rd.lines)
        ts = self.now()
        plan = sorted(plan, key=lambda x: x[0])
        while self.now() - ts < max_s * 1000:
            self.rd.pump()
            while plan and self.now() - ts >= plan[0][0] * 1000:
                self.ser.write(plan.pop(0)[1])
            if until_end and not plan and any(s.strip() == "end" for _, s in self.rd.lines[i:]):
                break
        self.idle(tail_s)
        return [(t - ts, s.strip()) for t, s in self.rd.lines[i:] if s.strip()]

    def close(self):
        """收尾一定送 stop#(關泵閥與 slot)"""
        try:
            self.ser.write(b"stop#")
            time.sleep(0.5)
        finally:
            self.ser.close()


def first_t(lines, pred):
    """第一個符合條件的行的時間(ms),沒有則 None"""
    return next((t for t, s in lines if pred(s)), None)


def show(lines, skip=("mcuAlive",)):
    for t, s in lines:
        if s not in skip:
            print(f"  {t:9.0f} ms  {s}")


class Checks:
    def __init__(self):
        self.items = []

    def check(self, name, ok, detail=""):
        ok = bool(ok)
        self.items.append((name, ok))
        print(f"  {'PASS' if ok else 'FAIL'}  {name}{'(' + detail + ')' if detail else ''}")
        return ok

    def summary(self):
        n = sum(ok for _, ok in self.items)
        print(f"\n結果:{n} / {len(self.items)} PASS")
        for name, ok in self.items:
            if not ok:
                print(f"  FAIL:{name}")
        return n == len(self.items)
