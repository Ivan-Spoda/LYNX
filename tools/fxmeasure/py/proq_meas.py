"""Pro-Q 4 black-box sweep: impulse responses for shapes x slopes x Q x gain, saved as complex responses on a log grid."""
import sys, json, itertools; sys.path.insert(0,'.')
from sig import *
NIMP = 1 << 16

def grid(sr):
    return np.geomspace(10, sr/2*0.999, 400)

def configs():
    C = []
    for f, g, q in itertools.product([100, 1000, 10000], [-24, -12, -6, 6, 12, 24], [0.1, 0.5, 1, 2, 5, 10]):
        C.append(dict(shape='Bell', f=f, g=g, q=q, slope='12'))
    for s in ['0','6','12','18','24','30','36','48','72','96','Brickwall']:
        for q in [0.5, 1, 4]:
            C.append(dict(shape='Bell', f=1000, g=12, q=q, slope=s))
    for sh in ['Low Shelf', 'High Shelf', 'Tilt Shelf', 'Flat Tilt']:
        for f, g, q in itertools.product([100, 1000, 10000], [-12, -6, 6, 12], [0.1, 0.5, 1, 2, 5]):
            C.append(dict(shape=sh, f=f, g=g, q=q, slope='12'))
        for s in ['6','12','18','24','30','36','48','72','96']:
            C.append(dict(shape=sh, f=1000, g=12, q=1, slope=s))
    for sh in ['Low Cut', 'High Cut']:
        for f, s, q in itertools.product([100, 1000, 10000], ['6','12','18','24','30','36','48','72','96','Brickwall'], [0.5, 0.7071, 1, 2, 5]):
            C.append(dict(shape=sh, f=f, g=0, q=q, slope=s))
    for sh in ['Notch', 'Band Pass', 'All Pass']:
        for f, s, q in itertools.product([100, 1000, 10000], ['6','12','24','48','96'], [0.5, 1, 2, 10]):
            C.append(dict(shape=sh, f=f, g=0, q=q, slope=s))
    return C

def run(mode='Zero Latency', sr=48000, subset=None, tag=None, chunk=120):
    C = configs() if subset is None else subset
    tag = tag or 'pq_%s_%d' % (mode.split()[0].lower(), sr)
    H = []; lat = None
    for k in range(0, len(C), chunk):
        h, lat = run_chunk(mode, sr, C[k:k+chunk], tag)
        H += h
    np.savez_compressed(DATA + '/%s.npz' % tag, fq=grid(sr), H=np.array(H), latency=lat, sr=sr, configs=json.dumps(C))
    print(tag, len(C), 'captures latency', lat)

def run_chunk(mode, sr, C, tag):
    r = Run('Pro-Q 4', tag, sr)
    r.settext('Processing Mode', mode)
    r.settext('Band 1 Used', 'Used')
    names = []
    imp = impulse(NIMP)
    for c in C:
        r.settext('Band 1 Shape', c['shape'])
        r.settext('Band 1 Frequency', '%g' % c['f'])
        r.settext('Band 1 Gain', '%+g' % c['g'])
        r.settext('Band 1 Q', '%g' % c['q'])
        r.settext('Band 1 Slope', c['slope'] if c['slope'] == 'Brickwall' else c['slope'] + ' dB/oct')
        names.append(r.process(imp, settle=0.05))
    r.cmd('latency')
    log = r.run(timeout=3600)
    lat = int([l for l in log.splitlines() if l.startswith('latency ')][-1].split()[1])
    fq = grid(sr); out = []
    for n in names:
        y = r.out(n, True)[:, 0] / 0.25
        f, H = spectrum(y, sr)
        H = H * np.exp(2j*np.pi*f*lat/sr)  # remove the reported latency
        out.append(np.interp(fq, f, H.real) + 1j*np.interp(fq, f, H.imag))
    sets = [l for l in log.splitlines() if l.startswith('set ')]
    open(DATA + '/%s_sets.txt' % tag, 'a').write('\n'.join(sets) + '\n')
    return out, lat

if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'Zero Latency'
    sr = int(sys.argv[2]) if len(sys.argv) > 2 else 48000
    run(mode, sr)
