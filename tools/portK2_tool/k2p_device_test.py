"""K2 移植版(main-k2port-v4)裝置開關測試:0x00 SWITCH_SET、0x01 SWITCH_START、0x02 SWITCH_STOP、
0xB5 狀態查詢(K1 自訂:SUB 00 = Status、01 = 實際腳位)。

用法:python -I -X utf8 tools/portK2_tool/k2p_device_test.py --port COM28 [--hold 3]
每項印出 TX / 預期 / RX(hex)與結果;LED 步驟會印「應該亮哪些」並停 --hold 秒讓人看板子。
⚠ 會讓 slot、水泵、閥的輸出腳實際動作(LED),只在空板上跑。

適用韌體:main-k2port-v4 P2 起(正式 / 除錯版皆可,115200)
最後驗證:P5 + 0x07 自動開 UVC,71 / 71 PASS(2026-11-12)"""
import argparse, time, serial

HDR = [0x40, 0x71, 0x30]
N_ID = 26
OUT_IDS = list(range(18)) + [25]                                 # 輸出裝置(K2 0~17、25)
PIN_IDS = [i for i in range(16) if i != 11]                      # K1 有實體腳位:0~10、12~15
LED_STEP = 0.88                                                  # D3 跑馬燈每顆間隔(秒,使用者指定)
NAME = {**{i: f'slot{i + 1}' for i in range(10)}, 10: '水泵 A13', 11: 'UVC0(K1 無)',
        12: 'carbonated A9', 13: 'spout A10', 14: '常溫閥 A14', 15: '冷水閥 A15',
        16: '氣泡水(K1 無)', 17: '清潔(K1 無)', 18: 'Flow(感測器)', 19: 'CO2 壓力(感測器)',
        20: '水壓(感測器)', 21: '溫度0(感測器)', 22: '溫度1(感測器)', 23: '溫度2(感測器)',
        24: '漏水(感測器)', 25: '容器(K1 無)'}


def cs(b):
    return (256 - (sum(b) & 0xFF)) & 0xFF


def pkt(cmd, d4=0, d5=0, d6=0, d7=0):
    b = HDR + [cmd, d4, d5, d6, d7]
    return bytes(b + [cs(b)])


def mask_pkt(cmd, mask):
    return pkt(cmd, mask & 0xFF, (mask >> 8) & 0xFF, (mask >> 16) & 0xFF, (mask >> 24) & 0xFF)


def set_pkt(dev, ms):
    return pkt(0x00, dev, ms & 0xFF, (ms >> 8) & 0xFF, (ms >> 16) & 0xFF)


def hx(b):
    return ' '.join(f'{x:02X}' for x in b) if b else '(無)'


def mask_of(ids):
    m = 0
    for i in ids:
        m |= 1 << i
    return m


def names(mask):
    ids = [i for i in range(N_ID) if mask & (1 << i)]
    return '、'.join(NAME[i] for i in ids) if ids else '(全滅)'


class T:
    def __init__(self, s):
        self.s, self.ok, self.n = s, 0, 0

    def xfer(self, data, wait=0.05):
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

    def query(self, sub):
        rx = self.xfer(pkt(0xB5, sub))
        if len(rx) == 9 and rx[:4] == bytes(HDR + [0xB5]):
            return rx, rx[4] | rx[5] << 8 | rx[6] << 16 | rx[7] << 24
        return rx, None

    def expect_state(self, name, status, pins=None):
        """0xB5 00 比 Status、01 比實際腳位(pins 省略 = Status 中有實體的部分)"""
        if pins is None:
            pins = status & mask_of(PIN_IDS)
        self.check(f'{name}:0xB5 00 Status = {names(status)}', pkt(0xB5, 0), mask_pkt(0xB5, status))
        self.check(f'{name}:0xB5 01 實際腳位 = {names(pins)}', pkt(0xB5, 1), mask_pkt(0xB5, pins))


def led(msg, hold):
    print(f'   👀 LED:{msg}(停 {hold} 秒)\n')
    time.sleep(hold)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True)
    ap.add_argument('--hold', type=float, default=3.0, help='LED 步驟停留秒數')
    a = ap.parse_args()
    s = serial.Serial(a.port, 115200, timeout=0.1)
    time.sleep(2.5)                      # 開 port 會重置板子,等 bootloader 跑完
    s.reset_input_buffer()
    t = T(s)
    all_out = mask_of(OUT_IDS)

    print('══ D1 開機狀態 ══')
    t.expect_state('D1 開機', 0)

    print('══ D2 0x01 全部輸出開 / 0x02 全關 ══')
    t.check('D2 0x01 遮罩 0x0203FFFF(ID 0~17、25)→ 回原封包', mask_pkt(0x01, all_out), mask_pkt(0x01, all_out))
    t.expect_state('D2 全開後', all_out)
    led('15 顆全亮:slot1~10、A13 水泵、A9、A10、A14、A15', a.hold)
    t.check('D2 0x02 遮罩 0x0203FFFF → 回原封包', mask_pkt(0x02, all_out), mask_pkt(0x02, all_out))
    t.expect_state('D2 全關後', 0)
    led('全滅', a.hold / 2)

    print('══ D3 逐一 ID 開 / 關(跑馬燈,確認 ID 對應腳位)══')
    for i in range(N_ID):
        m = 1 << i
        t_step = time.time()
        t.xfer(mask_pkt(0x01, m))
        exp = m if i in OUT_IDS else 0
        _, st = t.query(0)
        _, pn = t.query(1)
        exp_pin = m if i in PIN_IDS else 0
        ok = st == exp and pn == exp_pin
        t.n += 1
        t.ok += ok
        print(f'■ D3 ID{i:<2} {NAME[i]:<14} 0x01 → Status {st if st is None else f"{st:08X}"}'
              f'(預期 {exp:08X})、腳位 {pn if pn is None else f"{pn:08X}"}(預期 {exp_pin:08X})'
              f'  {"PASS" if ok else "FAIL"}'
              + ('  👀 這顆亮' if i in PIN_IDS else ''))
        # 〔2026-11-11〕間隔由 0.6 秒改為固定 0.88 秒(扣掉送命令與查詢的時間)。原寫法:
        # time.sleep(0.6 if i in PIN_IDS else 0.1)
        if i in PIN_IDS:
            time.sleep(max(0.0, LED_STEP - (time.time() - t_step)))
        t.xfer(mask_pkt(0x02, m))
    t.expect_state('D3 結束', 0)

    print('══ D4 0x00 SWITCH_SET 定時關閉 ══')
    tx = set_pkt(0, 1000)
    t.check('D4 0x00 ID0(slot1)1000 ms → 回原封包', tx, tx)
    t0 = time.time()
    t.expect_state('D4 剛開', 1 << 0)
    off_at = None
    while time.time() - t0 < 2.0:
        _, st = t.query(0)
        if st == 0:
            off_at = (time.time() - t0) * 1000
            break
    ok = off_at is not None and 950 <= off_at <= 1100
    t.n += 1
    t.ok += ok
    print(f'■ D4 slot1 自動關閉時間:{off_at:.0f} ms(預期約 1000,允許 950~1100,含序列埠往返)'
          if off_at else '■ D4 slot1 2 秒內沒有關閉', ' PASS\n' if ok else ' FAIL\n')
    t.expect_state('D4 關閉後', 0)

    tx = set_pkt(13, 0)
    t.check('D5 0x00 ID13(spout A10)時間 0 → 不自動關(同 K2)', tx, tx)
    time.sleep(1.5)
    t.expect_state('D5 1.5 秒後仍開', 1 << 13)
    led('A10 spout 亮', a.hold / 2)
    t.check('D5 0x02 ID13 → 關', mask_pkt(0x02, 1 << 13), mask_pkt(0x02, 1 << 13))
    t.expect_state('D5 關閉後', 0)

    tx = set_pkt(11, 500)
    t.check('D6 0x00 ID11(UVC0,K1 無)500 ms → Status 有、腳位無', tx, tx)
    t.expect_state('D6 剛開', 1 << 11, pins=0)
    time.sleep(0.7)
    t.expect_state('D6 0.7 秒後', 0)

    tx = set_pkt(18, 500)
    t.check('D7 0x00 ID18(Flow 感測器)→ 忽略', tx, tx)
    t.expect_state('D7', 0)
    tx = set_pkt(0x30, 500)
    t.check('D7 0x00 ID 0x30(超出範圍)→ 忽略', tx, tx)
    t.expect_state('D7', 0)

    tx = set_pkt(3, 3000)
    t.check('D8 0x00 ID3(slot4)3000 ms', tx, tx)
    time.sleep(0.5)
    t.check('D8 0.5 秒時 0x02 ID3 → 提早關、清倒數', mask_pkt(0x02, 1 << 3), mask_pkt(0x02, 1 << 3))
    t.expect_state('D8 提早關', 0)
    t.check('D8 再用 0x01 開 ID3(倒數已清 → 不應自動關)', mask_pkt(0x01, 1 << 3), mask_pkt(0x01, 1 << 3))
    time.sleep(3.0)
    t.expect_state('D8 3 秒後仍開', 1 << 3)
    t.xfer(mask_pkt(0x02, 1 << 3))

    t.check('D9 0x01 開 ID5(slot6)', mask_pkt(0x01, 1 << 5), mask_pkt(0x01, 1 << 5))
    tx = set_pkt(5, 800)
    t.check('D9 已開的 ID5 再下 0x00 800 ms → 800 ms 後關(同 K2)', tx, tx)
    time.sleep(1.0)
    t.expect_state('D9 1 秒後', 0)

    t.check('D10 0xB5 SUB 02(未定義)→ FF', pkt(0xB5, 2), mask_pkt(0xB5, 0xFFFFFFFF))

    t.xfer(mask_pkt(0x02, 0xFFFFFFFF))
    s.close()
    print(f'合計 {t.ok}/{t.n} PASS')


if __name__ == '__main__':
    main()
