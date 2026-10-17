"""Pro-L 2 black-box: latency per option, static ceiling, GR attack/release per style, true-peak behaviour."""
import sys, json; sys.path.insert(0, '.')
from dyn import *

STYLES = ['Transparent', 'Punchy', 'Dynamic', 'Allround', 'Aggressive', 'Modern', 'Bus', 'Safe']


def base(r, style='Modern', gain=0, out=-10, look=None, att=250, rel=500, os_='Off', tp='Off'):
    r.settext('Style', style)
    r.settext('Gain', '%g' % gain); r.settext('Output Level', '%g' % out)
    r.settext('Oversampling', os_); r.settext('True Peak Limiting', tp)
    r.settext('Dithering', 'Off'); r.settext('Unity Gain', 'Off'); r.settext('Filter DC Offset', 'Off')
    r.settext('Channel Link Transients', '100'); r.settext('Channel Link Release', '100')
    if look is not None: r.settext('Lookahead', '%g' % look)
    if att is not None: r.settext('Attack', '%g' % att)
    if rel is not None: r.settext('Release', '%g' % rel)


def latency():
    r = Run('Pro-L 2', 'pl_lat')
    tags = []
    for os_ in ['Off', '2x', '4x', '8x', '16x', '32x']:
        for tp in ['Off', 'On']:
            for look in [0, 1, 5]:
                base(r, os_=os_, tp=tp, look=look); r.cmd('latency'); tags.append((os_, tp, look))
    log = r.run()
    lats = [int(l.split()[1]) for l in log.splitlines() if l.startswith('latency ')]
    res = [dict(os=a, tp=b, look=c, latency=l) for (a, b, c), l in zip(tags, lats)]
    for x in res: print(x)
    json.dump(res, open(DATA + '/pl_latency.json', 'w'))


def static():
    r = Run('Pro-L 2', 'pl_static')
    x, n = stair(list(range(-30, 1, 2)), 0.6)
    jobs = []
    for st in STYLES:
        base(r, st, gain=10, out=0); jobs.append((dict(style=st, gain=10, out=0), r.process(x, settle=0.5)))
    base(r, 'Modern', gain=10, out=-1); jobs.append((dict(style='Modern', gain=10, out=-1), r.process(x, settle=0.5)))
    log = r.run(timeout=3600)
    lats = [int(l.split('latency=')[1]) for l in log.splitlines() if l.startswith('processed')]
    res = []
    for (c, name), lat in zip(jobs, lats):
        y = r.out(name, True)[:, 0]
        i, o = stair_read(y, x, n, latency=lat)
        c['in'] = i.round(3).tolist(); c['out'] = o.round(3).tolist(); c['peak'] = float(np.max(np.abs(y))); res.append(c)
    json.dump(res, open(DATA + '/pl_static.json', 'w'))


def timing():
    r = Run('Pro-L 2', 'pl_time')
    x = burst(-30, -2, 1.0, 1.0, 3.0, f=100.0)
    jobs = []
    for st in STYLES:
        base(r, st, gain=10, out=0); jobs.append((dict(style=st), r.process(x, settle=0.5)))
        for rel in [10, 100, 1000]:
            base(r, st, gain=10, out=0, rel=rel); jobs.append((dict(style=st, rel=rel), r.process(x, settle=0.5)))
        for att in [1, 30]:
            base(r, st, gain=10, out=0, att=att); jobs.append((dict(style=st, att=att), r.process(x, settle=0.5)))
    log = r.run(timeout=3600)
    lats = [int(l.split('latency=')[1]) for l in log.splitlines() if l.startswith('processed')]
    sets = log
    res = []; envs = []
    for (c, name), lat in zip(jobs, lats):
        y = r.out(name, True)[:, 0]
        g = gain_env(y, x, lat, smooth=480)
        c['latency'] = lat
        on = SR; off = 2 * SR
        c['gr_db'] = round(float(g[off - 2000]), 3)
        c['peak_db'] = round(float(db(np.max(np.abs(y)))), 3)
        for frac in (0.5, 0.9):
            c['att_%g' % frac] = round(time_to(g, on, frac)[0], 3)
        # release: time from the end of the burst to recover 3 / 6 dB and to within 1 dB
        seg = g[off:off + 3 * SR]
        for lab, thr in (('rel_3db', g[off - 2000] + 3), ('rel_6db', g[off - 2000] + 6), ('rel_1db_left', -1.0)):
            idx = np.argmax(seg >= thr)
            c[lab] = round(idx / SR * 1000, 2) if seg[idx] >= thr else None
        res.append(c); envs.append(g[::48].astype(np.float32))
    json.dump(res, open(DATA + '/pl_time.json', 'w'))
    np.savez_compressed(DATA + '/pl_time_env.npz', env=np.array(envs))


def truepeak():
    """fs/4 sine at 45 degrees: sample peaks are 3 dB under the true peak"""
    r = Run('Pro-L 2', 'pl_tp')
    t = np.arange(2 * SR)
    x = 10 ** (-1 / 20) * np.sin(2 * np.pi * t / 4 + np.pi / 4)  # true peak -1 dBFS, sample peak -4 dBFS
    jobs = []
    for tp in ['Off', 'On']:
        for os_ in ['Off', '4x']:
            base(r, 'Modern', gain=3, out=0, os_=os_, tp=tp); jobs.append((dict(tp=tp, os=os_), r.process(x, settle=0.5)))
    log = r.run()
    lats = [int(l.split('latency=')[1]) for l in log.splitlines() if l.startswith('processed')]
    res = []
    for (c, name), lat in zip(jobs, lats):
        y = r.out(name, True)[lat + SR // 2:lat + 3 * SR // 2, 0]
        Y = np.fft.rfft(y); y8 = np.fft.irfft(Y, 8 * len(y)) * 8  # 8x band-limited upsampling
        c['sample_peak_db'] = round(float(db(np.max(np.abs(y)))), 3)
        c['true_peak_db'] = round(float(db(np.max(np.abs(y8)))), 3)
        res.append(c); print(c)
    json.dump(res, open(DATA + '/pl_tp.json', 'w'))


if __name__ == '__main__':
    for a in sys.argv[1:]:
        globals()[a]()
