"""Saturn 2 black-box: harmonic levels and fundamental gain vs style / drive / input level / frequency (1 band)."""
import sys, json; sys.path.insert(0, '.')
from sig import *

SR = 48000
STYLES = ['Subtle Tube', 'Clean Tube', 'Warm Tube', 'Broken Tube', 'Subtle Tape', 'Clean Tape', 'Warm Tape', 'Old Tape',
          'American Tweed Amp', 'American Plexi Amp', 'British Rock Amp', 'British Pop Amp', 'Smooth Amp', 'Crunchy Amp',
          'Lead Amp', 'Screaming Amp', 'Power Amp', 'Subtle Saturation', 'Gentle Saturation', 'Heavy Saturation',
          'Subtle Transformer', 'Gentle Transformer', 'Warm Transformer', 'Smudge', 'Breakdown', 'Foldback', 'Rectify', 'Destroy']
NH = 12


def harmonics(y, f, sr=SR):
    """levels (dB re input-amplitude units) of harmonics 0 (DC) .. NH, from an integer number of cycles"""
    n = int(round(int(len(y) * f / sr) * sr / f))
    y = y[:n]; Y = np.fft.rfft(y) / n * 2
    k = int(round(n * f / sr))
    return [abs(Y[0]) / 2] + [abs(Y[h * k]) if h * k < len(Y) else 0.0 for h in range(1, NH + 1)]


def main(styles=STYLES, tag='sat'):
    r = Run('Saturn 2', tag)
    r.settext('Num Active Bands', '1'); r.settext('Mix', '100'); r.settext('Band 1 Mix', '100'); r.settext('Band 1 Level', '0')
    for p in ('Bass', 'Mid', 'Treble', 'Presence'): r.settext('Band 1 ' + p, '0')
    r.settext('Band 1 Dynamics', '0'); r.settext('Band 1 Feedback Amount', '0')
    jobs = []
    for st in styles:
        r.settext('Band 1 Style', st)
        for drive in [0, 10, 25, 50, 75, 100]:
            r.settext('Band 1 Drive', '%g' % drive)
            for lvl in [-18, -6]:
                x = sine(1000, 0.5, amp=10 ** (lvl / 20))
                jobs.append((dict(style=st, drive=drive, level=lvl, f=1000), r.process(x, settle=0.1), x))
            if drive == 50:
                for f in [100, 5000]:
                    x = sine(f, 0.5, amp=10 ** (-6 / 20))
                    jobs.append((dict(style=st, drive=drive, level=-6, f=f), r.process(x, settle=0.1), x))
    r.run(timeout=3600)
    res = []
    for c, name, x in jobs:
        y = r.out(name, True)[:, 0]
        seg = y[SR // 4:]
        h = harmonics(seg, c['f'])
        a = 10 ** (c['level'] / 20)
        c['gain_db'] = round(float(db(h[1] / a)), 3)
        c['dc'] = round(float(h[0]), 6)
        c['h_db'] = [round(float(db(v / max(h[1], 1e-12))), 2) for v in h[2:]]  # harmonics 2..NH re fundamental
        c['thd_pct'] = round(float(np.sqrt(sum(v * v for v in h[2:])) / max(h[1], 1e-12) * 100), 4)
        res.append(c)
    json.dump(res, open(DATA + '/%s.json' % tag, 'w'))


if __name__ == '__main__':
    main()
