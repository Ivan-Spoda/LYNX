#include "core/FacetEq.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace lynx::facet
{
namespace
{
constexpr double pi = 3.14159265358979323846;
using cd = std::complex<double>;

// Analog section in normalized frequency (s = jω/ω0): (n2 s² + n1 s + n0) / (d2 s² + d1 s + d0)
struct Analog { double n2 = 0, n1 = 0, n0 = 1, d2 = 0, d1 = 0, d0 = 1; };

cd evalAnalog (const Analog& a, double w) noexcept // w = ω/ω0
{
    const cd s (0.0, w);
    return (a.n2 * s * s + a.n1 * s + a.n0) / (a.d2 * s * s + a.d1 * s + a.d0);
}

double mag2 (const Analog& a, double w) noexcept { return std::norm (evalAnalog (a, w)); }

// Magnitude-matched digital section for an analog section at w0 = 2π f0 / fs. `ok` reports whether the numerator
// was realisable as asked (false = the magnitude at the match point had to be clamped).
Section matched (const Analog& a, double w0, bool& ok) noexcept
{
    Section s;
    ok = true;
    const double nyq = pi / w0; // digital Nyquist on the normalized analog axis

    if (a.d2 == 0.0)
    {
        // first order: (n1 s + n0) / (d1 s + d0); pole by impulse invariance, zero matched at DC and Nyquist
        const double wp = std::min (a.d0 / a.d1 * w0, 0.999 * pi);
        const double p = std::exp (-wp);
        const double h0 = std::sqrt (mag2 (a, 0.0)), hn = std::sqrt (mag2 (a, nyq));
        const double sum = h0 * (1.0 - p), diff = hn * (1.0 + p);
        s.b0 = 0.5 * (sum + diff);
        s.b1 = 0.5 * (sum - diff);
        s.a1 = -p;
        return s;
    }

    // poles of d2 s² + d1 s + d0: natural frequency wp, damping zeta
    const double wpn = std::sqrt (a.d0 / a.d2);
    const double zeta = a.d1 / (2.0 * std::sqrt (a.d0 * a.d2));
    const double wp = std::min (wpn * w0, 0.999 * pi);
    const double e = std::exp (-zeta * wp);
    s.a2 = e * e;
    s.a1 = zeta <= 1.0 ? -2.0 * e * std::cos (std::sqrt (1.0 - zeta * zeta) * wp)
                       : -2.0 * e * std::cosh (std::sqrt (zeta * zeta - 1.0) * wp);

    // squared magnitude of a digital polynomial as A0 φ0 + A1 φ1 + A2 φ2 (φ0 = cos²(w/2), φ1 = sin²(w/2), φ2 = 4 φ0 φ1)
    const double A0 = (1.0 + s.a1 + s.a2) * (1.0 + s.a1 + s.a2);
    const double A1 = (1.0 - s.a1 + s.a2) * (1.0 - s.a1 + s.a2);
    const double A2 = -4.0 * s.a2;
    auto den = [&] (double w) {
        const double p1 = std::sin (w * 0.5) * std::sin (w * 0.5), p0 = 1.0 - p1;
        return A0 * p0 + A1 * p1 + A2 * 4.0 * p0 * p1;
    };
    const double wm = std::min (w0, 0.9 * pi);
    const double p1 = std::sin (wm * 0.5) * std::sin (wm * 0.5), p0 = 1.0 - p1;
    const double target = mag2 (a, wm / w0) * den (wm);

    if (a.n1 == 0.0 && a.n0 == 0.0)
    {
        // high-pass: double zero at DC, k (1 − z⁻¹)², matched at the band frequency
        const double k = std::sqrt (target) / (4.0 * p1);
        s.b0 = k; s.b1 = -2.0 * k; s.b2 = k;
        return s;
    }
    if (a.n1 == 0.0 && a.n2 != 0.0 && a.n0 != 0.0)
    {
        // notch: zeros on the unit circle at the notch frequency, gain matched at DC
        const double wz = std::min (std::sqrt (a.n0 / a.n2) * w0, 0.999 * pi);
        const double k = std::sqrt (mag2 (a, 0.0)) * (1.0 + s.a1 + s.a2) / (2.0 - 2.0 * std::cos (wz));
        s.b0 = k; s.b1 = -2.0 * k * std::cos (wz); s.b2 = k;
        return s;
    }

    // numerator matched at DC, Nyquist and the match point (the band frequency, kept inside the band)
    const double B0 = A0 * mag2 (a, 0.0);
    const double B1 = A1 * mag2 (a, nyq);
    double B2 = (target - B0 * p0 - B1 * p1) / (4.0 * p0 * p1);

    const double sb0 = std::sqrt (std::max (0.0, B0)), sb1 = std::sqrt (std::max (0.0, B1));
    const double W = 0.5 * (sb0 + sb1);
    if (B2 < -W * W) { B2 = -W * W; ok = false; }
    s.b0 = 0.5 * (W + std::sqrt (W * W + B2));
    s.b1 = 0.5 * (sb0 - sb1);
    s.b2 = s.b0 != 0.0 ? -B2 / (4.0 * s.b0) : 0.0;
    return s;
}

// matched design; when the numerator cannot be realised (cuts near Nyquist), the reciprocal section is designed
// instead and inverted (its zeros become the poles)
Section matched (const Analog& a, double w0) noexcept
{
    bool ok = true;
    const auto s = matched (a, w0, ok);
    if (ok || a.d2 == 0.0 || a.n2 == 0.0 || a.n0 == 0.0)
        return s;
    const Analog inv { a.d2, a.d1, a.d0, a.n2, a.n1, a.n0 };
    bool ok2 = true;
    const auto r = matched (inv, w0, ok2);
    if (! ok2 || r.b0 == 0.0)
        return s;
    Section out;
    out.b0 = 1.0 / r.b0; out.b1 = r.a1 / r.b0; out.b2 = r.a2 / r.b0;
    out.a1 = r.b1 / r.b0; out.a2 = r.b2 / r.b0;
    return out;
}

double bandQ (const EffectParams& p, int b) noexcept { return q (p[(std::size_t) param (b, 3)]); }

// Butterworth section Qs of an order-n filter, ascending
int butterworthQs (int n, double* qs) noexcept
{
    const int m = n / 2;
    for (int k = 0; k < m; ++k)
        qs[k] = 1.0 / (2.0 * std::sin ((2 * k + 1) * pi / (2.0 * n)));
    std::sort (qs, qs + m);
    return m;
}

// analog sections of one band (normalized to the band frequency); returns the count
int analogSections (const EffectParams& p, int b, Analog* out, double extraDb = 0.0, bool keepFlat = false) noexcept
{
    const auto sh = shape (p[(std::size_t) param (b, 0)]);
    const double scale = gainScale (p[(std::size_t) scaleParam]);
    const double g = (gainDb (p[(std::size_t) param (b, 2)]) + extraDb) * scale;
    const double Q = bandQ (p, b);
    const int slope = slopeDb (p[(std::size_t) param (b, 4)]);

    switch (sh)
    {
        case Shape::off: return 0;
        case Shape::bell:
        {
            // symmetric bell: (s² + s·A/Qb + 1) / (s² + s/(A·Qb) + 1), Qb = Q/√2 (measured convention)
            if (std::abs (g) < 1e-9 && ! keepFlat) return 0;
            const double A = std::pow (10.0, g / 40.0), qb = Q / std::sqrt (2.0);
            out[0] = { 1.0, A / qb, 1.0, 1.0, 1.0 / (A * qb), 1.0 };
            return 1;
        }
        case Shape::lowShelf:
        case Shape::highShelf:
        case Shape::tiltShelf:
        {
            if (std::abs (g) < 1e-9 && ! keepFlat) return 0;
            const bool tilt = sh == Shape::tiltShelf;
            const double gg = tilt ? 2.0 * g : g;
            const double A = std::pow (10.0, gg / 40.0), sa = std::sqrt (A);
            const double norm = tilt ? std::pow (10.0, -g / 20.0) : 1.0;
            const bool high = sh != Shape::lowShelf;
            if (slope <= 6)
            {
                // first-order shelf, geometric half-gain point at the band frequency: high (A s + 1)/(s/A + 1), low (s + A)/(s + 1/A)
                if (high) out[0] = { 0.0, A * norm, norm, 0.0, 1.0 / A, 1.0 };
                else out[0] = { 0.0, norm, A * norm, 0.0, 1.0, 1.0 / A };
                return 1;
            }
            // second-order cookbook shelf, Qs = 0.7071·Q^0.4697 (measured), gain independent
            const double qs = std::sqrt (0.5) * std::pow (Q, 0.4697);
            if (high) out[0] = { A * A * norm, A * sa / qs * norm, A * norm, 1.0, sa / qs, A };
            else out[0] = { A * norm, A * sa / qs * norm, A * A * norm, A, sa / qs, 1.0 };
            return 1;
        }
        case Shape::lowCut:
        case Shape::highCut:
        {
            const bool hp = sh == Shape::lowCut;
            const int n = std::max (1, slope / 6);
            double qs[16];
            const int m = butterworthQs (n, qs);
            if (m > 0) qs[m - 1] *= Q / 1.0; // the highest-Q section carries the band's resonance (Q 1 = Butterworth)
            int c = 0;
            for (int k = 0; k < m; ++k)
                out[c++] = hp ? Analog { 1.0, 0.0, 0.0, 1.0, 1.0 / qs[k], 1.0 } : Analog { 0.0, 0.0, 1.0, 1.0, 1.0 / qs[k], 1.0 };
            if (n % 2)
                out[c++] = hp ? Analog { 0.0, 1.0, 0.0, 0.0, 1.0, 1.0 } : Analog { 0.0, 0.0, 1.0, 0.0, 1.0, 1.0 };
            return c;
        }
        case Shape::notch:
        {
            const double qb = Q / std::sqrt (2.0);
            out[0] = { 1.0, 0.0, 1.0, 1.0, 1.0 / qb, 1.0 };
            return 1;
        }
        case Shape::bandPass:
        {
            const double qb = Q / std::sqrt (2.0);
            out[0] = { 0.0, 1.0 / qb, 0.0, 1.0, 1.0 / qb, 1.0 };
            return 1;
        }
    }
    return 0;
}


cd evalDigital (const Section& s, double w) noexcept
{
    const cd z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
}

// the FIR grid factor of a phase mode: 1 up to 48 kHz, 2 up to 96 kHz, 4 above
int rateFactor (double sr) noexcept { return sr <= 50000.0 ? 1 : sr <= 100000.0 ? 2 : 4; }
constexpr int naturalBlock = 128, naturalDelay = 192, linearBlock = 512, firTaps = 8192;

// --- FFT and the partitioned convolver (the FIR phase modes) ------------------------------------------------------

struct Fft
{
    int n = 0;
    std::vector<cd> tw;
    std::vector<int> rev;

    void init (int size)
    {
        n = size;
        tw.resize ((std::size_t) n / 2);
        for (int k = 0; k < n / 2; ++k) tw[(std::size_t) k] = std::polar (1.0, -2.0 * pi * k / n);
        rev.resize ((std::size_t) n);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
            rev[(std::size_t) i] = r;
        }
    }

    // in place, unscaled (the inverse is n times too large)
    void run (cd* a, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(std::size_t) i]) std::swap (a[i], a[rev[(std::size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const double wr = tw[(std::size_t) (j * step)].real(), wi = inverse ? -tw[(std::size_t) (j * step)].imag() : tw[(std::size_t) (j * step)].imag();
                    const double xr = a[i + j + half].real(), xi = a[i + j + half].imag();
                    const cd u = a[i + j], v (xr * wr - xi * wi, xr * wi + xi * wr);
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
    }
};

// Uniformly partitioned overlap-save convolution of a stereo signal (left + j·right) with a real FIR; block P, latency
// P. Buffers are sized once for the largest configuration; a new kernel is cross-faded in over one block.
class Convolver
{
public:
    void allocate (int maxTaps, int maxBlock)
    {
        fdl.assign ((std::size_t) maxTaps * 2, {});
        kern[0].assign ((std::size_t) maxTaps * 2, {});
        kern[1].assign ((std::size_t) maxTaps * 2, {});
        timeBuf.assign ((std::size_t) maxBlock * 2, {});
        acc.assign ((std::size_t) maxBlock * 2, {});
        acc2.assign ((std::size_t) maxBlock * 2, {});
        in.assign ((std::size_t) maxBlock, {});
        out.assign ((std::size_t) maxBlock, {});
    }

    // P must be a power of two whose FFT (2P) is `fft`
    void configure (const Fft& f, int blockSize, int taps) noexcept
    {
        fft = &f;
        P = blockSize;
        K = taps / blockSize;
        reset();
    }

    void reset() noexcept
    {
        std::fill (fdl.begin(), fdl.end(), cd {});
        std::fill (timeBuf.begin(), timeBuf.end(), cd {});
        std::fill (in.begin(), in.end(), cd {});
        std::fill (out.begin(), out.end(), cd {});
        pos = head = 0;
        pending = -1;
    }

    // a new FIR (K·P taps); the first one is used at once, later ones fade in over the next block
    void load (const double* h, bool immediate) noexcept
    {
        const int next = immediate ? active : 1 - active;
        auto* frame = acc2.data();
        for (int k = 0; k < K; ++k)
        {
            for (int i = 0; i < P; ++i) frame[i] = cd (h[k * P + i], 0.0);
            for (int i = P; i < 2 * P; ++i) frame[i] = {};
            fft->run (frame, false);
            std::copy (frame, frame + 2 * P, kern[(std::size_t) next].begin() + (std::ptrdiff_t) k * 2 * P);
        }
        pending = immediate ? -1 : next;
    }

    bool fading() const noexcept { return pending >= 0; }

    void step (float& l, float& r) noexcept
    {
        in[(std::size_t) pos] = cd (l, r);
        const auto y = out[(std::size_t) pos];
        l = (float) y.real();
        r = (float) y.imag();
        if (++pos == P) block();
    }

private:
    void accumulate (int which, cd* dst) noexcept
    {
        const int n2 = 2 * P;
        std::fill (dst, dst + n2, cd {});
        const auto* kr = kern[(std::size_t) which].data();
        for (int k = 0; k < K; ++k)
        {
            const auto* x = fdl.data() + (std::ptrdiff_t) (((head - k + K) % K) * n2);
            const auto* hk = kr + (std::ptrdiff_t) k * n2;
            // plain products (std::complex's operator* checks for infinities, several times slower)
            for (int i = 0; i < n2; ++i)
            {
                const double xr = x[i].real(), xi = x[i].imag(), hr = hk[i].real(), hi = hk[i].imag();
                dst[i] += cd (xr * hr - xi * hi, xr * hi + xi * hr);
            }
        }
        fft->run (dst, true);
    }

    void block() noexcept
    {
        const int n2 = 2 * P;
        auto* tb = timeBuf.data();
        std::copy (tb + P, tb + n2, tb);
        std::copy (in.begin(), in.begin() + P, tb + P);
        auto* x = fdl.data() + (std::ptrdiff_t) head * n2;
        std::copy (tb, tb + n2, x);
        fft->run (x, false);
        const double scale = 1.0 / n2;
        accumulate (active, acc.data());
        if (pending >= 0)
        {
            accumulate (pending, acc2.data());
            for (int i = 0; i < P; ++i)
            {
                const double t = (i + 0.5) / P;
                out[(std::size_t) i] = (acc[(std::size_t) (P + i)] * (1.0 - t) + acc2[(std::size_t) (P + i)] * t) * scale;
            }
            active = pending;
            pending = -1;
        }
        else
            for (int i = 0; i < P; ++i) out[(std::size_t) i] = acc[(std::size_t) (P + i)] * scale;
        head = (head + 1) % K;
        pos = 0;
    }

    const Fft* fft = nullptr;
    int P = 128, K = 1, pos = 0, head = 0, active = 0, pending = -1;
    std::vector<cd> fdl, kern[2], timeBuf, acc, acc2, in, out;
};

// a biquad on doubles (detector / solo filters)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
    double run (double x, int ch) noexcept
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void clear() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
};

// the band's region: band pass at its frequency and Q (low pass / high pass for the shelves and cuts)
void regionFilter (Biquad& f, const EffectParams& p, int b, double sr) noexcept
{
    const auto sh = shape (p[(std::size_t) param (b, 0)]);
    const double f0 = std::min (hz (p[(std::size_t) param (b, 1)]), 0.45 * sr);
    const double w0 = 2.0 * pi * f0 / sr, cw = std::cos (w0), sw = std::sin (w0);
    const bool low = sh == Shape::lowShelf || sh == Shape::highCut;
    const bool high = sh == Shape::highShelf || sh == Shape::lowCut;
    const double Q = low || high ? std::sqrt (0.5) : std::clamp (q (p[(std::size_t) param (b, 3)]), 0.1, 40.0);
    const double alpha = sw / (2.0 * Q), a0 = 1.0 + alpha;
    if (low) { f.b0 = (1.0 - cw) * 0.5 / a0; f.b1 = (1.0 - cw) / a0; f.b2 = f.b0; }
    else if (high) { f.b0 = (1.0 + cw) * 0.5 / a0; f.b1 = -(1.0 + cw) / a0; f.b2 = f.b0; }
    else { f.b0 = alpha / a0; f.b1 = 0.0; f.b2 = -alpha / a0; }
    f.a1 = -2.0 * cw / a0;
    f.a2 = (1.0 - alpha) / a0;
}

class FacetEqEffect final : public Effect
{
public:
    explicit FacetEqEffect (const EffectParams* initial)
    {
        if (initial != nullptr) mode = phase ((*initial)[(std::size_t) phaseParam]);
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        const int fct = rateFactor (sr);
        taps = firTaps * fct;
        fftNatural.init (2 * naturalBlock * fct);
        fftLinear.init (2 * linearBlock * fct);
        fftDesign.init (taps);
        conv.allocate (taps, linearBlock * fct);
        spec.assign ((std::size_t) taps, {});
        fir.assign ((std::size_t) taps, 0.0);
        delayLen = latencyFor (Phase::linear, sr) + 1;
        delay[0].assign ((std::size_t) delayLen, 0.0f);
        delay[1].assign ((std::size_t) delayLen, 0.0f);
        cached.fill (-1.0f);
        configureMode (mode);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& band : state)
            for (auto& st : band)
                st = {};
        for (auto& d : det) { d.filter.clear(); d.ms = 0.0; d.gain = 0.0; }
        solo.clear();
        conv.reset();
        firLoaded = false;
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;
        dynUsed.fill (0.0);
        for (auto& m : meters) m.store (0.0f, std::memory_order_relaxed);
    }

    void setSidechain (const float* left, const float* right) noexcept override
    {
        sideL = left;
        sideR = right;
        sideGiven = true;
    }

    float controlOutput (int o) const noexcept override
    {
        return o >= 0 && o < bands ? meters[(std::size_t) o].load (std::memory_order_relaxed) / 60.0f : 0.0f;
    }

    int latencySamples() const noexcept override { return latencyNow.load (std::memory_order_relaxed); }

    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        bool changed = false;
        for (int i = 0; i < paramCount; ++i)
            if (params[(std::size_t) i] != cached[(std::size_t) i]) { changed = true; break; }
        if (changed)
            update (params);

        // the detector: the slot's sidechain when asked for and routed (nullptr = a silent source), else the input
        const bool useSide = sideGiven && sidechainDetector (params[(std::size_t) detectorParam]);
        const float* dl = useSide ? sideL : left;
        const float* dr = useSide ? (sideR != nullptr ? sideR : sideL) : right;
        sideGiven = false;
        sideL = sideR = nullptr;

        constexpr int chunk = 32;
        for (int off = 0; off < n; off += chunk)
        {
            const int m = std::min (chunk, n - off);
            if (anyDynamic)
                detect (dl != nullptr ? dl + off : nullptr, dr != nullptr ? dr + off : nullptr, left + off, right + off, m, useSide);

            if (soloOn)
            {
                runSolo (left + off, right + off, m);
                continue;
            }
            if (mode == Phase::zeroLatency)
            {
                if (anyDynamic) redesignDynamic();
                runIir (left + off, right + off, m);
            }
            else
            {
                sinceDesign += m;
                if (! firLoaded || (sinceDesign >= updateEvery && ! conv.fading() && (firDirty || (anyDynamic && dynamicMoved()))))
                    loadFir();
                for (int i = 0; i < m; ++i) conv.step (left[off + i], right[off + i]);
            }
        }

        if (outGain != 1.0f)
            for (int i = 0; i < n; ++i) { left[i] *= outGain; right[i] *= outGain; }
        for (int b = 0; b < bands; ++b) meters[(std::size_t) b].store ((float) det[(std::size_t) b].gain, std::memory_order_relaxed);
    }

private:
    struct State { double z1[2] {}, z2[2] {}; };
    struct Detector
    {
        Biquad filter;
        double ms = 0.0, gain = 0.0, msCoef = 0.0, threshold = -20.0, range = 0.0;
        bool on = false;
    };

    void configureMode (Phase p) noexcept
    {
        mode = p;
        const int fct = rateFactor (sampleRate);
        if (p == Phase::natural) conv.configure (fftNatural, naturalBlock * fct, taps);
        else if (p == Phase::linear) conv.configure (fftLinear, linearBlock * fct, taps);
        updateEvery = std::max (1024 * fct, p == Phase::linear ? linearBlock * fct : naturalBlock * fct);
        firLoaded = false;
        latencyNow.store (latencyFor (p, sampleRate), std::memory_order_relaxed);
    }

    void update (const EffectParams& params) noexcept
    {
        for (int i = 0; i < paramCount; ++i) cached[(std::size_t) i] = params[(std::size_t) i];
        current = params;
        const auto newMode = phase (params[(std::size_t) phaseParam]);
        if (newMode != mode)
        {
            configureMode (newMode);
            conv.reset();
            for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        }
        anyDynamic = false;
        for (int b = 0; b < bands; ++b)
        {
            auto& d = det[(std::size_t) b];
            const bool was = d.on;
            d.on = isDynamic (params, b);
            d.threshold = thresholdDb (params[(std::size_t) (thresholdParam0 + b)]);
            d.range = rangeDb (params[(std::size_t) (rangeParam0 + b)]);
            if (d.on)
            {
                regionFilter (d.filter, params, b, sampleRate);
                const double f0 = hz (params[(std::size_t) param (b, 1)]);
                d.msCoef = 1.0 - std::exp (-1.0 / (std::max (0.001, 1.5 / f0) * sampleRate));
                anyDynamic = true;
                if (! was) { d.filter.clear(); d.ms = 0.0; }
            }
            else
                d.gain = 0.0;

            const int before = counts[(std::size_t) b];
            counts[(std::size_t) b] = facet::design (params, b, sampleRate, sections[(std::size_t) b], d.gain);
            if (d.on && counts[(std::size_t) b] == 0) // a dynamic band at 0 dB keeps its (flat) section to move it later
                counts[(std::size_t) b] = designKeep (b, d.gain);
            if (counts[(std::size_t) b] != before) // the structure changed: start this band clean
                for (auto& st : state[(std::size_t) b]) st = {};
            dynUsed[(std::size_t) b] = d.gain;
        }
        attackCoef = 1.0 - std::exp (-1.0 / (attackMs (params[(std::size_t) attackParam]) * 0.001 * sampleRate));
        releaseCoef = 1.0 - std::exp (-1.0 / (releaseMs (params[(std::size_t) releaseParam]) * 0.001 * sampleRate));
        outGain = (float) std::pow (10.0, outputDb (params[(std::size_t) outputParam]) / 20.0);

        const int sb = soloBand (params[(std::size_t) soloParam]);
        const bool wasSolo = soloOn;
        soloOn = sb >= 0 && shape (params[(std::size_t) param (sb, 0)]) != Shape::off;
        if (soloOn) regionFilter (solo, params, sb, sampleRate);
        if (soloOn && ! wasSolo) solo.clear();
        firDirty = true;
    }

    int designKeep (int b, double extra) noexcept
    {
        Analog a[maxSections];
        const int n = analogSections (current, b, a, extra, true);
        const double f0 = std::min (hz (current[(std::size_t) param (b, 1)]), 0.4999 * sampleRate);
        const double w0 = 2.0 * pi * f0 / sampleRate;
        for (int k = 0; k < n; ++k) sections[(std::size_t) b][(std::size_t) k] = matched (a[k], w0);
        return n;
    }

    // detector levels and the dynamic gains, sample by sample over one chunk
    void detect (const float* dl, const float* dr, const float* inL, const float* inR, int m, bool side) noexcept
    {
        for (int b = 0; b < bands; ++b)
        {
            auto& d = det[(std::size_t) b];
            if (! d.on) continue;
            for (int i = 0; i < m; ++i)
            {
                double x = 0.0;
                if (side) x = dl != nullptr ? 0.5 * ((double) dl[i] + (double) dr[i]) : 0.0;
                else x = 0.5 * ((double) inL[i] + (double) inR[i]);
                if (! std::isfinite (x)) x = 0.0;
                const double y = d.filter.run (x, 0);
                d.ms += d.msCoef * (y * y - d.ms);
                const double level = 10.0 * std::log10 (2.0 * d.ms + 1e-20);
                const double target = dynamicGainDb (level, d.threshold, d.range);
                d.gain += (std::abs (target) > std::abs (d.gain) ? attackCoef : releaseCoef) * (target - d.gain);
            }
            if (std::abs (d.ms) < 1e-30) d.ms = 0.0;
        }
    }

    void redesignDynamic() noexcept
    {
        for (int b = 0; b < bands; ++b)
        {
            const auto& d = det[(std::size_t) b];
            if (! d.on || std::abs (d.gain - dynUsed[(std::size_t) b]) < 0.01) continue;
            const int before = counts[(std::size_t) b];
            const int c = designKeep (b, d.gain);
            if (c != before)
            {
                for (auto& st : state[(std::size_t) b]) st = {};
                counts[(std::size_t) b] = c;
            }
            dynUsed[(std::size_t) b] = d.gain;
        }
    }

    bool dynamicMoved() const noexcept
    {
        for (int b = 0; b < bands; ++b)
            if (det[(std::size_t) b].on && std::abs (det[(std::size_t) b].gain - dynUsed[(std::size_t) b]) > 0.05) return true;
        return false;
    }

    void runIir (float* left, float* right, int n) noexcept
    {
        for (int b = 0; b < bands; ++b)
            for (int k = 0; k < counts[(std::size_t) b]; ++k)
            {
                const auto& c = sections[(std::size_t) b][(std::size_t) k];
                auto& st = state[(std::size_t) b][(std::size_t) k];
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* x = ch == 0 ? left : right;
                    double z1 = st.z1[ch], z2 = st.z2[ch];
                    for (int i = 0; i < n; ++i)
                    {
                        const double in = x[i];
                        const double y = c.b0 * in + z1;
                        z1 = c.b1 * in - c.a1 * y + z2;
                        z2 = c.b2 * in - c.a2 * y;
                        x[i] = (float) y;
                    }
                    // denormal guard
                    st.z1[ch] = std::abs (z1) < 1e-30 ? 0.0 : z1;
                    st.z2[ch] = std::abs (z2) < 1e-30 ? 0.0 : z2;
                }
            }
    }

    // solo: the band's region alone (delayed like the FIR modes so switching does not jump)
    void runSolo (float* left, float* right, int n) noexcept
    {
        const int lat = mode == Phase::zeroLatency ? 0 : latencyFor (mode, sampleRate);
        for (int i = 0; i < n; ++i)
        {
            const float yl = (float) solo.run (left[i], 0), yr = (float) solo.run (right[i], 1);
            if (lat == 0) { left[i] = yl; right[i] = yr; continue; }
            delay[0][(std::size_t) delayPos] = yl;
            delay[1][(std::size_t) delayPos] = yr;
            const int rd = (delayPos - lat + delayLen) % delayLen;
            left[i] = delay[0][(std::size_t) rd];
            right[i] = delay[1][(std::size_t) rd];
            delayPos = (delayPos + 1) % delayLen;
        }
    }

    // the FIR of the current response (analog magnitude; analog phase in natural mode), windowed around its delay
    void loadFir() noexcept
    {
        const int M = taps;
        const bool natural = mode == Phase::natural;
        const int fct = rateFactor (sampleRate);
        const int D = natural ? naturalDelay * fct : M / 2;
        // every band's analog sections once, then the product of their responses on the FFT grid
        Analog a[bands * maxSections];
        double f0[bands * maxSections];
        int used = 0;
        for (int b = 0; b < bands; ++b)
        {
            if (shape (current[(std::size_t) param (b, 0)]) == Shape::off) continue;
            const int c = analogSections (current, b, a + used, det[(std::size_t) b].on ? det[(std::size_t) b].gain : 0.0);
            for (int k = 0; k < c; ++k) f0[used + k] = hz (current[(std::size_t) param (b, 1)]);
            used += c;
        }
        const cd rot = std::polar (1.0, -2.0 * pi * D / M);
        cd phasor (1.0, 0.0);
        for (int k = 0; k <= M / 2; ++k)
        {
            const double f = std::max ((double) k * sampleRate / M, 0.01);
            double hr = 1.0, hi = 0.0;
            for (int j = 0; j < used; ++j)
            {
                const auto& s = a[j];
                const double w = f / f0[j], w2 = w * w;
                const double nr = s.n0 - s.n2 * w2, ni = s.n1 * w, dr = s.d0 - s.d2 * w2, di = s.d1 * w;
                const double dd = dr * dr + di * di;
                const double qr = (nr * dr + ni * di) / dd, qi = (ni * dr - nr * di) / dd;
                const double tr = hr * qr - hi * qi;
                hi = hr * qi + hi * qr;
                hr = tr;
            }
            cd h = natural ? cd (hr, hi) : cd (std::sqrt (hr * hr + hi * hi), 0.0);
            h = cd (h.real() * phasor.real() - h.imag() * phasor.imag(), h.real() * phasor.imag() + h.imag() * phasor.real());
            if ((k & 255) == 255) phasor = std::polar (1.0, -2.0 * pi * (double) (k + 1) * D / M); // no drift
            else phasor *= rot;
            if (k == M / 2 || k == 0) h = cd (h.real(), 0.0);
            spec[(std::size_t) k] = h;
            if (k > 0 && k < M / 2) spec[(std::size_t) (M - k)] = std::conj (h);
        }
        fftDesign.run (spec.data(), true);
        for (int i = 0; i < M; ++i)
        {
            const double w = i < D ? 0.5 * (1.0 - std::cos (pi * (i + 0.5) / D)) : 0.5 * (1.0 + std::cos (pi * (i - D) / (double) (M - D)));
            fir[(std::size_t) i] = spec[(std::size_t) i].real() / M * w;
        }
        conv.load (fir.data(), ! firLoaded);
        firLoaded = true;
        firDirty = false;
        sinceDesign = 0;
        for (int b = 0; b < bands; ++b) dynUsed[(std::size_t) b] = det[(std::size_t) b].gain;
    }

    double sampleRate = 48000.0;
    Phase mode = Phase::zeroLatency;
    EffectParams current {};
    std::array<float, paramCount> cached {};
    std::array<std::array<Section, maxSections>, bands> sections {};
    std::array<std::array<State, maxSections>, bands> state {};
    std::array<int, bands> counts {};
    std::array<Detector, bands> det {};
    std::array<double, bands> dynUsed {};
    std::array<std::atomic<float>, bands> meters {};
    double attackCoef = 0.1, releaseCoef = 0.001;
    float outGain = 1.0f;
    bool anyDynamic = false, soloOn = false, firLoaded = false, firDirty = true, sideGiven = false;
    Biquad solo;
    const float* sideL = nullptr;
    const float* sideR = nullptr;
    Fft fftNatural, fftLinear, fftDesign;
    Convolver conv;
    int taps = firTaps, updateEvery = 1024, sinceDesign = 0;
    std::vector<cd> spec;
    std::vector<double> fir;
    std::vector<float> delay[2];
    int delayLen = 1, delayPos = 0;
    std::atomic<int> latencyNow { 0 };
};
} // namespace


Shape shape (float v) noexcept { return (Shape) std::clamp ((int) std::lround (v * (shapeCount - 1)), 0, shapeCount - 1); }
float shapeValue (Shape s) noexcept { return (float) (int) s / (float) (shapeCount - 1); }
double hz (float v) noexcept { return 10.0 * std::pow (3000.0, (double) std::clamp (v, 0.0f, 1.0f)); }
float hzValue (double f) noexcept { return (float) std::clamp (std::log (f / 10.0) / std::log (3000.0), 0.0, 1.0); }
double gainDb (float v) noexcept { return ((double) v - 0.5) * 60.0; }
float gainValue (double db) noexcept { return (float) std::clamp (db / 60.0 + 0.5, 0.0, 1.0); }
double q (float v) noexcept { return 0.025 * std::pow (1600.0, (double) std::clamp (v, 0.0f, 1.0f)); }
float qValue (double qq) noexcept { return (float) std::clamp (std::log (qq / 0.025) / std::log (1600.0), 0.0, 1.0); }
int slopeDb (float v) noexcept { return slopes[(std::size_t) std::clamp ((int) std::lround (v * 8.0f), 0, 8)]; }
float slopeValue (int d) noexcept
{
    for (int i = 0; i < 9; ++i)
        if (slopes[(std::size_t) i] >= d) return (float) i / 8.0f;
    return 1.0f;
}
double outputDb (float v) noexcept { return ((double) v - 0.5) * 48.0; }
double gainScale (float v) noexcept { return (double) v * 2.0; }

const char* shapeName (Shape s) noexcept
{
    static const char* names[] = { "Off", "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf" };
    return names[(int) s];
}

const EffectInfo& info()
{
    static const EffectInfo i = [] {
        EffectInfo e { "Facet EQ", {} };
        static std::array<std::array<char, 24>, bands * fieldsPerBand> names {};
        const char* fields[] = { "Shape", "Freq", "Gain", "Q", "Slope" };
        for (int b = 0; b < bands; ++b)
            for (int f = 0; f < fieldsPerBand; ++f)
            {
                auto& n = names[(std::size_t) param (b, f)];
                std::snprintf (n.data(), n.size(), "%d %s", b + 1, fields[f]);
                const float def = f == 0 ? 0.0f : f == 1 ? hzValue (std::array<double, 8> { 50, 120, 300, 700, 1500, 3500, 8000, 15000 }[(std::size_t) b])
                                : f == 2 ? 0.5f : f == 3 ? 0.5f : slopeValue (12);
                e.params.push_back ({ n.data(), def });
            }
        e.params.push_back ({ "Output", 0.5f });
        e.params.push_back ({ "Gain Scale", 0.5f });
        static std::array<std::array<char, 24>, bands * 2> dynNames {};
        for (int k = 0; k < bands * 2; ++k)
        {
            auto& n = dynNames[(std::size_t) k];
            std::snprintf (n.data(), n.size(), k < bands ? "%d Range" : "%d Threshold", k % bands + 1);
            e.params.push_back ({ n.data(), k < bands ? 0.5f : thresholdValue (-20.0) });
        }
        e.params.push_back ({ "Attack", 0.5f });
        e.params.push_back ({ "Release", 0.5f });
        e.params.push_back ({ "Phase", 0.0f });
        e.params.push_back ({ "Solo", 0.0f });
        e.params.push_back ({ "Detector", 0.0f });
        e.params.push_back ({ "Analyzer", 1.0f });
        return e;
    }();
    return i;
}

std::string paramText (int p, float v)
{
    char t[48];
    if (p == outputParam) { std::snprintf (t, sizeof (t), "%+.2f dB", outputDb (v)); return t; }
    if (p == scaleParam) { std::snprintf (t, sizeof (t), "%.0f%%", gainScale (v) * 100.0); return t; }
    if (p >= rangeParam0 && p < rangeParam0 + bands) { std::snprintf (t, sizeof (t), "%+.2f dB", rangeDb (v)); return t; }
    if (p >= thresholdParam0 && p < thresholdParam0 + bands) { std::snprintf (t, sizeof (t), "%.1f dB", thresholdDb (v)); return t; }
    if (p == attackParam) { std::snprintf (t, sizeof (t), attackMs (v) < 10.0 ? "%.2f ms" : "%.1f ms", attackMs (v)); return t; }
    if (p == releaseParam) { const auto ms = releaseMs (v); std::snprintf (t, sizeof (t), ms < 1000.0 ? "%.0f ms" : "%.2f s", ms < 1000.0 ? ms : ms / 1000.0); return t; }
    if (p == phaseParam) return phaseName (phase (v));
    if (p == soloParam) { const auto b = soloBand (v); if (b < 0) return "Off"; std::snprintf (t, sizeof (t), "Band %d", b + 1); return t; }
    if (p == detectorParam) return sidechainDetector (v) ? "Sidechain" : "Input";
    if (p == analyzerParam) { static const char* a[] = { "Off", "Pre", "Post", "Pre + Post" }; return a[(int) analyzer (v)]; }
    switch (p % fieldsPerBand)
    {
        case 0: return shapeName (shape (v));
        case 1: { const auto f = hz (v); std::snprintf (t, sizeof (t), f < 1000 ? "%.1f Hz" : "%.2f kHz", f < 1000 ? f : f / 1000.0); return t; }
        case 2: std::snprintf (t, sizeof (t), "%+.2f dB", gainDb (v)); return t;
        case 3: std::snprintf (t, sizeof (t), "%.3f", q (v)); return t;
        default: std::snprintf (t, sizeof (t), "%d dB/oct", slopeDb (v)); return t;
    }
}

int design (const EffectParams& params, int band, double sampleRate, std::array<Section, maxSections>& out, double extraDb) noexcept
{
    Analog a[maxSections];
    const int n = analogSections (params, band, a, extraDb);
    const double f0 = std::min (hz (params[(std::size_t) param (band, 1)]), 0.4999 * sampleRate);
    const double w0 = 2.0 * pi * f0 / sampleRate;
    for (int k = 0; k < n; ++k)
        out[(std::size_t) k] = matched (a[k], w0);
    return n;
}

double analogDb (const EffectParams& params, int band, double f, double extraDb) noexcept
{
    Analog a[maxSections];
    const int n = analogSections (params, band, a, extraDb);
    const double w = f / hz (params[(std::size_t) param (band, 1)]);
    double m = 1.0;
    for (int k = 0; k < n; ++k) m *= std::sqrt (mag2 (a[k], w));
    return 20.0 * std::log10 (std::max (m, 1e-15));
}

double bandResponseDb (const EffectParams& params, int band, double sampleRate, double f, double extraDb) noexcept
{
    if (phase (params[(std::size_t) phaseParam]) != Phase::zeroLatency)
        return analogDb (params, band, f, extraDb);
    std::array<Section, maxSections> s;
    const int n = design (params, band, sampleRate, s, extraDb);
    const double w = 2.0 * pi * std::min (f, 0.5 * sampleRate) / sampleRate;
    double m = 1.0;
    for (int k = 0; k < n; ++k) m *= std::abs (evalDigital (s[(std::size_t) k], w));
    return 20.0 * std::log10 (std::max (m, 1e-15));
}

void bandCurveDb (const EffectParams& params, int band, double sampleRate, const double* f, int n, double* out, double extraDb) noexcept
{
    if (phase (params[(std::size_t) phaseParam]) != Phase::zeroLatency)
    {
        Analog a[maxSections];
        const int c = analogSections (params, band, a, extraDb);
        const double f0 = hz (params[(std::size_t) param (band, 1)]);
        for (int i = 0; i < n; ++i)
        {
            double m = 1.0;
            for (int k = 0; k < c; ++k) m *= mag2 (a[k], f[i] / f0);
            out[i] = 10.0 * std::log10 (std::max (m, 1e-30));
        }
        return;
    }
    std::array<Section, maxSections> s;
    const int c = design (params, band, sampleRate, s, extraDb);
    for (int i = 0; i < n; ++i)
    {
        const double w = 2.0 * pi * std::min (f[i], 0.5 * sampleRate) / sampleRate;
        double m = 1.0;
        for (int k = 0; k < c; ++k) m *= std::abs (evalDigital (s[(std::size_t) k], w));
        out[i] = 20.0 * std::log10 (std::max (m, 1e-15));
    }
}

double responseDb (const EffectParams& params, double sampleRate, double f, const float* dynamicDb) noexcept
{
    double db = outputDb (params[(std::size_t) outputParam]);
    for (int b = 0; b < bands; ++b)
        db += bandResponseDb (params, b, sampleRate, f, dynamicDb != nullptr && isDynamic (params, b) ? (double) dynamicDb[b] : 0.0);
    return db;
}

bool hasGain (Shape s) noexcept { return s == Shape::bell || s == Shape::lowShelf || s == Shape::highShelf || s == Shape::tiltShelf; }
double rangeDb (float v) noexcept { return ((double) std::clamp (v, 0.0f, 1.0f) - 0.5) * 60.0; }
float rangeValue (double db) noexcept { return (float) std::clamp (db / 60.0 + 0.5, 0.0, 1.0); }
double thresholdDb (float v) noexcept { return -80.0 + 80.0 * (double) std::clamp (v, 0.0f, 1.0f); }
float thresholdValue (double db) noexcept { return (float) std::clamp ((db + 80.0) / 80.0, 0.0, 1.0); }
double attackMs (float v) noexcept { return 0.25 * std::pow (200.0, (double) std::clamp (v, 0.0f, 1.0f)); }
double releaseMs (float v) noexcept { return 20.0 * std::pow (200.0, (double) std::clamp (v, 0.0f, 1.0f)); }
Phase phase (float v) noexcept { return (Phase) std::clamp ((int) std::lround (v * (phaseCount - 1)), 0, phaseCount - 1); }
float phaseValue (Phase p) noexcept { return (float) (int) p / (float) (phaseCount - 1); }
int soloBand (float v) noexcept { return std::clamp ((int) std::lround (v * bands), 0, bands) - 1; }
float soloValue (int band) noexcept { return band < 0 ? 0.0f : (float) (band + 1) / (float) bands; }
bool sidechainDetector (float v) noexcept { return v >= 0.5f; }
Analyzer analyzer (float v) noexcept { return (Analyzer) std::clamp ((int) std::lround (v * 3.0f), 0, 3); }
float analyzerValue (Analyzer a) noexcept { return (float) (int) a / 3.0f; }
const char* phaseName (Phase p) noexcept
{
    static const char* names[] = { "Zero Latency", "Natural Phase", "Linear Phase" };
    return names[(int) p];
}

bool isDynamic (const EffectParams& p, int b) noexcept
{
    return hasGain (shape (p[(std::size_t) param (b, 0)])) && std::abs (rangeDb (p[(std::size_t) (rangeParam0 + b)])) > 0.01;
}

double dynamicGainDb (double level, double threshold, double range) noexcept
{
    const double x = level - threshold;
    constexpr double knee = 10.0; // fitted to the reference (REA_rodada103 §2.1)
    const double over = x <= -0.5 * knee ? 0.0 : x < 0.5 * knee ? (x + 0.5 * knee) * (x + 0.5 * knee) / (2.0 * knee) : x;
    return range < 0.0 ? -std::min (-range, over) : std::min (range, over);
}

int latencyFor (Phase p, double sampleRate) noexcept
{
    const int f = rateFactor (sampleRate);
    switch (p)
    {
        case Phase::natural: return (naturalBlock + naturalDelay) * f;
        case Phase::linear: return linearBlock * f + firTaps * f / 2;
        default: return 0;
    }
}

std::unique_ptr<Effect> make (const EffectParams* initial) { return std::make_unique<FacetEqEffect> (initial); }
} // namespace lynx::facet
