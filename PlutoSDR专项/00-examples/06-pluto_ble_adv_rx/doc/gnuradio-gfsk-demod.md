# GNU Radio GFSK Demod 对照参考

本工程解调器（`ble_phy.c`）按 GNU Radio `digital.gfsk_demod` 的思路写，
这份文档记录**参考实现的确切行为**、以及**为什么不能 1:1 照搬**。
所有参数都从本机 GNU Radio 源码（`~/Desktop/B210/gnuradio`）逐行核对过。

---

## 1. 参考链路与源码位置

| 级 | 模块 | 源文件 |
|---|---|---|
| 组合 | `digital.gfsk_demod` | `gr-digital/python/digital/gfsk.py` (`gfsk_demod.__init__`) |
| 鉴频 | `analog.quadrature_demod_cf` | `gr-analog/lib/quadrature_demod_cf_impl.cc` |
| 定时 | `digital.clock_recovery_mm_ff` | `gr-digital/lib/clock_recovery_mm_ff_impl.cc` |
| 插值 | MMSE FIR 插值器 | `gr-filter/lib/mmse_fir_interpolator_ff.cc`、`interpolator_taps.h` |
| 判决 | `digital.binary_slicer_fb` | `gr-digital/lib/binary_slicer_fb_impl.cc` |

`gfsk.py` 里 `gfsk_demod` 的连接关系（源码 234~248 行）：

```python
self.fmdemod      = analog.quadrature_demod_cf(1.0 / sensitivity)   # sensitivity 默认 1
self.clock_recovery = digital.clock_recovery_mm_ff(
                        self._omega, self._gain_omega, self._mu,
                        self._gain_mu, self._omega_relative_limit)
self.slicer       = digital.binary_slicer_fb()
self.connect(self, self.fmdemod, self.clock_recovery, self.slicer, self)
```

即：**鉴频 → M&M 定时恢复 → 0 门限硬判决**，没有匹配滤波、没有 DC 消除。

---

## 2. 参考实现的确切参数 / 公式

### 2.1 鉴频（`quadrature_demod_cf_impl.cc` 62、64 行）

```c
volk_32fc_x2_multiply_conjugate_32fc(&tmp[0], &in[1], &in[0], noutput_items);
out[i] = d_gain * gr::fast_atan2f(imag(tmp[i]), real(tmp[i]));
```

即 `tmp[n] = in[n] * conj(in[n-1])`，

```
out[n] = gain * atan2( Q[n]*I[n-1] - I[n]*Q[n-1] , I[n]*I[n-1] + Q[n]*Q[n-1] )
```

本工程用的是同一个式子（`ble_phy.c` 第 1 级），无差别。
`gfsk_demod` 默认 `sensitivity = 1`（`gfsk.py` 第 43 行 `_def_sensitivity = 1`），
所以 gain = 1.0，输出单位就是 **rad/样点**（正好等于 2π·Δf/fs）。

### 2.2 M&M 定时环（`clock_recovery_mm_ff_impl.cc` 99~108 行）

```c
out[oo] = d_interp->interpolate(&in[ii], d_mu);
mm_val  = slice(d_last_sample) * out[oo] - slice(out[oo]) * d_last_sample;
d_last_sample = out[oo];
d_omega = d_omega + d_gain_omega * mm_val;
d_omega = d_omega_mid + branchless_clip(d_omega - d_omega_mid, d_omega_lim);
d_mu    = d_mu + d_omega + d_gain_mu * mm_val;
ii     += (int)floor(d_mu);
d_mu    = d_mu - floor(d_mu);
```

注意两点（和很多"仿写版"不一样）：

- 相位累加是 `d_mu += d_omega + gain_mu*mm_val`，**不是** `mu += 1/omega`；
- 插值用 `d_interp->interpolate()` 即 **8 抽头 / 128 相 MMSE FIR 插值器**
  （`interpolator_taps.h`：`NTAPS = 8`、`NSTEPS = 128`），不是相邻两点线性插值。

### 2.3 参数默认值（`gfsk.py`）

| 参数 | 默认 | 出处 |
|---|---|---|
| `sensitivity` | 1 | 第 43 行 `_def_sensitivity = 1` |
| `mu` | 0.5 | 第 49 行 `_def_mu = 0.5` |
| `freq_error` | 0.0 | 第 50 行（→ `omega = sps * (1+freq_error)`） |
| `omega_relative_limit` | 0.005 | 第 51 行 |
| `gain_mu` | 0.175 | 第 223~224 行（`if not gain_mu: self._gain_mu = 0.175`） |
| `gain_omega` | `0.25 * gain_mu^2` = 0.00766 | 第 226 行「critically damped」 |

（发送端 `gfsk_mod` 参考值：`bt = 0.35`、`ntaps = 4 * sps`，
见第 44、96 行；本工程合成包生成器用 BT=0.5，接近 BLE 规范的 0.5。）

---

## 3. 为什么不能 1:1 照搬（实测结论）

在真实空口录波（10 Msps、信道 37）和合成包上都量过：

| 问题 | 现象 | 结论 |
|---|---|---|
| 无匹配滤波 | 10 Msps 下单样点鉴频的相位步进只有 ±0.157 rad，噪声按 5 MHz 级带宽进来；裸 GR 链在合成 20 dB 信号上解不出 152 bit 的包 | 判决前必须做 1 符号积分（本工程加的 boxcar），约 +10 dB |
| M&M 前导码太短 | BLE 广播包前导码只有 8 bit；M&M 的两个稳定平衡点之一是**眼图交叉点**，实测环会锁在交叉点上（软判决幅度 0.16，而正确采样点 1.5） | 不用反馈环，改全相位并行前馈搜索 |
| DC 扣除 | 曾按"要消残余 LO 偏置"的直觉加了门控 DC 跟踪，实测**有害**：估计器收敛到噪声的不对称性（-0.061 rad/样点）而非包处的眼图偏置（~0），2.1 s 录波 6 包 vs 不扣 16 包 | 与 GR 一致：**不扣 DC**（见 `doc/demodulator.md` §2 问题 4） |

反过来说，本工程保留 GR 语义的部分：鉴频公式完全一致，
M&M 里"软化判决 + 相位/频率双增益"的思想被"10 个相位并行窗口 + 汉明距离"替代，
判决依然是 0 门限硬判决。

---

## 4. 想进一步对齐 GR 时看哪里

- 要 bit-for-bit 对比 GR：用 `gr_gfsk.py` 那种"GNU Radio 跑同一段录波、导出比特流"的方式，
  和 `make replay` 的输出逐比特 diff。差异只会来自上面第 3 节的三点。
- MMSE 插值器抽头表如果要搬：`gr-filter/include/gnuradio/filter/interpolator_taps.h`，
  由 `gr-filter/lib/gen_interpolator_taps/gen_interpolator_taps.c` 生成，
  搬之前先确认生成参数（beta / 截止频率）与要对比的 GR 版本一致。
