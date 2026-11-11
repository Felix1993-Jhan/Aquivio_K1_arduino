"""K2 移植版(main-k2port-v4)UART 收包測試:標頭逐 byte 檢查、CS、收包逾時、黏包、回覆格式。

用法:python -I -X utf8 tools/portK2_tool/k2p_uart_test.py --port COM28
每項印出 TX / 預期 / RX(hex)與結果。只走序列埠收發,不會讓泵 / 閥動作。

適用韌體:main-k2port-v4 P1 起(正式 / 除錯版皆可,115200)
最後驗證:P1,12 / 12 PASS(2026-11-11)
注意:T2、T3 的預期是 P1 的暫時行為(任何 CMD 都回 PING 內容);
      各命令實作後要改預期(舊判斷用註解保留),未知 CMD 屆時改為不回(同 K2 查表)。"""
import argparse, time, serial

HDR = [0x40, 0x71, 0x30]
VER = [0x00, 0x04, 0x00, 0x00]          # 0.0.4.0


def cs(b):
    return (256 - (sum(b) & 0xFF)) & 0xFF


def pkt(cmd, data=(0, 0, 0, 0)):
    b = HDR + [cmd] + list(data)
    return bytes(b + [cs(b)])


def reply(cmd, data):
    return pkt(cmd, data)


def hx(b):
    return ' '.join(f'{x:02X}' for x in b) if b else '(無)'


def run(s, name, steps, expect):
    """steps: [(bytes, 之後等待秒數), ...];expect: 預期收到的全部 bytes"""
    s.reset_input_buffer()
    tx_all = []
    for data, wait in steps:
        s.write(data)
        tx_all.append(hx(data))
        time.sleep(wait)
    time.sleep(0.2)
    rx = s.read(s.in_waiting or 0)
    ok = rx == expect
    print(f'■ {name}')
    for i, t in enumerate(tx_all):
        print(f'   TX{i + 1}: {t}')
    print(f'   預期: {hx(expect)}')
    print(f'   RX  : {hx(rx)}')
    print(f'   結果: {"PASS" if ok else "FAIL"}\n')
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True)
    a = ap.parse_args()
    s = serial.Serial(a.port, 115200, timeout=0.1)
    time.sleep(2.5)                      # 開 port 會重置板子,等 bootloader 跑完(P1 開機不送 ready)
    s.reset_input_buffer()

    bad = bytearray(pkt(0x05)); bad[-1] ^= 0x01
    tests = [
        ('T1 PING 0x05', [(pkt(0x05), 0)], reply(0x05, VER)),
        ('T2 其他 CMD 0x01(P1:回 PING 內容)', [(pkt(0x01), 0)], reply(0x01, VER)),
        ('T3 未知 CMD 0x99(P1:也回;K2 不回)', [(pkt(0x99), 0)], reply(0x99, VER)),
        ('T4 CS 錯誤 → [4..7] 填 FF', [(bytes(bad), 0)], reply(0x05, [0xFF] * 4)),
        ('T5 標頭錯誤 41 71 30 → 不回', [(bytes([0x41, 0x71, 0x30, 0x05, 0, 0, 0, 0, 0]), 0)], b''),
        ('T6 半包 5 byte,等 50 ms 逾時,再送完整 PING → 只回一包',
         [(pkt(0x05)[:5], 0.05), (pkt(0x05), 0)], reply(0x05, VER)),
        ('T7 兩包連在一起送 → 回兩包',
         [(pkt(0x05) + pkt(0x02), 0)], reply(0x05, VER) + reply(0x02, VER)),
        # 〔2026-11-11〕標頭改逐 byte 檢查(與 K2 不同):前面有雜訊也能收到
        ('T8 前面 1 byte 雜訊 11 + PING → 回 PING',
         [(bytes([0x11]) + pkt(0x05), 0)], reply(0x05, VER)),
        ('T9 前面 2 byte 雜訊 11 22 + CMD 0x01 → 回 0x01',
         [(bytes([0x11, 0x22]) + pkt(0x01), 0)], reply(0x01, VER)),
        ('T10 重複的 40:40 + PING → 回 PING',
         [(bytes([0x40]) + pkt(0x05), 0)], reply(0x05, VER)),
        ('T11 標頭斷在一半:40 71 11 + PING → 回 PING',
         [(bytes([0x40, 0x71, 0x11]) + pkt(0x05), 0)], reply(0x05, VER)),
        ('T12 標頭斷在一半且接 40:40 71 + PING → 回 PING',
         [(bytes([0x40, 0x71]) + pkt(0x05), 0)], reply(0x05, VER)),
    ]
    n_ok = sum(run(s, *t) for t in tests)
    s.close()
    print(f'合計 {n_ok}/{len(tests)} PASS')


if __name__ == '__main__':
    main()
