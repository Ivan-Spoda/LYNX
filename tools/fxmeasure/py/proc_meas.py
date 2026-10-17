"""Pro-C 3 black-box: static curves per style / knee / ratio, attack-release-hold step responses, lookahead."""
import sys, json; sys.path.insert(0, '.')
from dyn import *

STYLES = ['Clean', 'Versatile', 'Punch', 'Upward', 'TTM', 'Vari-Mu', 'Classic', 'Opto', 'Vocal', 'Bus', 'Pumping']
LEVELS = list(range(-50, 1, 2))


def base(r, style, thr=-18, ratio=4, knee=0, att=10, rel=200, hold=0, look=0):
    r.settext('Style', style)
    r.settext('Auto Gain', 'Off'); r.settext('Auto Release', 'Off'); r.settext('Auto Threshold', 'Off')
    r.settext('Threshold', '%g' % thr); r.settext('Ratio', '%g' % ratio); r.settext('Knee', '%g' % knee)
    r.settext('Range', '60'); r.settext('Attack', '%g' % att); r.settext('Release', '%g' % rel)
    r.settext('Hold', '%g' % hold); r.settext('Lookahead', '%g' % look); r.settext('Character', 'Off')
    r.settext('Mix', '100'); r.settext('Wet Gain', '0'); r.set('Dry Gain', 0.0)


def static():
    r = Run('Pro-C 3', 'pc_static')
    x, n = stair(LEVELS, 0.6)
    jobs = []
    for st in STYLES:
        base(r, st); jobs.append((dict(style=st, knee=0, ratio=4), r.process(x, settle=0.3)))
    for st in ['Clean', 'Versatile', 'Classic', 'Opto']:
        for knee in [6, 12, 24, 48, 72]:
            base(r, st, knee=knee); jobs.append((dict(style=st, knee=knee, ratio=4), r.process(x, settle=0.3)))
    for st in ['Clean', 'Upward', 'Vari-Mu', 'Opto']:
        for ratio in [1.5, 2, 8, 20, 100]:
            base(r, st, ratio=ratio); jobs.append((dict(style=st, knee=0, ratio=ratio), r.process(x, settle=0.3)))
    r.run(timeout=3600)
    res = []
    for c, name in jobs:
        y = r.out(name, True)[:, 0]
        i, o = stair_read(y, x, n)
        c['in'] = i.round(3).tolist(); c['out'] = o.round(3).tolist(); res.append(c)
    json.dump(res, open(DATA + '/pc_static.json', 'w'))


def timing():
    r = Run('Pro-C 3', 'pc_time')
    x = burst(-40, -6, 1.0, 1.0, 2.0)
    jobs = []
    for st in STYLES:
        base(r, st, thr=-30); jobs.append((dict(style=st, att=10, rel=200, hold=0), r.process(x, settle=0.3)))
    for st in ['Clean', 'Versatile', 'Punch', 'Classic', 'Opto', 'Vari-Mu', 'Bus', 'Vocal']:
        for att in [0.1, 1, 30, 100]:
            base(r, st, thr=-30, att=att); jobs.append((dict(style=st, att=att, rel=200, hold=0), r.process(x, settle=0.3)))
        for rel in [20, 50, 500, 1500]:
            base(r, st, thr=-30, rel=rel); jobs.append((dict(style=st, att=10, rel=rel, hold=0), r.process(x, settle=0.3)))
    for hold in [50, 200]:
        base(r, 'Clean', thr=-30, hold=hold); jobs.append((dict(style='Clean', att=10, rel=200, hold=hold), r.process(x, settle=0.3)))
    for look in [1, 5, 20]:
        r.settext('Maximum Lookahead', 'Maximum 20 ms'); base(r, 'Clean', thr=-30, att=1, look=look); jobs.append((dict(style='Clean', att=1, rel=200, hold=0, look=look), r.process(x, settle=0.3)))
    r.settext('Maximum Lookahead', 'Off'); base(r, 'Clean', thr=-30); r.settext('Auto Release', 'On'); jobs.append((dict(style='Clean', att=10, rel='auto', hold=0), r.process(x, settle=0.3)))
    log = r.run(timeout=3600)
    lats = [int(l.split('latency=')[1]) for l in log.splitlines() if l.startswith('processed')]
    res = []; envs = []
    for (c, name), lat in zip(jobs, lats):
        y = r.out(name, True)[:, 0]
        g = gain_env(y, x, lat)
        c['latency'] = lat
        on = SR; off = 2 * SR
        for frac in (0.1, 0.5, 0.632, 0.9):
            c['att_%g' % frac] = round(time_to(g, on, frac)[0], 3)
            c['rel_%g' % frac] = round(time_to(g, off, frac)[0], 3)
        c['gr_hold_db'] = round(float(g[off - 100]), 3)
        res.append(c); envs.append(g[::12].astype(np.float32))
    json.dump(res, open(DATA + '/pc_time.json', 'w'))
    np.savez_compressed(DATA + '/pc_time_env.npz', env=np.array(envs))


if __name__ == '__main__':
    {'static': static, 'timing': timing}[sys.argv[1]]()
