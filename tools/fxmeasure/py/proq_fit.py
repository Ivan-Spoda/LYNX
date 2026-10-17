"""Compare the measured Pro-Q 4 responses with analog prototypes (evaluated on the analog frequency axis = no
bilinear cramping). Prints the max |dB error| over 20 Hz..20 kHz per family and Q mapping."""
import sys, json; sys.path.insert(0, '.')
import os; DATA = os.environ.get('FXMEASURE_DATA', '../data')
import numpy as np

d = np.load(sys.argv[1] if len(sys.argv) > 1 else DATA + '/pq_zero_48000.npz')
fq, H, C = d['fq'], d['H'], json.loads(str(d['configs']))
band = (fq >= 20) & (fq <= 20000)


def bell(s, A, Q):
    return (s*s + s*A/Q + 1) / (s*s + s/(A*Q) + 1)


def lowshelf(s, A, Q):  # RBJ analog low shelf
    sa = np.sqrt(A)
    return A * (s*s + sa/Q*s + A) / (A*s*s + sa/Q*s + 1)


def highshelf(s, A, Q):
    sa = np.sqrt(A)
    return A * (A*s*s + sa/Q*s + 1) / (s*s + sa/Q*s + A)


def model(c, qmap):
    s = 1j * fq / c['f']
    A = 10 ** (c['g'] / 40)
    Q = qmap(c)
    if c['shape'] == 'Bell':
        return bell(s, A, Q)
    if c['shape'] == 'Low Shelf':
        return lowshelf(s, A, Q)
    if c['shape'] == 'High Shelf':
        return highshelf(s, A, Q)
    return None


QMAPS = {
    'Q': lambda c: c['q'],
    'Q*sqrtA': lambda c: c['q'] * 10 ** (abs(c['g']) / 80),
    'Q/sqrtA': lambda c: c['q'] / 10 ** (abs(c['g']) / 80),
    'Q*A': lambda c: c['q'] * 10 ** (abs(c['g']) / 40),
    'Q/0.7071': lambda c: c['q'] / np.sqrt(0.5),
    'Q*0.7071': lambda c: c['q'] * np.sqrt(0.5),
}

if __name__ == '__main__':
    for shape in ['Bell', 'Low Shelf', 'High Shelf']:
        idx = [i for i, c in enumerate(C) if c['shape'] == shape and c['slope'] == '12']
        for name, qm in QMAPS.items():
            errs = []
            for i in idx:
                m = model(C[i], qm)
                errs.append(np.max(np.abs(20*np.log10(np.abs(H[i][band])) - 20*np.log10(np.abs(m[band])))))
            errs = np.array(errs)
            print('%-10s %-9s median %.3f  max %.3f' % (shape, name, np.median(errs), errs.max()))
