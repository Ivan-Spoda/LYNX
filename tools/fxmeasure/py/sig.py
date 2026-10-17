"""Test signals, WAV I/O and analysis helpers for the black-box effect measurements (round 99). numpy only."""
import os, struct, subprocess
import numpy as np

WIN = os.environ.get('FXMEASURE_DIR', '/mnt/c/Users/kauan/lynx-test/ffmeas')  # Windows folder holding fxmeasure.exe
DATA = os.environ.get('FXMEASURE_DATA', '../data')  # where the reduced results go (not the repository)
VST = r'C:\Program Files\Common Files\VST3'


def winpath(p):
    p = os.path.abspath(p)
    assert p.startswith('/mnt/c/')
    return 'C:\\' + p[7:].replace('/', '\\')


def write_wav(path, x, sr=48000):
    x = np.asarray(x, dtype=np.float32)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    ch = x.shape[1]
    data = x.astype('<f4').tobytes()
    hdr = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
    hdr += b'fmt ' + struct.pack('<IHHIIHH', 16, 3, ch, sr, sr * 4 * ch, 4 * ch, 32)
    hdr += b'data' + struct.pack('<I', len(data))
    with open(path, 'wb') as f:
        f.write(hdr + data)


def read_wav(path):
    b = open(path, 'rb').read()
    pos = 12; fmt = None; data = None
    while pos < len(b) - 8:
        cid = b[pos:pos + 4]; n = struct.unpack('<I', b[pos + 4:pos + 8])[0]
        body = b[pos + 8:pos + 8 + n]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', body[:16])
        elif cid == b'data':
            data = body
        pos += 8 + n + (n & 1)
    tag, ch, sr, _, _, bits = fmt
    if tag in (3, 0xFFFE) and bits == 32:
        x = np.frombuffer(data, '<f4')
    elif bits == 16:
        x = np.frombuffer(data, '<i2') / 32768.0
    elif bits == 24:
        a = np.frombuffer(data, np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)) << 8 >> 8) / 8388608.0
    else:
        raise ValueError(fmt)
    return x.reshape(-1, ch).astype(np.float64), sr


class Run:
    """Collects host commands and runs fxmeasure.exe once (one plugin load per run)."""

    def __init__(self, plugin, tag, sr=48000, block=512):
        self.plugin, self.tag, self.sr = plugin, tag, sr
        self.dir = os.path.join(WIN, 'w', tag)
        os.makedirs(self.dir, exist_ok=True)
        self.cmds = ['load "%s\\FabFilter %s.vst3" %d %d' % (VST, plugin, sr, block)]
        self.n = 0

    def f(self, name):
        return os.path.join(self.dir, name)

    def cmd(self, s):
        self.cmds.append(s)

    def set(self, name, v):
        self.cmds.append('set "%s" %r' % (name, float(v)))

    def settext(self, name, t):
        self.cmds.append('settext "%s" %s' % (name, t))

    def process(self, x, name=None, sc=None, settle=0.2, reset=True):
        """queues x (mono or N×2) through the plugin; returns the output file name"""
        self.n += 1
        name = name or 'c%04d' % self.n
        key = hash(np.asarray(x, np.float32).tobytes())
        if not hasattr(self, 'inputs'): self.inputs = {}
        if key not in self.inputs:
            self.inputs[key] = name + '_in.wav'
            write_wav(self.f(name + '_in.wav'), x, self.sr)
        infile = self.inputs[key]
        if reset:
            self.cmds.append('reset')
        if settle:
            self.cmds.append('settle %g' % settle)
        line = 'process "%s" "%s"' % (winpath(self.f(infile)), winpath(self.f(name + '_out.wav')))
        if sc is not None:
            write_wav(self.f(name + '_sc.wav'), sc, self.sr)
            line += ' "%s"' % winpath(self.f(name + '_sc.wav'))
        self.cmds.append(line)
        return name

    def run(self, timeout=1200):
        script = self.f('script.txt')
        open(script, 'w').write('\n'.join(self.cmds) + '\n')
        r = subprocess.run(['nice', os.path.join(WIN, 'fxmeasure.exe'), winpath(script)], capture_output=True, text=True,
                           timeout=timeout, cwd=WIN)
        open(self.f('log.txt'), 'w').write(r.stdout + r.stderr)
        errs = [l for l in r.stdout.splitlines() if l.startswith('error')]
        if errs:
            print('\n'.join(errs[:10]))
        return r.stdout

    def out(self, name, delete=False):
        y, _ = read_wav(self.f(name + '_out.wav'))
        if delete: os.remove(self.f(name + '_out.wav'))
        return y


def impulse(n=1 << 16, amp=0.25, at=0):
    x = np.zeros(n); x[at] = amp; return x


def log_sweep(f1, f2, secs, sr=48000, amp=0.5):
    t = np.arange(int(secs * sr)) / sr
    L = secs / np.log(f2 / f1)
    return amp * np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1))


def sine(f, secs, sr=48000, amp=0.5, phase=0.0):
    t = np.arange(int(secs * sr)) / sr
    return amp * np.sin(2 * np.pi * f * t + phase)


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def spectrum(h, sr=48000, n=None):
    n = n or len(h)
    H = np.fft.rfft(h, n)
    f = np.fft.rfftfreq(n, 1 / sr)
    return f, H


def at_freqs(f, H, fq):
    """complex response interpolated at fq (linear in mag-dB and unwrapped phase)"""
    mag = np.interp(fq, f, db(H)); ph = np.interp(fq, f, np.unwrap(np.angle(H)))
    return mag, ph


def tone_amp(y, f, sr=48000):
    """amplitude and phase of frequency f in y by projection (y should hold an integer number of cycles ideally)"""
    t = np.arange(len(y)) / sr
    w = np.hanning(len(y)); w /= w.sum() / 2
    c = np.sum(y * w * np.exp(-2j * np.pi * f * t))
    return np.abs(c), np.angle(c)


def rms_env(y, win):
    k = np.ones(win) / win
    return np.sqrt(np.convolve(y ** 2, k, 'same'))
