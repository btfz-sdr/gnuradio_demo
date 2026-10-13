#!/usr/bin/env python3
"""Synthetic BLE ADV_IND (LE 1M) I/Q generator, 10 sps @ 1 Msps symbol rate."""
import numpy as np

AA_ADV = 0x8E89BED6
FS = 10e6
SYM = 1e6
SPS = int(FS / SYM)          # 10
BT = 0.5
H = 0.5                      # modulation index -> peak deviation = h*Rs/2 = 250 kHz


def swap_bits(x):
    return int(f"{x:08b}"[::-1], 2)


def whiten(data, channel):
    lfsr = swap_bits(channel) | 2
    out = np.zeros_like(data)
    for k, byte in enumerate(data):
        d = swap_bits(int(byte))
        for i in range(8):
            mask = 1 << (7 - i)
            if lfsr & 0x80:
                lfsr ^= 0x11
                d ^= mask
            lfsr = (lfsr << 1) & 0xFF
        out[k] = swap_bits(d)
    return out


def crc24(data):
    ret = 0x555555
    for byte in data:
        d = int(byte)
        for _ in range(8):
            t = (ret >> 23) & 1
            ret = (ret << 1) & 0xFFFFFF
            if (d & 1) != t:
                ret ^= 0x65B
            d >>= 1
    return ret


def bytes_to_air_bits(data):
    return np.array([(int(b) >> i) & 1 for b in data for i in range(8)], dtype=np.uint8)


def adv_pdu(addr=b"\x99\x88\x77\x66\x55\x44", name=b"\x02\x01\x06"):
    payload = addr + name
    pdu = np.frombuffer(bytes([0x40, len(payload)]) + payload, dtype=np.uint8)
    c = crc24(pdu)
    # CRC is shifted out MSB-byte-first on air, with each byte bit-reversed
    sw = lambda x: int(f"{x:08b}"[::-1], 2)
    crc = np.array([sw((c >> 16) & 0xFF), sw((c >> 8) & 0xFF), sw(c & 0xFF)], dtype=np.uint8)
    raw = np.concatenate([pdu, crc])
    return raw, whiten(raw, 37)


def build_bits(channel=37):
    pre = np.array([(0xAA >> i) & 1 for i in range(8)], dtype=np.uint8)
    aa = np.array([(AA_ADV >> i) & 1 for i in range(32)], dtype=np.uint8)
    raw, wh = adv_pdu()
    return np.concatenate([pre, aa, bytes_to_air_bits(wh)]), raw


def gaussian_taps(sps, bt=BT, os=16, span=3.0):
    """Discrete Gaussian LPF taps (unit DC gain), sigma = sqrt(ln2)/(2*pi*BT) symbols."""
    N = sps * os
    n = np.arange(-int(span * N), int(span * N) + 1)
    t = n / N                                    # symbol units
    sigma = np.sqrt(np.log(2)) / (2 * np.pi * bt)
    g = np.exp(-t ** 2 / (2 * sigma ** 2))
    g /= g.sum()                                 # unit DC gain
    return g, N


def gfsk(bits, sps=SPS, bt=BT, dev=H * SYM / 2, fs=FS, os=16):
    """GFSK: NRZ (+-1, 1 symbol wide) -> Gaussian LPF -> FM.

    For a long run of identical bits the instantaneous frequency is flat at
    +-dev (2*pi*dev/fs rad per sample), exactly like gnuradio's gfsk_mod which
    convolves firdes.gaussian() with a 1-symbol rectangular window.
    """
    a = 2.0 * bits.astype(np.float64) - 1.0
    nrz = np.repeat(a, sps * os)                 # rectangular NRZ at 16 samples/symbol
    g, N = gaussian_taps(sps, bt, os)
    shape = np.convolve(nrz, g, mode="same")[: len(nrz)]
    shape = shape.reshape(-1, os).mean(axis=1)[: len(a) * sps]
    phase = 2 * np.pi * dev / fs * np.cumsum(shape)
    return np.exp(1j * phase)


def make_packet(snr_db=20.0, freq_off_hz=0.0, frac_delay=0.37, channel=37,
                amp=1200.0, invert=False, phase0=0.3, pre_sym=4, post_sym=24, seed=1234):
    np.random.seed(seed)
    bits, raw = build_bits(channel)
    x = np.concatenate([np.zeros(pre_sym * SPS), gfsk(bits), np.zeros(post_sym * SPS)])
    n = len(x)
    X = np.fft.fft(x)
    X *= np.exp(-2j * np.pi * np.fft.fftfreq(n) * frac_delay)
    x = np.fft.ifft(X)
    x *= np.exp(2j * np.pi * freq_off_hz / FS * np.arange(n))
    x *= np.exp(1j * phase0)
    if invert:
        x = np.conj(x)
    # 先把"信号"归一化到目标幅度，再按目标 SNR 加噪。
    # 注意不要在加噪之后按总峰值归一化：噪声会反过来改变信号电平，
    # 让 SNR 标签失去意义（实测会出现 10 dB 能解、20 dB 解不出的非单调行为）。
    x *= amp / (np.max(np.abs(x)) + 1e-12)
    p_sig = np.mean(np.abs(x) ** 2)
    n0 = p_sig / (10 ** (snr_db / 10.0))
    y = x + (np.random.randn(n) + 1j * np.random.randn(n)) * np.sqrt(n0 / 2)
    iq = np.empty(2 * n, dtype=np.int16)
    iq[0::2] = np.round(y.real).clip(-32767, 32767)
    iq[1::2] = np.round(y.imag).clip(-32767, 32767)
    return iq, bits, raw


if __name__ == "__main__":
    import sys
    snr = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    inv = len(sys.argv) > 2 and sys.argv[2] == "inv"
    iq, bits, raw = make_packet(snr, freq_off_hz=0, invert=inv)
    fn = "synth_snr%d%s.bin" % (int(snr), "_inv" if inv else "")
    iq.tofile(fn)
    print("wrote", fn, len(iq) // 2, "samples;", len(bits), "bits;",
          "first bytes", bytes(raw[:6]).hex(), "raw total", bytes(raw).hex())
