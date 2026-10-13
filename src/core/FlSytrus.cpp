// Sytrus (FM/RM de 6 operadores + 3 filtros) aproximado pelo synth de 3 osciladores.
// Formato e conversões em docs/analise/REA_rodada11.md §1-3 (clean-room: só o comportamento).

#include "core/FlSynths.h"
#include "core/Inflate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace lynx
{
namespace
{
struct Reader
{
    const std::uint8_t* data;
    std::size_t size, pos = 0;
    bool failed = false;

    const std::uint8_t* raw (std::size_t n)
    {
        if (failed || n > size - pos)
        {
            failed = true;
            return nullptr;
        }
        const auto* p = data + pos;
        pos += n;
        return p;
    }

    template <typename T>
    T get()
    {
        T v {};
        if (const auto* p = raw (sizeof (T)))
            std::memcpy (&v, p, sizeof (T));
        return v;
    }

    std::int32_t i32() { return get<std::int32_t>(); }
    std::uint8_t u8() { return get<std::uint8_t>(); }
};

struct Envelope
{
    std::vector<std::pair<double, double>> points; // (Δt, nível)
    int flags = 0;
    bool on = false;
    int sustain = -1;
};

struct Articulator
{
    Envelope env, lfo;
    std::array<std::int32_t, 4> knobs { 128, 128, 0, 128 }; // A, D, S, R do envelope
    std::int32_t lfoSpeed = 0;
    float envDepth = 1.0f, lfoDepth = 0.0f;
};

struct Unit
{
    std::array<std::int32_t, 10> p {};
    // operadores
    double ratio = 1.0, offsetHz = 0.0;
    std::vector<float> harmonics;
    int shape = 0;
    // filtros
    int filterKind = 3;
    std::vector<Articulator> art;
};

struct State
{
    int version = 0;
    std::array<std::int32_t, 200> main {};
    std::array<std::uint8_t, 162> mute {};
    bool porta = false, altUnisonPitch = false;
    std::array<Unit, 10> units;

    int cell (int layer, int row, int col) const
    {
        const auto i = (std::size_t) (layer * 81 + row * 9 + col);
        return mute[i] != 0 ? 0 : main[3 + i];
    }
};

bool readEnvelope (Reader& r, Envelope& e)
{
    const auto kind = r.i32(), version = r.i32(), n = r.i32();
    if (r.failed || n < 0 || n > 5000 || version < 0 || version > 3)
        return false;

    e.points.clear();
    for (int k = 0; k < n; ++k)
    {
        double x = 0.0, y = 0.0;
        if (version < 2)
        {
            x = kind == 6 ? r.get<double>() : (double) r.get<float>();
            y = r.get<float>();
            r.raw (version < 1 ? 4 : 8);
        }
        else
        {
            x = r.get<double>();
            y = r.get<double>();
            r.raw (8);
        }
        if (r.failed)
            return false;
        e.points.emplace_back (std::isfinite (x) ? std::clamp (x, 0.0, 1000.0) : 0.0, std::isfinite (y) ? std::clamp (y, 0.0, 1.0) : 0.0);
    }

    e.flags = r.i32();
    e.on = r.i32() != 0;
    r.i32(); // ponto de decay
    r.i32(); // início do loop
    e.sustain = r.i32();
    return ! r.failed;
}

bool readArticulator (Reader& r, Articulator& a, int maps = 6)
{
    r.i32();
    r.i32();
    a.envDepth = r.get<float>();
    a.lfoDepth = r.get<float>();
    if (! readEnvelope (r, a.env))
        return false;
    for (auto& k : a.knobs)
        k = r.i32();

    // a versão do LFO decide se há um inteiro a mais: ler a versão antes de consumir o envelope
    const auto lfoVersionAt = r.pos + 4;
    if (! readEnvelope (r, a.lfo))
        return false;
    std::int32_t lfoVersion = 0;
    std::memcpy (&lfoVersion, r.data + lfoVersionAt, 4);
    a.lfoSpeed = r.i32();
    r.raw (12);
    if (lfoVersion >= 3)
        r.i32();

    Envelope map;
    for (int k = 0; k < maps; ++k) // mapas (Sytrus: teclado, velocity, X, Y, aleatório, uníssono)
        if (! readEnvelope (r, map))
            return false;
    return ! r.failed;
}

bool parse (const std::vector<std::uint8_t>& blob, State& s)
{
    if (blob.size() < 8)
        return false;

    std::uint32_t version = 0;
    std::memcpy (&version, blob.data(), 4);
    if (version > 0xffff)
        version >>= 16;
    if (version > 64)
        return false;
    s.version = (int) version;
    const auto v = s.version;

    std::vector<std::uint8_t> body;
    if (v >= 5)
    {
        auto inflated = inflateZlib (blob.data() + 4, blob.size() - 4, false, 4u << 20);
        if (! inflated)
            return false;
        body = std::move (*inflated);
    }
    else
        body.assign (blob.begin() + 4, blob.end());

    Reader r { body.data(), body.size() };
    const auto mainSize = (std::size_t) (962 - (v < 1 ? 9 : 0) - (v < 2 ? 162 : 0));
    const auto* main = r.raw (mainSize);
    if (main == nullptr)
        return false;
    std::memcpy (s.main.data(), main, std::min (mainSize / 4 * 4, s.main.size() * 4));
    if (v >= 2)
        std::memcpy (s.mute.data(), main + 0x320, s.mute.size());

    if (v > 3)
        r.i32();
    if (v > 5)
        r.u8();
    if (v > 6)
        r.u8();
    if (v > 12)
        r.u8();
    if (v > 13)
    {
        const auto k = (std::size_t) (1 + (v > 14 ? 4 : 0) + (v > 17 ? 3 : 0) + (v > 18 ? 1 : 0));
        if (const auto* flags = r.raw (k))
        {
            s.porta = k > 1 && flags[1] != 0;
            s.altUnisonPitch = k > 8 && flags[8] != 0;
        }
    }
    r.raw (3 * 23); // delays
    if (v >= 10)
        r.raw (0x25 + (v > 10 ? 1 : 0)); // reverb
    if (v > 2)
    {
        r.i32();
        r.raw (3 * 4 * (v > 13 ? 4 : 3)); // EQ
    }
    // texto de info
    if (v < 0x17)
    {
        const auto n = r.i32();
        if (n < 0)
            return false;
        r.raw ((std::size_t) n);
    }
    else
    {
        std::size_t n = r.u8();
        if (n == 255)
        {
            const auto big = r.i32();
            if (big < 0)
                return false;
            n = (std::size_t) big;
        }
        r.raw (2 * n);
    }

    for (int u = 0; u < 10 && ! r.failed; ++u)
    {
        auto& unit = s.units[(std::size_t) u];
        for (auto& p : unit.p)
            p = r.i32();

        if (u < 6)
        {
            const std::size_t blockSize = v < 8 ? 0xe7 : v < 0x11 ? 0x428 : 0x828;
            const auto* op = r.raw (blockSize);
            if (op == nullptr)
                return false;
            std::int32_t ratio = 0, offset = 0;
            std::memcpy (&ratio, op, 4);
            std::memcpy (&offset, op + 4, 4);
            unit.ratio = ratio / 20000.0;
            unit.offsetHz = offset / 100.0;
            const std::size_t harmonicsAt = v < 8 ? 8 : 0xc, count = v < 8 ? 24 : v < 0x11 ? 128 : 256;
            unit.harmonics.resize (count);
            std::memcpy (unit.harmonics.data(), op + harmonicsAt, count * 4);
            if (v >= 0x11)
                std::memcpy (&unit.shape, op + 0x80c, 4);
        }
        else if (u < 9)
        {
            const auto* f = r.raw (v < 9 ? 0x29 : 0x30);
            if (f == nullptr)
                return false;
            if (v >= 9)
                std::memcpy (&unit.filterKind, f + 0x2c, 4);
            Envelope shaper;
            if (! readEnvelope (r, shaper))
                return false;
            if (v >= 12)
                r.u8();
        }

        unit.art.resize (u < 6 ? 6 : u < 9 ? 8 : 2);
        for (auto& a : unit.art)
            if (! readArticulator (r, a))
                return false;
    }

    return ! r.failed;
}

double curve (int v, double full = 256.0)
{
    const auto g = (std::pow (11.0, std::min (std::abs (v) / full, 2.0)) - 1.0) / 10.0;
    return v < 0 ? -g : g;
}

// forma da onda do operador: pela tabela de harmônicas (§1.4) ou, se só tem a fundamental, pela Shape
Waveform waveOf (const Unit& op)
{
    std::vector<double> a (std::min<std::size_t> (32, op.harmonics.size()));
    double peak = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k)
    {
        const auto h = op.harmonics[k];
        a[k] = std::isfinite (h) ? std::abs ((double) h) : 0.0;
        peak = std::max (peak, a[k]);
    }

    int nonzero = 0;
    for (auto& x : a)
    {
        x /= std::max (peak, 1e-12);
        nonzero += x > 0.02 ? 1 : 0;
    }

    if (peak <= 0.0 || (nonzero == 1 && a[0] > 0.02))
    {
        const auto x = std::clamp (op.shape, 0, 512);
        return x == 0 ? Waveform::sine : x < 192 ? Waveform::triangle : x < 320 ? Waveform::saw : Waveform::square;
    }

    // perto de serra (1/k), quadrada (ímpares 1/k) ou triângulo (ímpares 1/k²)?
    auto error = [&] (int kind) {
        double refSum = 0.0, sum = 0.0, e = 0.0;
        std::vector<double> ref (a.size());
        for (std::size_t k = 0; k < a.size(); ++k)
        {
            const auto h = (double) k + 1.0;
            const bool odd = (k % 2) == 0;
            ref[k] = kind == 0 ? 1.0 / h : ! odd ? 0.0 : kind == 1 ? 1.0 / h : 1.0 / (h * h);
            refSum += ref[k];
            sum += a[k];
        }
        for (std::size_t k = 0; k < a.size(); ++k)
            e += std::abs (a[k] / std::max (sum, 1e-9) - ref[k] / refSum);
        return e;
    };
    const double errors[] = { error (0), error (1), error (2) };
    const auto best = (int) (std::min_element (std::begin (errors), std::end (errors)) - std::begin (errors));
    if (errors[best] < 0.35)
        return best == 0 ? Waveform::saw : best == 1 ? Waveform::square : Waveform::triangle;

    double high = 0.0;
    for (std::size_t k = 4; k < a.size(); ++k)
        high += a[k];
    return high > 1.0 ? Waveform::saw : Waveform::triangle;
}

struct Adsr
{
    double a = 0.0, d = 0.0, s = 1.0, r = 0.01, peak = 1.0;
};

// envelope de pontos -> ADSR (§1.6); `knobs` = A/D/S/R do articulador, `timeScale` = s por unidade
Adsr adsrOf (const Envelope& e, const std::array<std::int32_t, 4>& knobs, double timeScale)
{
    Adsr out;
    if (e.points.empty())
        return out;

    std::vector<double> t (e.points.size());
    double acc = 0.0;
    std::size_t peak = 0;
    for (std::size_t i = 0; i < e.points.size(); ++i)
    {
        acc += e.points[i].first;
        t[i] = acc;
        if (e.points[i].second > e.points[peak].second)
            peak = i;
    }

    const auto last = e.points.size() - 1;
    const auto oneShot = e.sustain < 0 || (std::size_t) e.sustain > last;
    const auto sus = oneShot ? last : (std::size_t) e.sustain;
    const auto peakLevel = std::max (1e-6, e.points[peak].second);

    out.peak = e.points[peak].second;
    out.a = t[peak] - t[0];
    if (oneShot)
    {
        out.d = t[last] - t[peak];
        out.s = e.points[last].second / peakLevel;
        out.r = out.d; // continua caindo depois de soltar a tecla
    }
    else
    {
        out.d = sus > peak ? t[sus] - t[peak] : 0.0;
        out.s = e.points[sus].second / peakLevel;
        out.r = t[last] - t[sus];
    }

    auto knob = [&] (int i) { return std::clamp (knobs[(std::size_t) i], 0, 1024) / 128.0; };
    out.a *= knob (0) * timeScale;
    out.d *= knob (1) * timeScale;
    out.r *= knob (3) * timeScale;
    out.s = std::clamp (out.s + std::clamp (knobs[2], -128, 128) / 128.0, 0.0, 1.0);
    out.a = std::min (out.a, 20.0);
    out.d = std::min (out.d, 20.0);
    out.r = std::clamp (out.r, 0.003, 20.0);
    return out;
}

// cutoff em Hz pela curva do tipo de filtro (§1.7), c = 0..1
double cutoffHz (int kind, double c)
{
    c = std::clamp (c, 0.0, 1.0);
    double hz = 0.0;
    switch (kind)
    {
        case 0: hz = 22050.0 * 0.0396 * (std::pow (26.0, c) - 1.0); break;
        case 1: case 2: hz = std::min (17640.0, 22050.0 * (0.007 + 0.01324 * (std::pow (76.0, c) - 1.0))); break;
        case 3: case 11: hz = 44100.0 * (0.003 + 0.000665 * (std::pow (1501.0, c) - 1.0)); break;
        default: hz = 44100.0 * (0.0001 + 0.0004 * (std::pow (1501.0, c) - 1.0)); break;
    }
    return std::clamp (hz, 20.0, 20000.0);
}

// fora do alcance do oscilador (±47 semitons) desloca por oitavas, mantendo a nota
void setPitch (SynthOscillator& osc, double semitones)
{
    if (! std::isfinite (semitones))
        semitones = 0.0;
    while (semitones < -47.0)
        semitones += 12.0;
    while (semitones > 47.0)
        semitones -= 12.0;
    const auto whole = (int) std::lround (semitones);
    osc.octave = std::clamp (whole / 12, -3, 3);
    osc.semitones = std::clamp (whole - osc.octave * 12, -11, 11);
    osc.detune = (float) std::clamp ((semitones - whole) * 100.0, -100.0, 100.0);
}

// afinação do operador em semitons relativos à nota (ratio + offset; ratio 0 = frequência fixa)
double opSemitones (const Unit& op)
{
    if (op.ratio <= 1e-6)
        return 12.0 * std::log2 (std::max (1.0, std::abs (op.offsetHz)) / 261.63);
    auto semis = 12.0 * std::log2 (op.ratio);
    if (std::abs (op.offsetHz) > 1e-6)
        semis += 12.0 * std::log2 (std::max (0.01, 1.0 + op.offsetHz / (261.63 * op.ratio)));
    return semis;
}
} // namespace

bool synthFromSytrus (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain)
{
    State s;
    if (! parse (state, s))
        return false;

    const auto beatSeconds = 60.0 / std::max (1.0, bpm);
    auto timeScale = [&] (const Envelope& e) { return (e.flags & 1) != 0 ? beatSeconds : 1.0; };
    auto opVolume = [&] (int i) { return s.units[(std::size_t) i].p[1] / 256.0; };

    // saída efetiva de cada filtro (direto no OUT ou pelo "to next filter"); filtro desligado passa o som
    std::array<double, 3> filterOut {};
    for (int f = 2; f >= 0; --f)
    {
        filterOut[(std::size_t) f] = curve (s.cell (0, 6 + f, 8));
        if (f < 2)
            filterOut[(std::size_t) f] += curve (s.units[(std::size_t) (6 + f)].p[9]) * filterOut[(std::size_t) f + 1];
    }

    // 1. portadoras: nível direto + nível passando pelos filtros
    std::array<double, 6> direct {}, viaFilter {};
    for (int i = 0; i < 6; ++i)
    {
        direct[(std::size_t) i] = curve (s.cell (0, i, 8)) * opVolume (i);
        for (int f = 0; f < 3; ++f)
            viaFilter[(std::size_t) i] += curve (s.cell (0, 6 + f, i)) * filterOut[(std::size_t) f] * opVolume (i);
    }

    std::array<int, 6> order { 0, 1, 2, 3, 4, 5 };
    auto amp = [&] (int i) { return std::abs (direct[(std::size_t) i] + viaFilter[(std::size_t) i]); };
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return amp (a) > amp (b); });

    std::vector<int> carriers;
    for (auto i : order)
        if (amp (i) > 0.01 && carriers.size() < 3)
            carriers.push_back (i);

    SynthSettings out;
    out.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    out.attack = 0.0f;
    out.decay = 0.0f;
    out.sustain = 1.0f;
    out.release = 0.1f;
    out.cutoff = 20000.0f;
    out.porta = s.porta;

    auto masterPitch = (double) std::clamp (s.main[2], -48, 48);
    gain = (float) std::clamp (curve (std::clamp (s.main[0], 0, 256), 128.0), 0.0, 4.0);

    if (carriers.empty()) // nada chega à saída: patch padrão
    {
        synth = out;
        return true;
    }

    // patches graves demais (ratio 0,25 com Master pitch −24...): sobe tudo por oitavas para caber
    double lowest = 0.0;
    for (auto i : carriers)
        lowest = std::min (lowest, opSemitones (s.units[(std::size_t) i]) + masterPitch);
    while (lowest < -47.0)
    {
        lowest += 12.0;
        masterPitch += 12.0;
    }

    // 2-4. osciladores: afinação, onda, FM -> brilho, nível e pan
    double fmIndex = 0.0; // soma das FMs fortes (para o envelope de filtro)
    int fmModulator = -1;
    for (std::size_t k = 0; k < carriers.size(); ++k)
    {
        const auto i = carriers[k];
        const auto& op = s.units[(std::size_t) i];
        auto& osc = out.osc[k];
        osc.wave = waveOf (op);
        setPitch (osc, opSemitones (op) + masterPitch);
        osc.level = (float) std::min (1.0, amp (i));
        osc.pan = (float) std::clamp (2.0 * s.cell (0, i, 6) / 512.0, -1.0, 1.0);

        double fm = 0.0, strongest = 0.0;
        int modulator = -1;
        for (int j = 0; j < 6; ++j)
        {
            if (j == i)
                continue;
            const auto g = std::abs (curve (s.cell (0, i, j)) * opVolume (j));
            fm += g;
            if (g > strongest)
                strongest = g, modulator = j;
        }

        const auto feedback = std::abs (curve (s.cell (0, i, i)));
        if (feedback > 0.3 && osc.wave == Waveform::sine)
            osc.wave = Waveform::saw;

        if (fm >= 0.15 && modulator >= 0)
        {
            const auto ratio = s.units[(std::size_t) modulator].ratio / std::max (1e-6, op.ratio);
            const auto nearest = std::round (ratio);
            if (std::abs (ratio - nearest) < 0.02 && nearest >= 1.0)
            {
                if (osc.wave == Waveform::sine || osc.wave == Waveform::triangle)
                    osc.wave = ((int) nearest % 2 == 0) ? Waveform::square : Waveform::saw;
            }
            else if (carriers.size() < 3 && out.osc[2].level == 0.0f) // inarmônico: seno extra no ratio do modulador
            {
                out.osc[2] = { Waveform::sine, 0, 0.0f, (float) std::min (1.0, fm) * osc.level * 0.5f };
                setPitch (out.osc[2], opSemitones (s.units[(std::size_t) modulator]) + masterPitch);
            }

            if (k == 0)
            {
                fmIndex = fm;
                fmModulator = modulator;
            }
        }
    }

    // RM forte na portadora principal: anel com o modulador no osc 3, portadora × (1 − |a| + |a|·mod)
    // (REA_rodada18 §2; o sinal de a inverte a fase do modulador e não é reproduzido)
    if (carriers.size() < 3)
    {
        const auto i = carriers[0];
        for (int j = 0; j < 6; ++j)
            if (j != i && std::abs (curve (s.cell (1, i, j))) >= 0.2)
            {
                out.am = true;
                out.ring = true;
                out.osc[2] = { Waveform::sine, 0, 0.0f, (float) std::min (1.0, std::abs (curve (s.cell (1, i, j)))) };
                setPitch (out.osc[2], opSemitones (s.units[(std::size_t) j]) + masterPitch);
                break;
            }
    }

    // 5. envelope de amplitude da portadora principal (articulador 1) + escalas globais
    const auto& main = s.units[(std::size_t) carriers[0]];
    if (const auto& vol = main.art[1]; vol.env.on)
    {
        auto adsr = adsrOf (vol.env, vol.knobs, timeScale (vol.env));
        auto global = [&] (int i) { return std::clamp (s.main[165 + (std::size_t) i], 0, 1024) / 128.0; };
        out.attack = (float) std::min (20.0, adsr.a * global (0));
        out.decay = (float) std::min (20.0, adsr.d * global (1));
        out.sustain = (float) adsr.s;
        out.release = (float) std::clamp (adsr.r * global (3), 0.003, 20.0);
        for (auto& o : out.osc)
            o.level = (float) std::clamp (o.level * adsr.peak, 0.0, 1.0);
    }
    else
    {
        out.attack = 0.002f;
        out.release = 0.01f;
    }

    // 6-7. filtro: o primeiro ativo que recebe alguma portadora, se a maior parte do som passa por ele
    double total = 0.0, filtered = 0.0;
    for (auto i : carriers)
    {
        total += amp (i);
        filtered += std::abs (viaFilter[(std::size_t) i]);
    }

    bool hasFilter = false;
    for (int f = 0; f < 3 && filtered >= 0.5 * total; ++f)
    {
        const auto& unit = s.units[(std::size_t) (6 + f)];
        const auto mode = std::clamp (unit.p[0], 0, 5);
        bool fed = false;
        for (auto i : carriers)
            fed = fed || s.cell (0, 6 + f, i) != 0;
        if (mode == 0 || ! fed || std::abs (filterOut[(std::size_t) f]) < 1e-4)
            continue;

        const auto kind = std::clamp (unit.filterKind, 0, 12);
        int lynxType = 0;
        switch (kind)
        {
            case 1: case 3: case 11: lynxType = 0; break;
            case 2: case 6: lynxType = 2; break;
            case 4: lynxType = 1; break;
            case 5: lynxType = 3; break;
            case 0:
            {
                const auto low = unit.p[4], band = unit.p[5], high = unit.p[6];
                lynxType = low >= band && low >= high ? 0 : band >= high ? 1 : 2;
                break;
            }
            default: lynxType = -1; break; // all pass, shelf, peak, phaser: sem filtro
        }
        if (lynxType < 0)
            break;

        const auto c = std::clamp (unit.p[2], 0, 256) / 256.0;
        const auto r = std::clamp (unit.p[3], 0, 256) / 256.0;
        const int stages[] = { 1, 1, 2, 3, 2, 3 };
        const auto stageFactor = std::pow (0.9, stages[mode] - 1);

        double q = 0.0;
        if (kind == 0)
            out.resonance = (float) std::clamp (0.8 * r, 0.0, 0.95);
        else
        {
            q = kind <= 2 ? 1.0 + 0.049 * (std::pow (1001.0, r) - 1.0) : 0.1 + 0.02495 * (std::pow (2001.0, r) - 1.0);
            out.resonance = (float) std::clamp ((q - 0.707) / 8.0, 0.0, 0.95);
        }
        out.filterType = lynxType;

        const auto& cutArt = unit.art[2];
        const auto depth = std::clamp (unit.p[1], 0, 256) / 128.0;
        if (cutArt.env.on && ! cutArt.env.points.empty() && depth > 0.0)
        {
            const auto adsr = adsrOf (cutArt.env, cutArt.knobs, timeScale (cutArt.env));
            // o articulador de cutoff é bipolar como o de afinação: nível 0,5 = o knob Cutoff
            const auto start = cutArt.env.points.front().second;
            double peak = start;
            for (const auto& p : cutArt.env.points)
                peak = std::max (peak, p.second);
            auto at = [&] (double level) { return cutoffHz (kind, c + depth * (level - 0.5)); };
            const auto susLevel = adsr.s * peak; // (o LYNX parte de `base`: um envelope que fecha abaixo do começo parte do fundo)
            const auto base = at (std::min (start, susLevel));
            const auto top = at (peak);
            const auto susHz = at (susLevel);
            const auto octaves = std::log2 (top / base);
            out.cutoff = (float) std::clamp (base * stageFactor, 20.0, 20000.0);
            if (octaves > 0.05)
                out.filterEnv = { (float) std::min (10.0, octaves), (float) adsr.a, 0.0f, (float) adsr.d,
                                  (float) std::clamp (std::log2 (susHz / base) / octaves, 0.0, 1.0), (float) adsr.r };
        }
        else
            out.cutoff = (float) std::clamp (cutoffHz (kind, c) * stageFactor, 20.0, 20000.0);

        hasFilter = true;
        break;
    }

    // FM forte numa portadora só: FM de verdade (o modulador no osc 3, índice ≈ 4·g ciclos, com o
    // envelope de volume do modulador fazendo o índice cair)
    if (fmIndex >= 0.15 && fmModulator >= 0 && carriers.size() == 1 && ! out.am)
    {
        const auto& modOp = s.units[(std::size_t) fmModulator];
        out.osc[0].wave = waveOf (main);
        out.osc[2] = { waveOf (modOp), 0, 0.0f, 1.0f };
        setPitch (out.osc[2], opSemitones (modOp) + masterPitch);
        out.fm = (float) std::min (8.0, 4.0 * fmIndex);
        if (const auto& modVol = modOp.art[1]; modVol.env.on)
        {
            const auto adsr = adsrOf (modVol.env, modVol.knobs, timeScale (modVol.env));
            if (adsr.s < 0.98)
            {
                out.fmDecay = (float) std::clamp (adsr.d * 2.0, 0.01, 20.0); // (o decay do LYNX é até −60 dB)
                out.fmSustain = (float) adsr.s;
            }
        }
    }
    // FM forte sem filtro do Sytrus: o índice que cai vira um passa-baixas com envelope
    else if (fmIndex >= 0.15 && fmModulator >= 0 && (! hasFilter || (out.filterType == 0 && out.filterEnv.amount == 0.0f)))
    {
        const auto octaves = std::min (6.0, 4.0 * fmIndex);
        const auto& modVol = s.units[(std::size_t) fmModulator].art[1];
        if (modVol.env.on)
        {
            const auto adsr = adsrOf (modVol.env, modVol.knobs, timeScale (modVol.env));
            if (! hasFilter)
                out.cutoff = 400.0f;
            out.filterEnv = { (float) octaves, (float) adsr.a, 0.0f, (float) std::max (0.02, adsr.d), (float) adsr.s, (float) adsr.r };
        }
        else if (! hasFilter)
            out.cutoff = (float) std::clamp (400.0 * std::exp2 (octaves), 20.0, 20000.0);
    }

    // 8. envelope de afinação da portadora principal (articulador 3): nível 0,5 = sem desvio
    if (const auto& pitch = main.art[3]; pitch.env.on && pitch.env.points.size() >= 2)
    {
        const auto start = pitch.env.points.front().second;
        const auto semis = (start - 0.5) * 2.0 * std::clamp (main.p[2], 0, 4800) / 100.0;
        if (std::abs (semis) > 0.1)
        {
            double t = 0.0, settle = 0.0;
            for (const auto& p : pitch.env.points)
            {
                t += p.first;
                settle = t;
                if (std::abs (p.second - 0.5) < 0.02)
                    break;
            }
            out.pitchEnv = { (float) std::clamp (semis, -48.0, 48.0), 0.0f, 0.0f, (float) std::min (20.0, settle * timeScale (pitch.env)), 0.0f, 0.05f };
        }
    }

    // 9. LFOs ligados: volume da portadora, cutoff do filtro, afinação, pan
    auto lfoFrom = [&] (const Articulator& a, float amount) {
        ChannelLfo lfo;
        const auto speed = std::clamp (a.lfoSpeed, 512, 65536);
        lfo.period = (float) std::clamp (625.0 / (std::pow (10001.0, speed / 65536.0) - 1.0), 0.01, 600.0);
        lfo.sync = (a.lfo.flags & 1) != 0;
        lfo.global = (a.lfo.flags & 2) != 0;
        lfo.amount = amount;
        return lfo;
    };
    auto depthOf = [] (const Articulator& a) { return std::isfinite (a.lfoDepth) ? std::clamp ((double) std::abs (a.lfoDepth), 0.0, 1.0) : 0.5; };
    if (main.art[1].lfo.on)
        out.lfo[lfoVolume] = lfoFrom (main.art[1], (float) (0.5 * depthOf (main.art[1])));
    if (main.art[3].lfo.on)
        out.lfo[lfoPitch] = lfoFrom (main.art[3], (float) (std::clamp (main.p[2], 0, 4800) / 100.0 * depthOf (main.art[3])));
    if (main.art[0].lfo.on)
        out.lfo[lfoPan] = lfoFrom (main.art[0], (float) depthOf (main.art[0]));
    for (int f = 0; f < 3 && hasFilter; ++f)
        if (const auto& a = s.units[(std::size_t) (6 + f)].art[2]; s.units[(std::size_t) (6 + f)].p[0] != 0 && a.lfo.on)
        {
            out.lfo[lfoCutoff] = lfoFrom (a, (float) (3.0 * depthOf (a)));
            break;
        }

    // 10. uníssono: só 1 portadora -> três cópias desafinadas e abertas
    const auto unison = std::clamp (s.main[173], 1, 9);
    if (unison >= 2 && carriers.size() == 1 && ! out.am && out.fm > 0.0f) // o osc 3 é o modulador: só uma cópia
    {
        const auto pv = std::clamp (s.main[176], 0, 256) / 256.0;
        const auto spread = std::max (5.0, s.altUnisonPitch ? (std::pow (41.0, pv) - 1.0) * 1.875 : (std::pow (11.0, pv) - 1.0) * 5.0);
        const auto pan = (float) (std::clamp (s.main[174], 0, 128) / 128.0);
        out.osc[1] = out.osc[0];
        out.osc[0].level = out.osc[1].level = std::min (1.0f, out.osc[0].level * 0.75f);
        out.osc[1].detune = (float) std::clamp (out.osc[0].detune + spread, -100.0, 100.0);
        out.osc[0].detune = (float) std::clamp (out.osc[0].detune - spread, -100.0, 100.0);
        out.osc[0].pan = std::clamp (out.osc[0].pan - pan, -1.0f, 1.0f);
        out.osc[1].pan = std::clamp (out.osc[1].pan + pan, -1.0f, 1.0f);
    }
    else if (unison >= 2 && carriers.size() == 1 && ! out.am)
    {
        const auto pv = std::clamp (s.main[176], 0, 256) / 256.0;
        const auto spread = std::max (5.0, s.altUnisonPitch ? (std::pow (41.0, pv) - 1.0) * 1.875 : (std::pow (11.0, pv) - 1.0) * 5.0);
        const auto pan = (float) (std::clamp (s.main[174], 0, 128) / 128.0);
        const auto base = out.osc[0];
        const auto level = base.level / 3.0f * 1.5f;
        for (int k = 0; k < 3; ++k)
        {
            out.osc[(std::size_t) k] = base;
            out.osc[(std::size_t) k].level = std::min (1.0f, level);
        }
        out.osc[1].detune = (float) std::clamp (base.detune + spread, -100.0, 100.0);
        out.osc[1].pan = std::clamp (base.pan + pan, -1.0f, 1.0f);
        out.osc[2].detune = (float) std::clamp (base.detune - spread, -100.0, 100.0);
        out.osc[2].pan = std::clamp (base.pan - pan, -1.0f, 1.0f);
    }

    for (auto& o : out.osc)
        if (! std::isfinite (o.level) || ! std::isfinite (o.detune) || ! std::isfinite (o.pan))
            return false;

    synth = out;
    return true;
}
// Harmor (REA_rodada11 §4): aditivo de 2 partes; cada parte vira um oscilador serra, com o filtro 1
// (ou o 2), o envelope de volume, o envelope do filtro e o uníssono. Imagem, blur, prism,
// harmonizador, phaser e FX ficam de fora.
namespace
{
// curva de volume do Harmor (REA_rodada18 §1.3): exatamente 1 em x = 1; senão (3^x − 1)/2
double harmorGain (double x)
{
    return x == 1.0 ? 1.0 : (std::pow (3.0, x) - 1.0) / 2.0;
}
} // namespace

bool synthFromHarmor (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    if (state.size() < 12)
        return false;
    Reader r { state.data(), state.size() };
    auto version = (std::uint32_t) r.i32();
    if (version > 0xffff)
        version >>= 16;
    if (version < 11 || version > 32)
        return false;
    const auto v = (int) version;
    const auto mask = (std::uint32_t) r.i32();
    if ((mask & 1) == 0)
        return false;

    struct Part
    {
        bool present = false;
        std::vector<std::int32_t> p;
        int filterType[2] { 0, 0 };
        std::vector<Articulator> art;
    };
    Part parts[2];

    for (int k = 0; k < 2; ++k)
    {
        if (((mask >> k) & 1) == 0)
            continue;
        const auto size = (std::size_t) (std::uint32_t) r.i32();
        const auto start = r.pos;
        if (r.failed || size > state.size() - start)
            return false;

        auto& part = parts[k];
        part.p.resize (v >= 16 ? 386 : 385);
        for (auto& x : part.p)
            x = r.i32();
        r.raw (0x30);
        if (const auto* misc = r.raw (v >= 11 ? 0x4b : 0x47))
        {
            std::memcpy (&part.filterType[0], misc + 52, 4);
            std::memcpy (&part.filterType[1], misc + 56, 4);
        }
        if (r.failed)
            return false;

        if (v >= 16)
        {
            part.art.resize (43);
            for (auto& a : part.art)
                if (! readArticulator (r, a, 9))
                {
                    part.art.clear();
                    break;
                }
        }
        part.present = true;
        r.pos = start + size;
        r.failed = false;
    }

    std::int32_t g[53] {};
    if ((mask & 4) != 0)
        for (auto& x : g)
            x = r.i32();
    if (r.failed)
        std::fill (std::begin (g), std::end (g), 0);
    const auto abMix = (mask & 4) != 0 && ! r.failed ? std::clamp (g[10], 0, 1000) / 1000.0 : 0.5;
    const auto mainPitch = std::clamp (g[14], -4800, 4800) / 100.0;

    SynthSettings out;
    out.osc = { SynthOscillator { Waveform::saw, 0, 0.0f, 0.0f }, SynthOscillator { Waveform::saw, 0, 0.0f, 0.0f }, SynthOscillator {} };
    double partSemis[2] {};
    for (int k = 0; k < 2; ++k)
    {
        if (! parts[k].present)
            continue;
        const auto& p = parts[k].p;
        const auto ratio = std::clamp (p[40], 1000, 640000) / 10000.0 / std::clamp (p[41], 1, 16);
        partSemis[k] = mainPitch + 12.0 * std::log2 (ratio);
        auto& osc = out.osc[(std::size_t) k];
        osc.detune = 0.0f;
        setPitch (osc, partSemis[k]);
        const auto mix = k == 0 ? 1.0 - abMix : abMix;
        // volume da parte (REA_rodada18 §1.3): g(x) = (3^x − 1)/2, x = |p11|/1000 (1000 → 1,0; 2000 → 4,0)
        // × p12 "Frequency domain envelope volume" na mesma curva (multiplica o nível do articulador de volume)
        osc.level = (float) std::clamp (harmorGain (std::abs (p[11]) / 1000.0) * harmorGain (std::clamp (p[12], 0, 2000) / 1000.0) * mix, 0.0, 4.0); // A&B mix = crossfade (até +12 dB)
    }

    if (out.osc[0].level + out.osc[1].level < 0.01f)
        out.osc[0].level = 1.0f;

    const auto lead = out.osc[0].level >= out.osc[1].level ? 0 : 1;
    const auto& main = parts[lead];

    // envelope de volume (articulador 1). Desligado, o FL não aplica envelope nenhum (nível constante,
    // REA_rodada18 §1.1); o A 2 ms / R 0,1 s abaixo é só o declick do LYNX. Ligado, o nível usado é o
    // quadrado da saída do articulador: sustain S² (o ataque/decay mantêm os tempos).
    out.attack = 0.002f;
    out.decay = 0.0f;
    out.sustain = 1.0f;
    out.release = 0.1f;
    if (! main.art.empty() && main.art[1].env.on)
    {
        const auto adsr = adsrOf (main.art[1].env, main.art[1].knobs, 1.0);
        out.attack = (float) adsr.a;
        out.decay = (float) adsr.d;
        out.sustain = (float) (adsr.s * adsr.s);
        out.release = (float) adsr.r;
    }

    // filtro 1 (ou o 2 se o 1 estiver aberto). REA_rodada18 §1.2/§1.4: o knob é linear em log2(Hz) entre
    // 73,416 Hz e 17 739,7 Hz para uma nota de 130,81 Hz (tecla 48) e acompanha o teclado com
    // KT = p55/1000 (p67 no filtro 2). Tipos 0…18 = combo sem separadores; só os passa-baixas/faixa/altas
    // e rejeita-faixa têm equivalente. A ressonância e o envelope do filtro não têm escala confirmada:
    // ficam de fora (relatado como aproximação no import).
    const auto& p = main.p;
    int which = p[52] < 8192 ? 0 : p[64] < 8192 ? 1 : -1;
    out.cutoff = 20000.0f;
    if (which >= 0)
    {
        const auto type = main.filterType[which];
        int lynxType = -1;
        switch (type)
        {
            case 0: case 4: case 8: case 9: case 10: case 11: case 12: case 13: lynxType = 0; break;
            case 1: case 5: case 16: lynxType = 1; break;
            case 2: case 6: lynxType = 3; break;
            case 3: case 7: lynxType = 2; break;
            default: break;
        }
        if (lynxType >= 0)
        {
            const auto freq = std::clamp (which == 0 ? p[52] : p[64], 0, 8192);
            const auto lo = std::log2 (73.416), hi = std::log2 (17739.7);
            out.filterType = lynxType;
            out.cutoff = (float) std::clamp (std::exp2 (lo + freq / 8192.0 * (hi - lo)), 20.0, 20000.0);
            out.keyTrack = (float) std::clamp ((which == 0 ? p[55] : p[67]) / 1000.0, -2.0, 2.0);
        }
    }

    // uníssono: cópia desafinada da parte principal no oscilador livre
    if (const auto order = std::clamp (p[34], 1, 9); order >= 2)
    {
        const auto spread = std::max (5.0, (std::pow (41.0, std::clamp (p[38], 0, 256) / 256.0) - 1.0) * 1.875);
        const auto pan = (float) (std::clamp (p[37], 0, 256) / 256.0);
        auto copy = out.osc[(std::size_t) lead];
        copy.detune = (float) std::clamp (copy.detune + spread, -100.0, 100.0);
        copy.pan = pan;
        out.osc[(std::size_t) lead].detune = (float) std::clamp (out.osc[(std::size_t) lead].detune - spread, -100.0, 100.0);
        out.osc[(std::size_t) lead].pan = -pan;
        out.osc[2] = copy;
        if (out.osc[(std::size_t) (1 - lead)].level < 0.05f) // a outra parte calada: três vozes
        {
            out.osc[(std::size_t) (1 - lead)] = out.osc[(std::size_t) lead];
            out.osc[(std::size_t) (1 - lead)].detune = copy.detune - (float) spread;
            out.osc[(std::size_t) (1 - lead)].pan = 0.0f;
        }
    }

    out.porta = g[5] != 0;
    out.mono = g[4] != 0;
    // volume principal (global 11) na mesma curva do volume da parte (REA_rodada18 §1.3)
    gain = (float) std::clamp (((mask & 4) != 0 ? harmorGain (std::clamp (g[11], 0, 2000) / 1000.0) : 1.0), 0.0, 4.0);
    if (gain <= 0.0f)
        gain = 1.0f;
    synth = out;
    return true;
}
// --- Fruity Envelope Controller (REA_rodada15 §10) ---

std::optional<EnvelopeControllerData> parseEnvelopeController (const std::vector<std::uint8_t>& state)
{
    Reader r { state.data(), state.size() };
    auto version = (std::uint32_t) r.i32();
    if (version > 0xffff)
        version >>= 16;
    if (r.failed || version < 1 || version > 16)
        return std::nullopt;
    const auto articulators = version >= 6 ? 8 : 4;
    const auto maps = version >= 6 ? 5 : 2;
    r.raw ((std::size_t) (articulators * 11 + 2) * 4); // parâmetros (os mesmos valores vêm nos articuladores)
    r.raw (4 * 4);                                       // estado da UI e flags globais

    EnvelopeControllerData out {};
    for (int k = 0; k < articulators && ! r.failed; ++k)
    {
        auto& a = out[(std::size_t) k];
        const auto flags = version >= 2 ? r.i32() : 0;
        a.bipolar = (flags & 1) != 0;
        r.i32(); // versão do articulador
        a.base = std::clamp (r.get<float>(), 0.0f, 1.0f);
        a.envLevel = std::clamp (r.get<float>(), -1.0f, 1.0f);
        r.get<float>(); // nível do LFO (o LFO fica de fora)

        Envelope env, lfo, map;
        if (! readEnvelope (r, env))
            return std::nullopt;
        r.raw (16);
        const auto lfoVersionAt = r.pos + 4;
        if (! readEnvelope (r, lfo))
            return std::nullopt;
        std::int32_t lfoVersion = 0;
        std::memcpy (&lfoVersion, r.data + lfoVersionAt, 4);
        r.raw (16);
        if (lfoVersion >= 3)
            r.i32();
        for (int m = 0; m < maps; ++m)
            if (! readEnvelope (r, map))
                return std::nullopt;
        a.lowKey = r.i32();
        a.highKey = r.i32();

        a.envOn = env.on && ! env.points.empty();
        a.tempo = (env.flags & 1) != 0;
        a.sustain = env.sustain;
        double t = 0.0;
        for (const auto& [dx, y] : env.points)
        {
            if (a.count >= (int) a.time.size())
                break;
            t += dx;
            a.time[(std::size_t) a.count] = (float) t;
            a.level[(std::size_t) a.count] = (float) y;
            ++a.count;
        }
        if (a.sustain >= a.count)
            a.sustain = -1;
    }
    if (r.failed)
        return std::nullopt;
    return out;
}

float controllerValue (const ControllerArticulator& a, double sinceOn, double sinceRelease) noexcept
{
    if (! a.envOn || a.count == 0)
        return a.base;

    auto at = [&] (double t) {
        if (t <= a.time[0])
            return (double) a.level[0];
        for (int i = 1; i < a.count; ++i)
            if (t < a.time[(std::size_t) i])
            {
                const auto t0 = a.time[(std::size_t) i - 1], t1 = a.time[(std::size_t) i];
                const auto f = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0;
                return a.level[(std::size_t) i - 1] + (a.level[(std::size_t) i] - a.level[(std::size_t) i - 1]) * f;
            }
        return (double) a.level[(std::size_t) a.count - 1];
    };

    double env = 0.0;
    if (a.sustain >= 0)
    {
        const auto sustainTime = (double) a.time[(std::size_t) a.sustain];
        env = sinceRelease < 0.0 ? at (std::min (sinceOn, sustainTime)) : at (sustainTime + sinceRelease);
    }
    else
        env = at (sinceOn); // sem sustain: corre até o fim mesmo com a tecla presa

    const auto v = a.bipolar ? a.base + (env - 0.5) * a.envLevel : a.base + env * a.envLevel;
    return (float) std::clamp (v, 0.0, 1.0);
}
double lovePhilterMapAt (const LovePhilterMap& map, double x) noexcept
{
    if (map.points.empty())
        return 1.0;
    double t = 0.0, prevT = 0.0, prevY = map.points.front().second;
    for (std::size_t i = 0; i < map.points.size(); ++i)
    {
        t += map.points[i].first;
        const auto y = map.points[i].second;
        if (x < t)
            return i == 0 ? y : prevY + (y - prevY) * (x - prevT) / std::max (1e-12, t - prevT);
        prevT = t;
        prevY = y;
    }
    return prevY;
}

std::optional<KeyboardControllerData> parseKeyboardController (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 553)
        return std::nullopt;
    std::uint32_t version = 0;
    std::memcpy (&version, d.data(), 4);
    if (version < 2 || version > 3)
        return std::nullopt; // v0/v1 têm outro layout (120 teclas, um tempo só)
    KeyboardControllerData out;
    std::memcpy (out.keys.data(), d.data() + 12, 132 * 4);
    for (auto& v : out.keys)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;
    std::memcpy (&out.noteOffValue, d.data() + 548, 4);
    out.noteOffValue = std::isfinite (out.noteOffValue) ? std::clamp (out.noteOffValue, 0.0f, 1.0f) : 0.0f;
    out.autoRelease = d[552] != 0;
    return out;
}
std::optional<LovePhilterState> parseLovePhilter (const std::vector<std::uint8_t>& blob)
{
    if (blob.size() < 8)
        return std::nullopt;
    std::uint32_t version = 0;
    std::memcpy (&version, blob.data(), 4);
    if (version > 0xffff)
        version >>= 16;
    if (version < 1 || version > 12)
        return std::nullopt;
    const auto body = inflateZlib (blob.data() + 4, blob.size() - 4, false, 8u << 20);
    if (! body)
        return std::nullopt;

    LovePhilterState out;
    out.version = (int) version;
    const auto v = out.version;
    Reader r { body->data(), body->size() };
    for (auto& g : out.globals)
        g = r.i32();
    const auto globals2 = (std::size_t) (v >= 11 ? 0x21 : v == 10 ? 0x20 : 0x1F);
    if (const auto* g2 = r.raw (globals2))
    {
        std::memcpy (&out.modX, g2, 4);
        std::memcpy (&out.modY, g2 + 4, 4);
        std::int32_t solo = -1;
        std::memcpy (&solo, g2 + 16, 4);
        out.soloUnit = solo >= 0 && solo < 8 ? solo : -1;
    }
    if (v >= 2)
    {
        r.u8();
        r.i32();
    }
    if (v < 6)
    {
        const auto n = r.i32();
        if (n < 0)
            return std::nullopt;
        r.raw ((std::size_t) n);
    }
    else
    {
        std::size_t n = r.u8();
        if (n == 255)
        {
            const auto big = r.i32();
            if (big < 0)
                return std::nullopt;
            n = (std::size_t) big;
        }
        r.raw (2 * n);
    }

    for (auto& unit : out.units)
    {
        for (auto& p : unit.params)
            p = r.i32();
        unit.mode = r.i32();
        unit.type = r.i32();
        unit.alt = r.u8() != 0;
        unit.enabled = r.u8() != 0;
        unit.waveshaper = r.u8() != 0;
        Envelope e;
        if (! readEnvelope (r, e)) // curva do waveshaper
            return std::nullopt;
        r.u8();
        for (auto& a : unit.articulators)
        {
            r.i32(); // versão
            a.base = r.get<float>();
            a.envDepth = r.get<float>();
            r.get<float>(); // profundidade do LFO
            for (int pattern = 0; pattern < 10; ++pattern)
            {
                if (! readEnvelope (r, e))
                    return std::nullopt;
                r.raw (16);
            }
            const auto lfoVersionAt = r.pos + 4;
            if (! readEnvelope (r, e))
                return std::nullopt;
            std::int32_t lfoVersion = 0;
            std::memcpy (&lfoVersion, r.data + lfoVersionAt, 4);
            r.raw (lfoVersion >= 3 ? 20 : 16);
            for (int m = 0; m < 3; ++m) // X, Y, seguidor de envelope
            {
                if (! readEnvelope (r, e))
                    return std::nullopt;
                if (m == 0)
                    a.mapX.points = e.points;
                else if (m == 1)
                    a.mapY.points = e.points;
            }
        }
        if (r.failed)
            return std::nullopt;
    }
    if (v >= 12)
        r.u8();
    if (r.failed || r.pos != body->size())
        return std::nullopt;
    return out;
}
} // namespace lynx
