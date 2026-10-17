#include "core/VocalPitch.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lynx::vocal
{
namespace
{
constexpr double pi = 3.14159265358979323846;
}

// ---------------------------------------------------------------------------------------------------------------------
// Scales (the usual Western interval sets, from the key's root)

const char* scaleName (Scale s) noexcept
{
    switch (s)
    {
        case Scale::chromatic: return "Chromatic";
        case Scale::major: return "Major";
        case Scale::minor: return "Minor";
        case Scale::harmonicMinor: return "Harmonic minor";
        case Scale::melodicMinor: return "Melodic minor";
        case Scale::majorPentatonic: return "Major pentatonic";
        case Scale::minorPentatonic: return "Minor pentatonic";
        case Scale::blues: return "Blues";
        case Scale::dorian: return "Dorian";
        case Scale::mixolydian: return "Mixolydian";
        case Scale::phrygian: return "Phrygian";
        case Scale::lydian: return "Lydian";
        case Scale::locrian: return "Locrian";
        case Scale::custom: return "Custom";
    }
    return "Chromatic";
}

std::uint16_t scaleMask (Scale s, int key, std::uint16_t customMask) noexcept
{
    // intervals above the root, bit k = k semitones
    auto bits = [] (std::initializer_list<int> steps) {
        std::uint16_t m = 0;
        for (auto k : steps)
            m = (std::uint16_t) (m | (1u << k));
        return m;
    };
    std::uint16_t rel = 0xfff;
    switch (s)
    {
        case Scale::chromatic: return 0xfff;
        case Scale::custom: return (std::uint16_t) (customMask & 0xfff);
        case Scale::major: rel = bits ({ 0, 2, 4, 5, 7, 9, 11 }); break;
        case Scale::minor: rel = bits ({ 0, 2, 3, 5, 7, 8, 10 }); break;
        case Scale::harmonicMinor: rel = bits ({ 0, 2, 3, 5, 7, 8, 11 }); break;
        case Scale::melodicMinor: rel = bits ({ 0, 2, 3, 5, 7, 9, 11 }); break;
        case Scale::majorPentatonic: rel = bits ({ 0, 2, 4, 7, 9 }); break;
        case Scale::minorPentatonic: rel = bits ({ 0, 3, 5, 7, 10 }); break;
        case Scale::blues: rel = bits ({ 0, 3, 5, 6, 7, 10 }); break;
        case Scale::dorian: rel = bits ({ 0, 2, 3, 5, 7, 9, 10 }); break;
        case Scale::mixolydian: rel = bits ({ 0, 2, 4, 5, 7, 9, 10 }); break;
        case Scale::phrygian: rel = bits ({ 0, 1, 3, 5, 7, 8, 10 }); break;
        case Scale::lydian: rel = bits ({ 0, 2, 4, 6, 7, 9, 11 }); break;
        case Scale::locrian: rel = bits ({ 0, 1, 3, 5, 6, 8, 10 }); break;
    }
    key = ((key % 12) + 12) % 12;
    std::uint16_t m = 0;
    for (int k = 0; k < 12; ++k)
        if (rel & (1u << k))
            m = (std::uint16_t) (m | (1u << ((k + key) % 12)));
    return m;
}

int nearestNote (double note, std::uint16_t mask, int current, double hysteresis) noexcept
{
    mask &= 0xfff;
    auto allowed = [mask] (int n) { return mask == 0 || (mask & (1u << (((n % 12) + 12) % 12))) != 0; };

    if (current >= 0 && allowed (current) && std::abs (note - current) < 0.5 + hysteresis)
        return current;

    const auto base = (int) std::lround (note);
    int best = base;
    double bestD = std::numeric_limits<double>::max();
    for (int d = -6; d <= 6; ++d)
    {
        const auto n = base + d;
        if (! allowed (n))
            continue;
        const auto dist = std::abs (note - n);
        if (dist < bestD - 1e-12)
        {
            bestD = dist;
            best = n;
        }
    }
    return best;
}

double hzToNote (double hz, double a4) noexcept { return 69.0 + 12.0 * std::log2 (hz / a4); }
double noteToHz (double note, double a4) noexcept { return a4 * std::exp2 ((note - 69.0) / 12.0); }

// ---------------------------------------------------------------------------------------------------------------------
// Pitch detector

PitchDetector::PitchDetector()
    : full ((std::size_t) maxFull, 0.0f), deci ((std::size_t) maxDecimated, 0.0f), diff ((std::size_t) maxDecimated, 0.0f)
{
}

void PitchDetector::prepare (double sampleRate, double fminHz, double fmaxHz, int hop) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    fmin = std::clamp (fminHz, 30.0, 1000.0);
    fmax = std::clamp (fmaxHz, fmin * 2.0, 2000.0);
    dec = std::max (1, (int) std::lround (sr / 11025.0));
    const auto fsd = sr / dec;
    tauMax = std::min ((int) std::ceil (fsd / fmin) + 2, maxDecimated / 2 - 8);
    tauMin = std::max (2, (int) std::floor (fsd / fmax));
    window = tauMax;
    hopDec = std::max (1, (int) std::lround ((double) hop / dec));
    centre = (window + tauMax) / 2 * dec;

    // 4th-order Butterworth low-pass (two biquads, Q 0.5412 / 1.3066) before decimation; RBJ cookbook form
    const auto fc = std::min (0.35 * fsd, 2000.0);
    const double qs[2] { 0.54119610, 1.30656296 };
    for (int s = 0; s < 2; ++s)
    {
        const auto w = 2.0 * pi * fc / sr;
        const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * qs[s]);
        const auto a0 = 1.0 + alpha;
        auto& b = lp[(std::size_t) s];
        b.b0 = (float) ((1.0 - cw) * 0.5 / a0);
        b.b1 = (float) ((1.0 - cw) / a0);
        b.b2 = b.b0;
        b.a1 = (float) (-2.0 * cw / a0);
        b.a2 = (float) ((1.0 - alpha) / a0);
    }
    reset();
}

void PitchDetector::reset() noexcept
{
    for (auto& b : lp)
        b.z1 = b.z2 = 0.0f;
    std::fill (full.begin(), full.end(), 0.0f);
    std::fill (deci.begin(), deci.end(), 0.0f);
    written = writtenDec = 0;
    phase = hopCount = 0;
    est = {};
}

bool PitchDetector::push (float x) noexcept
{
    auto y = lp[1].run (lp[0].run (x));
    if (! std::isfinite (y))
    {
        reset();
        y = 0.0f;
    }
    full[(std::size_t) (written & (maxFull - 1))] = y;
    ++written;

    if (++phase < dec)
        return false;
    phase = 0;
    deci[(std::size_t) (writtenDec & (maxDecimated - 1))] = y;
    ++writtenDec;

    if (++hopCount < hopDec || writtenDec < window + tauMax + 1)
        return false;
    hopCount = 0;
    analyse();
    return true;
}

void PitchDetector::analyse() noexcept
{
    constexpr auto dm = maxDecimated - 1;
    const auto base = writtenDec - (window + tauMax);
    auto at = [&] (std::int64_t j) { return deci[(std::size_t) ((base + j) & dm)]; };

    // YIN steps 2-3: difference function and its cumulative mean normalised form
    double energy = 0.0;
    for (int j = 0; j < window; ++j)
        energy += (double) at (j) * at (j);
    est.level = (float) std::sqrt (energy / window);

    diff[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const auto e = (double) at (j) - at (j + tau);
            d += e * e;
        }
        running += d;
        diff[(std::size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }

    // step 4: the first dip under the threshold (then down to its local minimum); otherwise the global minimum
    int tau = -1;
    for (int t = tauMin; t < tauMax; ++t)
        if (diff[(std::size_t) t] < threshold)
        {
            while (t + 1 < tauMax && diff[(std::size_t) t + 1] < diff[(std::size_t) t])
                ++t;
            tau = t;
            break;
        }
    bool found = tau > 0;
    if (! found)
    {
        tau = tauMin;
        for (int t = tauMin + 1; t < tauMax; ++t)
            if (diff[(std::size_t) t] < diff[(std::size_t) tau])
                tau = t;
    }
    est.clarity = std::clamp (1.0f - diff[(std::size_t) tau], 0.0f, 1.0f);
    est.voiced = found && est.level > gate && tau > tauMin && tau < tauMax - 1;
    if (! est.voiced)
        return; // keep the last period (the shifter keeps its marks' spacing through short gaps)

    // step 5: parabolic interpolation
    auto refine = [] (double a, double b, double c) {
        const auto den = a - 2.0 * b + c;
        return std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
    };
    auto period = ((double) tau + refine (diff[(std::size_t) tau - 1], diff[(std::size_t) tau], diff[(std::size_t) tau + 1])) * dec;

    // full-rate refinement: the raw difference function around the coarse period
    if (dec > 1)
    {
        constexpr auto fm = maxFull - 1;
        const auto lo = std::max (2, (int) std::floor (period) - dec - 1);
        const auto hi = (int) std::ceil (period) + dec + 1;
        const auto wf = std::min (window * dec, 2048);
        if (wf + hi + 4 < maxFull && written > wf + hi + 1)
        {
            const auto start = written - wf - hi - 1;
            auto y = [&] (std::int64_t j) { return full[(std::size_t) ((start + j) & fm)]; };
            std::array<double, 64> d {};
            const auto count = std::min (hi - lo + 1, 64);
            int bestK = 0;
            for (int k = 0; k < count; ++k)
            {
                double s = 0.0;
                for (int j = 0; j < wf; ++j)
                {
                    const auto e = (double) y (j) - y (j + lo + k);
                    s += e * e;
                }
                d[(std::size_t) k] = s;
                if (s < d[(std::size_t) bestK])
                    bestK = k;
            }
            if (bestK > 0 && bestK < count - 1)
                period = lo + bestK + refine (d[(std::size_t) bestK - 1], d[(std::size_t) bestK], d[(std::size_t) bestK + 1]);
        }
    }

    est.period = period;
    est.hz = sr / period;
}

std::vector<PitchFrame> analysePitch (const float* x, int n, double sampleRate, int hop, double fminHz, double fmaxHz, float threshold)
{
    hop = std::max (1, hop);
    std::vector<PitchFrame> frames ((std::size_t) (n / hop + 1));
    if (n <= 0 || x == nullptr)
        return frames;

    PitchDetector det;
    det.prepare (sampleRate, fminHz, fmaxHz, std::max (1, hop / 2));
    det.setThreshold (threshold);
    const auto c = det.windowCentre();

    // estimates at their window centres
    struct At
    {
        std::int64_t t;
        PitchEstimate e;
    };
    std::vector<At> all;
    all.reserve ((std::size_t) (2 * n / std::max (1, hop / 2) + 4));
    for (int i = 0; i < n + c; ++i)
        if (det.push (i < n ? x[i] : 0.0f))
            all.push_back ({ (std::int64_t) i - c, det.estimate() });

    std::size_t k = 0;
    for (std::size_t f = 0; f < frames.size(); ++f)
    {
        const auto t = (std::int64_t) f * hop;
        while (k + 1 < all.size() && std::llabs (all[k + 1].t - t) <= std::llabs (all[k].t - t))
            ++k;
        if (all.empty())
            break;
        const auto& e = all[k].e;
        frames[f].hz = e.voiced ? e.hz : 0.0;
        frames[f].clarity = e.clarity;
        frames[f].level = e.level;
    }
    return frames;
}

// ---------------------------------------------------------------------------------------------------------------------
// TD-PSOLA

Psola::Psola() : env ((std::size_t) ringSize, 0.0f)
{
    for (int c = 0; c < 2; ++c)
    {
        in[c].assign ((std::size_t) ringSize, 0.0f);
        acc[c].assign ((std::size_t) ringSize, 0.0f);
    }
}

void Psola::prepare (double sampleRate, double maxPeriod, int latency) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxP = std::clamp (maxPeriod, 16.0, ringSize / 8.0);
    delay = std::clamp (latency, 8, ringSize / 2);
    lag = 0;
    unvoicedHop = std::clamp ((int) std::lround (128.0 * sr / 44100.0), 4, std::max (4, (delay - 4) / 2));
    reset();
}

void Psola::reset() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (in[c].begin(), in[c].end(), 0.0f);
        std::fill (acc[c].begin(), acc[c].end(), 0.0f);
    }
    std::fill (env.begin(), env.end(), 0.0f);
    envZ = 0.0f;
    now = 0;
    nextMark = 0.0;
    nextSynth = 0.0;
    markHead = markCount = 0;
    period = 0.0;
    voiced = false;
    ratio = formant = 1.0;
}

void Psola::setMarkLag (int samples) noexcept
{
    lag = std::clamp (samples, 0, std::max (0, delay - (int) std::ceil (1.5 * maxP)));
}

void Psola::setPitch (double p, bool v) noexcept
{
    voiced = v && p >= 2.0 && p <= maxP * 1.5;
    if (voiced)
        period = p;
}

void Psola::setShift (double r, double f) noexcept
{
    ratio = std::clamp (std::isfinite (r) ? r : 1.0, 0.25, 4.0);
    formant = std::clamp (std::isfinite (f) ? f : 1.0, 0.5, 2.0);
}

float Psola::readIn (int ch, double pos) const noexcept
{
    const auto i = (std::int64_t) std::floor (pos);
    if (i - 1 < now - ringSize + 2 || i + 2 > now - 1 || i - 1 < 0)
        return 0.0f;
    const auto t = (float) (pos - (double) i);
    constexpr auto m = ringSize - 1;
    const auto& b = in[ch];
    const auto y0 = b[(std::size_t) ((i - 1) & m)], y1 = b[(std::size_t) (i & m)], y2 = b[(std::size_t) ((i + 1) & m)],
               y3 = b[(std::size_t) ((i + 2) & m)];
    // 4-point Hermite
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void Psola::placeMarks() noexcept
{
    const auto last = (double) (now - 1 - lag);
    // far behind (start, or after a long period): restart the marks at the present
    if (nextMark < last - 2.0 * maxP)
        nextMark = last;

    auto push = [this] (double pos, float p, bool v) {
        if (markCount == maxMarks)
        {
            markHead = (markHead + 1) % maxMarks;
            --markCount;
        }
        marks[(std::size_t) ((markHead + markCount) % maxMarks)] = { pos, p, v, (float) ratio, (float) formant };
        ++markCount;
    };

    if (! voiced || period <= 0.0)
    {
        if (nextMark <= last)
        {
            push (nextMark, (float) unvoicedHop, false);
            nextMark += unvoicedHop;
        }
    }
    else if (peakAlign)
    {
        const auto reach = period * 0.25;
        if (nextMark + reach <= last)
        {
            constexpr auto m = ringSize - 1;
            const auto from = (std::int64_t) std::ceil (nextMark - reach), to = (std::int64_t) std::floor (nextMark + reach);
            std::int64_t best = (std::int64_t) std::lround (nextMark);
            float top = -std::numeric_limits<float>::max();
            for (auto i = std::max<std::int64_t> (from, now - ringSize + 1); i <= to; ++i)
            {
                // prefer the predicted position a little: ties and flat tops do not make the marks wander
                const auto v = env[(std::size_t) (i & m)] - 1e-4f * (float) std::abs ((double) i - nextMark) / (float) period;
                if (v > top)
                {
                    top = v;
                    best = i;
                }
            }
            push ((double) best, (float) period, true);
            nextMark = (double) best + period;
        }
    }
    else if (nextMark <= last)
    {
        push (nextMark, (float) period, true);
        nextMark += period;
    }

    // forget the marks no grain can use any more
    const auto oldest = (double) (now - 1 - delay) - 2.0 * maxP - 8.0;
    while (markCount > 1 && marks[(std::size_t) markHead].pos < oldest)
    {
        markHead = (markHead + 1) % maxMarks;
        --markCount;
    }
}

void Psola::process (const float* x, float* y, int channels) noexcept
{
    constexpr auto m = ringSize - 1;
    channels = std::clamp (channels, 1, 2);
    for (int c = 0; c < channels; ++c)
        in[c][(std::size_t) (now & m)] = std::isfinite (x[c]) ? x[c] : 0.0f;
    const auto mono = channels == 2 ? 0.5f * (in[0][(std::size_t) (now & m)] + in[1][(std::size_t) (now & m)]) : in[0][(std::size_t) (now & m)];
    // a gentle low-pass (one pole near 900 Hz) of the input: the marks lock to its peaks
    const auto a = (float) (1.0 - std::exp (-2.0 * pi * 900.0 / sr));
    envZ += a * (mono - envZ);
    if (! std::isfinite (envZ))
        envZ = 0.0f;
    env[(std::size_t) (now & m)] = envZ;
    ++now;

    placeMarks();

    const auto e = now - 1 - delay; // the output sample of this call (input timeline)
    const auto last = (double) (now - 1);

    // overlap-add every grain that starts at or before e
    for (int guard = 0; guard < 64; ++guard)
    {
        if (nextSynth < (double) e - maxP)
            nextSynth = (double) e; // fell behind (start): resume at the present
        if (nextSynth - 2.5 * maxP > (double) e)
            break; // no grain can reach e yet

        // the analysis mark nearest the synthesis mark among those whose whole grain is already in
        int best = -1;
        double bestD = std::numeric_limits<double>::max();
        for (int k = 0; k < markCount; ++k)
        {
            const auto& mk = marks[(std::size_t) ((markHead + k) % maxMarks)];
            const auto need = mk.voiced ? mk.pos + mk.period + 2.0 : mk.pos;
            if (need > last)
                continue;
            const auto d = std::abs (mk.pos - nextSynth);
            if (d < bestD)
            {
                bestD = d;
                best = k;
            }
        }

        const Mark* mk = best >= 0 ? &marks[(std::size_t) ((markHead + best) % maxMarks)] : nullptr;
        const auto isVoiced = mk != nullptr && mk->voiced;
        const auto f = isVoiced ? (double) mk->formant : 1.0;
        const auto r = isVoiced ? (double) mk->ratio : 1.0;
        const auto ha = isVoiced ? (double) mk->period : (double) unvoicedHop; // analysis half-length
        const auto hs = ha / f;                                                // synthesis half-length
        const auto ts = nextSynth;

        if (ts - hs > (double) e)
            break; // this grain starts later

        // unvoiced: read around the synthesis time itself (plain overlap-add, transparent); voiced: around the mark
        const auto ta = isVoiced ? mk->pos : ts;
        if (! isVoiced && ts + hs + 2.0 > last)
            break; // not in yet (only with a very short latency)

        const auto gain = (float) std::clamp (f / r, 0.25, 4.0);
        const auto o0 = std::max<std::int64_t> ((std::int64_t) std::ceil (ts - hs), e);
        const auto o1 = std::min<std::int64_t> ((std::int64_t) std::floor (ts + hs), e + ringSize - 1);
        const auto invHs = 1.0 / hs;
        for (auto o = o0; o <= o1; ++o)
        {
            const auto u = ((double) o - ts) * invHs;
            if (u <= -1.0 || u >= 1.0)
                continue;
            const auto w = gain * (float) (0.5 + 0.5 * std::cos (pi * u));
            const auto src = ta + ((double) o - ts) * f;
            for (int c = 0; c < channels; ++c)
                acc[c][(std::size_t) (o & m)] += w * readIn (c, src);
        }
        // unshifted voiced grains (within 1.7 cents) drift toward their marks, so a voice already on its note comes
        // out as the input itself (delayed), not displaced by up to half a period
        auto hop = isVoiced ? ha / r : ha;
        if (isVoiced && std::abs (r - 1.0) < 1e-3 && std::abs (f - 1.0) < 1e-3)
            hop += 0.25 * std::clamp (ta - ts, -0.5 * ha, 0.5 * ha);
        nextSynth = ts + hop;
    }

    for (int c = 0; c < channels; ++c)
    {
        if (e < 0)
        {
            y[c] = 0.0f;
            continue;
        }
        auto& slot = acc[c][(std::size_t) (e & m)];
        y[c] = slot;
        slot = 0.0f;
    }
}
} // namespace lynx::vocal
