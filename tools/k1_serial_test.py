"""K1 序列埠簡易測試 — 量 mcuAlive 間隔、ping 回應時間

只送 `ping#`,**不送任何會讓泵 / 閥 / slot 動作的命令**。

用法(Git Bash):
    python -I -X utf8 tools/k1_serial_test.py                 # 自動找埠,收 30 秒
    python -I -X utf8 tools/k1_serial_test.py --port COM27 --seconds 60 --pings 10

流程:
    1. 開 COM port(Mega 的 auto-reset 會讓板子重開)→ 等 `ready`
       bootloader 期間送資料會被當成燒錄命令,所以收到 `ready` 前不送任何東西
    2. 收 N 秒,期間平均分散送 `ping#`
    3. 統計 mcuAlive 間隔、ping → pong 時間,列出其他收到的訊息

注意:時間戳是 PC 收到資料的時間,含 USB 轉串口(16U2)與 Windows 排程延遲,
單筆間隔會有數 ms 抖動;看「平均」判斷 MCU 的週期是否正確。
"""

import argparse
import statistics
import sys
import time

import serial
import serial.tools.list_ports

MEGA_PIDS = (0x0042, 0x0010)      # Mega2560 R3 / 舊版 Mega(VID 不限,相容板常改 VID)
ALIVE = "mcuAlive"
EXPECT_MS = 1000.0                # 韌體設定的 mcuAlive 週期
TOLERANCE_MS = 10.0               # 平均值容許誤差(板上振盪器 + PC 時間)


def find_port():
    ports = list(serial.tools.list_ports.comports())
    mega = [p for p in ports if p.pid in MEGA_PIDS]
    if len(mega) == 1:
        return mega[0].device
    if len(ports) == 1:
        return ports[0].device
    names = ", ".join(f"{p.device}({p.description})" for p in ports) or "無"
    sys.exit(f"找不到唯一的 Mega,請用 --port 指定。目前的埠:{names}")


class LineReader:
    """逐行讀取並記錄收到每一行的時間(以 '\\n' 分行,去掉 '\\r')"""

    def __init__(self, ser, t0):
        self.ser, self.t0, self.buf, self.lines = ser, t0, b"", []

    def pump(self):
        data = self.ser.read(self.ser.in_waiting or 1)   # 最多等 timeout
        if not data:
            return
        now = (time.perf_counter() - self.t0) * 1000.0
        self.buf += data
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            self.lines.append((now, raw.decode("utf-8", "replace").rstrip("\r")))

    def wait_for(self, text, timeout_ms):
        start = len(self.lines)
        end = (time.perf_counter() - self.t0) * 1000.0 + timeout_ms
        while (time.perf_counter() - self.t0) * 1000.0 < end:
            self.pump()
            for t, s in self.lines[start:]:
                if s.strip() == text:
                    return t
        return None


def stats_line(name, values):
    if not values:
        return f"{name}:無資料"
    sd = statistics.pstdev(values) if len(values) > 1 else 0.0
    return (f"{name}:{len(values)} 筆  平均 {statistics.mean(values):.1f} ms  "
            f"最小 {min(values):.1f}  最大 {max(values):.1f}  標準差 {sd:.1f}")


def main():
    ap = argparse.ArgumentParser(description="K1 序列埠簡易測試(只送 ping#)")
    ap.add_argument("--port", help="COM 埠,例如 COM27;不填則自動找")
    ap.add_argument("--seconds", type=float, default=30.0, help="收資料秒數(預設 30)")
    ap.add_argument("--pings", type=int, default=5, help="期間送幾次 ping#(預設 5,0 = 不送)")
    ap.add_argument("--baud", type=int, default=9600)
    args = ap.parse_args()

    port = args.port or find_port()
    print(f"開啟 {port} @ {args.baud}(板子會因 auto-reset 重開)")

    try:
        ser = serial.Serial(port, args.baud, timeout=0.02)
    except serial.SerialException as e:
        sys.exit(f"開不了 {port}:{e}\n→ COM port 同時只能一個程式開,請先關掉 Arduino IDE 的 Serial Monitor / PuTTY 等")

    with ser:
        t0 = time.perf_counter()
        rd = LineReader(ser, t0)

        t_ready = rd.wait_for("ready", 6000)
        if t_ready is None:
            print("⚠ 6 秒內沒收到 ready(板子可能沒重開,或韌體不同)→ 再等 2 秒後照常測試")
            time.sleep(2.0)
            t_ready = (time.perf_counter() - t0) * 1000.0
        else:
            print(f"收到 ready:開 port 後 {t_ready:.0f} ms")

        # 收 N 秒,平均分散送 ping
        test_end = t_ready + args.seconds * 1000.0
        ping_at = [t_ready + args.seconds * 1000.0 * (i + 0.5) / args.pings
                   for i in range(args.pings)] if args.pings > 0 else []
        sent = []                                   # (送出時間, 送出前已收到的行數)
        while (now := (time.perf_counter() - t0) * 1000.0) < test_end:
            if ping_at and now >= ping_at[0]:
                ping_at.pop(0)
                sent.append((now, len(rd.lines)))
                ser.write(b"ping#")
            rd.pump()
        end_wait = time.perf_counter() + 0.5       # 收尾:多收 0.5 秒
        while time.perf_counter() < end_wait:
            rd.pump()

    lines = [(t, s) for t, s in rd.lines if t >= t_ready]

    # mcuAlive 間隔
    alive_t = [t for t, s in lines if s.strip() == ALIVE]
    intervals = [b - a for a, b in zip(alive_t, alive_t[1:])]

    # ping → pong:每次送出後的第一個 pong
    latency = []
    for t_send, idx in sent:
        pong = next((t for t, s in rd.lines[idx:] if s.strip() == "pong" and t >= t_send), None)
        latency.append(None if pong is None else pong - t_send)

    print("\n══════ 結果 ══════")
    if alive_t:
        print(f"第一個 mcuAlive:ready 後 {alive_t[0] - t_ready:.0f} ms")
    print(stats_line("mcuAlive 間隔", intervals))
    if intervals:
        mean = statistics.mean(intervals)
        ok = abs(mean - EXPECT_MS) <= TOLERANCE_MS
        print(f"  判斷:平均 {mean:.1f} ms,預期 {EXPECT_MS:.0f} ± {TOLERANCE_MS:.0f} ms → {'OK' if ok else '⚠ 超出範圍'}")
        miss = [round(x) for x in intervals if x > EXPECT_MS * 1.5]
        if miss:
            print(f"  ⚠ 有 {len(miss)} 個間隔超過 1.5 秒(可能漏送或主迴圈卡住):{miss}")

    if sent:
        got = [x for x in latency if x is not None]
        print(stats_line("ping → pong", got))
        if len(got) < len(sent):
            print(f"  ⚠ 送出 {len(sent)} 次 ping,只收到 {len(got)} 次 pong")

    others = [(t, s) for t, s in lines if s.strip() not in (ALIVE, "pong", "")]
    print(f"\n其他訊息({len(others)} 行):")
    for t, s in others:
        print(f"  {t - t_ready:9.0f} ms  {s}")


if __name__ == "__main__":
    main()
