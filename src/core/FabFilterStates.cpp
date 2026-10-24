#include "core/FabFilterStates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

#include "core/DynamicsSuite.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"

namespace lynx::fabfilter
{
namespace
{
std::uint32_t u32 (const std::uint8_t* p) noexcept
{
    return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24);
}

float f32 (const std::uint8_t* p) noexcept
{
    const auto u = u32 (p);
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}

// JUCE's MemoryBlock::toBase64Encoding: "<size>." + 6 bits per character, least significant bits first
bool juceBase64 (std::string_view text, std::vector<std::uint8_t>& out)
{
    static constexpr std::string_view alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    const auto dot = text.find ('.');
    if (dot == std::string_view::npos || dot == 0 || dot > 9)
        return false;
    std::size_t size = 0;
    for (std::size_t i = 0; i < dot; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
        size = size * 10 + (std::size_t) (text[i] - '0');
    }
    if (size > (1u << 24))
        return false;
    out.assign (size, 0);
    std::size_t bit = 0;
    for (std::size_t i = dot + 1; i < text.size() && bit < size * 8; ++i)
    {
        const auto v = alphabet.find (text[i]);
        if (v == std::string_view::npos)
            return false;
        for (int b = 0; b < 6 && bit < size * 8; ++b, ++bit)
            if ((v >> b) & 1)
                out[bit >> 3] = (std::uint8_t) (out[bit >> 3] | (1u << (bit & 7)));
    }
    return true;
}

std::optional<State> parseRaw (const std::uint8_t* p, std::size_t n)
{
    State s;
    std::size_t at = 0;
    if (n >= 12 && std::memcmp (p, "FFBS", 4) == 0)
    {
        s.version = u32 (p + 4);
        s.count = u32 (p + 8);
        at = 12;
    }
    else if (n >= 20 && std::memcmp (p, "FabF", 4) == 0)
    {
        s.named = true;
        s.version = u32 (p + 4);
        const auto nameLength = u32 (p + 8);
        if (nameLength > 1024 || 12 + (std::size_t) nameLength + 8 > n)
            return std::nullopt;
        at = 12 + nameLength;
        s.count = u32 (p + at + 4); // after one word we haven't seen change
        at += 8;
    }
    else
        return std::nullopt;
    if (s.count == 0 || s.count > 4096 || at + 4 * (std::size_t) s.count > n)
        return std::nullopt;
    s.values.resize ((n - at) / 4);
    for (std::size_t k = 0; k < s.values.size(); ++k)
        s.values[k] = f32 (p + at + 4 * k);
    return s;
}

// piecewise-linear lookup on (x, y) points with increasing x
double interp (const double* xs, const double* ys, int n, double x) noexcept
{
    if (x <= xs[0])
        return ys[0];
    for (int i = 1; i < n; ++i)
        if (x <= xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[n - 1];
}

// the eleven-point value ↔ text tables we read through the parameter API (the normalized value → the plug-in's text)
constexpr double tenths[11] { 0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0 };
// ratio (Pro-C 3 / Pro-MB / Pro-G): interpolated in log(ratio)
double ratioFromNorm (float v) noexcept
{
    static constexpr double r[11] { 1.0, 1.1, 1.25, 1.5, 2.0, 2.75, 4.0, 6.0, 8.0, 10.0, 100.0 };
    double logs[11];
    for (int i = 0; i < 11; ++i)
        logs[i] = std::log (r[i]);
    return std::exp (interp (tenths, logs, 11, std::clamp ((double) v, 0.0, 1.0)));
}
// interpolated in log(ms + 1)
double tableMs (const double* ms, float v) noexcept
{
    double logs[11];
    for (int i = 0; i < 11; ++i)
        logs[i] = std::log (ms[i] + 1.0);
    return std::exp (interp (tenths, logs, 11, std::clamp ((double) v, 0.0, 1.0))) - 1.0;
}

float clamp01 (double v) noexcept { return (float) std::clamp (v, 0.0, 1.0); }
float linValue (double x, double lo, double hi) noexcept { return clamp01 ((x - lo) / (hi - lo)); }
int roundIndex (float v) noexcept { return (int) std::lround (v); }
bool finiteAll (const State& s, std::size_t n) noexcept
{
    if (s.values.size() < n)
        return false;
    for (std::size_t k = 0; k < n; ++k)
        if (! std::isfinite (s.values[k]))
            return false;
    return true;
}
} // namespace

double levelDb (float v) noexcept
{
    if (v >= -0.6f)
        return 36.0 * (double) v;
    const auto g = 0.0832 * std::pow (((double) v + 1.0) / 0.4, 2.0); // −0.6 → −21.6 dB, −0.8 → −33.6 dB, −1 → −∞
    return g <= 1e-6 ? -120.0 : 20.0 * std::log10 (g);
}

std::optional<State> parseState (const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < 12)
        return std::nullopt;
    // a JUCE host state: the component is base64 in <IComponent>
    static constexpr std::string_view open = "<IComponent>", close = "</IComponent>";
    const std::string_view text ((const char*) bytes.data(), bytes.size());
    if (const auto a = text.find (open); a != std::string_view::npos)
    {
        const auto b = text.find (close, a);
        std::vector<std::uint8_t> component;
        if (b == std::string_view::npos || ! juceBase64 (text.substr (a + open.size(), b - a - open.size()), component))
            return std::nullopt;
        return parseRaw (component.data(), component.size());
    }
    return parseRaw (bytes.data(), bytes.size());
}

// ---------------------------------------------------------------- Pro-C 3 -> Crest Compressor
// layout (FFBS v1, 100 values): 0 style, 1 threshold dB, 4 ratio (norm), 5 knee dB, 6 range dB, 7 attack (norm),
// 8 release (norm), 9 auto release, 10 lookahead ms, 11 hold (norm), 12 character, 13 routing (0 pre / 1 post),
// 14 drive dB, 15 wet gain (level), 17 dry gain (level), 19 auto gain, 21 side-chain input, 22 side-chain level dB,
// 26 stereo link (norm: 0.5 = 100 %), 32 + 9b side-chain EQ band b (used, enabled, log2 Hz, gain dB, Q (norm),
// shape, slope, placement, speakers), 88 mix (0 … 2), 89 input level, 91 output level, 94 oversampling,
// 95 maximum look-ahead
int fromProC3 (const State& s, EffectSlot& slot)
{
    if (slot.kind != EffectKind::crestCompressor || s.named || s.count != 100 || ! finiteAll (s, 96))
        return 0;
    using namespace dyn::crest;
    auto& p = slot.params;
    int n = 0;
    const auto set = [&] (int k, float v) {
        p[(std::size_t) k] = clamp01 (v);
        ++n;
    };
    // their style list (index → ours); the indices we never saw named map to Allround
    static constexpr Style styles[14] { Style::clean, Style::allround, Style::allround, Style::punch, Style::upward, Style::twoWay,
                                        Style::allround, Style::variMu, Style::classic, Style::opto, Style::vocal, Style::allround,
                                        Style::bus, Style::pumping };
    set (style, styleValue (styles[std::clamp (roundIndex (s.at (0)), 0, 13)]));
    set (threshold, linValue (s.at (1), -60.0, 0.0));
    set (ratio, ratioValue (ratioFromNorm (s.at (4))));
    set (knee, linValue (s.at (5), 0.0, 72.0));
    set (range, linValue (s.at (6), 0.0, 60.0));
    set (attack, attackValue (0.005 + 250.0 * std::pow (std::clamp ((double) s.at (7), 0.0, 1.0), 3.0)));
    static constexpr double releases[11] { 10.0, 21.62, 56.5, 115.0, 198.2, 309.5, 456.3, 655.4, 944.9, 1429.0, 2500.0 };
    set (release, releaseValue (tableMs (releases, s.at (8))));
    set (autoRelease, s.at (9) >= 0.5f ? 1.0f : 0.0f);
    set (lookahead, linValue (s.at (10), 0.0, 20.0));
    static constexpr double holds[11] { 0.0, 2.5, 5.0, 10.0, 25.0, 40.0, 80.0, 140.0, 200.0, 350.0, 500.0 };
    set (hold, holdValue (interp (tenths, holds, 11, s.at (11))));
    set (character, (float) std::clamp (roundIndex (s.at (12)), 0, 3) / 3.0f);
    set (characterPre, s.at (13) < 0.5f ? 1.0f : 0.0f);
    set (drive, linValue (s.at (14), -24.0, 24.0));
    set (autoGain, s.at (19) >= 0.5f ? 1.0f : 0.0f);
    set (scExternal, roundIndex (s.at (21)) == 1 ? 1.0f : 0.0f);
    set (scLevel, linValue (s.at (22), -36.0, 36.0));
    set (stereoLink, clamp01 (2.0 * s.at (26)));
    // round 128: the link knob's upper half goes "mid-only" (0.75 = 100 %, 50 % mid-only): past the middle of it the
    // mode (27: Mid, Side, M → S, S → M) becomes ours; auto threshold (2), dry gain (17, level law, relative to the
    // wet gain that went into our output), meter scale (97), input meter (99)
    {
        static constexpr LinkMode modes[4] { LinkMode::mid, LinkMode::side, LinkMode::midToSide, LinkMode::sideToMid };
        set (linkMode, linkModeValue (s.at (26) > 0.75f ? modes[std::clamp (roundIndex (s.at (27)), 0, 3)] : LinkMode::stereo));
    }
    set (autoThreshold, s.at (2) >= 0.5f ? 1.0f : 0.0f);
    {
        const auto dryLevel = levelDb (s.at (17));
        set (dryGain, dryLevel <= -119.0 ? 0.0f : dryValue (dryLevel - levelDb (s.at (15))));
    }
    if (s.count > 99)
    {
        set (meterScale, (float) std::clamp (roundIndex (s.at (97)), 0, 4) / 4.0f);
        set (inputMeter, s.at (99) >= 0.5f ? 1.0f : 0.0f);
    }
    // side-chain EQ: the first three bands that are used and enabled
    static constexpr facet::Shape shapes[10] { facet::Shape::bell, facet::Shape::lowShelf, facet::Shape::lowCut, facet::Shape::highShelf,
                                               facet::Shape::highCut, facet::Shape::notch, facet::Shape::bandPass,
                                               facet::Shape::tiltShelf, facet::Shape::tiltShelf, facet::Shape::off };
    static constexpr int slopes[10] { 6, 12, 18, 24, 30, 36, 48, 72, 96, 96 };
    int band = 0;
    for (int b = 0; b < 6 && band < scBands; ++b)
    {
        const auto base = (std::size_t) (32 + 9 * b);
        if (s.at (base) < 0.5f || s.at (base + 1) < 0.5f)
            continue;
        const auto shape = shapes[std::clamp (roundIndex (s.at (base + 5)), 0, 9)];
        if (shape == facet::Shape::off)
            continue;
        set (scParam (band, 0), facet::shapeValue (shape));
        set (scParam (band, 1), facet::hzValue (std::pow (2.0, (double) s.at (base + 2))));
        set (scParam (band, 2), facet::gainValue (s.at (base + 3)));
        set (scParam (band, 3), clamp01 (s.at (base + 4))); // the same Q law (0.025 … 40, 0.5 = 1)
        set (scParam (band, 4), facet::slopeValue (slopes[std::clamp (roundIndex (s.at (base + 6)), 0, 9)]));
        ++band;
    }
    set (mix, clamp01 (s.at (88)));
    set (input, linValue (levelDb (s.at (89)), -36.0, 36.0));
    set (output, linValue (levelDb (s.at (91)) + levelDb (s.at (15)), -36.0, 36.0));
    set (oversampling, (float) std::clamp (roundIndex (s.at (94)), 0, 3) / 3.0f);
    set (maxLookahead, (float) std::clamp (roundIndex (s.at (95)), 0, 5) / 5.0f);
    return n;
}

// ---------------------------------------------------------------- Pro-MB -> Prism Multiband
// layout (FabF v2, 151 values): band b (0 … 5) at 22b: 0 state (0 disabled, 1 enabled, 2 unused), 1 low crossover
// (log2 Hz), 2 low slope (norm), 3 high crossover, 4 high slope, 5 mode, 6 threshold (norm), 7 range dB, 8 ratio
// (norm), 9 attack %, 10 release %, 11 knee dB, 12 look-ahead ms, 13 level dB, 21 solo / mute; 133 mix (0 … 2),
// 134 input level, 136 output level, 139 processing mode, 140 oversampling, 141 look-ahead on
int fromProMB (const State& s, EffectSlot& slot)
{
    if (slot.kind != EffectKind::prismMultiband || ! s.named || s.count != 151 || ! finiteAll (s, 142))
        return 0;
    using namespace dyn::prism;
    auto& p = slot.params;
    int n = 0;
    const auto set = [&] (int k, float v) {
        p[(std::size_t) k] = clamp01 (v);
        ++n;
    };
    struct Ref { int index; double lo, hi; };
    std::array<Ref, 6> used {};
    int count = 0;
    for (int b = 0; b < 6; ++b)
        if (roundIndex (s.at ((std::size_t) (22 * b))) == 1)
            used[(std::size_t) count++] = { b, std::pow (2.0, (double) s.at ((std::size_t) (22 * b + 1))),
                                            std::pow (2.0, (double) s.at ((std::size_t) (22 * b + 3))) };
    if (count == 0)
        return 0;
    std::sort (used.begin(), used.begin() + count, [] (const Ref& a, const Ref& b) { return a.lo * a.hi < b.lo * b.hi; });
    // our contiguous bands (up to six, round 135): each used reference band in order, with a disabled gap band where the
    // reference leaves room below / between (gaps are dropped first when there are more than six)
    struct Seg { int ref; double lo, hi; };
    std::array<Seg, 16> segs {};
    int nseg = 0;
    double edge = 30.0;
    for (int k = 0; k < count; ++k)
    {
        const auto& u = used[(std::size_t) k];
        if (u.lo > edge * 1.12)
            segs[(std::size_t) nseg++] = { -1, edge, u.lo };
        segs[(std::size_t) nseg++] = { u.index, std::max (edge, u.lo), std::max (u.hi, std::max (edge, u.lo) * 1.06) };
        edge = segs[(std::size_t) (nseg - 1)].hi;
    }
    if (edge < 16000.0)
        segs[(std::size_t) nseg++] = { -1, edge, 30000.0 };
    while (nseg > bands)
    {
        int gap = -1;
        for (int k = nseg - 1; k >= 0; --k)
            if (segs[(std::size_t) k].ref < 0)
            {
                gap = k;
                break;
            }
        if (gap < 0)
        {
            nseg = bands; // more than six reference bands: the six lowest
            break;
        }
        // give the gap to a neighbour
        if (gap > 0)
            segs[(std::size_t) (gap - 1)].hi = segs[(std::size_t) gap].hi;
        else
            segs[1].lo = segs[0].lo;
        for (int k = gap; k + 1 < nseg; ++k)
            segs[(std::size_t) k] = segs[(std::size_t) (k + 1)];
        --nseg;
    }
    // unused bands of ours sit above everything, disabled; round 135: up to six bands (four at least, as before)
    const int nb = std::max (legacyBands, std::min (nseg, bands));
    set (Global::bandCountParam, bandCountValue (nb));
    double la = 0.0;
    for (int b = 0; b < nb; ++b)
    {
        if (b < nb - 1)
        {
            const auto hz = b + 1 < nseg ? segs[(std::size_t) b].hi : 17000.0 + 300.0 * b;
            set (crossoverParam (b), crossoverValue (hz));
        }
        if (b >= nseg || segs[(std::size_t) b].ref < 0)
        {
            set (param (b, fEnabled), 0.0f);
            continue;
        }
        const auto base = (std::size_t) (22 * segs[(std::size_t) b].ref);
        set (param (b, fEnabled), 1.0f);
        set (param (b, fMode), s.at (base + 5) >= 0.5f ? 1.0f : 0.0f);
        static constexpr double thresholds[3] { -90.0, -72.0, -48.0 };
        const auto tn = std::clamp ((double) s.at (base + 6), 0.0, 1.0);
        set (param (b, fThreshold), thresholdValue (tn < 0.2 ? interp (tenths, thresholds, 3, tn) : -60.0 + 60.0 * tn));
        set (param (b, fRange), linValue (s.at (base + 7), -30.0, 30.0));
        set (param (b, fRatio), dyn::crest::ratioValue (ratioFromNorm (s.at (base + 8))));
        set (param (b, fAttack), clamp01 (s.at (base + 9) / 100.0));  // our laws are on their 0 … 100 % scale
        set (param (b, fRelease), clamp01 (s.at (base + 10) / 100.0));
        set (param (b, fKnee), linValue (s.at (base + 11), 0.0, 48.0));
        set (param (b, fLevel), linValue (s.at (base + 13), -30.0, 30.0));
        const auto soloMute = roundIndex (s.at (base + 21));
        set (param (b, fSolo), soloMute == 1 || soloMute == 3 ? 1.0f : 0.0f);
        la = std::max (la, (double) s.at (base + 12));
        set (bandLookaheadParam (b), linValue (s.at (base + 12), 0.0, 20.0)); // round 128: the band's own
        set (bandPanParam (b), (float) ((std::clamp ((double) s.at (base + 14), -1.0, 1.0) + 1.0) * 0.5)); // round 144: mid / side
        if (b == 0)
        {
            // the slope: the steeper of the band's sides, to our nearest
            const auto dbOct = 6.0 + 42.0 * std::max (s.at (base + 2), s.at (base + 4));
            set (Global::slope, dbOct < 18.0 ? 0.0f : (dbOct < 36.0 ? 0.5f : 1.0f));
        }
    }
    set (Global::lookahead, s.at (141) >= 0.5f ? linValue (la, 0.0, 20.0) : 0.0f);
    set (Global::mix, clamp01 (s.at (133)));
    set (Global::input, linValue (levelDb (s.at (134)), -36.0, 36.0));
    set (Global::output, linValue (levelDb (s.at (136)), -36.0, 36.0));
    // round 144: 139 processing mode (0 linear, 1 dynamic, 2 minimum phase), 140 oversampling (0 off, 1 2x, 2 4x),
    // read black-box from the trial's saved states (REA_rodada144b); our parameters use the same value law
    set (Global::processingMode, (float) std::clamp (roundIndex (s.at (139)), 0, 2) * 0.5f);
    set (Global::oversampling, (float) std::clamp (roundIndex (s.at (140)), 0, 2) * 0.5f);
    set (Global::panMode, 1.0f); // the reference's band pan is always mid / side (state 14: −1 … +1)
    return n;
}

// ---------------------------------------------------------------- Pro-G -> Sentry Gate
// layout (FabF v2, 34 values): 0 threshold dB, 1 upward threshold dB, 2 ratio (norm), 3 upward ratio (norm, 1 … 3),
// 4 range (norm), 5 style, 6 attack (norm), 7 release (norm), 8 hold (norm), 9 knee dB, 10 look-ahead (norm),
// 11 look-ahead on, 16 side-chain input, 17 low pass (norm), 18 high pass (norm), 20 wet level, 22 dry level,
// 30 output level
int fromProG (const State& s, EffectSlot& slot)
{
    if (slot.kind != EffectKind::sentryGate || ! s.named || s.count != 34 || ! finiteAll (s, 31))
        return 0;
    using namespace dyn::sentry;
    auto& p = slot.params;
    int n = 0;
    const auto set = [&] (int k, float v) {
        p[(std::size_t) k] = clamp01 (v);
        ++n;
    };
    set (style, styleValue ((Style) std::clamp (roundIndex (s.at (5)), 0, styles - 1)));
    set (threshold, linValue (s.at (0), -60.0, 0.0));
    set (upThreshold, linValue (s.at (1), -30.0, 0.0));
    set (ratio, dyn::crest::ratioValue (ratioFromNorm (s.at (2))));
    set (upRatio, clamp01 (s.at (3)));                              // 1 … 3, linear, as ours
    static constexpr double ranges[11] { 0.0, 0.2, 3.4, 7.2, 11.6, 16.0, 26.0, 36.0, 63.5, 91.0, 100.0 };
    set (range, linValue (interp (tenths, ranges, 11, s.at (4)), 0.0, 100.0));
    set (attack, clamp01 (s.at (6)));                               // 1000 · v⁴ ms, as ours
    static constexpr double releases[11] { 0.0, 3.429, 27.43, 92.59, 219.5, 428.7, 740.7, 1176.0, 1756.0, 2500.0, 5000.0 };
    set (release, clamp01 (std::cbrt (tableMs (releases, s.at (7)) / 5000.0)));
    static constexpr double holds[11] { 0.0, 0.5, 8.5, 18.0, 29.0, 40.0, 65.0, 90.0, 158.8, 227.5, 250.0 };
    set (hold, clamp01 (std::sqrt (interp (tenths, holds, 11, s.at (8)) / 250.0)));
    set (knee, linValue (s.at (9), 0.0, 30.0));
    static constexpr double looks[11] { 0.0, 0.02, 0.34, 0.72, 1.16, 1.6, 2.6, 3.6, 6.35, 9.1, 10.0 };
    set (lookahead, linValue (interp (tenths, looks, 11, s.at (10)), 0.0, 10.0));
    set (lookaheadOn, s.at (11) >= 0.5f ? 1.0f : 0.0f);
    set (scExternal, s.at (16) >= 0.5f ? 1.0f : 0.0f);
    set (lowPass, clamp01 (s.at (17)));                             // 5 Hz … 30 kHz log, as ours
    set (highPass, clamp01 (s.at (18)));
    const auto wetDb = levelDb (s.at (20));
    const auto dryDb = levelDb (s.at (22));
    set (dry, clamp01 (std::pow (10.0, (dryDb - wetDb) / 20.0)));
    set (output, linValue (levelDb (s.at (30)) + wetDb, -24.0, 24.0));
    // round 128: expert side chain (12 / 14 level dB, 13 / 15 mix −1 … 1), wet / dry pan (21 / 23), channel mode
    // (27), input level (28)
    set (scGainL, linValue (s.at (12), -36.0, 36.0));
    set (scMixL, linValue (s.at (13), -1.0, 1.0));
    set (scGainR, linValue (s.at (14), -36.0, 36.0));
    set (scMixR, linValue (s.at (15), -1.0, 1.0));
    set (wetPan, linValue (s.at (21), -1.0, 1.0));
    set (dryPan, linValue (s.at (23), -1.0, 1.0));
    set (channelMode, s.at (27) >= 0.5f ? 1.0f : 0.0f);
    set (input, linValue (levelDb (s.at (28)), -24.0, 24.0));
    return n;
}

// ---------------------------------------------------------------- Saturn 2 -> Ember Saturator
// layout (FFBS v1, 951 values): 0 input gain dB, 2 output gain dB, 5 mix %, 6 band count, band b at 7 + 17b:
// 2 dynamics, 3 style, 4 drive (norm), 6 … 9 bass / mid / treble / presence dB, 10 mix %, 11 level, 13 enabled,
// 15 crossover (log2 Hz, the band's upper edge), 16 crossover slope (6, 12, 24, 36, 48)
int fromSaturn2 (const State& s, EffectSlot& slot)
{
    if (slot.kind != EffectKind::ember || s.named || s.count != 951 || ! finiteAll (s, 7 + 17 * 6))
        return 0;
    using namespace mixsuite::ember;
    auto& p = slot.params;
    int n = 0;
    const auto set = [&] (int k, float v) {
        p[(std::size_t) k] = clamp01 (v);
        ++n;
    };
    // style families: tubes → tube, tapes and "saturation" → tape (odd), amps and transformers → transistor,
    // the heavy amps and the effects → fuzz
    const auto ourStyle = [] (int theirs) {
        if (theirs <= 3) return Style::tube;
        if (theirs <= 7) return Style::tape;
        if (theirs == 13 || theirs == 14 || theirs == 15 || theirs >= 23) return Style::fuzz;
        if (theirs >= 17 && theirs <= 19) return Style::tape;
        return Style::transistor;
    };
    const int nb = std::clamp (roundIndex (s.at (6)), 1, 6);
    const int ours = std::min (nb, maxBands);
    set (bands, (float) (ours - 1) / 3.0f);
    for (int b = 0; b < ours; ++b)
    {
        const auto base = (std::size_t) (7 + 17 * b);
        set (bandParam (b, 0), (float) (int) ourStyle (roundIndex (s.at (base + 3))) / 3.0f);
        // drive 0 … 100 % → 0 … +30 dB of ours (their 50 % already saturates a −6 dBFS tone hard)
        set (bandParam (b, 1), clamp01 (30.0 * std::clamp ((double) s.at (base + 4), 0.0, 1.0) / 36.0));
        set (bandParam (b, 2), clamp01 (s.at (base + 10) / 100.0));
        const auto enabled = s.at (base + 13) >= 0.5f;
        set (bandParam (b, 3), enabled ? linValue (levelDb (s.at (base + 11)), -18.0, 18.0) : 0.0f);
        if (b < ours - 1)
            set (xover1 + b, crossoverValue (std::pow (2.0, (double) s.at (base + 15))));
        // round 128 (Ember's per-band controls): feedback amount (%) and frequency (Hz) at 0 / 1, dynamics at 2, tone
        // bass / mid / treble / presence (dB) at 6 … 9, state (normal, solo, mute, solo-mute) at 14. Offsets 0, 1 and
        // 14 follow the order of the plug-in's own VST3 parameter list (17 per band) [model]; Ember's tone stages are
        // LYNX's own curves, not measured against Saturn's.
        set (bandExtra (b, 6), clamp01 (s.at (base + 0) / 100.0));
        set (bandExtra (b, 7), (float) std::clamp (std::log (std::clamp ((double) s.at (base + 1), 10.0, 1000.0) / 10.0) / std::log (100.0), 0.0, 1.0));
        set (bandExtra (b, 8), linValue (std::clamp ((double) s.at (base + 2), -1.0, 1.0), -1.0, 1.0));
        for (int k = 0; k < 4; ++k)
            set (bandExtra (b, 2 + k), linValue (std::clamp ((double) s.at (base + 6 + (std::size_t) k), -18.0, 18.0), -18.0, 18.0));
        {
            const auto st = roundIndex (s.at (base + 14));
            set (bandExtra (b, 1), st == 1 ? 0.5f : (st == 2 || st == 3) ? 1.0f : 0.0f);
        }
        if (b == 0)
        {
            static constexpr int slopes[5] { 12, 12, 24, 24, 48 };
            const auto d = slopes[std::clamp (roundIndex (s.at (base + 16)), 0, 4)];
            set (xoverSlope, d == 12 ? 0.0f : (d == 24 ? 0.5f : 1.0f));
        }
    }
    set (mix, clamp01 (s.at (5) / 100.0));
    set (inputGain, linValue (std::clamp ((double) s.at (0), -24.0, 24.0), -24.0, 24.0)); // round 128
    set (output, linValue (s.at (2), -18.0, 18.0));
    set (autoGain, 1.0f); // Saturn compensates its level as the drive rises (§4)
    return n;
}

// ---------------------------------------------------------------- Pro-Q 4 -> Facet EQ
// layout (FFBS v1, 600 values; REA_rodada124_extras §2, read back through the official trial's parameter API): band b
// (0 … 23) at 23b: 0 used, 1 enabled, 2 frequency (log2 Hz), 3 gain dB, 4 Q (norm: 0.025 · 1600^v, 0.5 = 1),
// 5 shape (0 bell, 1 low shelf, 2 low cut, 3 high shelf, 4 high cut, 5 notch, 6 band pass, 7 tilt shelf, 8 flat tilt,
// 9 all pass), 6 slope (0 = 0, 1 … 9 = 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct, 10 brickwall), 7 stereo placement
// (0 left, 1 right, 2 stereo, 3 mid, 4 side), 9 dynamic range dB; 552 processing mode (0 zero latency, 1 natural,
// 2 linear), 555 gain scale (1 = 100 %), 556 output level (level encoding). Facet EQ has eight stereo bands with the
// same shapes, Q law and slopes: the first eight used, enabled, stereo-placed bands are taken; dynamic bands keep
// their static curve (her states use none).
int fromProQ4 (const State& s, EffectSlot& slot)
{
    if (slot.kind != EffectKind::facetEq || s.named || s.count != 600 || ! finiteAll (s, 600))
        return 0;
    auto& p = slot.params;
    int n = 0;
    const auto set = [&] (int k, float v) {
        p[(std::size_t) k] = clamp01 (v);
        ++n;
    };
    // round 135: Facet has the flat tilt and the all pass too (they were a tilt shelf and off)
    static constexpr facet::Shape shapes[10] { facet::Shape::bell, facet::Shape::lowShelf, facet::Shape::lowCut, facet::Shape::highShelf,
                                               facet::Shape::highCut, facet::Shape::notch, facet::Shape::bandPass,
                                               facet::Shape::tiltShelf, facet::Shape::flatTilt, facet::Shape::allPass };
    static constexpr int slopes[11] { 6, 6, 12, 18, 24, 30, 36, 48, 72, 96, 96 };
    int band = 0;
    for (int b = 0; b < 24 && band < facet::bands; ++b)
    {
        const auto base = (std::size_t) (23 * b);
        if (s.at (base) < 0.5f || s.at (base + 1) < 0.5f || roundIndex (s.at (base + 7)) != 2)
            continue;
        const auto shape = shapes[std::clamp (roundIndex (s.at (base + 5)), 0, 9)];
        if (shape == facet::Shape::off)
            continue;
        set (facet::param (band, 0), facet::shapeValue (shape));
        set (facet::param (band, 1), facet::hzValue (std::pow (2.0, (double) s.at (base + 2))));
        set (facet::param (band, 2), facet::gainValue (s.at (base + 3)));
        set (facet::param (band, 3), clamp01 (s.at (base + 4)));
        set (facet::param (band, 4), facet::slopeValue (slopes[std::clamp (roundIndex (s.at (base + 6)), 0, 10)]));
        ++band;
    }
    for (int b = band; b < facet::bands; ++b)
        p[(std::size_t) facet::param (b, 0)] = facet::shapeValue (facet::Shape::off);
    set (facet::phaseParam, facet::phaseValue ((facet::Phase) std::clamp (roundIndex (s.at (552)), 0, facet::phaseCount - 1)));
    set (facet::scaleParam, clamp01 (s.at (555) / 2.0));
    set (facet::outputParam, linValue (levelDb (s.at (556)), -24.0, 24.0));
    return n;
}

Translator translatorNamed (std::string_view name)
{
    if (name == "pro-q4")
        return fromProQ4;
    if (name == "pro-c3")
        return fromProC3;
    if (name == "pro-mb")
        return fromProMB;
    if (name == "pro-g")
        return fromProG;
    if (name == "saturn2")
        return fromSaturn2;
    return nullptr;
}
} // namespace lynx::fabfilter
