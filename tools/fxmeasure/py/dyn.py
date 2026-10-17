"""Dynamics measurements (static curves, step responses) on any hosted effect. numpy only."""
import sys; sys.path.insert(0, '.')
from sig import *

SR = 48000
F0 = 1000.0


def analytic(x):
    n = len(x); X = np.fft.fft(x); h = np.zeros(n); h[0] = 1
    h[1:(n + 1) // 2] = 2
    if n % 2 == 0: h[n // 2] = 1
    return np.fft.ifft(X * h)


def stair(levels_db, hold=0.5, f=F0, sr=SR):
    """sine stair: one level per `hold` seconds (phase continuous)"""
    n = int(hold * sr)
    t = np.arange(n * len(levels_db)) / sr
    env = np.repeat(10 ** (np.array(levels_db) / 20), n)
    return np.sin(2 * np.pi * f * t) * env, n


def stair_read(y, x, n, latency=0, tail=0.3, sr=SR):
    """output level (dB, sine amplitude) at the end of each stair step, and input level"""
    y = y[latency:]; m = len(y) // n
    outs, ins = [], []
    k = int(tail * sr) if tail < 1 else int(tail)
    for i in range(m):
        seg = y[i * n + n - k:(i + 1) * n]; sx = x[i * n + n - k:(i + 1) * n]
        outs.append(20 * np.log10(np.sqrt(2 * np.mean(seg ** 2)) + 1e-12))
        ins.append(20 * np.log10(np.sqrt(2 * np.mean(sx ** 2)) + 1e-12))
    return np.array(ins), np.array(outs)


def burst(lo_db, hi_db, t_lo=1.0, t_hi=1.0, t_after=2.0, f=F0, sr=SR):
    a = np.concatenate([np.full(int(t_lo * sr), lo_db), np.full(int(t_hi * sr), hi_db), np.full(int(t_after * sr), lo_db)])
    t = np.arange(len(a)) / sr
    return np.sin(2 * np.pi * f * t) * 10 ** (a / 20)


def gain_env(y, x, latency=0, smooth=48):
    """gain (dB) over time: |analytic(out)| / |analytic(in)|, both smoothed over `smooth` samples"""
    if latency: y = np.concatenate([y[latency:], np.zeros(latency)])
    ay = np.abs(analytic(y)); ax = np.abs(analytic(x))
    k = np.ones(smooth) / smooth
    ay = np.convolve(ay, k, 'same'); ax = np.convolve(ax, k, 'same')
    return 20 * np.log10((ay + 1e-12) / (ax + 1e-12))


def time_to(g, start, frac, sr=SR):
    """time (ms) after `start` until g has covered `frac` of its change (start value -> final value of the segment)"""
    g0 = g[start - 10]; gend = g[min(len(g) - 1, start + int(0.9 * sr))]
    target = g0 + frac * (gend - g0)
    seg = g[start:start + int(0.95 * sr)]
    if gend < g0:
        idx = np.argmax(seg <= target)
    else:
        idx = np.argmax(seg >= target)
    return idx / sr * 1000, g0, gend
