#!/usr/bin/env python3
"""候选点 -> 完整解码（按候选自己的定时相位切片 -> 解白化 -> CRC-24），把真包和误报分开。

独立参考实现：不共用 ble_phy.c，用来核对 C 解调器是否漏包。

用法:
    python3 decode_cand.py <iq.bin> <信道 37|38|39> [相关阈值=0.90]

判据: 阈值只决定"有多少候选"，候选数不是包数（噪声也会偶然出现 r≈0.90）。
真包以 CRC-OK 为准。
"""
import sys
import numpy as np

from aa_scan import demask, scan, SPS
from gen_ble import whiten, crc24

# 字节内比特反转表（CRC-24 在空口是 swap(高), swap(中), swap(低)）
SW = [int(f"{x:08b}"[::-1], 2) for x in range(256)]


def crc_bytes(reg):
    return bytes([SW[(reg >> 16) & 0xFF], SW[(reg >> 8) & 0xFF], SW[reg & 0xFF]])


def try_decode(f, pos, ph, chan):
    """pos = 候选窗口起点样点号, ph = 该候选的定时相位"""
    s = f[ph::SPS]
    i = (pos - ph) // SPS
    if i < 0 or i + 40 + 16 > len(s):
        return None
    bits = (s[i:] > 0).astype(np.uint8)          # bit0 = 前导码第 1 位
    body = bits[40:]                             # 跳过前导码 + 32bit AA
    nb = len(body) // 8
    if nb < 2:
        return None
    raw = np.array([sum(int(body[k * 8 + o]) << o for o in range(8)) for k in range(nb)],
                   dtype=np.uint8)
    d = whiten(raw, chan)                        # 白化就是异或，同一个函数即解白化
    plen = int(d[1]) & 0x3F
    tot = 2 + plen + 3
    if tot < 6 or tot > len(d):
        return None
    exp = crc_bytes(crc24(d[:tot - 3]))
    got = bytes(d[tot - 3:tot])
    return {"pdu": d[:tot], "plen": plen, "tot": tot,
            "crc_ok": exp == got, "exp": exp, "got": got}


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        raise SystemExit(2)
    path = sys.argv[1]
    chan = int(sys.argv[2]) if len(sys.argv) > 2 else 37
    th = float(sys.argv[3]) if len(sys.argv) > 3 else 0.90

    d = np.fromfile(path, dtype=np.int16).astype(np.float32)
    iq = d[0::2] + 1j * d[1::2]
    f = demask(iq)
    hits = scan(iq, chan, th, verbose=False)     # 全 10 相位，已按样点位置去重

    ok = 0
    for r, pos, ph in hits:
        res = try_decode(f, pos, ph, chan)
        tag = "（相位/长度不成立）"
        if res:
            tag = (f"plen={res['plen']} crc={'OK' if res['crc_ok'] else 'no'} "
                   f"pdu={bytes(res['pdu']).hex()}")
            if res["crc_ok"]:
                ok += 1
            elif r > 0.97:
                tag += f"  exp={res['exp'].hex()} got={res['got'].hex()}"
        if r > 0.97 or (res and res["crc_ok"]):
            print(f"  pos={pos} phase={ph} r={r:.3f} {tag}")

    print(f"{path} chan={chan} th={th}: candidates={len(hits)} CRC-OK={ok}")
