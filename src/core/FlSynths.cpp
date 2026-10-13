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

// afinação em semitons -> oitava + semitons do oscilador (somando ao que ele já tem)
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
    const auto tail = 4 + (std::size_t) count * 4; // os 7 combos sem índice de parâmetro
    const auto filterType = std::clamp (i32At (d, tail), 0, 16);
    const auto color = std::clamp (i32At (d, tail + 16), 0, 11);
    const auto beatsPerSecond = std::max (1.0, bpm) / 60.0;

    SynthSettings s;

    // --- osciladores: timbre, máscara (ímpares -> quadrada), sub, uníssono ---
    const auto mask = std::clamp (x (23), 0.0, 1.0);
    auto wave = (color == 5 || color == 6) ? Waveform::square : color == 7 ? Waveform::triangle : Waveform::saw;
    if (mask >= 0.5)
        wave = Waveform::square;

    const auto voices = std::clamp (p (65), 1, 9);
    s.osc = { SynthOscillator { wave, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };

    if (voices >= 2) // uníssono: três cópias desafinadas e abertas no estéreo
    {
        const auto spread = std::max (5.0, (std::pow (41.0, std::clamp (p (67), 0, 256) / 256.0) - 1.0) * 1.875);
        const auto pan = std::clamp (p (66), 0, 128) / 128.0f;
        s.osc[0].level = s.osc[1].level = s.osc[2].level = 1.0f / 3.0f;
        s.osc[1] = { wave, 0, (float) std::min (100.0, spread), 1.0f / 3.0f, 0, pan };
        s.osc[2] = { wave, 0, (float) -std::min (100.0, spread), 1.0f / 3.0f, 0, -pan };
    }
    else
    {
        if (color >= 8 && color <= 10) // serra dupla/tripla
        {
            s.osc[0].level = 0.4f;
            s.osc[1] = { Waveform::saw, 0, 7.0f, 0.3f };
            s.osc[2] = { Waveform::saw, 0, -7.0f, 0.3f };
        }
        else if (mask > 0.05 && mask < 0.5) // meio caminho entre serra e quadrada
        {
            s.osc[0].level = (float) (1.0 - mask);
            s.osc[1] = { Waveform::square, 0, 0.0f, (float) mask };
        }

        if (p (2) > 0) // sub-harmônica: seno uma oitava abaixo
            s.osc[2] = { Waveform::sine, -1, 0.0f, (float) std::min (1.0, p (2) / 1024.0) * 0.5f };
    }

    for (auto& o : s.osc)
        transpose (o, std::clamp (p (38), -2400, 2400) / 100.0); // afinação mestre em centésimos

    // --- envelope de volume: o decay do Harmless cai até −40 dB (sem sustain); 1000 = segura ---
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

    // --- filtro: curva numa escala log de 73 Hz a 17 kHz ---
    const auto cutoff = 73.4 * std::pow (2.0, 7.855 * std::clamp (p (54), 0, 8192) / 8192.0);
    s.cutoff = (float) std::clamp (cutoff, 20.0, 20000.0);
    s.resonance = (float) std::clamp (0.9 * x (59) + (p (62) > 0 ? 0.1 : 0.0), 0.0, 1.0);

    switch (filterType)
    {
        case 1: case 5: case 14: case 15: case 16: s.filterType = 1; break; // passa-banda (os phasers, largos)
        case 2: case 6: s.filterType = 3; break;                            // rejeita-faixa
        case 3: case 7: s.filterType = 2; break;                            // passa-altas
        default: s.filterType = 0; break;                                   // os passa-baixas
    }

    // --- envelope de filtro: deslocamento de a·faixa (não adaptativo) ou da faixa que resta ---
    if (const auto amount = std::clamp (x (52), -1.0, 1.0); std::abs (amount) > 0.01)
    {
        const auto adaptive = p (47) != 0;
        const auto octaves = ! adaptive ? amount * 7.9
                                        : amount > 0.0 ? amount * std::log2 (17000.0 / cutoff) : amount * std::log2 (cutoff / 73.4);
        auto envTime = [] (double v) { return v >= 1.0 ? 20.0 : 0.12 * (std::pow (1001.0, std::clamp (v, 0.0, 1.0)) - 1.0); };
        s.filterEnv = { (float) std::clamp (octaves, -10.0, 10.0), (float) envTime (x (48)), 0.0f, (float) envTime (x (49)), 0.0f, s.release };
    }

    // --- LFO: período 64/2^(|v|/1000) tempos; formas aproximadas; destinos pitch, corte, volume ---
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
            s.lfo[lfoCutoff] = { (float) std::clamp (6.0 * x (58), -8.0, 8.0), period, true, lfoShape, 0.0f, attackBeats };

        if (p (25) > 0)
            s.lfo[lfoVolume] = { (float) std::clamp (x (25), 0.0, 1.0), period, true, lfoShape, 0.0f, attackBeats };
    }

    // --- mono / portamento ---
    s.mono = p (44) != 0;
    s.porta = p (43) != 0;
    s.portaBeats = (float) std::clamp ((std::pow (41.0, std::abs (x (45))) - 1.0) / 26.667 * beatsPerSecond, 0.0, 8.0);

    // volume mestre do Harmless e a compensação do uníssono
    const auto master = 0.75 * (std::pow (3.0, std::clamp (x (36), 0.0, 2.0)) - 1.0) / 2.0;
    gain = (float) std::clamp (master / 0.75, 0.05, 4.0); // 0,75 = o padrão (0 dB aqui); os 3 osciladores já somam 1

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

    // constantes de tempo (s) dos coeficientes por amostra do Fruit Kick
    auto tau = [] (double coefficient) { return coefficient > 0.0 && coefficient < 1.0 ? -1.0 / (fs * std::log (coefficient)) : 0.002; };
    const auto pitchTau = tau (0.9997 - (std::pow (1001.0, 1.0 - std::clamp (p (2), 0, 256) / 256.0) - 1.0) * 9.7e-6);
    const auto ampTau = tau (0.99998 - (std::pow (1001.0, 1.0 - std::clamp (p (3), 0, 256) / 256.0) - 1.0) * 4.98e-6);

    // fim fixo (min − 3600 ct a partir do C5 do FL = 523 Hz); começo = max + 1200 ct acima da nota
    const auto endCents = std::clamp (p (1), -1200, 1200) - 3600.0;
    const auto sweep = std::clamp ((std::clamp (p (0), -2400, 3600) + 1200.0 - endCents) / 100.0, 0.0, 48.0);

    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    // a nota C5 do FL (tecla 60 aqui, 261,6 Hz no synth) termina na frequência final
    const auto endHz = 523.25 * std::pow (2.0, endCents / 1200.0);
    transpose (s.osc[0], 12.0 * std::log2 (endHz / 261.63));
    s.cutoff = 20000.0f;
    s.attack = 0.0005f;
    s.decay = (float) std::clamp (6.9 * ampTau, 0.01, 10.0); // −60 dB em ~6,9 τ
    s.sustain = 0.0f;
    s.release = 0.02f;
    s.pitchEnv = { (float) sweep, 0.0f, 0.0f, (float) std::clamp (4.6 * pitchTau, 0.005, 2.0), 0.0f, 0.02f };
    synth = s;
    return true;
}
bool synthFromMiniSynth (const std::vector<std::uint8_t>& d, double bpm, SynthSettings& synth)
{
    // "SSLF" ... chunk "DATA": "0rev" + 43 floats 0..1
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

    switch (kind)
    {
        case 1: o[0].level = o[1].level = 0.5f; o[1] = { Waveform::saw, 0, 10.0f * (0.2f + m), 0.5f }; break;            // serra + serra
        case 2: o[0].wave = Waveform::square; break;                                                                    // pulso (sem PWM)
        case 3: o[0] = { Waveform::square, 0, 0.0f, 0.5f }; o[1] = { Waveform::saw, 0, 10.0f * m, 0.5f }; break;           // quadrada + serra
        case 4: o[0] = { Waveform::square, 0, 0.0f, 0.5f }; o[1] = { Waveform::square, 0, 10.0f * (0.2f + m), 0.5f }; break;
        case 5: // supersaw: 3 serras com o espalhamento de ±5 %·m
        {
            const auto cents = (float) std::min (100.0, 1200.0 * std::log2 (1.0 + 0.05 * std::max (0.1f, m)));
            o[0].level = o[1].level = o[2].level = 1.0f / 3.0f;
            o[1] = { Waveform::saw, 0, cents, 1.0f / 3.0f };
            o[2] = { Waveform::saw, 0, -cents, 1.0f / 3.0f };
            break;
        }
        case 8:
        case 9: // serras espalhadas em oitavas
            o[0] = { Waveform::saw, 0, 0.0f, 0.4f };
            o[1] = { Waveform::saw, kind == 8 ? -1 : 0, 0.0f, 0.3f, kind == 8 ? 0 : 7 };
            o[2] = { Waveform::saw, 1, 0.0f, 0.3f };
            break;
        default: break; // serra (as variações de fase soam parecidas)
    }

    if (v[7] > 0.01f && o[2].level == 0.0f) // ruído
        o[2] = { Waveform::noise, 0, 0.0f, std::min (1.0f, v[7] * v[7]) };

    const auto transpose = (int) ((v[1] - 0.5f) * 24.0f);
    for (auto& osc : o)
        osc.semitones = std::clamp (osc.semitones + transpose, -11, 11);

    // filtro ladder 24 dB: fc = 2^(4 + 10,3·F) Hz
    s.cutoff = (float) std::clamp (std::pow (2.0, 4.0 + 10.3 * v[8]), 20.0, 20000.0);
    s.resonance = std::clamp (0.99f * v[9], 0.0f, 1.0f);

    // envelopes (amostras a 44,1 kHz): k = 0,95p + 0,05
    auto k = [] (float p) { return 0.95 * p + 0.05; };
    auto attack = [&] (float p) { return p < 0.001f ? 0.0005 : 0.288 * 500.0 * std::pow (k (p), 4.0) / 0.0002 / 44100.0; };
    auto tau = [&] (float p) { return 50.0 * std::pow (k (p), 4.0) / 0.0002 / 44100.0; };
    s.attack = (float) std::min (5.0, attack (v[21]));
    s.sustain = v[23];
    s.decay = (float) std::min (10.0, 6.9 * tau (v[22]));
    s.release = (float) std::min (10.0, 6.9 * tau ((float) (0.95 * k (v[24]) + 0.05)) / 10.0);

    if (const auto amount = 10.0f * (2.0f * v[16] - 1.0f); std::abs (amount) > 0.05f)
        s.filterEnv = { amount, (float) std::min (5.0, attack (v[12])), 0.0f, (float) std::min (10.0, 4.6 * tau (v[13])), v[14],
                        (float) std::min (10.0, 4.6 * tau (v[15])) };

    // LFO: rótulos 1/4 … 16 bars (duração em tempos)
    constexpr float lfoBeats[] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 16.0f, 32.0f, 64.0f };
    const auto amount = 2.0f * v[20] - 1.0f;
    const auto dest = std::clamp ((int) (2.0f * v[18]), 0, 2);

    if (std::abs (amount) > 0.02f && dest < 2)
    {
        const auto shape = std::clamp ((int) (2.0f * v[17]), 0, 2) == 0 ? 0 : (std::clamp ((int) (2.0f * v[17]), 0, 2) == 1 ? 1 : 2);
        const auto period = lfoBeats[std::clamp ((int) (11.0f * v[19]), 0, 11)];
        if (dest == 0)
            s.lfo[lfoCutoff] = { 4.0f * amount, period, true, shape, 0.0f, 0.0f };
        else
            s.lfo[lfoPitch] = { 12.0f * amount, period, true, shape, 0.0f, 0.0f };
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

    // razão do modulador: inteira + fina
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
    s.osc = { SynthOscillator { Waveform::sine, std::clamp (octave + 1, -3, 3), 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };

    // FM de verdade: o osc 3 é o modulador na razão R; o índice cai de Init até Sustain
    if (index > 0.01)
    {
        s.osc[2] = { Waveform::sine, s.osc[0].octave, 0.0f, 1.0f };
        transpose (s.osc[2], 12.0 * std::log2 (r));
        s.osc[2].detune = (float) std::clamp ((12.0 * std::log2 (r) - std::round (12.0 * std::log2 (r))) * 100.0, -50.0, 50.0);
        s.fm = (float) (2.0 * index * index); // índice ∝ p² [HIP na escala]
        const auto sustain = p (7) * p (7) / std::max (1e-6, index * index);
        if (sustain < 0.98)
        {
            s.fmDecay = (float) std::min (20.0, 6.9 * std::exp (7.0 * p (6) - 6.0)); // τ -> −60 dB
            s.fmSustain = (float) std::clamp (sustain, 0.0, 1.0);
        }
    }

    if (const auto thru = 0.25 * p (12) * p (12); thru > 0.01) // o modulador direto na saída
    {
        s.osc[1] = { Waveform::sine, s.osc[0].octave, 0.0f, (float) std::min (0.5, thru * 2.0) };
        transpose (s.osc[1], 12.0 * std::log2 (r));
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
