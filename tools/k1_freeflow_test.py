"""K1 JSON freeflow 測試(mcu/jsonfreeflow):按住出水、slot 依流量比例開啟、每 200 ms 回報 pulse

適用韌體:main-v4 jsonfreeflow(2026-11-12)起;**除錯版**(K1_DEBUG_FLOW_GEN)+ 杜邦線 D22 → A8
          slot 開啟時間用除錯版 stop 時印的統計量(freeflow stat slot ms / open),正式版沒有這兩行
最後驗證:jsonfreeflow(2026-11-12,除錯版)F1~F6 + F9 5 杯(10 段)共 60 / 60 PASS(--seed 43176;
          slot 劑量誤差平均 −3.3%、最差 −13.9% / +8.0%;同時開最多 4 台、≥3 台平均 18.0% 的時間)

⚠ 會讓泵 / 閥 / slot / UVC 實際動作,空板測試用。

用法(Git Bash):
    python -I -X utf8 tools/k1_freeflow_test.py --port COM28 [--rounds 5] [--seed N]

測項:
    F1  {"freeflow":"cold","slot1":24,"slot3":50},5 秒後 stop#
          回 Freeflow cold requested;每 ~200 ms 一行 freeflow pulse:N(N 遞增、約 6 pulse/s);
          不印 Progress、不送 mcuAlive;stop 立刻回 → pulse:N(≥ 最後回報)→ cold water usage → 統計 → finish → end;
          slot 開啟時間在允許範圍
    F2  {"freeflow":"warm"}(不帶 slot = 只出水),3 秒後 stop# → Freeflow warm requested、warm water usage
    F3  {"freeflow":"hot"} → Failed to parse JSON: freeflow、沒有 end / 回報;接著 ping# 立刻回 pong
    F4  freeflow 中:1.0 s ping#、1.5 s pressure#、2.0 s stopslot1#、2.5 s 出杯 JSON、4.0 s stop#
          pressure、stopslot1 立刻回;slot1 之後不再開;pong 與出杯在 end 之後才執行
    F5  freeflow 結束後出一杯 {"cold":"0.05"} → 照常到量停水(finish: stopped by flow pulse threshold)
    F6  時間值:slot1 50、slot2 "100"(字串)、slot3 255(夾到 150)、slot4 0(不用),8 秒
    F9  隨機出杯(同 K2 移植版 k2p_freeflow_test F9):每輪隨機冷 / 常溫、slot1~10 挑 4~6 個、
          各給 1:10(72)或 1:32(21~23);杯量 250 / 330 / 500 ml 以 ceil(ml / 3.3) 換成 pulse 數,
          看 freeflow pulse:N 到量就送 stop#;每杯隨機按 1~3 次(每段 ≥ 10 pulse、放開 0.5~2 秒,每段送同一包)
          印出亂數種子,FAIL 時用 --seed 重跑同一組

slot 開啟時間的允許範圍(韌體每 1 ms 依腳位狀態累計,精度 1 ms):
    注入量 = 起點 s(依 slot 編號輪流 17 / 50 / 83)+ pulse 數 P × 時間值 a(上限 150)
    結束時 acc 殘量 < 250(開到一半被 stop 時含未扣的 100),stop 前後流量計可能多 1 個 pulse
    → s + P × a − 250 − a − 5  ≤  實測  ≤  s + P × a + 5
"""

import argparse
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from k1_harness import Board, Checks, first_t, show   # noqa: E402

FAST_MS = 300                 # 「立刻」:送出到收到回傳的上限(含 9600 baud 與 USB 延遲)
ACC_START = (17, 50, 83)      # 同 jsonfreeflow.cpp
ADD_MAX = 150
UL_PER_PULSE = 3300           # 同 flowmeter.h FLOW_UL_PER_PULSE
RATIO = {'1:10': (72, 72), '1:32': (21, 23)}   # 同 k2p_freeflow_test(使用者 2026-11-11 以上位機公式算出)
CUPS = (250, 330, 500)
MIN_SEG = 10


def parse(L):
    """從一段回傳取出:回報 [(t, N)]、stop 後 pulse:N、slot 統計 {編號: ms}、同時開台數 {台數: ms}"""
    rep = [(t, int(s.split(':')[1])) for t, s in L if s.startswith('freeflow pulse:')]
    pulse = next((int(s.split(':')[1]) for _, s in L if s.startswith('pulse:')), None)
    slot_ms, open_ms = {}, {}
    for _, s in L:
        if s.startswith('freeflow stat slot ms:'):
            slot_ms = {int(k): int(v) for k, v in (x.split('=') for x in s.split(':', 1)[1].split())}
        elif s.startswith('freeflow stat open:'):
            open_ms = {int(k): int(v) for k, v in (x.split('=') for x in s.split(':', 1)[1].split())}
    return rep, pulse, slot_ms, open_ms


def dose(slots, slot_ms, P, skip=()):
    """slots = {slot 編號: 送出的值};回傳 [(編號, a, s, 理論 P×a, 實測, 是否在範圍內)]"""
    out, k = [], 0
    for n in sorted(slots):
        a = min(int(slots[n]), ADD_MAX)
        if a == 0:
            continue
        s = ACC_START[k % 3]
        k += 1
        if n in skip:
            continue
        th = P * a
        m = slot_ms.get(n)
        ok = m is not None and s + th - 250 - a - 5 <= m <= s + th + 5
        out.append((n, a, s, th, m, ok))
    return out


def print_dose(rows):
    for n, a, s, th, m, ok in rows:
        err = f'{(m - th) / th * 100:+.1f}%' if m is not None and th else '—'
        print(f'     slot{n}:時間值 {a}、起點 {s} → 理論 {th} ms,實測 {m} ms({err}){"" if ok else "  ← 超出範圍"}')


def session(b, json_cmd, stop_when, max_s=60, extra=()):
    """送 json_cmd(與 extra 時間表);stop_when(回報[(t, N)], 經過 ms) 成立時送 stop#,收到 end 後返回。
    回傳 (回傳行 [(相對 ms, 字串)], 送出 stop# 的相對 ms)"""
    i0 = len(b.rd.lines)
    ts = b.now()
    plan = sorted([(0.0, json_cmd)] + list(extra), key=lambda x: x[0])
    t_stop = None
    while b.now() - ts < max_s * 1000:
        b.rd.pump()
        while plan and b.now() - ts >= plan[0][0] * 1000:
            b.ser.write(plan.pop(0)[1])
        L = [(t - ts, s.strip()) for t, s in b.rd.lines[i0:] if s.strip()]
        if t_stop is None and stop_when(parse(L)[0], b.now() - ts):
            b.ser.write(b'stop#')
            t_stop = b.now() - ts
        if t_stop is not None and not plan and any(s == 'end' and t >= t_stop for t, s in L):
            break
    b.idle(0.3)
    return [(t - ts, s.strip()) for t, s in b.rd.lines[i0:] if s.strip()], t_stop


def check_basic(c, tag, L, t_stop, water):
    """F1 / F2 共用:開始字串、回報節奏、結束順序"""
    rep, pulse, _, _ = parse(L)
    t_end = first_t(L, lambda s: s == 'end')
    c.check(f'{tag} 回 Freeflow {water} requested', first_t(L, lambda s: s == f'Freeflow {water} requested') is not None)
    iv = sorted(b - a for (a, _), (b, _) in zip(rep, rep[1:]))
    med = iv[len(iv) // 2] if iv else None
    c.check(f'{tag} 回報間隔 ≈ 200 ms', med is not None and 170 <= med <= 230,
            f'{len(rep)} 筆、中位數 {med:.0f} ms' if med else '沒有回報')
    c.check(f'{tag} 回報 N 不遞減', all(b >= a for (_, a), (_, b) in zip(rep, rep[1:])))
    if rep:
        rate = rep[-1][1] / (rep[-1][0] / 1000)
        c.check(f'{tag} 流量約 6 pulse/s(D22 模擬)', 5.0 <= rate <= 7.0, f'{rate:.2f} pulse/s')
    t_s = first_t(L, lambda s: s == 'stop')
    c.check(f'{tag} stop 立刻回', t_s is not None and t_s - t_stop <= FAST_MS,
            f'{t_s - t_stop:.0f} ms' if t_s else '沒收到')
    c.check(f'{tag} 結束 pulse:N ≥ 最後回報', pulse is not None and rep and pulse >= rep[-1][1],
            f'pulse:{pulse}、最後回報 {rep[-1][1] if rep else None}')
    order = [s.split(':')[0] for _, s in L if t_end is not None and t_s is not None and t_s <= _ <= t_end]
    want = ['stop', 'pulse', f'{water} water usage', 'freeflow stat slot ms', 'freeflow stat open',
            'finish', 'end']
    c.check(f'{tag} 結束順序', order == want, f'{order}')
    c.check(f'{tag} finish 字串', 'finish: freeflow stopped by external stop' in [s for _, s in L])
    t_req = first_t(L, lambda s: s == f'Freeflow {water} requested')   # 開始前的 mcuAlive 是待機中送的,不算
    c.check(f'{tag} 不印 Progress、進行中不送 mcuAlive',
            not any(s.startswith('Progress') or (s == 'mcuAlive' and (t_req or 0) <= t < (t_end or 1e9))
                    for t, s in L))


def main():
    ap = argparse.ArgumentParser(description='K1 JSON freeflow 測試(除錯版 + D22 → A8)')
    ap.add_argument('--port')
    ap.add_argument('--rounds', type=int, default=5, help='F9 隨機出杯輪數')
    ap.add_argument('--seed', type=int, default=None, help='F9 亂數種子(重跑同一組搭配)')
    a = ap.parse_args()

    b = Board(a.port)
    c = Checks()
    try:
        # ── F1 ──
        cmd = b'{"freeflow":"cold","slot1":24,"slot3":50}#'
        print(f'\n══ F1 冷水 + slot1 24、slot3 50,5 秒後 stop#\n   TX {cmd.decode()}')
        L, t_stop = session(b, cmd, lambda r, t: t >= 5000)
        show([(t, s) for t, s in L if not s.startswith('freeflow pulse:')])
        rep, P, sm, om = parse(L)
        print(f'   (freeflow pulse 回報 {len(rep)} 行:{[n for _, n in rep[:6]]} … {[n for _, n in rep[-3:]]})')
        c.check('F1 回顯 / start / Received actions',
                [s for _, s in L if s != 'mcuAlive'][:3] ==
                [cmd.decode()[:-1], 'start', 'Received actions: ' + cmd.decode()[:-1]],
                f'{[s for _, s in L if s != "mcuAlive"][:3]}')
        check_basic(c, 'F1', L, t_stop, 'cold')
        rows = dose({1: 24, 3: 50}, sm, P)
        print_dose(rows)
        c.check('F1 slot 開啟時間在範圍內', rows and all(r[5] for r in rows))
        c.check('F1 統計 open 總和 ≈ 出水時間', abs(sum(om.values()) - t_stop) <= 300,
                f'{sum(om.values())} ms vs stop 於 {t_stop:.0f} ms')

        # ── F2 ──
        cmd = b'{"freeflow":"warm"}#'
        print(f'\n══ F2 常溫、不帶 slot(只出水),3 秒後 stop#\n   TX {cmd.decode()}')
        L, t_stop = session(b, cmd, lambda r, t: t >= 3000)
        show([(t, s) for t, s in L if not s.startswith('freeflow pulse:')])
        check_basic(c, 'F2', L, t_stop, 'warm')
        _, _, sm, om = parse(L)
        c.check('F2 沒有 slot 開啟', sm == {} and set(om) == {0}, f'slot {sm}、open {om}')

        # ── F3 ──
        print('\n══ F3 值錯誤 {"freeflow":"hot"} → 不啟動;接著 ping#\n   TX {"freeflow":"hot"}#、1.0 s ping#')
        L = b.run([(0.0, b'{"freeflow":"hot"}#'), (1.0, b'ping#')], 2.0)
        show(L)
        c.check('F3 Failed to parse JSON: freeflow', 'Failed to parse JSON: freeflow' in [s for _, s in L])
        c.check('F3 沒有 end、沒有回報', not any(s == 'end' or s.startswith('freeflow pulse:') for _, s in L))
        t_p = first_t(L, lambda s: s == 'pong')
        c.check('F3 ping 立刻回 pong(沒卡在忙碌)', t_p is not None and t_p - 1000 <= FAST_MS,
                f'{t_p - 1000:.0f} ms' if t_p else '沒收到')

        # ── F4 ──
        print('\n══ F4 freeflow 中收命令\n   TX {"freeflow":"cold","slot1":72,"slot2":72}#;'
              '1.0 ping#、1.5 pressure#、2.0 stopslot1#、2.5 {"slot3":"500"}#、4.0 stop#')
        L = b.run([(0.0, b'{"freeflow":"cold","slot1":72,"slot2":72}#'), (1.0, b'ping#'), (1.5, b'pressure#'),
                   (2.0, b'stopslot1#'), (2.5, b'{"slot3":"500"}#'), (4.0, b'stop#')], 7.0)
        show([(t, s) for t, s in L if not s.startswith('freeflow pulse:')])
        rep, P, sm, _ = parse(L)
        ends = [t for t, s in L if s == 'end']
        t_pr = first_t(L, lambda s: s.startswith('pressure:'))
        t_ss = first_t(L, lambda s: s == 'stopslot1')
        c.check('F4 pressure 立刻回', t_pr is not None and t_pr - 1500 <= FAST_MS,
                f'{t_pr - 1500:.0f} ms' if t_pr else '沒收到')
        c.check('F4 stopslot1 立刻回', t_ss is not None and t_ss - 2000 <= FAST_MS,
                f'{t_ss - 2000:.0f} ms' if t_ss else '沒收到')
        P2 = max([n for t, n in rep if t <= 2000] or [0]) + 1     # stopslot1 前最多的 pulse 數
        c.check('F4 slot1 在 stopslot1 後不再開', sm.get(1, 0) <= 17 + P2 * 72 + 5,
                f'slot1 {sm.get(1)} ms ≤ {17 + P2 * 72 + 5}')
        rows = dose({1: 72, 2: 72}, sm, P, skip=(1,))
        print_dose(rows)
        c.check('F4 slot2 照常', rows and all(r[5] for r in rows))
        t_pong = first_t(L, lambda s: s == 'pong')
        starts = [t for t, s in L if s == 'start']
        c.check('F4 pong 在 end 之後', t_pong is not None and ends and t_pong > ends[0])
        c.check('F4 出杯 JSON 在 end 之後開始、自己也 end',
                len(starts) == 2 and len(ends) == 2 and starts[1] > ends[0], f'start {len(starts)}、end {len(ends)}')

        # ── F5 ──
        print('\n══ F5 出杯模式沒被改壞:{"cold":"0.05"}')
        L = b.run([(0.0, b'{"cold":"0.05"}#')], 10, until_end=True)
        show(L)
        c.check('F5 到量停水', 'finish: stopped by flow pulse threshold' in [s for _, s in L])
        c.check('F5 end', any(s == 'end' for _, s in L))

        # ── F6 ──
        cmd = b'{"freeflow":"cold","slot1":50,"slot2":"100","slot3":255,"slot4":0}#'
        print(f'\n══ F6 時間值:50、"100"(字串)、255(夾到 150)、0(不用),8 秒\n   TX {cmd.decode()}')
        L, t_stop = session(b, cmd, lambda r, t: t >= 8000)
        rep, P, sm, om = parse(L)
        print(f'   pulse:{P}、slot ms {sm}、同時開 {om}')
        rows = dose({1: 50, 2: 100, 3: 255, 4: 0}, sm, P)
        print_dose(rows)
        c.check('F6 slot1~3 開啟時間在範圍內(255 以 150 計)', len(rows) == 3 and all(r[5] for r in rows))
        c.check('F6 slot4 = 0 不列入統計', 4 not in sm, f'{sorted(sm)}')

        # ── F9 ──
        seed = a.seed if a.seed is not None else random.randrange(100000)
        rnd = random.Random(seed)
        print(f'\n══ F9 隨機出杯 {a.rounds} 輪(亂數種子 {seed},重跑同一組:--seed {seed})══')
        errs, maxk, ge3 = [], [], []
        for r in range(a.rounds):
            water = rnd.choice(['cold', 'warm'])
            picks = sorted(rnd.sample(range(1, 11), rnd.randint(4, 6)))
            slots = {}
            for n in picks:
                lo, hi = RATIO[rnd.choice(list(RATIO))]
                slots[n] = rnd.randint(lo, hi)
            ml = rnd.choice(CUPS)
            total = math.ceil(ml * 1000 / UL_PER_PULSE)
            presses = rnd.randint(1, 3)
            while presses > 1 and total < MIN_SEG * presses:
                presses -= 1
            extra = total - MIN_SEG * presses                  # 每段至少 MIN_SEG,其餘隨機分配
            pts = [0] + sorted(rnd.randint(0, extra) for _ in range(presses - 1)) + [extra]
            cuts, acc = [], 0
            for p0, p1 in zip(pts, pts[1:]):
                acc += MIN_SEG + p1 - p0
                cuts.append(acc)                               # 累計目標(最後一段 = total)
            body = {'freeflow': water, **{f'slot{n}': v for n, v in slots.items()}}
            cmd = ('{' + ','.join(f'"{k}":' + (f'"{v}"' if isinstance(v, str) else str(v))
                                  for k, v in body.items()) + '}#').encode()
            print(f'\n── 第 {r + 1} 杯:{ml} ml({total} pulse)、按 {presses} 次\n   TX {cmd.decode()}')
            done = 0
            for seg in range(presses):
                target = max(1, cuts[seg] - done)
                L, t_stop = session(b, cmd, lambda rep, t, tg=target: bool(rep) and rep[-1][1] >= tg)
                rep, P, sm, om = parse(L)
                done += P or 0
                rows = dose(slots, sm, P or 0)
                k_on = max(om) if om else 0
                pct3 = sum(v for k, v in om.items() if k >= 3) / max(1, sum(om.values())) * 100
                print(f'   第 {seg + 1} 段:目標 {target} pulse → 實際 {P}(stop 於 {t_stop:.0f} ms);'
                      f'同時開最多 {k_on} 台、≥3 台 {pct3:.1f}%')
                print_dose(rows)
                c.check(f'F9 杯{r + 1} 段{seg + 1} 各 slot 在範圍內', rows and all(x[5] for x in rows))
                c.check(f'F9 杯{r + 1} 段{seg + 1} finish / end',
                        'finish: freeflow stopped by external stop' in [s for _, s in L] and
                        any(s == 'end' for _, s in L))
                errs += [(m - th) / th * 100 for _, _, _, th, m, _ in rows if m is not None and th]
                maxk.append(k_on)
                ge3.append(pct3)
                if seg < presses - 1:
                    gap = rnd.uniform(0.5, 2.0)
                    print(f'   (放開 {gap:.1f} 秒)')
                    b.idle(gap)
            c.check(f'F9 杯{r + 1} 總量達標', done >= total, f'{done} / {total} pulse')
        if errs:
            print(f'\n══ F9 總結(亂數種子 {seed},共 {len(maxk)} 段出水)══')
            print(f'   slot 劑量誤差:平均 {sum(errs) / len(errs):+.1f}%、最差 {min(errs):+.1f}% / {max(errs):+.1f}%')
            print(f'   同時開著的 slot:最多(平均){sum(maxk) / len(maxk):.2f} 台、最差 {max(maxk)} 台;'
                  f'≥3 台平均 {sum(ge3) / len(ge3):.1f}% 的時間')
    finally:
        b.close()
    sys.exit(0 if c.summary() else 1)


if __name__ == '__main__':
    main()
