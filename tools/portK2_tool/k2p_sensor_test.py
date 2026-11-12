"""K2 移植版(main-k2port-v4)感測器與不支援命令測試:0x03 SENSOR_READ、0x04 SENSOR_CLEAR、
不支援格式(0x08 / 0x09 / 0x0A / 0xB0,討論稿 3.7)、0x07 變長收包。

用法:python -I -X utf8 tools/portK2_tool/k2p_sensor_test.py --port COM28
每項印出 TX / 預期 / RX(hex)與結果。
⚠ 流量項目用 0x01 開 ID10 水泵(A13)讓除錯版 D22 輸出模擬流量計 → 需除錯版 + 杜邦線 D22 → A8;
  正式版或沒接線時 S3 會 FAIL(計數不增加)。只在空板上跑。

適用韌體:main-k2port-v4 P3 起(除錯版,115200)
最後驗證:P5 + 0x07 自動開 UVC,42 / 42 PASS(2026-11-12)
P4 起:S7 的 0x06 項目移到 k2p_pulse_test.py(總項數 42)
P5 起:S7 的 0x07 改用 Type 2(不支援格式、不動作)"""
import argparse, time, serial

HDR = [0x40, 0x71, 0x30]
VER = [0x00, 0x04, 0x00, 0x00]          # 0.0.4.0


def cs(b):
    return (256 - (sum(b) & 0xFF)) & 0xFF


def raw(body):
    b = HDR + list(body)
    return bytes(b + [cs(b)])


def pkt(cmd, d4=0, d5=0, d6=0, d7=0):
    return raw([cmd, d4, d5, d6, d7])


def unsup(cmd, code):
    return pkt(cmd, code, 0xFF, code, 0xFF)


def hx(b):
    return ' '.join(f'{x:02X}' for x in b) if b else '(無)'


class T:
    def __init__(self, s):
        self.s, self.ok, self.n = s, 0, 0

    def xfer(self, data, wait=0.1):
        self.s.reset_input_buffer()
        self.s.write(data)
        time.sleep(wait)
        return self.s.read(self.s.in_waiting or 0)

    def check(self, name, tx, expect, rx=None):
        if rx is None:
            rx = self.xfer(tx)
        ok = rx == expect
        self.n += 1
        self.ok += ok
        print(f'■ {name}')
        print(f'   TX  : {hx(tx)}')
        print(f'   預期: {hx(expect)}')
        print(f'   RX  : {hx(rx)}')
        print(f'   結果: {"PASS" if ok else "FAIL"}\n')
        return ok

    def flow(self):
        rx = self.xfer(pkt(0x03, 18))
        if len(rx) == 9 and rx[:5] == bytes(HDR + [0x03, 18]):
            return rx, rx[5] | rx[6] << 8 | rx[7] << 16
        return rx, None

    def judge(self, name, ok, detail):
        self.n += 1
        self.ok += ok
        print(f'■ {name}\n   {detail}\n   結果: {"PASS" if ok else "FAIL"}\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True)
    a = ap.parse_args()
    s = serial.Serial(a.port, 115200, timeout=0.1)
    time.sleep(2.5)                      # 開 port 會重置板子(流量計數歸零)
    s.reset_input_buffer()
    t = T(s)

    print('══ S1~S4 流量(ID18)══')
    t.check('S1 開機讀 Flow → 0', pkt(0x03, 18), pkt(0x03, 18, 0, 0, 0))
    t.check('S2 0x04 ID18 歸零 → 回原封包', pkt(0x04, 18), pkt(0x04, 18))
    pump = 1 << 10
    t.xfer(raw([0x01, pump & 0xFF, pump >> 8, 0, 0]))
    print('   (0x01 開 ID10 水泵 A13 → 除錯版 D22 約 6 Hz → A8,等 3 秒)\n')
    time.sleep(3.0)
    t.xfer(raw([0x02, pump & 0xFF, pump >> 8, 0, 0]))
    rx, n1 = t.flow()
    t.judge('S3 水泵開 3 秒後讀 Flow → 約 18 pulse(允許 14~22)',
            n1 is not None and 14 <= n1 <= 22, f'RX {hx(rx)} → {n1} pulse')
    time.sleep(1.0)
    rx, n2 = t.flow()
    t.judge('S3 水泵關 1 秒後再讀 → 計數不變', n2 == n1, f'RX {hx(rx)} → {n2} pulse(前次 {n1})')
    t.check('S4 0x04 ID0(非 Flow)→ 回原封包、不清除', pkt(0x04, 0), pkt(0x04, 0))
    rx, n3 = t.flow()
    t.judge('S4 讀 Flow 仍為前值', n3 == n1, f'RX {hx(rx)} → {n3} pulse')
    t.check('S4 0x04 ID18 → 回原封包', pkt(0x04, 18), pkt(0x04, 18))
    t.check('S4 歸零後讀 Flow → 0', pkt(0x03, 18), pkt(0x03, 18, 0, 0, 0))

    print('══ S5 0x03 讀 K1 沒有的 ID → [ID] FF [ID] FF ══')
    for i in list(range(26)) + [0xF0, 0x30]:
        if i == 18:
            continue
        t.check(f'S5 0x03 ID {i}(0x{i:02X})', pkt(0x03, i), unsup(0x03, i))

    print('══ S6 不支援的命令 → 9 byte [代碼] FF [代碼] FF ══')
    t.check('S6 0x08 sub 00 QUERY(請求 7 byte)', raw([0x08, 0x00, 0x00]), unsup(0x08, 0x08))
    set16 = raw([0x08, 0x01, 0x00, 0x0D, 0x02, 0x76, 0x01, 0xE0, 0x00, 0x1E, 0x00, 0x05])
    t.check('S6 0x08 sub 01 SET(請求 16 byte)+ 緊接 PING → 兩包都對齊',
            set16 + pkt(0x05), unsup(0x08, 0x08) + pkt(0x05, *VER))
    t.check('S6 0x08 sub 00 + 緊接 PING(7 byte 請求後對齊)',
            raw([0x08, 0x00, 0xFF]) + pkt(0x05), unsup(0x08, 0x08) + pkt(0x05, *VER))
    t.check('S6 0x09 MAX30102', pkt(0x09), unsup(0x09, 0x09))
    t.check('S6 0x0A 水桶模式', pkt(0x0A, 0x03), unsup(0x0A, 0x0A))
    t.check('S6 0xB0 Bootloader', pkt(0xB0), unsup(0xB0, 0xB0))

    print('══ S7 0x07 變長收包對齊(Type 2 不支援 → 不動作)══')
    # 〔P4〕0x06 已實作(測試移到 k2p_pulse_test.py)。P3 原判斷:
    # t.check('S7 0x06 SET_PULSE → PING 內容', pkt(0x06, 1, 10, 0, 0), pkt(0x06, *VER))
    # 〔P5〕0x07 已實作(Type 1 會啟動 freeflow,測試在 k2p_freeflow_test.py),改用 Type 2。P4 原判斷:
    # lp = raw([0x07, 0x02, 0x00, 0x64, 0x00, 0x03, 0x32, 0x00, 0x01])   # N=2,13 byte
    # t.check('S7 0x07 N=2(請求 13 byte)+ 緊接 PING → PING 內容 9 byte,兩包都對齊',
    #         lp + pkt(0x05), pkt(0x07, *VER) + pkt(0x05, *VER))
    lp = raw([0x07, 0x02, 0x00, 0x64, 0x00, 0x03, 0x32, 0x00, 0x02])   # N=2,13 byte,Type 2
    t.check('S7 0x07 N=2 Type 2(請求 13 byte)+ 緊接 PING → 不支援格式 9 byte,兩包都對齊',
            lp + pkt(0x05), unsup(0x07, 0x02) + pkt(0x05, *VER))

    s.close()
    print(f'合計 {t.ok}/{t.n} PASS')


if __name__ == '__main__':
    main()
