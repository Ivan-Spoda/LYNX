#include "core/FlSynths.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace lynx
{
namespace
{
std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t offset)
{
    std::int32_t v = 0;
    if (offset + 4 <= d.size())
        std::memcpy (&v, d.data() + offset, 4);
    return v;
}

// tuning in semitones -> oscillator octave + semitones (added to what it already has)
void transpose (SynthOscillator& osc, double semitones)
{
    const auto total = osc.octave * 12 + osc.semitones + (int) std::lround (semitones);
    osc.octave = std::clamp (total / 12, -3, 3);
    osc.semitones = std::clamp (total - osc.octave * 12, -11, 11);
}
} // namespace

bool synthFromHarmless (const std::vector<std::uint8_t>& d, double bpm, SynthSettings& synth, float& gain)
{
    const auto version = i32At (d, 0);

    if (version < 4 || version > 12)
        return false;

    const auto count = version < 9 ? 99 : 100;

    if (d.size() < 4 + (std::size_t) count * 4 + 7 * 4)
        return false;

    auto p = [&] (int i) { return i < count ? i32At (d, 4 + 4 * (std::size_t) i) : 0; };
    auto x = [&] (int i) { return p (i) / 1000.0; };
    const auto tail = 4 + (std::size_t) count * 4; // the 7 combos without a parameter index
    const auto filterType = std::clamp (i32At (d, tail), 0, 16);
    const auto color = std::clamp (i32At (d, tail + 16), 0, 11);
    const auto beatsPerSecond = std::max (1.0, bpm) / 60.0;

    SynthSettings s;

    // --- oscillators (REA_rodada21 §1.1–1.4): the color gives the amplitude and phase of each harmonic, the mask multiplies
    // harmonic n by g_((n−1) mod 12)·m + (1 − m) and the "detune" p39 spaces the partials at f·(1 + (k − 1)·D);
    // everything becomes the spectrum of LYNX's "spectral" oscillator. Sub 1 is a sine one octave below ---
    const auto mask = std::clamp (x (23), 0.0, 1.0);
    auto colorAmplitude = [color] (double n) {
        const auto even = std::fmod (n, 2.0) == 0.0, mod4 = std::fmod (n, 4.0) == 0.0;
        switch (color)
        {
            case 1: return std::sqrt (2.0) / (n + 1.0);
            case 2: return 2.0 / (n + 3.0);
            case 5: return even ? 1.1 / (n * (n - 1.0)) : 1.1 / n;
            case 6: return even ? 1.1 / (n * std::pow (n - 1.0, 0.2)) : 1.1 / n;
            case 7: return 0.29167 * (std::pow (7.0, 1.0 / n) - 1.0);
            case 8: return 0.5 / n + (even ? 1.0 / (n + 1.0) : 0.0);
            case 9: return 1.0 / (3.0 * n) + (even ? 2.0 / (3.0 * (n + 1.0)) : 0.0) + (mod4 ? 4.0 / (3.0 * (n + 3.0)) : 0.0);
            case 10: return 0.5 / n + (even ? 0.5 / (n + 1.0) : 0.0) + (mod4 ? 0.5 / (n + 3.0) : 0.0);
            default: return 1.0 / n; // 0 Saw, 3/4 saws with shifted phase
        }
    };
    auto colorPhase = [color] (int n) {
        switch (color)
        {
            case 3: return n % 2 == 0 ? 0.25 : 0.0;
            case 4: return n % 4 == 2 ? 0.5 : 0.0;
            case 5: case 6: return n % 2 == 0 ? 0.25 : 0.0;
            case 7: return n % 2 == 0 ? 0.75 : 0.0;
            default: return 0.0;
        }
    };
    // Custom color: n amplitudes + n phases after the combos (REA_rodada10 §1.1/§1.5)
    std::vector<float> customAmplitude, customPhase;
    if (const auto customCount = i32At (d, tail + 28); color == 11 && customCount > 0 && customCount <= 4096
        && d.size() >= tail + 32 + (std::size_t) customCount * 8)
        for (int k = 0; k < customCount; ++k)
        {
            float a = 0.0f, ph = 0.0f;
            std::memcpy (&a, d.data() + tail + 32 + 4 * (std::size_t) k, 4);
            std::memcpy (&ph, d.data() + tail + 32 + 4 * (std::size_t) (customCount + k), 4);
            customAmplitude.push_back (std::isfinite (a) ? a : 0.0f);
            customPhase.push_back (std::isfinite (ph) ? ph : 0.0f);
        }
    double maskGain[12] {};
    for (int j = 0; j < 12; ++j)
    {
        const auto g = std::clamp (p (11 + j), -2000, 2000) / 1000.0;
        maskGain[j] = (g < 0.0 ? -1.0 : 1.0) * (std::pow (11.0, std::abs (g)) - 1.0) / 10.0;
    }
    // spacing D = 2^(p39/1200): reproduced when D or 1/D is 1, 2 or 4 (the series stays harmonic)
    const auto detune = std::clamp (p (39), -2400, 2400);
    int stride = 1, offset = 1, octaveShift = 0;
    if (detune == 1200 || detune == 2400)
        stride = detune == 1200 ? 2 : 4;
    else if (detune == -1200 || detune == -2400)
    {
        offset = detune == -1200 ? 2 : 4; // partial k at f/m·(k + m − 1): series of f/m
        octaveShift = detune == -1200 ? -1 : -2;
    }
    // timbre EQ (REA_rodada24 §2.1/2.2): 6 Hann windows of ±1 octave in log2 of the ratio, centered on harmonics
    // 1, 2, 4, 8, 16, 32 (the 1st is 1 below and the last above); gain of band k = (3^((p(5+k) + 1024)/1024) − 1)/2
    double eqGain[6] {};
    for (int b = 0; b < 6; ++b)
        eqGain[b] = (std::pow (3.0, (std::clamp (p (5 + b), -1024, 1024) + 1024) / 1024.0) - 1.0) / 2.0;
    auto timbreEq = [&eqGain] (double ratio) {
        double sum = 0.0;
        for (int b = 0; b < 6; ++b)
        {
            const auto x = std::log2 (ratio) - b;
            double w = (std::cos (3.141592653589793 * std::clamp (x, -1.0, 1.0)) + 1.0) / 2.0;
            if ((b == 0 && x < 0.0) || (b == 5 && x > 0.0))
                w = 1.0;
            sum += eqGain[b] * w;
        }
        return sum;
    };
    // Low Harmonics Protection (§2.4): on harmonics 1…4 undoes the mask by p_n·w, w = (11^(p1/1024) − 1)/10
    const auto protection = (std::pow (11.0, std::clamp (p (1), 0, 2048) / 1024.0) - 1.0) / 10.0;
    static constexpr double protectionCurve[4] { 1.0, 0.0831, 0.000625, 2e-7 };
    // Subs 3/4 (1.5f and 2.5f) only fit in the f/2 series: with them (and without "detune") the spectrum drops half a series
    const auto halfSeries = detune == 0 && (p (3) > 0 || p (4) > 0);
    if (halfSeries)
    {
        stride = 2;
        offset = 2;
        octaveShift = -1;
    }
    for (int k = 1; k <= SynthSettings::spectrumSize; ++k)
    {
        const auto index = offset + (k - 1) * stride; // position in the oscillator's series
        if (index > SynthSettings::spectrumSize)
            break;
        double amplitude = 0.0, phase = 0.0;
        if (color == 11)
        {
            if (k <= (int) customAmplitude.size())
                amplitude = customAmplitude[(std::size_t) k - 1], phase = customPhase[(std::size_t) k - 1];
        }
        else
            amplitude = colorAmplitude (k), phase = colorPhase (k);
        const auto maskFactor = maskGain[(k - 1) % 12] * mask + (1.0 - mask);
        amplitude *= timbreEq ((double) k);
        amplitude *= k <= 4 ? maskFactor + (1.0 - maskFactor) * protectionCurve[k - 1] * protection : maskFactor;
        s.spectrum[(std::size_t) index - 1] = (float) amplitude;
        s.spectrumPhase[(std::size_t) index - 1] = (float) phase;
    }

    const auto voices = std::clamp (p (65), 1, 9);
    const auto wave = Waveform::spectral;
    s.osc = { SynthOscillator { wave, octaveShift, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };

    if (voices >= 2) // unison: three detuned copies spread in stereo
    {
        const auto spread = std::max (5.0, (std::pow (41.0, std::clamp (p (67), 0, 256) / 256.0) - 1.0) * 1.875);
        const auto pan = std::clamp (p (66), 0, 128) / 128.0f;
        s.osc[0].level = 1.0f / 3.0f;
        s.osc[1] = { wave, octaveShift, (float) std::min (100.0, spread), 1.0f / 3.0f, 0, pan };
        s.osc[2] = { wave, octaveShift, (float) -std::min (100.0, spread), 1.0f / 3.0f, 0, -pan };
    }
    // Subs (§2.3): Sub 1 = (p2/1024)·A(1)·g₀; Sub 3 = (p3/1024)·A(1.5)·(0.3681g₀ + 0.6319g₁);
    // Sub 4 = (p4/1024)·A(2.5)·(0.7654g₁ + 0.2346g₂); the mask does not act on them
    const auto a1 = color == 11 ? (customAmplitude.empty() ? 0.0 : (double) customAmplitude[0]) : colorAmplitude (1.0);
    const auto sub1 = std::clamp (p (2), 0, 2048) / 1024.0 * a1 * eqGain[0];
    if (halfSeries) // f/2 series: Sub 1 = index 1, Sub 3 = index 3, Sub 4 = index 5
    {
        s.spectrum[0] = (float) std::clamp (sub1, 0.0, 64.0);
        s.spectrum[2] = (float) std::clamp (std::clamp (p (3), 0, 2048) / 1024.0 * colorAmplitude (1.5) * (0.3681 * eqGain[0] + 0.6319 * eqGain[1]), 0.0, 64.0);
        s.spectrum[4] = (float) std::clamp (std::clamp (p (4), 0, 2048) / 1024.0 * colorAmplitude (2.5) * (0.7654 * eqGain[1] + 0.2346 * eqGain[2]), 0.0, 64.0);
        s.spectrumPhase[0] = s.spectrumPhase[2] = s.spectrumPhase[4] = 0.0f;
    }
    else if (voices < 2 && p (2) > 0) // Sub 1: sine one octave below (saw ±1 = 2/π at the fundamental)
        s.osc[2] = { Waveform::sine, -1, 0.0f, (float) std::clamp (sub1 * 2.0 / 3.141592653589793, 0.0, 4.0) };

    for (auto& o : s.osc)
        transpose (o, std::clamp (p (38), -2400, 2400) / 100.0); // master tuning in cents

    // --- volume envelope: Harmless's decay falls to −40 dB (no sustain); 1000 = hold ---
    s.attack = (float) std::min (5.0, 0.05 * (std::pow (101.0, std::clamp (x (26), 0.0, 1.0)) - 1.0));

    if (p (27) >= 1000)
    {
        s.decay = 0.0f;
        s.sustain = 1.0f;
    }
    else
    {
        s.sustain = 0.0f;
        s.decay = (float) std::min (10.0, 1.5 * (0.11995 * (std::pow (1001.0, std::clamp (x (27), 0.0, 1.0)) - 1.0) + 0.05)); // −40 -> −60 dB
    }

    s.release = (float) std::min (10.0, 1.5 * (0.0249 * (std::pow (101.0, std::clamp (x (28), 0.0, 1.0)) - 1.0) + 0.01));

    // --- filter (REA_rodada21 §1.6): range in log2 Hz from 73.42 Hz to 17 kHz widened by M = (2/3)·2^(0.004·p56 − 1)
    // according to the type's extensions (eL, eR); cutoff = lo + p54/8192·(hi − lo); keytracking p57/1000 from 128 Hz ---
    switch (filterType)
    {
        case 1: case 5: case 14: case 15: case 16: s.filterType = 1; break; // band-pass (the phasers, wide)
        case 2: case 6: s.filterType = 3; break;                            // band-reject
        case 3: case 7: s.filterType = 2; break;                            // high-pass
        default: s.filterType = 0; break;                                   // the low-passes
    }
    const auto band = filterType == 1 || filterType == 2 || filterType == 5 || filterType == 6 || (filterType >= 14 && filterType <= 16);
    const auto eL = filterType == 3 || filterType == 7 ? 0.0 : 1.0, eR = band || filterType == 3 || filterType == 7 ? 1.0 : 0.0;
    const auto margin = 2.0 / 3.0 * std::pow (2.0, 0.004 * std::clamp (p (56), 0, 1000) - 1.0);
    const auto lo = std::log2 (440.0 * std::pow (2.0, -31.0 / 12.0)) - std::max (0.0, margin * eL - 1.0);
    const auto hi = std::log2 (17000.0) + std::max (0.0, margin * eR - 1.0);
    auto position = lo + std::clamp (p (54), 0, 8192) / 8192.0 * (hi - lo);
    const auto keyTrack = std::clamp (p (57), -2000, 2000) / 1000.0;
    position += keyTrack * std::log2 (130.81 / 128.0); // LYNX measures keytracking from key 48
    s.keyTrack = (float) keyTrack;
    s.resonance = (float) std::clamp (0.9 * x (59) + (p (62) > 0 ? 0.1 : 0.0), 0.0, 1.0); // approximation (peak width not decoded)

    // --- filter envelope: non-adaptive is centered ((E − 0.5)·x52·(hi − lo)); adaptive goes up to the limit ---
    if (const auto amount = std::clamp (x (52), -1.0, 1.0); std::abs (amount) > 0.01 && p (48) < 1000)
    {
        const auto adaptive = p (47) != 0;
        double octaves = 0.0;
        if (adaptive)
            octaves = amount > 0.0 ? amount * (hi - position) : amount * (position - lo);
        else
        {
            octaves = amount * (hi - lo);
            position -= 0.5 * octaves;
        }
        auto envTime = [] (double v) { return 0.12 * (std::pow (1001.0, std::clamp (v, 0.0, 1.0)) - 1.0); };
        const auto holds = p (49) >= 1000; // decay that holds at 1
        s.filterEnv = { (float) std::clamp (octaves, -10.0, 10.0), (float) envTime (x (48)), 0.0f, holds ? 0.0f : (float) envTime (x (49)),
                        holds ? 1.0f : 0.0f, s.release };
    }
    else if (std::abs (amount) > 0.01 && p (47) == 0) // attack that never rises: E = 0, offset only
        position -= 0.5 * amount * (hi - lo);

    // --- LFO: period 64/2^(|v|/1000) beats; phase locked to the song with p87; targets pitch (bipolar, ct) and cutoff
    // (unipolar: 0…6·x58 octaves = offset of A/2 ± A/2); p25 is a separate 2.5 Hz tremolo, not the LFO ---
    // stereo tremolo d = 0.001·p25 and vibrato 0.075·p41 cents (REA_rodada27 §1.1/§1.2)
    s.stereoTremolo = (float) std::clamp (p (25), 0, 1000) / 1000.0f;
    s.vibratoCents = 0.075f * (float) std::clamp (p (41), 0, 1000);

    const auto shape = std::clamp (p (86), 0, 13);
    const auto speed = std::abs (std::clamp (p (89), -8000, 8000));

    if (speed > 0 && shape <= 8)
    {
        const auto lfoShape = shape == 3 || shape == 6 ? 1 : (shape == 2 || shape == 4 || shape == 5) ? 2 : 0;
        const auto period = (float) (64.0 / std::pow (2.0, speed / 1000.0));
        const auto attackBeats = (float) (0.05 * (std::pow (101.0, std::clamp (x (88), 0.0, 1.0)) - 1.0) * beatsPerSecond);

        if (std::abs (p (42)) > 0)
        {
            const auto cents = (std::pow (101.0, std::abs (x (42))) - 1.0) * 12.0 * (p (42) < 0 ? -1.0 : 1.0);
            s.lfo[lfoPitch] = { (float) (cents / 100.0), period, true, lfoShape, 0.0f, attackBeats };
        }

        if (std::abs (p (58)) > 0)
        {
            const auto a = std::clamp (6.0 * x (58), -8.0, 8.0);
            position += a / 2.0;
            s.lfo[lfoCutoff] = { (float) (a / 2.0), period, true, lfoShape, 0.0f, attackBeats };
        }

        for (auto& lfo : s.lfo)
            lfo.global = p (87) > 0;
    }
    s.cutoff = (float) std::clamp (std::exp2 (position), 20.0, 20000.0);

    // --- mono / portamento ---
    s.mono = p (44) != 0;
    s.porta = p (43) != 0;
    s.portaBeats = (float) std::clamp ((std::pow (41.0, std::abs (x (45))) - 1.0) / 26.667 * beatsPerSecond, 0.0, 8.0);

    // Harmless master volume and unison compensation
    const auto master = 0.75 * (std::pow (3.0, std::clamp (x (36), 0.0, 2.0)) - 1.0) / 2.0;
    gain = (float) std::clamp (master / 0.75, 0.05, 4.0); // 0.75 = the default (0 dB here); the 3 oscillators already sum to 1

    synth = s;
    return true;
}

bool synthFromFruitKick (const std::vector<std::uint8_t>& d, SynthSettings& synth)
{
    const auto version = i32At (d, 0);

    if (version < 0 || version > 1 || d.size() < 4 + 6 * 4)
        return false;

    auto p = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    constexpr double fs = 44100.0;

    // time constants (s) of Fruit Kick's per-sample coefficients
    auto tau = [] (double coefficient) { return coefficient > 0.0 && coefficient < 1.0 ? -1.0 / (fs * std::log (coefficient)) : 0.002; };
    const auto pitchTau = tau (0.9997 - (std::pow (1001.0, 1.0 - std::clamp (p (2), 0, 256) / 256.0) - 1.0) * 9.7e-6);
    const auto ampTau = tau (0.99998 - (std::pow (1001.0, 1.0 - std::clamp (p (3), 0, 256) / 256.0) - 1.0) * 4.98e-6);

    // fixed end (min − 3600 ct from FL's C5 = 523 Hz); start = max + 1200 ct above the note
    const auto endCents = std::clamp (p (1), -1200, 1200) - 3600.0;
    const auto sweep = std::clamp ((std::clamp (p (0), -2400, 3600) + 1200.0 - endCents) / 100.0, 0.0, 48.0);

    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    // FL's C5 note (key 60 here, 261.6 Hz in the synth) ends at the final frequency
    const auto endHz = 523.25 * std::pow (2.0, endCents / 1200.0);
    transpose (s.osc[0], 12.0 * std::log2 (endHz / 261.63));
    s.cutoff = 20000.0f;
    s.attack = 0.0005f;
    s.decay = (float) std::clamp (6.9 * ampTau, 0.01, 10.0); // −60 dB in ~6.9 τ
    s.sustain = 0.0f;
    s.release = 0.02f;
    s.pitchEnv = { (float) sweep, 0.0f, 0.0f, (float) std::clamp (4.6 * pitchTau, 0.005, 2.0), 0.0f, 0.02f };
    synth = s;
    return true;
}
bool synthFromMiniSynth (const std::vector<std::uint8_t>& d, double bpm, SynthSettings& synth)
{
    // "SSLF" ... "DATA" chunk: "0rev" + 43 floats 0..1
    if (d.size() < 16 || std::memcmp (d.data(), "SSLF", 4) != 0)
        return false;

    std::size_t pos = std::string::npos;
    for (std::size_t i = 8; i + 8 + 4 + 43 * 4 <= d.size(); ++i)
        if (std::memcmp (d.data() + i, "DATA", 4) == 0 && std::memcmp (d.data() + i + 8, "0rev", 4) == 0)
        {
            pos = i + 12;
            break;
        }

    if (pos == std::string::npos)
        return false;

    float v[43] {};
    std::memcpy (v, d.data() + pos, sizeof (v));
    for (auto& f : v)
        f = std::isfinite (f) ? std::clamp (f, 0.0f, 1.0f) : 0.0f;

    SynthSettings s;
    const auto kind = std::clamp ((int) (9.0f * v[5]), 0, 9);
    const auto m = v[6];
    auto& o = s.osc;
    o = { SynthOscillator { Waveform::saw, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };

    // 2nd voice of the dual types: trunc(24m − 12) semitones + 7.5 cents (REA_rodada21 §3.2)
    auto second = [&] (Waveform w) {
        SynthOscillator osc { w, 0, 7.5f, 0.5f };
        const auto semis = std::clamp ((int) (24.0f * m - 12.0f), -12, 12);
        osc.octave = semis / 12;
        osc.semitones = semis - 12 * osc.octave;
        return osc;
    };
    switch (kind)
    {
        case 1: o[0].level = 0.5f; o[1] = second (Waveform::saw); break;                                   // saw + saw
        case 2: o[0].wave = Waveform::square; break;                                                       // pulse (no PWM)
        case 3: o[0] = { Waveform::square, 0, 0.0f, 0.5f }; o[1] = second (Waveform::saw); break;          // square + saw
        case 4: o[0] = { Waveform::square, 0, 0.0f, 0.5f }; o[1] = second (Waveform::square); break;       // square + square
        case 5: // supersaw: 3 saws with a spread of ±5 %·m
        {
            const auto cents = (float) std::min (100.0, 1200.0 * std::log2 (1.0 + 0.05 * std::max (0.1f, m)));
            o[0].level = o[1].level = o[2].level = 1.0f / 3.0f;
            o[1] = { Waveform::saw, 0, cents, 1.0f / 3.0f };
            o[2] = { Waveform::saw, 0, -cents, 1.0f / 3.0f };
            break;
        }
        case 8:
        case 9: // saws spread across octaves
            o[0] = { Waveform::saw, 0, 0.0f, 0.4f };
            o[1] = { Waveform::saw, kind == 8 ? -1 : 0, 0.0f, 0.3f, kind == 8 ? 0 : 7 };
            o[2] = { Waveform::saw, 1, 0.0f, 0.3f };
            break;
        default: break; // saw (the phase variations sound similar)
    }

    if (v[7] > 0.01f && o[2].level == 0.0f) // noise
        o[2] = { Waveform::noise, 0, 0.0f, std::min (1.0f, v[7] * v[7]) };

    const auto transpose = (int) ((v[1] - 0.5f) * 24.0f);
    for (auto& osc : o)
        osc.semitones = std::clamp (osc.semitones + transpose, -11, 11);

    // 24 dB ladder filter: fc = 2^(4 + 10.3·F) Hz
    s.cutoff = (float) std::clamp (std::pow (2.0, 4.0 + 10.3 * v[8]), 20.0, 20000.0);
    s.resonance = std::clamp (0.99f * v[9], 0.0f, 1.0f);

    // envelopes (samples at 44.1 kHz): k = 0.95p + 0.05
    auto k = [] (float p) { return 0.95 * p + 0.05; };
    auto attack = [&] (float p) { return p < 0.001f ? 0.0005 : 0.288 * 500.0 * std::pow (k (p), 4.0) / 0.0002 / 44100.0; };
    auto tau = [&] (float p) { return 50.0 * std::pow (k (p), 4.0) / 0.0002 / 44100.0; };
    s.attack = (float) std::min (5.0, attack (v[21]));
    s.sustain = v[23];
    s.decay = (float) std::min (10.0, 6.9 * tau (v[22]));
    s.release = (float) std::min (10.0, 6.9 * tau ((float) (0.95 * v[24] + 0.05)) / 10.0); // k applied twice (REA_rodada21 §3.4)

    if (const auto amount = 10.0f * (2.0f * v[16] - 1.0f); std::abs (amount) > 0.05f)
        s.filterEnv = { amount, (float) std::min (5.0, attack (v[12])), 0.0f, (float) std::min (10.0, 4.6 * tau (v[13])), v[14],
                        (float) std::min (10.0, 4.6 * tau (v[15])) };

    // LFO (REA_rodada21 §3.5): period R beats, phase locked to the song, amount sign·a²
    constexpr float lfoBeats[] = { 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 64.0f, 128.0f, 256.0f };
    const auto a = 2.0f * v[20] - 1.0f;
    const auto amount = a < 0.0f ? -a * a : a * a;
    const auto dest = std::clamp ((int) (2.0f * v[18]), 0, 2);

    if (std::abs (amount) > 1e-4f && dest < 2)
    {
        const auto shape = std::clamp ((int) (2.0f * v[17]), 0, 2) == 0 ? 0 : (std::clamp ((int) (2.0f * v[17]), 0, 2) == 1 ? 1 : 2);
        const auto period = lfoBeats[std::clamp ((int) (11.0f * v[19]), 0, 11)];
        if (dest == 0)
            s.lfo[lfoCutoff] = { 4.0f * amount, period, true, shape, 0.0f, 0.0f, true };
        else
            s.lfo[lfoPitch] = { 12.0f * amount, period, true, shape, 0.0f, 0.0f, true };
    }

    const auto voices = std::clamp ((int) (2.0f * v[0]), 0, 2);
    s.mono = voices >= 1;
    s.porta = voices == 2 || v[2] > 0.01f;
    s.portaBeats = (float) std::clamp (v[2] * 0.5 * std::max (1.0, bpm) / 60.0, 0.01, 2.0);
    synth = s;
    return true;
}

bool synthFromDX10 (const std::vector<std::uint8_t>& d, SynthSettings& synth)
{
    const auto version = i32At (d, 0);

    if (version < 1 || version > 2 || d.size() < 4 + 22 * 4)
        return false;

    auto p = [&] (int i) { return std::clamp (i32At (d, 4 + 4 * (std::size_t) i), 0, 65536) / 65536.0; };
    const auto octave = std::clamp (i32At (d, 4 + 4 * 21), -3, 3);

    // modulator ratio: integer + fine
    auto ratio = [&] (int coarse, int fine) {
        auto r = std::floor (40.1 * p (coarse) * p (coarse));
        const auto f = p (fine);
        if (f < 0.5)
            r += 0.2 * f * f;
        else
        {
            const auto step = (int) std::lround (8.9 * f);
            r += step == 4 ? 0.25 : step == 5 ? 1.0 / 3.0 : step == 6 ? 0.5 : step == 7 ? 2.0 / 3.0 : step >= 8 ? 0.75 : 0.0;
        }
        return std::max (0.125, r);
    };
    const auto r = ratio (3, 4);
    const auto index = p (5);

    SynthSettings s;
    // carrier = MIDI note × 2^oct; modulator = R·carrier/2 (REA_rodada17 §11)
    s.osc = { SynthOscillator { Waveform::sine, std::clamp (octave, -3, 3), 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    const auto modulatorSemis = 12.0 * octave + 12.0 * std::log2 (r / 2.0);
    auto setModulatorPitch = [&] (SynthOscillator& o) {
        o.octave = 0;
        o.semitones = 0;
        transpose (o, modulatorSemis);
        o.detune = (float) std::clamp ((modulatorSemis - std::round (modulatorSemis)) * 100.0, -50.0, 50.0);
    };

    // real FM: osc 3 is the modulator; peak depth ≈ 11·p²/(R·2^oct) cycles (notes up to ~67;
    // above that FL limits the level), falling from Init to Sustain
    if (index > 0.01)
    {
        s.osc[2] = { Waveform::sine, 0, 0.0f, 1.0f };
        setModulatorPitch (s.osc[2]);
        s.fm = (float) std::min (16.0, 11.0 * index * index / (r * std::pow (2.0, octave)));
        const auto sustain = p (7) * p (7) / std::max (1e-6, index * index);
        if (sustain < 0.98)
        {
            s.fmDecay = (float) std::min (20.0, 6.9 * std::exp (7.0 * p (6) - 6.0)); // τ -> −60 dB
            s.fmSustain = (float) std::clamp (sustain, 0.0, 1.0);
        }
    }

    if (const auto thru = 0.25 * p (12) * p (12); thru > 0.01) // the modulator straight to the output
    {
        s.osc[1] = { Waveform::sine, 0, 0.0f, (float) std::min (0.5, thru * 2.0) };
        setModulatorPitch (s.osc[1]);
    }

    // amplitude
    s.attack = (float) std::min (5.0, 3.0 * std::exp (8.0 * p (0) - 8.0));
    if (p (1) > 0.98)
    {
        s.decay = 0.0f;
        s.sustain = 1.0f;
    }
    else
    {
        s.decay = (float) std::min (10.0, 6.9 * std::exp (8.0 * p (1) - 5.0));
        s.sustain = 0.0f;
    }
    s.release = (float) std::min (10.0, 6.9 * std::exp (5.0 * p (2) - 5.0));

    s.cutoff = 20000.0f;

    if (p (10) > 0.01) // vibrato
        s.lfo[lfoPitch] = { (float) (0.3 * p (10)), (float) (1.0 / std::max (0.1, 25.0 * p (13) * p (13))), false, 0, 0.0f, 0.0f };

    synth = s;
    return true;
}
} // namespace lynx
