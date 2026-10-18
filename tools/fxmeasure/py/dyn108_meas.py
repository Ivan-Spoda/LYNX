"""Round 108 black-box measurements (official trials, VST3 parameter API + audio only): what round 99 left out.

  pcsc    Pro-C 3 side-chain EQ (shapes on the detector) and side-chain level
  pcchar  Pro-C 3 character (tube / diode / bright, drive, routing): harmonics and small-signal response
  pcos    Pro-C 3 oversampling: latency and pass band per factor
  pmbdyn  Pro-MB per-band dynamics: static curves in / out of the band, range, knee, expansion, attack / release %
  satmb   Saturn 2 multiband interplay: crossover sum with clean bands
  states  FabFilter state layout (component state float k <-> parameter k), for the substitute translation

Usage: cd tools/fxmeasure/py && python3 -I dyn108_meas.py <what>   (FXMEASURE_DATA=<dir> for the results)
"""
import sys, json, struct, re
sys.path.insert(0, '.')
from dyn import *
from sat_meas import harmonics


def lat_of(log):
    return [int(l.split('latency=')[1]) for l in log.splitlines() if l.startswith('processed')]


# ---------------------------------------------------------------- Pro-C 3
def pc_base(r, style='Clean', thr=-30, ratio=4, knee=0, att=1, rel=50):
    r.settext('Style', style)
    r.settext('Auto Gain', 'Off'); r.settext('Auto Release', 'Off'); r.settext('Auto Threshold', 'Off')
    r.settext('Threshold', '%g' % thr); r.settext('Ratio', '%g' % ratio); r.settext('Knee', '%g' % knee)
    r.settext('Range', '60'); r.settext('Attack', '%g' % att); r.settext('Release', '%g' % rel)
    r.settext('Hold', '0'); r.settext('Character', 'Off')
    r.settext('Mix', '100'); r.settext('Wet Gain', '0'); r.set('Dry Gain', 0.0)


def sc_band(r, k, used, shape='Bell', freq=1000, gain=0, q=1, slope='12'):
    p = 'Side Chain EQ Band %d ' % k
    r.settext(p + 'Used', 'Used' if used else 'Unused')
    if used:
        r.settext(p + 'Enabled', 'Enabled'); r.settext(p + 'Shape', shape); r.settext(p + 'Frequency', '%g' % freq)
        r.settext(p + 'Gain', '%g' % gain); r.settext(p + 'Q', '%g' % q); r.settext(p + 'Slope', slope)


def pcsc():
    r = Run('Pro-C 3', 'pcsc108')
    levels = list(range(-50, 1, 2))
    jobs = []
    cases = [dict(shape=None), dict(shape='Low Cut', freq=1000, slope='12'), dict(shape='Low Cut', freq=200, slope='24'),
             dict(shape='High Cut', freq=1000, slope='12'), dict(shape='Bell', freq=1000, gain=12, q=1),
             dict(shape='Bell', freq=1000, gain=-12, q=2), dict(shape='High Shelf', freq=3000, gain=12, q=1),
             dict(shape='Low Shelf', freq=200, gain=-12, q=1), dict(shape='Band Pass', freq=1000, q=1)]
    for f in (100, 1000, 5000):
        x, n = stair(levels, 0.5, f=f)
        for c in cases:
            pc_base(r)
            sc_band(r, 1, c['shape'] is not None, shape=c['shape'] or 'Bell', **{k: v for k, v in c.items() if k != 'shape'})
            r.settext('Side Chain Level', '0')
            jobs.append((dict(c, f=f, sclevel=0), r.process(x, settle=0.3), x, n))
        pc_base(r); sc_band(r, 1, False); r.settext('Side Chain Level', '6')
        jobs.append((dict(shape=None, f=f, sclevel=6), r.process(x, settle=0.3), x, n))
    r.settext('Side Chain Level', '0')
    log = r.run(timeout=3600)
    res = []
    for (c, name, x, n), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0]
        i, o = stair_read(y, x, n, lat)
        c['in'] = i.round(3).tolist(); c['gain'] = (o - i).round(3).tolist(); c['latency'] = lat
        res.append(c)
    json.dump(res, open(DATA + '/pc_sc.json', 'w'))


def pcchar():
    r = Run('Pro-C 3', 'pcchar108')
    jobs = []
    imp = impulse(1 << 15, amp=0.001)
    for ch in ['Off', 'Tube', 'Diode', 'Bright']:
        for routing in ['Post Compression', 'Pre Compression']:
            if ch == 'Off' and routing.startswith('Pre'):
                continue
            for drive in ([0] if ch == 'Off' else [-12, 0, 12, 24]):
                for lvl in (-18, -6):
                    pc_base(r, ratio=1, thr=0)
                    r.settext('Character', ch); r.settext('Character Routing', routing); r.settext('Character Drive', '%g' % drive)
                    x = sine(1000, 1.0, amp=10 ** (lvl / 20))
                    jobs.append((dict(kind='sine', character=ch, routing=routing, drive=drive, level=lvl), r.process(x, settle=0.2), x))
                if routing.startswith('Post'):
                    pc_base(r, ratio=1, thr=0)
                    r.settext('Character', ch); r.settext('Character Routing', routing); r.settext('Character Drive', '%g' % drive)
                    jobs.append((dict(kind='imp', character=ch, routing=routing, drive=drive), r.process(imp, settle=0.2), imp))
    # does the character follow the compression? (pre vs post with 12 dB of reduction)
    for routing in ['Post Compression', 'Pre Compression']:
        pc_base(r, ratio=4, thr=-30)
        r.settext('Character', 'Tube'); r.settext('Character Routing', routing); r.settext('Character Drive', '12')
        x = sine(1000, 1.0, amp=10 ** (-6 / 20))
        jobs.append((dict(kind='sine', character='Tube', routing=routing, drive=12, level=-6, comp=True), r.process(x, settle=0.2), x))
    r.settext('Character', 'Off')
    log = r.run(timeout=3600)
    res = []
    fq = [50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000]
    for (c, name, x), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0]; c['latency'] = lat
        if c['kind'] == 'sine':
            seg = y[lat + 24000:lat + 48000 - 1]
            h = harmonics(seg, 1000)
            a = 10 ** (c['level'] / 20)
            c['fund_db'] = round(20 * np.log10(h[1] / a + 1e-15), 3)
            c['harm_db'] = [round(20 * np.log10(v / h[1] + 1e-15), 1) for v in h[2:8]]
            c['thd_pct'] = round(100 * np.sqrt(sum(v * v for v in h[2:])) / h[1], 3)
            c['dc'] = round(float(h[0]), 5)
        else:
            f, H = spectrum(y / 0.001)
            c['fq'] = fq; c['mag_db'] = np.interp(fq, f, db(H)).round(3).tolist()
        res.append(c)
    json.dump(res, open(DATA + '/pc_char.json', 'w'))


def pcos():
    r = Run('Pro-C 3', 'pcos108')
    jobs = []
    imp = impulse(1 << 15, amp=0.01)
    hi = sine(15000, 1.0, amp=10 ** (-6 / 20))
    for osf in ['Off', '2x', '4x', '8x', '16x', '32x']:
        pc_base(r, ratio=1, thr=0); r.settext('Oversampling', osf)
        jobs.append((dict(kind='imp', os=osf), r.process(imp, settle=0.3), imp))
        pc_base(r, ratio=8, thr=-30, att=0.005, rel=10); r.settext('Oversampling', osf)
        jobs.append((dict(kind='hi', os=osf), r.process(hi, settle=0.3), hi))
    r.settext('Oversampling', 'Off')
    log = r.run(timeout=3600)
    res = []
    fq = [1000, 5000, 10000, 15000, 18000, 20000, 22000, 23000]
    for (c, name, x), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0]; c['latency'] = lat
        if c['kind'] == 'imp':
            f, H = spectrum(y / 0.01)
            c['fq'] = fq; c['mag_db'] = np.interp(fq, f, db(H)).round(3).tolist()
        else:
            seg = y[lat + 24000:lat + 48000]
            Y = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
            Y = Y / Y.max()
            k15 = int(round(15000 * len(seg) / SR))
            mask = np.ones(len(Y), bool); mask[max(0, k15 - 40):k15 + 40] = False; mask[:40] = False
            c['spur_db'] = round(float(20 * np.log10(Y[mask].max() + 1e-15)), 1)
            c['out_db'] = round(float(20 * np.log10(np.sqrt(2 * np.mean(seg ** 2)))), 2)
        res.append(c)
    json.dump(res, open(DATA + '/pc_os.json', 'w'))


# ---------------------------------------------------------------- Pro-MB
def pmb_band(r, thr=-30, ratio=4, rng=-30, knee=0, att=20, rel=20, mode='Compression', look=0):
    r.settext('Band 1 State', 'Enabled'); r.settext('Band 1 Low Crossover', '300'); r.settext('Band 1 High Crossover', '3000')
    r.settext('Band 1 Low Slope', '24'); r.settext('Band 1 High Slope', '24')
    r.settext('Band 1 Dynamics Mode', mode); r.settext('Band 1 Threshold', '%g' % thr); r.settext('Band 1 Ratio', '%g' % ratio)
    r.settext('Band 1 Range', '%g' % rng); r.settext('Band 1 Knee', '%g' % knee); r.settext('Band 1 Attack', '%g' % att)
    r.settext('Band 1 Release', '%g' % rel); r.settext('Band 1 Lookahead', '%g' % look); r.settext('Band 1 Level', '0')


def pmbdyn():
    r = Run('Pro-MB', 'pmb108')
    r.settext('Processing Mode', 'Dynamic Phase')
    levels = list(range(-70, 1, 2))
    jobs = []
    statics = [dict(), dict(knee=24), dict(knee=48), dict(ratio=2), dict(ratio=100), dict(rng=-12), dict(rng=12),
               dict(rng=-60), dict(mode='Expansion', rng=-30), dict(mode='Expansion', rng=12), dict(mode='Expansion', ratio=2, rng=-30)]
    for f in (1000, 100):
        x, n = stair(levels, 0.5, f=f)
        for c in (statics if f == 1000 else statics[:1]):
            pmb_band(r, **c)
            jobs.append((dict(kind='static', f=f, **c), r.process(x, settle=0.3), x, n))
    xb = burst(-50, -10, 1.0, 1.0, 2.0)
    for att, rel in [(0, 20), (10, 20), (20, 20), (50, 20), (100, 20), (20, 0), (20, 50), (20, 100)]:
        pmb_band(r, att=att, rel=rel)
        jobs.append((dict(kind='time', att=att, rel=rel), r.process(xb, settle=0.3), xb, 0))
    for look in (0, 5, 20):
        pmb_band(r, att=0, rel=20, look=look)
        jobs.append((dict(kind='time', att=0, rel=20, look=look), r.process(xb, settle=0.3), xb, 0))
    log = r.run(timeout=3600)
    res = []
    for (c, name, x, n), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0]; c['latency'] = lat
        if c['kind'] == 'static':
            i, o = stair_read(y, x, n, lat)
            c['in'] = i.round(2).tolist(); c['gain'] = (o - i).round(3).tolist()
        else:
            g = gain_env(y, x, lat)
            on, off = SR, 2 * SR
            for frac in (0.1, 0.5, 0.9):
                c['att_%g' % frac] = round(time_to(g, on, frac)[0], 3)
                c['rel_%g' % frac] = round(time_to(g, off, frac)[0], 3)
            c['gr_db'] = round(float(g[off - 100]), 3)
        res.append(c)
    json.dump(res, open(DATA + '/pmb_dyn.json', 'w'))


# ---------------------------------------------------------------- Saturn 2
def satmb():
    r = Run('Saturn 2', 'satmb108')
    imp = impulse(1 << 15, amp=0.001)
    jobs = []
    r.settext('Mix', '100')
    for nb in ('1', '2', '3'):
        for slope in ('6', '12', '24', '48'):
            if nb == '1' and slope != '24':
                continue
            r.settext('Num Active Bands', nb)
            for b in range(1, 4):
                p = 'Band %d ' % b
                r.settext(p + 'Style', 'Subtle Transformer'); r.settext(p + 'Drive', '0'); r.settext(p + 'Mix', '100')
                r.settext(p + 'Level', '0'); r.settext(p + 'Dynamics', '0'); r.settext(p + 'Feedback Amount', '0')
                for t in ('Bass', 'Mid', 'Treble', 'Presence'):
                    r.settext(p + t, '0')
                r.settext(p + 'Crossover Slope', slope)
            r.settext('Band 1 Crossover Frequency', '200'); r.settext('Band 2 Crossover Frequency', '2000')
            jobs.append((dict(bands=int(nb), slope=slope), r.process(imp, settle=0.3)))
    log = r.run(timeout=3600)
    res = []
    fq = [30, 60, 100, 150, 200, 300, 500, 1000, 1500, 2000, 3000, 5000, 10000]
    for (c, name), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0]; c['latency'] = lat
        f, H = spectrum(y / 0.001)
        c['fq'] = fq; c['mag_db'] = np.interp(fq, f, db(H)).round(3).tolist()
        ph = np.unwrap(np.angle(H)); c['phase_deg'] = np.degrees(np.interp(fq, f, ph)).round(1).tolist()
        res.append(c)
    json.dump(res, open(DATA + '/sat_mb.json', 'w'))


# ---------------------------------------------------------------- state layout
ALPH = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def juce_b64(s):
    n, enc = s.split('.', 1)
    n = int(n); out = bytearray(n); pos = 0
    for ch in enc:
        v = ALPH.index(ch)
        for b in range(6):
            if pos >= n * 8: break
            if v >> b & 1: out[pos >> 3] |= 1 << (pos & 7)
            pos += 1
    return bytes(out)


def component_floats(path):
    t = open(path, 'rb').read()
    c = juce_b64(re.search(rb'<IComponent>([^<]*)</IComponent>', t).group(1).decode())
    return parse_ff_state(c)


def parse_ff_state(c):
    """(version, count, floats) of a FabFilter component state: 'FFBS' ver count floats... or
    'FabF' ver nameLen name u32 count floats..."""
    if c[:4] == b'FFBS':
        ver, count = struct.unpack('<II', c[4:12]); at = 12
    elif c[:4] == b'FabF':
        ver, nlen = struct.unpack('<II', c[4:12]); at = 12 + nlen
        _, count = struct.unpack('<II', c[at:at + 8]); at += 8
    else:
        raise ValueError(c[:8])
    m = (len(c) - at) // 4
    return ver, count, struct.unpack('<%df' % m, c[at:at + 4 * m])


def states():
    """for each listed parameter: normalized value -> component float, at 11 points"""
    plan = {'Pro-C 3': ['Style', 'Threshold', 'Ratio', 'Knee', 'Range', 'Attack', 'Release', 'Auto Release', 'Lookahead', 'Hold',
                        'Character', 'Character Routing', 'Character Drive', 'Wet Gain', 'Dry Gain', 'Auto Gain', 'Mix', 'Input Level',
                        'Output Level', 'Oversampling', 'Maximum Lookahead', 'Stereo Link', 'Side Chain Level',
                        'Side Chain EQ Band 1 Used', 'Side Chain EQ Band 1 Frequency', 'Side Chain EQ Band 1 Gain',
                        'Side Chain EQ Band 1 Q', 'Side Chain EQ Band 1 Shape', 'Side Chain EQ Band 1 Slope', 'Side Chain Input'],
            'Pro-MB': ['Band 1 State', 'Band 1 Low Crossover', 'Band 1 High Crossover', 'Band 1 Low Slope', 'Band 1 Dynamics Mode',
                       'Band 1 Threshold', 'Band 1 Range', 'Band 1 Ratio', 'Band 1 Attack', 'Band 1 Release', 'Band 1 Knee',
                       'Band 1 Lookahead', 'Band 1 Level', 'Mix', 'Output Level', 'Processing Mode', 'Input Level',
                       'Band 2 State', 'Band 2 Threshold', 'Band 1 Solo/Mute State'],
            'Pro-G': ['Threshold', 'Ratio', 'Range', 'Style', 'Attack', 'Release', 'Hold', 'Knee', 'Lookahead', 'Lookahead Enabled',
                      'Low Pass Frequency', 'High Pass Frequency', 'Side Chain Input Signal', 'Wet Level', 'Dry Level',
                      'Output Level', 'Input Level', 'Threshold (Upward)', 'Ratio (Upward)', 'Oversampling'],
            'Saturn 2': ['Num Active Bands', 'Mix', 'Output Gain', 'Input Gain', 'Band 1 Style', 'Band 1 Drive', 'Band 1 Mix',
                         'Band 1 Level', 'Band 1 Crossover Frequency', 'Band 1 Crossover Slope', 'Band 2 Drive', 'Band 1 Enabled',
                         'Band 1 Bass', 'Band 1 Treble', 'Band 1 Dynamics', 'Band 2 Style']}
    out = {}
    vals = [i / 10 for i in range(11)]
    for plug, names in plan.items():
        r = Run(plug, 'st108_' + plug.replace(' ', ''))
        files = []
        r.cmd('settle 0.05'); r.cmd('savestate "%s"' % winpath(r.f('base.bin')))
        for name in names:
            for v in vals:
                r.set(name, v); r.cmd('settle 0.02')
                fn = r.f('s%03d.bin' % len(files)); files.append((name, v, fn))
                r.cmd('savestate "%s"' % winpath(fn))
            r.cmd('defaults'); r.cmd('settle 0.02')
        log = r.run(timeout=3600)
        texts = {}
        for l in log.splitlines():
            m = re.match(r'set (.*) = ([0-9.]+) \((.*)\)$', l)
            if m:
                texts.setdefault(m.group(1), []).append(m.group(3))
        ver, count, base = component_floats(r.f('base.bin'))
        res = {'version': ver, 'count': count, 'floats': len(base), 'params': {}}
        for name in names:
            rows = [(v, component_floats(fn)[2]) for (nm, v, fn) in files if nm == name]
            # the float that follows the parameter
            ks = sorted({k for _, f in rows for k in range(len(f)) if f[k] != rows[0][1][k]})
            res['params'][name] = {'indices': ks, 'norm': vals, 'texts': texts.get(name, [])[:11],
                                   'values': {k: [round(f[k], 6) for _, f in rows] for k in ks}}
        out[plug] = res
    json.dump(out, open(DATA + '/states108.json', 'w'), indent=1)



def pmb2():
    """Pro-MB: upward compression vs ratio; gain envelopes after a level drop (release %)"""
    r = Run('Pro-MB', 'pmb108b')
    r.settext('Processing Mode', 'Dynamic Phase')
    levels = list(range(-70, 1, 2))
    x, n = stair(levels, 0.5)
    jobs = []
    for ratio in (1.5, 2, 4, 8, 100):
        pmb_band(r, ratio=ratio, rng=24)
        jobs.append((dict(kind='static', ratio=ratio, rng=24), r.process(x, settle=0.3), x, n))
    for knee in (24,):
        pmb_band(r, ratio=4, rng=24, knee=knee)
        jobs.append((dict(kind='static', ratio=4, rng=24, knee=knee), r.process(x, settle=0.3), x, n))
    xb = burst(-50, -10, 1.0, 1.0, 2.0)
    xc = burst(-10, -28, 1.0, 1.0, 2.0)
    for rel in (0, 20, 50, 100):
        for att in (0, 50):
            pmb_band(r, att=att, rel=rel)
            jobs.append((dict(kind='env', att=att, rel=rel, sig='b'), r.process(xb, settle=0.3), xb, 0))
        pmb_band(r, att=20, rel=rel)
        jobs.append((dict(kind='env', att=20, rel=rel, sig='c'), r.process(xc, settle=0.3), xc, 0))
    log = r.run(timeout=3600)
    res = []
    for (c, name, xx, nn), lat in zip(jobs, lat_of(log)):
        y = r.out(name, True)[:, 0]; c['latency'] = lat
        if c['kind'] == 'static':
            i, o = stair_read(y, xx, nn, lat)
            c['in'] = i.round(2).tolist(); c['gain'] = (o - i).round(3).tolist()
        else:
            g = gain_env(y, xx, lat)
            ms = [0, 1, 2, 5, 10, 20, 50, 100, 200, 500, 900]
            c['ms'] = ms
            c['on'] = [round(float(g[SR + int(m * 48)]), 2) for m in ms]
            c['off'] = [round(float(g[2 * SR + int(m * 48)]), 2) for m in ms]
            c['before_on'] = round(float(g[SR - 200]), 2); c['before_off'] = round(float(g[2 * SR - 200]), 2)
        res.append(c)
    json.dump(res, open(DATA + '/pmb_dyn2.json', 'w'))


if __name__ == '__main__':
    globals()[sys.argv[1]]()
