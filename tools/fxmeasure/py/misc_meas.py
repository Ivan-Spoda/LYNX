"""Pro-G, Pro-DS, Pro-MB, Pro-R 2, Timeless 3 black-box measurements."""
import sys, json; sys.path.insert(0, '.')
from dyn import *


def lat_of(log):
    return [int(l.split('latency=')[1]) for l in log.splitlines() if l.startswith('processed')]


# ---------------------------------------------------------------- Pro-G
def prog():
    r = Run('Pro-G', 'pg')
    x, n = stair(list(range(-80, 1, 2)), 0.5)
    xb = burst(-60, -10, 1.0, 1.0, 2.0)
    jobs = []

    def base(style, thr=-30, ratio=100, rng=100, att=0.1, rel=150, hold=0, knee=0):
        r.settext('Style', style); r.settext('Threshold', '%g' % thr); r.settext('Ratio', '%g' % ratio)
        r.settext('Range', '%g' % rng); r.settext('Attack', '%g' % att); r.settext('Release', '%g' % rel)
        r.settext('Hold', '%g' % hold); r.settext('Knee', '%g' % knee); r.settext('Lookahead Enabled', 'Disabled')
        r.settext('Wet Level', '0'); r.set('Dry Level', 0.0); r.settext('Expert Mode', 'Off')
    for st in ['Classic', 'Clean', 'Vocal', 'Guitar', 'Upward']:
        for ratio, rng, knee in [(100, 100, 0), (2, 100, 0), (100, 20, 0), (100, 100, 12)]:
            base(st, ratio=ratio, rng=rng, knee=knee)
            jobs.append((dict(kind='static', style=st, ratio=ratio, range=rng, knee=knee), r.process(x, settle=0.3), x, n))
        for att, rel, hold in [(0.1, 150, 0), (10, 150, 0), (0.1, 30, 0), (0.1, 1000, 0), (0.1, 150, 100)]:
            base(st, att=att, rel=rel, hold=hold)
            jobs.append((dict(kind='time', style=st, att=att, rel=rel, hold=hold), r.process(xb, settle=0.3), xb, 0))
    log = r.run(timeout=3600)
    res = []
    for (c, name, xx, nn), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0]; c['latency'] = lat
        if c['kind'] == 'static':
            i, o = stair_read(y, xx, nn, lat); c['in'] = i.round(2).tolist(); c['out'] = o.round(2).tolist()
        else:
            g = gain_env(y, xx, lat)
            on, off = SR, 2 * SR
            c['open_floor_db'] = round(float(g[on - 2000]), 2); c['open_db'] = round(float(g[off - 2000]), 2)
            seg = g[on:on + SR // 2]; c['open_ms_to_-1db'] = round(float(np.argmax(seg > -1) / SR * 1000), 3)
            seg = g[off:off + 2 * SR]
            for thr in (-6, -20, -40):
                idx = np.argmax(seg < thr); c['close_ms_to_%ddb' % thr] = round(float(idx / SR * 1000), 2) if seg[idx] < thr else None
        res.append(c)
    json.dump(res, open(DATA + '/pg.json', 'w'))


# ---------------------------------------------------------------- Pro-DS
def prods():
    r = Run('Pro-DS', 'pds')
    lv = list(range(-60, 1, 3))
    hold = 0.5; nseg = int(hold * SR)
    t = np.arange(nseg * len(lv)) / SR
    env = np.repeat(10 ** (np.array(lv) / 20), nseg)
    jobs = []
    for mode in ['Single Vocal', 'Allround']:
        for bp in ['Wide Band', 'Split Band']:
            r.settext('Mode', mode); r.settext('Band Processing', bp); r.settext('Threshold', '-30'); r.settext('Range', '12')
            r.settext('Lookahead Enabled', 'Enabled'); r.settext('Lookahead', '10')
            r.settext('High-Pass Frequency', '6000'); r.settext('Low-Pass Frequency', '20000')
            for f in [1000, 4000, 8000, 12000]:
                x = np.sin(2 * np.pi * f * t) * env
                jobs.append((dict(kind='stair', mode=mode, band=bp, f=f), r.process(x, settle=0.3), x))
            # mixture: constant 500 Hz "vowel" at -12 dB + 8 kHz stair: gain seen by each part
            x = 10 ** (-12 / 20) * np.sin(2 * np.pi * 500 * t) + np.sin(2 * np.pi * 8000 * t) * env
            jobs.append((dict(kind='mix', mode=mode, band=bp), r.process(x, settle=0.3), x))
    log = r.run(timeout=3600)
    res = []
    for (c, name, x), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[lat:, 0]; c['latency'] = lat
        out = []
        for k in range(len(lv)):
            seg = slice(k * nseg + nseg // 2, (k + 1) * nseg)
            if c['kind'] == 'stair':
                out.append(round(float(db(tone_amp(y[seg], c['f'])[0] / tone_amp(x[seg], c['f'])[0])), 2))
            else:
                out.append([round(float(db(tone_amp(y[seg], f)[0] / tone_amp(x[seg], f)[0])), 2) for f in (500, 8000)])
        c['levels'] = lv; c['gain_db'] = out; res.append(c)
    json.dump(res, open(DATA + '/pds.json', 'w'))


# ---------------------------------------------------------------- Pro-MB
def promb():
    r = Run('Pro-MB', 'pmb')
    imp = impulse(1 << 15)
    jobs = []
    r.settext('Processing Mode', 'Dynamic Phase')
    # one band 300 Hz .. 3 kHz, no dynamics (ratio 1) -> crossover shape; slopes
    for mode in ['Dynamic Phase', 'Linear Phase', 'Minimum Phase']:
        r.settext('Processing Mode', mode)
        for slope in ['6', '12', '24', '48']:
            r.settext('Band 1 State', 'Enabled'); r.settext('Band 1 Low Crossover', '300'); r.settext('Band 1 High Crossover', '3000')
            r.settext('Band 1 Low Slope', slope); r.settext('Band 1 High Slope', slope)
            r.settext('Band 1 Ratio', '1'); r.settext('Band 1 Range', '0'); r.settext('Band 1 Level', '-12')
            jobs.append((dict(kind='xover', mode=mode, slope=slope), r.process(imp, settle=0.2), imp))
    log = r.run(timeout=3600)
    res = []
    fq = np.geomspace(20, 20000, 200)
    for (c, name, x), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0] / 0.25; c['latency'] = lat
        f, H = spectrum(y)
        c['fq'] = fq.round(2).tolist(); c['mag_db'] = np.interp(fq, f, db(H)).round(3).tolist()
        res.append(c)
    json.dump(res, open(DATA + '/pmb.json', 'w'))


# ---------------------------------------------------------------- Pro-R 2
def band_rt60(y, fc, sr=SR):
    """RT60 (s) of y in a Gaussian octave band around fc: slope of the smoothed energy envelope between 5 and 35 dB
    below its maximum (the plugin's tail is noise-free, so no floor correction is needed)"""
    Y = np.fft.rfft(y); f = np.fft.rfftfreq(len(y), 1 / sr)
    w = np.exp(-0.5 * (np.log2(np.maximum(f, 1e-3) / fc) / 0.35) ** 2)
    b = np.fft.irfft(Y * w, len(y))
    cs = np.concatenate([[0.0], np.cumsum(b * b)]); env = 10 * np.log10((cs[2400:] - cs[:-2400]) / 2400 + 1e-30)
    i0 = int(np.argmax(env)); top = env[i0]
    seg = env[i0:]; t = np.arange(len(seg)) / sr
    m = (seg <= top - 5) & (seg >= top - 35)
    idx = np.nonzero(m)[0]
    if len(idx) < 100: return None
    lo, hi = idx[0], idx[-1]
    k = np.polyfit(t[lo:hi], seg[lo:hi], 1)[0]
    return round(float(-60 / k), 3) if k < 0 else None


def pror():
    r = Run('Pro-R 2', 'pr2')
    imp = impulse(SR * 10)
    jobs = []
    for style in ['Modern', 'Vintage', 'Plate']:
        for space in ['0.5', '2.5', '6']:
            for rate in ['100']:
                r.settext('Style', style); r.settext('Space', space); r.settext('Decay Rate', rate)
                r.settext('Mix', '100'); r.settext('Predelay', '0'); r.settext('Brightness', '0'); r.settext('Distance', '50')
                jobs.append((dict(style=style, space=space, rate=rate), r.process(imp, settle=0.1), imp))
    for rate in ['50', '200']:
        r.settext('Style', 'Modern'); r.settext('Space', '2.5'); r.settext('Decay Rate', rate)
        jobs.append((dict(style='Modern', space='2.5', rate=rate), r.process(imp, settle=0.1), imp))
    log = r.run(timeout=3600)
    res = []
    for (c, name, x), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True) / 0.25; c['latency'] = lat
        m = y[:, 0]
        c['onset_ms'] = round(float(np.argmax(np.abs(m) > 1e-3 * np.max(np.abs(m))) / SR * 1000), 2)
        c['rt60'] = {str(fc): band_rt60(m, fc) for fc in (125, 250, 500, 1000, 2000, 4000, 8000)}
        c['lr_corr'] = round(float(np.corrcoef(y[SR // 10:SR, 0], y[SR // 10:SR, 1])[0, 1]), 3)
        res.append(c); print(c)
    json.dump(res, open(DATA + '/pr2.json', 'w'))



# ---------------------------------------------------------------- Timeless 3
def timeless():
    r = Run('Timeless 3', 'tl3')
    imp = impulse(SR * 4)
    jobs = []
    for mode in ['Tape', 'Stretch']:
        for fb in ['0', '50', '100']:
            for pp in ['Ping Pong Off', 'Ping Pong L-R']:
                r.settext('Delay Time', '350'); r.settext('Delay Sync', 'Free'); r.settext('Delay Read Mode', mode)
                r.settext('Feedback', fb); r.settext('Ping Pong', pp); r.settext('Mix', '100'); r.settext('Drive Enabled', 'Off')
                r.settext('Diffuse Enabled', 'Off')
                jobs.append((dict(mode=mode, feedback=fb, pingpong=pp), r.process(imp, settle=0.2), imp))
    log = r.run(timeout=3600)
    res = []
    for (c, name, x), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True) / 0.25; c['latency'] = lat
        echoes = []
        for ch in (0, 1):
            e = np.abs(y[:, ch]); idx = []
            for k in range(1, 9):  # strongest sample around k * 350 ms
                c0 = int(k * 0.35 * SR); w = e[c0 - 2400:c0 + 2400]
                if len(w) == 0: break
                j = int(np.argmax(w)); idx.append((round((c0 - 2400 + j) / SR * 1000, 2), round(float(db(w[j])), 2)))
            echoes.append(idx)
        c['echoes_L'], c['echoes_R'] = echoes
        c['direct_db'] = round(float(db(np.max(np.abs(y[:240, 0])))), 2)
        res.append(c); print(c)
    json.dump(res, open(DATA + '/tl3.json', 'w'))


if __name__ == '__main__':
    for a in sys.argv[1:]:
        globals()[a]()
