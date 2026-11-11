"""K2 移植版(main-k2port-v4)0x06 SET_PULSE 測試:依流量脈衝數出水、完成通知、不支援 Type、
0x02 提早停止、0x04 出水中歸零,以及 0x02 後殘留 Pulse 的問題重現(K2 行為,待討論)。

用法:python -I -X utf8 tools/portK2_tool/k2p_pulse_test.py --port COM28
每項印出 TX / 預期 / RX(hex)與結果。
⚠ 需除錯版 + 杜邦線 D22 → A8(水泵開著時 D22 約 6 Hz 模擬流量計)。會開水泵 / 閥(LED),只在空板上跑。

適用韌體:main-k2port-v4 P4 起(除錯版,115200)
最後驗證:P5,25 / 25 PASS(2026-11-11)"""
import argparse, time, serial

HDR = [0x40, 0x71, 0x30]
PUMP, WARM, COLD = 1 << 10, 1 << 14, 1 << 15


def cs(b):
    return (256 - (sum(b) & 0xFF)) & 0xFF


def pkt(cmd, d4=0, d5=0, d6=0, d7=0):
    b = HDR + [cmd, d4, d5, d6, d7]
    return bytes(b + [cs(b)])


def mask_pkt(cmd, m):
    return pkt(cmd, m & 0xFF, (m >> 8) & 0xFF, (m >> 16) & 0xFF, (m >> 24) & 0xFF)


def pulse_pkt(typ, n):
    return pkt(0x06, typ, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF)


def done(typ):
    return pkt(0x06, typ, 0x01, 0x00, 0x00)


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

    def result(self, name, ok, lines):
        self.n += 1
        self.ok += ok
        print(f'■ {name}')
        for l in lines:
            print(f'   {l}')
        print(f'   結果: {"PASS" if ok else "FAIL"}\n')

    def check(self, name, tx, expect):
        rx = self.xfer(tx)
        self.result(name, rx == expect, [f'TX  : {hx(tx)}', f'預期: {hx(expect)}', f'RX  : {hx(rx)}'])
        return rx

    def status(self):
        rx = self.xfer(pkt(0xB5, 0))
        return rx[4] | rx[5] << 8 | rx[6] << 16 | rx[7] << 24 if len(rx) == 9 else None

    def flow(self):
        rx = self.xfer(pkt(0x03, 18))
        return rx[5] | rx[6] << 8 | rx[7] << 16 if len(rx) == 9 else None

    def check_status(self, name, exp):
        st = self.status()
        self.result(name, st == exp, [f'0xB5 00 → Status {st if st is None else f"{st:08X}"}(預期 {exp:08X})'])

    def wait_done(self, typ, timeout):
        """等主動送來的完成通知;回傳(經過秒數, 收到的 bytes)"""
        t0 = time.time()
        buf = b''
        while time.time() - t0 < timeout:
            buf += self.s.read(self.s.in_waiting or 1)
            if done(typ) in buf:
                return time.time() - t0, buf
        return None, buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True)
    a = ap.parse_args()
    s = serial.Serial(a.port, 115200, timeout=0.05)
    time.sleep(2.5)
    s.reset_input_buffer()
    t = T(s)

    print('══ P1 冷水(Type 1)12 pulse → 約 2 秒後主動送完成通知 ══')
    t.xfer(pkt(0x04, 18))
    t.check('P1 0x06 Type 1、12 pulse → 回原封包', pulse_pkt(1, 12), pulse_pkt(1, 12))
    t0 = time.time()
    t.check_status('P1 剛開:水泵 A13 + 冷水閥 A15', PUMP | COLD)
    dt, rx = t.wait_done(1, 5.0)
    dt = time.time() - t0                # 從 0x06 回覆起算
    t.result('P1 完成通知(預期約 2 秒,允許 1.5~2.8)', rx.endswith(done(1)) and 1.5 <= dt <= 2.8,
             [f'預期: {hx(done(1))}', f'RX  : {hx(rx)}', f'經過 {dt:.2f} 秒'])
    t.check_status('P1 完成後全關', 0)
    n = t.flow()
    t.result('P1 流量計數 = 12', n == 12, [f'0x03 ID18 → {n} pulse'])

    print('══ P2 常溫(Type 0)6 pulse ══')
    t.xfer(pkt(0x04, 18))
    t.check('P2 0x06 Type 0、6 pulse → 回原封包', pulse_pkt(0, 6), pulse_pkt(0, 6))
    t0 = time.time()
    t.check_status('P2 剛開:水泵 A13 + 常溫閥 A14', PUMP | WARM)
    dt, rx = t.wait_done(0, 4.0)
    dt = time.time() - t0
    t.result('P2 完成通知(預期約 1 秒,允許 0.7~1.6)', rx.endswith(done(0)) and 0.7 <= dt <= 1.6,
             [f'預期: {hx(done(0))}', f'RX  : {hx(rx)}', f'經過 {dt:.2f} 秒'])
    t.check_status('P2 完成後全關', 0)

    print('══ P3 不支援的 Type → [Type] FF [Type] FF,不動作 ══')
    t.check('P3 Type 2(氣泡水)', pulse_pkt(2, 10), pkt(0x06, 0x02, 0xFF, 0x02, 0xFF))
    t.check('P3 Type 5(未定義)', pulse_pkt(5, 10), pkt(0x06, 0x05, 0xFF, 0x05, 0xFF))
    t.check_status('P3 狀態仍全關', 0)

    print('══ P4 pulse = 0 → 開了不會自動停(同 K2)══')
    t.check('P4 0x06 Type 1、0 pulse', pulse_pkt(1, 0), pulse_pkt(1, 0))
    time.sleep(2.0)
    t.check_status('P4 2 秒後仍開', PUMP | COLD)
    t.check('P4 0x02 關水泵 + 冷水閥', mask_pkt(0x02, PUMP | COLD), mask_pkt(0x02, PUMP | COLD))
    t.check_status('P4 關閉後', 0)

    print('══ P5 0x04 出水中歸零 → 歸零前後的脈衝都有扣,仍約 2 秒完成 ══')
    t.xfer(pkt(0x04, 18))
    t.xfer(pulse_pkt(1, 12))
    t0 = time.time()
    time.sleep(1.0)
    t.check('P5 1 秒時 0x04 ID18 → 回原封包', pkt(0x04, 18), pkt(0x04, 18))
    dt, rx = t.wait_done(1, 4.0)
    dt = time.time() - t0
    t.result('P5 完成通知(預期約 2 秒,允許 1.5~2.8)', rx.endswith(done(1)) and 1.5 <= dt <= 2.8,
             [f'RX  : {hx(rx)}', f'從 0x06 起經過 {dt:.2f} 秒'])
    n = t.flow()
    t.result('P5 歸零後的流量計數 < 12(只算歸零後的)', n is not None and 0 < n < 12, [f'0x03 ID18 → {n} pulse'])

    print('══ P6 0x02 提早停止 ══')
    t.xfer(pulse_pkt(1, 30))
    time.sleep(1.0)
    t.check('P6 1 秒時 0x02 關水泵 + 冷水閥', mask_pkt(0x02, PUMP | COLD), mask_pkt(0x02, PUMP | COLD))
    t.check_status('P6 已關閉', 0)
    dt, rx = t.wait_done(1, 2.0)
    t.result('P6 之後 2 秒內沒有完成通知', dt is None, [f'RX  : {hx(rx)}'])

    print('══ P7 殘值問題重現(K2 行為):P6 剩約 24 pulse,只開水泵 → 殘值被扣完後自動關水並送完成通知 ══')
    t.check('P7 0x01 只開水泵 A13', mask_pkt(0x01, PUMP), mask_pkt(0x01, PUMP))
    t0 = time.time()
    dt, rx = t.wait_done(1, 8.0)
    dt = time.time() - t0
    t.result('P7 ⚠ 收到非預期的 0x06 完成通知(重現 K2 殘值問題;預期約 4 秒)', rx.endswith(done(1)),
             [f'RX  : {hx(rx)}', f'開水泵後經過 {dt:.2f} 秒'])
    t.check_status('P7 水泵被自動關掉', 0)

    t.xfer(mask_pkt(0x02, 0xFFFFFFFF))
    s.close()
    print(f'合計 {t.ok}/{t.n} PASS')


if __name__ == '__main__':
    main()
