"""K2 移植版(main-k2port-v4)0x07 LONG_PRESSURE(freeflow)測試:slot 依流量開啟的時間、Type、
N = 0(停料只出水)、0x02 結束封包重置、不支援 Type、N 上限;F9 隨機出杯。

用法:python -I -X utf8 tools/portK2_tool/k2p_freeflow_test.py --port COM28 [--rounds 5] [--seed N]
每項印出 TX / 預期 / RX(hex)或量測值與結果。
F9 隨機出杯:每輪隨機冷水 / 常溫、從 ID0~9 挑 4~6 個 slot、各給 1:10 或 1:32 的時間值,
  並帶 UVC ID11~13(時間值隨機,韌體不看);杯量隨機 250 / 330 / 500 ml,
  以 ceil(ml / --ml-per-pulse) 換成 pulse 數(預設 3.2,K2 上位機預設值),出水中每 250 ms 讀 0x03,到量送結束封包;
  每杯隨機按 1~3 次(⚠ 中斷:每段 ≥ 10 pulse、放開 0.5~2 秒,每段送同一包 0x07,最後一段補到杯量);
  印出亂數種子,FAIL 時用 --seed 重跑同一組搭配。
⚠ 需除錯版 + 杜邦線 D22 → A8(水泵開著時 D22 約 6 Hz 模擬流量計)。會開水泵 / 閥 / slot(LED),只在空板上跑。

slot 開啟時間的量法:出水中以 0xB5 00 連續輪詢 Status(每次約數 ms),slot bit 為 1 的時間累加;
結束後以 0x03 讀實際 pulse 數,理論值 = pulse 數 × 時間值(上限 150)。
允許範圍:理論 × 0.85 − 100 ~ 理論 × 1.1 + 50 ms(輪詢解析度、結束時 acc 殘量 < 100、每段少 1 ms)。

適用韌體:main-k2port-v4 P5 起(除錯版,115200)
最後驗證:P5,F1~F8 + F9 24 杯(16 杯有中斷)共 537 / 537 PASS(2026-11-11,--seed 81398,3.2 ml/pulse)"""
import argparse, math, random, time, serial

HDR = [0x40, 0x71, 0x30]
PUMP, WARM, COLD, UVC12 = 1 << 10, 1 << 14, 1 << 15, 1 << 12

# F9 比例 → 每 pulse 時間值(ms)。來源:使用者以上位機公式與預設校正算出(2026-11-11),
# 1:10 = 72;1:32 = 21~23(隨機)
RATIO = {'1:10': (72, 72), '1:32': (21, 23)}
MIN_SEG = 10                             # F9 中斷時每段至少幾個 pulse(約 1.6 秒)
CUPS = (250, 330, 500)                   # 常溫 / 冷水的出杯水量(ml),使用者 2026-11-11 提供


def cs(b):
    return (256 - (sum(b) & 0xFF)) & 0xFF


def raw(body):
    b = HDR + list(body)
    return bytes(b + [cs(b)])


def pkt(cmd, d4=0, d5=0, d6=0, d7=0):
    return raw([cmd, d4, d5, d6, d7])


def mask_pkt(cmd, m):
    return pkt(cmd, m & 0xFF, (m >> 8) & 0xFF, (m >> 16) & 0xFF, (m >> 24) & 0xFF)


def ff(groups, typ):
    """0x07:groups = [(ID, 時間 ms), ...]"""
    body = [0x07, len(groups)]
    for i, v in groups:
        body += [i, v & 0xFF, (v >> 8) & 0xFF]
    return raw(body + [typ])


END = raw([0x02, 0xFF, 0xFF, 0x03, 0x00])     # 上位機 buildFreeFlowEndFrame:40 71 30 02 FF FF 03 00 1C


def hx(b):
    return ' '.join(f'{x:02X}' for x in b) if b else '(無)'


class T:
    def __init__(self, s):
        self.s, self.ok, self.n = s, 0, 0
        self.conc, self.stats, self.errs = {}, [], []   # F9 統計:同時開台數、每段 slot 劑量誤差 %

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

    def check(self, name, tx, expect, wait=0.1):
        rx = self.xfer(tx, wait)
        self.result(name, rx == expect, [f'TX  : {hx(tx)}', f'預期: {hx(expect)}', f'RX  : {hx(rx)}'])

    def status(self):
        rx = self.xfer(pkt(0xB5, 0))
        return rx[4] | rx[5] << 8 | rx[6] << 16 | rx[7] << 24 if len(rx) == 9 else None

    def check_status(self, name, exp):
        st = self.status()
        self.result(name, st == exp, [f'0xB5 00 → Status {st if st is None else f"{st:08X}"}(預期 {exp:08X})'])

    def flow(self):
        rx = self.xfer(pkt(0x03, 18))
        return rx[5] | rx[6] << 8 | rx[7] << 16 if len(rx) == 9 else None

    def measure(self, dur, ids, target=None):
        """連續輪詢 Status 量各 ID 開啟時間(取樣保持)。
        target = None:量 dur 秒;target = pulse 數:同上位機每 250 ms 讀一次 0x03 流量,
        到量就停(dur 為逾時上限)。回傳 {ID: 開啟 ms}、樣本數、平均間隔 ms、實際經過秒數"""
        on = {i: 0.0 for i in ids}
        self.conc = {}                       # 同時開著幾台 slot(ID0~9)→ 累計秒數
        q_st, q_fl = pkt(0xB5, 0), pkt(0x03, 18)
        self.s.reset_input_buffer()
        t0 = prev = t_fl = time.perf_counter()
        n, st = 0, 0
        while prev - t0 < dur:
            read_flow = target is not None and prev - t_fl >= 0.25
            self.s.write(q_fl if read_flow else q_st)
            rx = self.s.read(9)
            now = time.perf_counter()
            for i in ids:                    # 這段時間沿用上一次的 Status
                if st >> i & 1:
                    on[i] += now - prev
            k = bin(st & 0x3FF).count('1')
            self.conc[k] = self.conc.get(k, 0.0) + now - prev
            if len(rx) == 9:
                if read_flow:
                    t_fl = now
                    if (rx[5] | rx[6] << 8 | rx[7] << 16) >= target:
                        prev = now
                        break
                else:
                    st = rx[4] | rx[5] << 8 | rx[6] << 16
                    n += 1
            prev = now
        el = prev - t0
        return {i: v * 1000 for i, v in on.items()}, n, el * 1000 / max(n, 1), el

    def pour(self, tag, groups, typ, dur=4.0, target=None):
        """送 0x07 → 量 dur 秒(或到 target 個 pulse)→ 結束封包 → 依 pulse 數判斷每個 slot 的開啟時間"""
        slots = [i for i, _ in groups if i < 10]
        uvcs = [i for i, _ in groups if 10 < i < 14]
        self.xfer(pkt(0x04, 18))
        self.check(f'{tag} 0x07 Type {typ}、N={len(groups)} {groups} → 回原封包', ff(groups, typ), ff(groups, typ))
        on, n, per, dur = self.measure(dur, slots + uvcs, target)
        if len(slots) > 1:                   # 同時開幾台(出水口噴水感指標),只印出不判斷
            tot = sum(self.conc.values()) or 1
            mx = max(k for k, v in self.conc.items() if v > 0)
            ge3 = sum(v for k, v in self.conc.items() if k >= 3) / tot * 100
            self.stats.append((mx, ge3))
            dist = ' '.join(f'{k}台 {v / tot * 100:.0f}%' for k, v in sorted(self.conc.items()) if v > 0)
            print(f'   (同時開著的 slot:最多 {mx} 台、≥3 台 {ge3:.1f}% 的時間 | {dist})\n')
        for i in uvcs:                       # UVC 不看時間值,收到 0x07 就開,一直開到 0x02
            self.result(f'{tag} UVC ID{i} 整段開著(時間值不影響)', on[i] >= dur * 1000 * 0.95,
                        [f'量到 {on[i]:.0f} ms / {dur * 1000:.0f} ms(需 ≥ 95%)'])
        self.check(f'{tag} 結束封包(0x02 遮罩 bit0~17)→ 回原封包', END, END)
        p = self.flow()
        for i, v in groups:
            if i >= 10:
                continue
            add = min(v, 150)
            ideal = (p or 0) * add
            lo, hi = ideal * 0.85 - 100, ideal * 1.1 + 50
            if ideal:
                self.errs.append((add, (on[i] / ideal - 1) * 100))
            self.result(f'{tag} slot{i} 時間值 {v}(實際用 {add})開啟時間 ≈ pulse 數 × {add}',
                        p is not None and lo <= on[i] <= hi,
                        [f'{dur:.1f} 秒內 pulse {p} 個 → 理論 {ideal} ms(允許 {lo:.0f}~{hi:.0f})',
                         f'量到 {on[i]:.0f} ms(佔 {on[i] / dur / 10:.0f}%;輪詢 {n} 次、平均每 {per:.1f} ms)'])
        return on, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True)
    ap.add_argument('--rounds', type=int, default=5, help='F9 隨機出杯輪數')
    ap.add_argument('--seed', type=int, default=None, help='F9 亂數種子(重跑同一組搭配)')
    ap.add_argument('--ml-per-pulse', type=float, default=3.2,
                    help='F9 杯量換算 pulse 數(K2 上位機 flowPulseConstant,k2-worker 預設 3.2)')
    a = ap.parse_args()
    s = serial.Serial(a.port, 115200, timeout=0.05)
    time.sleep(2.5)
    s.reset_input_buffer()
    t = T(s)

    print('══ F1 冷水、slot0 每 pulse 50 ms ══')
    t.xfer(pkt(0x04, 18))
    t.check('F1 0x07 Type 1、slot0 50 ms → 回原封包', ff([(0, 50)], 1), ff([(0, 50)], 1))
    t.check_status('F1 剛開:水泵 A13 + 冷水閥 A15(slot0 還沒累積,關著)', PUMP | COLD)
    t.xfer(END)
    t.pour('F1', [(0, 50)], 1)
    t.check_status('F1 結束後全關', 0)

    print('══ F2 slot0 每 pulse 100 ms(K2 原寫法會卡開的數值,修正後應約 60%)══')
    t.pour('F2', [(0, 100)], 1)

    print('══ F3 slot0 時間值 255(上位機最大值)→ 韌體限制為 150(ADD_MAX),異常輸入保護 ══')
    # 6 Hz 下:有限制 → 6 × 150 = 900 ms/s(約 90%);沒限制 → 6 × 255 > 1 秒(一直開,100%)
    on, _ = t.pour('F3', [(0, 255)], 1)
    t.result('F3 開啟時間 < 整段 95%(沒限制時會一直開)', on[0] < 4000 * 0.95,
             [f'量到 {on[0]:.0f} ms / 4000 ms = {on[0] / 40:.0f}%'])

    print('══ F4 常溫、3 組(ID 順序打亂:slot5、slot0、UVC12)══')
    t.xfer(pkt(0x04, 18))
    g4 = [(5, 25), (0, 50), (12, 0)]
    t.check('F4 0x07 Type 0、N=3 → 回原封包', ff(g4, 0), ff(g4, 0))
    t.check_status('F4 剛開:水泵 + 常溫閥 A14 + UVC12(carbonated A9)', PUMP | WARM | UVC12)
    t.xfer(END)
    t.pour('F4', g4, 0)
    t.check_status('F4 結束封包後全關(含 UVC12)', 0)

    print('══ F5 N=0(停料、只出水;同 K2)══')
    t.xfer(ff([(0, 150), (12, 0)], 1))
    time.sleep(1.5)
    t.check('F5 1.5 秒後送 0x07 N=0 Type 0 → 回原封包', ff([], 0), ff([], 0))
    t.check_status('F5 slot0、冷水閥關;水泵 + 常溫閥開;UVC12 重新開(上一包的,同 K2)', PUMP | WARM | UVC12)
    on, n, _, _ = t.measure(2.0, [0])
    t.result('F5 之後 2 秒 slot0 都不開', on[0] == 0, [f'slot0 開啟 {on[0]:.0f} ms(輪詢 {n} 次)'])
    t.check('F5 結束封包', END, END)

    print('══ F6 結束封包會重置 freeflow:之後只開水泵,slot 不會再開 ══')
    t.xfer(ff([(0, 150)], 1))
    time.sleep(1.0)
    t.xfer(END)
    t.check('F6 0x01 只開水泵 A13', mask_pkt(0x01, PUMP), mask_pkt(0x01, PUMP))
    on, n, _, _ = t.measure(2.0, [0])
    t.result('F6 有水流 2 秒,slot0 都不開', on[0] == 0, [f'slot0 開啟 {on[0]:.0f} ms(輪詢 {n} 次)'])
    t.check('F6 0x02 關水泵', mask_pkt(0x02, PUMP), mask_pkt(0x02, PUMP))

    print('══ F7 不支援的 Type → 9 byte [Type] FF [Type] FF,不動作 ══')
    t.check('F7 Type 2(氣泡水)', ff([(0, 50)], 2), pkt(0x07, 0x02, 0xFF, 0x02, 0xFF))
    t.check('F7 Type 5(未定義)', ff([(0, 50)], 5), pkt(0x07, 0x05, 0xFF, 0x05, 0xFF))
    t.check_status('F7 狀態仍全關', 0)

    print('══ F8 N 上限 19 ══')
    g19 = [(i, 0) for i in range(10)] + [(i, 0) for i in range(14, 23)]
    t.check('F8 N=19(64 byte)→ 回原封包 64 byte', ff(g19, 1), ff(g19, 1))
    t.check_status('F8 水泵 + 冷水閥開(時間值皆 0,slot 不開)', PUMP | COLD)
    t.xfer(END)
    g20 = [(i, 0) for i in range(20)]
    t.check('F8 N=20(67 byte,超過緩衝)→ 丟棄不回;等 50 ms 再送 PING → 只回 PING',
            ff(g20, 1), b'', wait=0.05)
    t.check('F8 (接上項)PING', pkt(0x05), pkt(0x05, 0x00, 0x04, 0x00, 0x00))
    t.check('F8 N=86(K2 長度會溢位成 9 byte)→ 丟棄不回', bytes(HDR + [0x07, 86] + [0] * 20), b'', wait=0.05)
    t.check('F8 (接上項)PING', pkt(0x05), pkt(0x05, 0x00, 0x04, 0x00, 0x00))
    t.check_status('F8 狀態全關', 0)

    seed = a.seed if a.seed is not None else int(time.time()) % 100000
    rng = random.Random(seed)
    print(f'══ F9 隨機出杯 {a.rounds} 輪(亂數種子 {seed},重跑同一組:--seed {seed})══')
    t.stats, t.errs = [], []                 # 只統計 F9
    for r in range(1, a.rounds + 1):
        typ = rng.randint(0, 1)
        cup = rng.choice(CUPS)
        target = math.ceil(cup / a.ml_per_pulse)    # 同上位機 Math.ceil(ml / flowPulseConstant)
        ids = sorted(rng.sample(range(10), rng.randint(4, 6)))
        groups, desc = [], []
        for i in ids:
            name = rng.choice(list(RATIO))
            v = rng.randint(*RATIO[name])
            groups.append((i, v))
            desc.append(f'ID{i}(slot{i + 1}) {name} → {v} ms')
        # UVC ID11~13 一起放進 0x07(使用者原設計;上位機現況沒帶),時間值隨便填,韌體不看
        for u in (11, 12, 13):
            v = rng.randint(0, 255)
            groups.append((u, v))
            desc.append(f'ID{u}(UVC) 時間值 {v}(不影響)')
        rng.shuffle(groups)                      # 送出順序打亂(韌體會排序)
        # 同一杯按 1~3 次(中途放開再按):總 pulse 切段,每段至少 MIN_SEG;放開間隔 0.5~2 秒(水泵關,只能計時)
        presses = rng.randint(1, min(3, target // MIN_SEG))
        while True:
            cuts = sorted(rng.sample(range(MIN_SEG, target - MIN_SEG + 1), presses - 1))
            segs = [b - c for c, b in zip([0] + cuts, cuts + [target])]
            if min(segs) >= MIN_SEG:
                break
        mark = f'  ⚠ 中斷 {presses - 1} 次(分 {presses} 段:{" + ".join(map(str, segs))} pulse)' if presses > 1 else ''
        print(f'── 第 {r} 輪:{"冷水" if typ else "常溫"} {cup} ml({target} pulse)、{len(ids)} 個 slot{mark}')
        for d in desc:
            print(f'   {d}')
        print(f'   0x07:{hx(ff(groups, typ))}\n')
        done, tot = 0, {i: 0.0 for i in ids}
        for j in range(presses):
            want = target - done if j == presses - 1 else segs[j]   # 最後一段補到杯量
            tag = f'F9-{r}' if presses == 1 else f'F9-{r}-第{j + 1}段'
            on, p = t.pour(tag, groups, typ, dur=60.0, target=want)   # 每段送同一包 0x07(同上位機)
            done += p or 0
            for i in ids:
                tot[i] += on[i]
            if j < presses - 1:
                gap = rng.uniform(0.5, 2.0)
                print(f'   (放開 {gap:.1f} 秒,再按)\n')
                time.sleep(gap)
        t.check_status(f'F9-{r} 結束後全關', 0)
        if presses > 1:
            t.result(f'F9-{r} ⚠ 各段 pulse 加總 = 杯量(水量接續,允許多 0~2)', target <= done <= target + 2,
                     [f'加總 {done} pulse,目標 {target}'])
            print(f'   ⚠ 第 {r} 輪整杯(中斷 {presses - 1} 次)各 slot 合計:')
            for i, v in sorted(groups):
                if i < 10:
                    ideal = done * v
                    print(f'     ID{i}(slot{i + 1}) 時間值 {v}:理論 {ideal} ms、實測 {tot[i]:.0f} ms、'
                          f'少 {ideal - tot[i]:.0f} ms({(tot[i] / ideal - 1) * 100:+.1f}%)')
            print()

    if t.stats:                              # F9 總結(比較韌體改動前後用,同一個 --seed)
        mxs, ge3s = [x[0] for x in t.stats], [x[1] for x in t.stats]
        print(f'══ F9 總結(亂數種子 {seed},共 {len(t.stats)} 段出水)══')
        print(f'   同時開著的 slot:最多(平均){sum(mxs) / len(mxs):.2f} 台、最差 {max(mxs)} 台;'
              f'≥3 台的時間平均 {sum(ge3s) / len(ge3s):.1f}%')
        for lab, cond in (('1:10', lambda v: v >= 50), ('1:32', lambda v: v < 50)):
            e = [x[1] for x in t.errs if cond(x[0])]
            if e:
                print(f'   {lab} slot 劑量誤差(每段):平均 {sum(e) / len(e):+.1f}%,範圍 {min(e):+.1f} ~ {max(e):+.1f}%(共 {len(e)} 筆)')
        print()

    t.xfer(mask_pkt(0x02, 0xFFFFFFFF))
    s.close()
    print(f'合計 {t.ok}/{t.n} PASS')


if __name__ == '__main__':
    main()
