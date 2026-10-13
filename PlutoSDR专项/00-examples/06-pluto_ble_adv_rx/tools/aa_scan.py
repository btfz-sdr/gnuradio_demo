#!/usr/bin/env python3
"""Find BLE advertising packets in a raw capture: matched filter + AA correlator
searched over all 10 timing phases (no clock-recovery loop needed for detection)."""
import sys
import numpy as np

AA_ADV = 0x8E89BED6
SPS = 10


def aa_pattern():
    pre = np.array([(0xAA >> i) & 1 for i in range(8)], dtype=np.float64)
    aa = np.array([(AA_ADV >> i) & 1 for i in range(32)], dtype=np.float64)
    return np.concatenate([pre, aa]) * 2 - 1


def demask(iq, lpf=SPS):
    pd = np.angle(iq[1:] * np.conj(iq[:-1])).astype(np.float32)
    c = np.concatenate([[0.0], np.cumsum(pd, dtype=np.float64)])
    y = np.empty(len(pd))
    y[lpf - 1:] = c[lpf:] - c[:-lpf]
    y[:lpf - 1] = c[1:lpf]
    return y.astype(np.float32)


def scan(iq, chan=37, thresh=0.5, p_len=40, verbose=True):
    f = demask(iq)
    p = aa_pattern()
    n = len(f)
    hits = []
    for ph in range(SPS):
        s = f[ph::SPS]
        m = len(s)
        if m < p_len + 8:
            continue
        # correlation of s with p (length 48)
        c = np.convolve(s, p[::-1], mode="valid")
        # local energy of s
        en = np.concatenate([[0.0], np.cumsum(s.astype(np.float64) ** 2)])
        e = en[p_len:] - en[: len(en) - p_len]
        r = c / np.sqrt(np.maximum(e, 1e-12) * np.sum(p ** 2))
        idx = np.where(r > thresh)[0]
        for i in idx:
            hits.append((float(r[i]), i * SPS + ph, ph))
    # dedupe by sample position
    hits.sort(key=lambda t: -t[0])
    keep = []
    for r, pos, ph in hits:
        if all(abs(pos - k[1]) > 300 for k in keep):
            keep.append((r, pos, ph))
    if verbose:
        print(f"  chan {chan}: candidates={len(hits)} unique={len(keep)}")
        for r, pos, ph in keep[:10]:
            print(f"     r={r:.3f} pos={pos} ({pos/1e7*1e3:.3f} ms) phase={ph}")
    return keep


if __name__ == "__main__":
    thresh = float(sys.argv[1])
    tot = 0
    for path in sys.argv[2:]:
        d = np.fromfile(path, dtype=np.int16).astype(np.float32)
        iq = d[0::2] + 1j * d[1::2]
        chan = 37
        if "ch38" in path:
            chan = 38
        elif "ch39" in path:
            chan = 39
        print(path, f"({len(iq)/1e7:.2f} s)")
        tot += len(scan(iq, chan, thresh))
    print("TOTAL unique candidates:", tot)
