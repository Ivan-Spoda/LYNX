// GMS, Ogun/Autogun, Sakura e Fruity DrumSynth Live aproximados pelo synth interno (ou por um
// sample de bateria). Formatos e fórmulas em docs/analise/REA_rodada12.md (clean-room).

#include "core/FlSynths.h"
#include "core/Inflate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace lynx
{
namespace
{
constexpr double pi = 3.141592653589793;

std::uint32_t u32At (const std::uint8_t* d, std::size_t offset)
{
    std::uint32_t v = 0;
    std::memcpy (&v, d + offset, 4);
    return v;
}

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
    osc.detune = (float) std::clamp ((semitones - whole) * 100.0 + osc.detune, -100.0, 100.0);
}

std::string lower (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

// ---- GMS ----

// forma do LYNX pelo nome do .wav do GMS (§1.4); `octaveUp` nas ondas com o 2º harmônico dominante
Waveform gmsWave (const std::string& path, bool& octaveUp)
{
    auto name = path.substr (path.find_last_of ("\\/") == std::string::npos ? 0 : path.find_last_of ("\\/") + 1);
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos)
        name.resize (dot);
    name = lower (name);
    octaveUp = name == "digital" || name == "double spike" || name == "devils horn" || name == "liquid";

    if (name.empty() || name == "sine" || name == "keys")
        return Waveform::sine;
    if (name == "triangle" || name == "smooth sub" || name == "rhodes" || name == "string")
        return Waveform::triangle;
    if (name.rfind ("square", 0) == 0 || name == "digital")
        return Waveform::square;
    return Waveform::saw;
}

// corte do ladder do GMS em Hz para u = 0..1 (tabela 2^(9,77u), polo a 4× 44,1 kHz)
double gmsCutoffHz (double u)
{
    const auto g = 0.0005 * std::pow (2.0, 9.7734 * std::clamp (u, 0.0, 0.999));
    return std::clamp (-std::log (1.0 - g) * 4.0 * 44100.0 / (2.0 * pi), 20.0, 20000.0);
}

int truncf32 (float x) { return (int) x; }
} // namespace

bool synthFromGms (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain)
{
    // "SSLF" + tamanho + o preset .gmsynth
    if (state.size() < 8 || std::memcmp (state.data(), "SSLF", 4) != 0)
        return false;
    const auto size = (std::size_t) u32At (state.data(), 4);
    if (size + 8 > state.size() || size < 4)
        return false;
    const auto* d = state.data() + 8;

    const auto tag = u32At (d, 0);
    const bool syn4 = tag == 0x53796E34, syn3 = tag == 0x53796E33;
    if (! syn4 && ! syn3)
        return false;
    const std::size_t paramsAt = syn4 ? 0xE10 : 0xE04;
    if (paramsAt + 212 * 4 > size)
        return false;

    float x[212];
    std::memcpy (x, d + paramsAt, sizeof (x));
    const auto velocityToCutoff = x[199] == 1.0f; // destino gravado como número inteiro (1 = corte)
    for (int i = 0; i < 212; ++i)
        if (! std::isfinite (x[i]) || x[i] < 0.0f || x[i] > 1.0f)
            x[i] = i < 197 ? std::clamp (std::isfinite (x[i]) ? x[i] : 0.0f, 0.0f, 1.0f) : 0.0f;

    std::string waves[3];
    for (int k = 0; k < 3; ++k)
    {
        const auto* slot = reinterpret_cast<const char*> (d + 4 + 512 * (std::size_t) k);
        waves[k].assign (slot, strnlen (slot, 512));
    }

    SynthSettings s;

    // afinação: oitava global, Δ do canal e semitons/cents de cada oscilador (em float32, truncando)
    const auto octave = std::clamp (truncf32 (4.0f * x[44]) - 2, -2, 2);
    const auto channel = -truncf32 (24.0f - 48.0f * x[2]);
    double semis[3] {};
    for (int k = 0; k < 3; ++k)
    {
        bool up = false;
        s.osc[(std::size_t) k].wave = gmsWave (waves[k], up);
        semis[k] = 12.0 * octave + channel + truncf32 (72.0f * x[55 + 2 * k] - 36.0f) + truncf32 (100.0f * x[56 + 2 * k] - 50.0f) / 100.0 + (up ? 12.0 : 0.0);
    }

    // mistura em cascata (§1.3)
    const double a = (double) x[48] * x[48], b = (double) x[49] * x[49], n = (double) x[45] * x[45];
    double level[3] = { (1 - a) * (1 - b) * (1 - n), a * (1 - b) * (1 - n), b * (1 - n) };
    for (int k = 0; k < 3; ++k)
    {
        auto& o = s.osc[(std::size_t) k];
        o.detune = 0.0f;
        setPitch (o, semis[k]);
        o.level = (float) std::clamp (level[k], 0.0, 1.0);
        o.pan = 0.0f;
    }

    // modulação osc2 -> osc1
    const auto modKind = std::clamp (truncf32 (2.0f * x[51]), 0, 2);
    const auto m = (double) x[50] * x[50];
    if (m > 0.0 && modKind == 2) // AM: o osc 2 do GMS vira o modulador (osc 3 do LYNX)
    {
        auto osc3 = s.osc[2];
        auto modulator = s.osc[1];
        modulator.level = (float) std::min (1.0, 3.0 * m);
        s.am = true;
        s.osc[1] = osc3; // o osc 3 do GMS (se audível) toma o lugar livre
        s.osc[2] = modulator;
    }
    else if (m > 0.0 && modKind == 1 && level[2] < 0.05) // FM/PM: o osc 2 do GMS vira o modulador (osc 3 do LYNX)
    {
        auto modulator = s.osc[1];
        modulator.level = 1.0f;
        modulator.pan = 0.0f;
        s.fm = (float) std::min (8.0, 3.75 * m); // fase do osc 1 recebe osc2·15·m quartos de ciclo
        s.osc[2] = modulator;
        if (level[1] < 0.05) // o osc 2 do GMS só modulava
            s.osc[1].level = 0.0f;
    }
    else if (m > 0.0 && modKind == 0) // hard sync: um osc1 mais agudo e rico
    {
        s.osc[0].wave = Waveform::saw;
        setPitch (s.osc[0], semis[0] + 12.0 * std::log2 (1.0 + 3.0 * m));
    }

    // ruído num slot vazio
    if (n > 0.05)
        for (auto& o : s.osc)
            if (o.level < 0.05f && ! ((s.am || s.fm > 0.0f) && &o == &s.osc[2]))
            {
                o = { Waveform::noise, 0, 0.0f, (float) std::min (1.0, n) };
                break;
            }

    // uníssono: cópias do osc 1 nos slots vazios
    const auto voices = 1 + truncf32 (15.0f * x[41]);
    if (voices >= 2)
    {
        const auto spread = std::max (3.0, 600.0 * std::pow (0.4 * x[42], 2.0));
        const auto compensation = 1.0 / (1.0 + 0.15 * (voices - 1));
        int sign = 1;
        for (std::size_t k = 1; k < 3; ++k)
            if (s.osc[k].level < 0.05f && ! ((s.am || s.fm > 0.0f) && k == 2))
            {
                s.osc[k] = s.osc[0];
                s.osc[k].detune = (float) std::clamp (s.osc[0].detune + sign * spread, -100.0, 100.0);
                s.osc[k].pan = (float) (sign * x[43]);
                sign = -sign;
            }
        for (std::size_t k = 0; k < 3; ++k)
            if (! ((s.am || s.fm > 0.0f) && k == 2))
                s.osc[k].level = (float) std::clamp (s.osc[k].level * compensation * 1.5, 0.0, 1.0);
    }

    // amplitude: a saída é e², os tempos são de −40 dB
    s.attack = (float) std::min (5.0, 1.265 * std::pow ((double) x[4], 3.0));
    const auto td = 50.0 * std::pow ((double) x[5], 4.0) + 0.001;
    const auto tr = 25.0 * std::pow (0.55 * x[7] + 0.1, 3.0) + 0.001;
    s.sustain = x[6] * x[6];
    s.decay = s.sustain >= 0.999f ? 0.0f : (float) std::min (20.0, 2.3 * td / 8.82);
    s.release = (float) std::clamp (2.3 * tr / 8.82, 0.003, 10.0);

    // filtro ladder -> SVF do LYNX
    auto u = (double) x[12];
    if (velocityToCutoff) // velocidade -> corte (com uma velocidade típica de 0,8)
        u += (2.0 * x[198] - 1.0) * 0.8;
    u = std::clamp (u, 0.0, 0.999);
    const auto filterType = std::clamp (truncf32 (2.0f * x[15]), 0, 2);
    s.filterType = filterType == 0 ? 0 : filterType == 1 ? 1 : 2;
    s.cutoff = (float) gmsCutoffHz (u);
    s.resonance = std::min (0.95f, x[13]);
    auto octavesFrom = [&] (double du) { return std::log2 (gmsCutoffHz (u + du) / gmsCutoffHz (u)); };

    // envelopes AD de modulação
    for (int k = 0; k < 2; ++k)
    {
        auto amount = (double) x[16 + k] * x[16 + k];
        if (x[18 + k] >= 0.5f)
            amount = -amount;
        if (std::abs (amount) < 1e-4)
            continue;
        const auto attack = std::min (10.0, 7.59 * std::pow ((double) x[20 + k], 5.0));
        const auto decay = std::min (20.0, 4.6 * (70.0 * std::pow (0.8 * x[22 + k] + 0.2, 5.0) + 0.001) / 8.82);
        const auto dest = std::clamp (truncf32 (3.0f * x[24 + k]), 0, 3);
        if (dest == 0 && s.filterEnv.amount == 0.0f)
        {
            if (amount < 0.0) // fecha: o corte parte de baixo e "abre" de volta
                s.filterEnv = { (float) std::clamp (octavesFrom (amount), -10.0, 0.0), (float) attack, 0.0f, (float) decay, 0.0f, s.release };
            else
                s.filterEnv = { (float) std::clamp (octavesFrom (amount), 0.0, 10.0), (float) attack, 0.0f, (float) decay, 0.0f, s.release };
        }
        else if (dest == 2 && s.pitchEnv.amount == 0.0f)
            s.pitchEnv = { (float) (24.0 * amount), (float) attack, 0.0f, (float) decay, 0.0f, s.release };
    }

    // LFOs
    for (int k = 0; k < 2; ++k)
    {
        auto amount = (double) x[28 + k] * x[28 + k];
        if (x[30 + k] >= 0.5f)
            amount = -amount;
        if (std::abs (amount) < 1e-4)
            continue;

        ChannelLfo lfo;
        const auto shape = std::clamp (truncf32 (3.0f * x[32 + k]), 0, 3);
        lfo.shape = shape == 0 ? 0 : shape == 3 ? 2 : 1;
        const auto sync = std::clamp (truncf32 (2.0f * x[36 + k]), 0, 2);
        if (sync == 0)
            lfo.period = (float) (1.0 / (25.0 * std::pow (1.0 - x[34 + k], 2.0) + 0.01));
        else
        {
            static constexpr double beats[] = { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0 };
            lfo.period = (float) (beats[std::clamp (truncf32 (9.0f * x[34 + k]), 0, 8)] * (sync == 2 ? 4.0 : 1.0));
            lfo.sync = true;
        }
        lfo.global = x[26 + k] < 0.5f; // sem retrig a fase corre livre

        // um envelope com destino "LFO" vira o fade-in deste LFO
        if (std::clamp (truncf32 (3.0f * x[24 + k]), 0, 3) == 3 && x[16 + k] > 0.0f)
            lfo.attack = (float) std::min (10.0, 7.59 * std::pow ((double) x[20 + k], 5.0));
        if (lfo.sync && lfo.attack > 0.0f)
            lfo.attack = (float) (lfo.attack * std::max (1.0, bpm) / 60.0);

        switch (std::clamp (truncf32 (3.0f * x[38 + k]), 0, 3))
        {
            case 0: lfo.amount = (float) std::clamp (octavesFrom (std::abs (amount)) * (amount < 0 ? -1 : 1), -10.0, 10.0); s.lfo[lfoCutoff] = lfo; break;
            case 2: lfo.amount = (float) (12.0 * amount); s.lfo[lfoPitch] = lfo; break;
            case 3: lfo.amount = (float) std::abs (amount); s.lfo[lfoVolume] = lfo; break;
            default: break; // "modulation": sem equivalente
        }
    }

    s.mono = x[40] > 0.0f;
    s.porta = x[11] > 0.0f;
    if (s.porta)
        s.portaBeats = (float) std::clamp (0.52 * (2.0 * std::pow ((double) x[10], 3.0)) * std::max (1.0, bpm) / 60.0, 0.01, 8.0);

    if (const auto pan = 2.0f * x[1] - 1.0f; std::abs (pan) > 0.01f)
        for (auto& o : s.osc)
            o.pan = std::clamp (o.pan + pan, -1.0f, 1.0f);

    gain = (float) std::clamp (std::pow (10.0, (80.0 * x[0] - 60.0) / 20.0) * std::pow (10.0, (50.0 * x[8] - 25.0) / 20.0), 0.0, 8.0);
    synth = s;
    return true;
}

// ---- Ogun / Autogun ----

bool synthFromOgun (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    if (state.size() < 8)
        return false;
    auto version = u32At (state.data(), 0);
    if (version > 0xffff)
        version >>= 16;
    if (version < 14 || version > 19)
        return false; // as versões antigas gravam um bloco reduzido (não usado nos demos atuais)

    const auto body = inflateZlib (state.data() + 4, state.size() - 4, false, 8u << 20);
    if (! body || body->size() < 64 * 4)
        return false;
    std::int32_t p[64];
    std::memcpy (p, body->data(), sizeof (p));

    // tempo dos knobs de timbre: 10000 = infinito
    auto seconds = [] (int v) { return v >= 10000 ? -1.0 : ((std::pow (21.0, std::clamp (v, 0, 10000) / 10000.0) - 1.0) * 997.5 + 50.0) / 1000.0; };

    SynthSettings s;
    const auto fullness = std::clamp (p[8], -1000, 1000);
    const auto richness = std::clamp (p[52] - 5, 1, 11);
    const auto wave = fullness <= -800 && richness <= 3 ? Waveform::sine : fullness < -300 ? Waveform::triangle : Waveform::saw;
    const auto transpose = std::clamp (p[1], -24, 24) + std::clamp (p[2], -50, 50) / 100.0;

    s.osc = { SynthOscillator { wave, 0, 0.0f, 0.8f }, SynthOscillator {}, SynthOscillator {} };
    setPitch (s.osc[0], transpose);
    if (fullness > 300) // timbre "metálico": um parcial uma oitava acima
    {
        s.osc[1] = { Waveform::sine, 0, 0.0f, 0.3f };
        setPitch (s.osc[1], transpose + 12.0);
    }

    const auto decay = seconds (p[6]), release = seconds (p[7]);
    s.attack = 0.0f;
    if (decay < 0.0)
    {
        s.decay = 0.0f;
        s.sustain = 1.0f;
    }
    else
    {
        s.decay = (float) std::min (30.0, decay);
        s.sustain = 0.0f;
    }
    s.release = (float) std::clamp (release < 0.0 ? 30.0 : release, 0.005, 30.0);

    if (const auto voices = std::clamp (p[12], 1, 9); voices >= 2)
    {
        const auto pan = std::clamp (p[13], 0, 128) / 128.0f;
        for (std::size_t k = 1; k < 3; ++k)
            if (s.osc[k].level == 0.0f)
            {
                s.osc[k] = s.osc[0];
                s.osc[k].detune = std::clamp (s.osc[0].detune + (k == 1 ? 10.0f : -10.0f), -100.0f, 100.0f);
                s.osc[k].pan = k == 1 ? pan : -pan;
            }
        for (auto& o : s.osc)
            o.level *= 0.6f;
    }

    // filtro: só existe com ordem > 0
    s.cutoff = 20000.0f;
    if (const auto order = std::clamp (p[50], 0, 3); order > 0)
    {
        const auto type = std::clamp (p[49], 0, 3);
        const auto u = std::clamp (p[17], 0, 256) / 256.0;
        double c = 0.0;
        if (type == 1 || type == 2)
            c = std::min (1.0001, (std::pow (1501.0, u) - 1.0) * 0.0004);
        else
        {
            c = (std::pow (1501.0, u) - 1.0) * 0.000665 + 0.003;
            if (type == 3)
                c = std::min (c, 0.6);
        }
        s.filterType = type == 0 ? 0 : type == 1 ? 1 : type == 2 ? 3 : 2;
        s.cutoff = (float) std::clamp (22050.0 * c * std::pow (0.9, order - 1), 20.0, 20000.0);
        s.resonance = std::min (0.95f, std::clamp (p[18], 0, 256) / 256.0f);
    }

    gain = (float) std::clamp ((std::pow (3.0, std::clamp (p[0], 0, 256) / 128.0) - 1.0) / 2.0, 0.0, 4.0); // (3^(P0/128) − 1)/2
    synth = s;
    return true;
}

// ---- Sakura ----

bool synthFromSakura (const std::vector<std::uint8_t>& state, SynthSettings& synth)
{
    if (state.size() < 12 + 146 * 4 || std::memcmp (state.data(), "SSLF", 4) != 0 || u32At (state.data(), 8) != 0x536B7231)
        return false;
    float t[146];
    std::memcpy (t, state.data() + 12, sizeof (t));
    for (auto& v : t)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;

    // As cordas do Sakura são físicas (excitador + guia de onda): aqui viram serra (corda 1) + triângulo
    // (corda 2) — a troca de modelo é aproximação. Confirmados (REA_rodada18 §10): Amp A = 35x⁵ + 0,001 s,
    // D = 50x⁵ + 0,001 s e R = 10x⁵ + 0,1 s com polos de constante T/8,82 (o LYNX mede o tempo até −60 dB:
    // ×6,908/8,82), sustain x², Tune trunc(198x − 99)·0,01 semitom, String Mix linear (corda 1 ∝ 1 − mix).
    // O decaimento da corda (realimentação d^0,05 por período → T60 ≈ 60/(f·(−log10 d)) s) depende da
    // nota: usa-se o T60 da tecla 60 (aproximação). O Hi-Cut fica de fora (a conversão para Hz não foi lida).
    SynthSettings s;
    const auto mix = t[17];
    s.osc = { SynthOscillator { Waveform::saw, 0, 0.0f, 1.0f - mix }, SynthOscillator { Waveform::triangle, 0, 0.0f, mix }, SynthOscillator {} };
    const auto tune = std::clamp ((int) (198.0f * t[1] - 99.0f), -99, 99) * 0.01;
    setPitch (s.osc[0], tune);
    setPitch (s.osc[1], tune);
    auto p5 = [] (float x) { return std::pow ((double) x, 5.0); };
    constexpr double toLynx = 6.907755 / 8.82;
    s.attack = (float) (35.0 * p5 (t[2]) + 0.001);
    s.decay = (float) ((50.0 * p5 (t[3]) + 0.001) * toLynx);
    s.sustain = t[4] * t[4];
    s.release = (float) ((10.0 * p5 (t[5]) + 0.1) * toLynx);
    const auto d = std::clamp ((double) t[6], 0.0, 0.999999);
    const auto stringT60 = d <= 0.0 ? 0.01 : 60.0 / (261.63 * -std::log10 (d));
    if (stringT60 < 30.0 && (s.sustain > 0.0f || stringT60 < s.decay))
    {
        s.sustain = 0.0f;
        s.decay = (float) std::max (0.01, stringT60);
    }
    s.cutoff = 20000.0f;
    synth = s;
    return true;
}

// ---- Fruity DrumSynth Live ----

std::optional<std::string> drumSynthLiveSound (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 9)
        return std::nullopt;
    const auto version = u32At (state.data(), 0), index = u32At (state.data(), 4);
    if (version < 3 || version > 8 || index >= 120)
        return std::nullopt;

    std::size_t pos = 8;
    std::string name;
    for (std::uint32_t i = 0; i <= index; ++i)
    {
        if (pos >= state.size())
            return std::nullopt;
        const auto len = (std::size_t) state[pos];
        if (pos + 1 + len > state.size())
            return std::nullopt;
        name.assign (reinterpret_cast<const char*> (state.data() + pos + 1), len);
        pos += 1 + len;
    }
    return name.empty() ? std::nullopt : std::optional<std::string> (name);
}

std::vector<std::string> drumSampleCandidates (const std::string& soundName)
{
    const auto name = lower (soundName);
    const bool is909 = name.find ("909") != std::string::npos;
    const std::string first = is909 ? "909" : "808", second = is909 ? "808" : "909";
    const std::string root = "%FLStudioData%\\Patches\\Packs\\Drums\\";
    auto has = [&] (const char* w) { return name.find (w) != std::string::npos; };

    std::vector<std::string> out;
    auto both = [&] (const std::string& folder, const std::string& rest909, const std::string& rest808) {
        for (const auto& kit : { first, second })
            out.push_back (root + folder + "\\" + kit + " " + (kit == "909" ? rest909 : rest808) + ".wav");
    };

    if (has ("kick"))
        both ("Kicks", "Kick", "Kick");
    else if (has ("snare"))
        both ("Snares", "Snare", "Snare");
    else if (has ("rim"))
        both ("Snares", "Rim", "Rim");
    else if (has ("clap"))
    {
        out.push_back (root + "Percussion\\909 Clap.wav");
        out.push_back ("%FLStudioData%\\Patches\\Packs\\Legacy\\Drums\\Dance\\Basic 808 Clap.wav");
    }
    else if (has ("hat o"))
        both ("Hats", "OH", "OH");
    else if (has ("hat"))
        both ("Hats", "CH 1", "CH");
    else if (has ("ride"))
        out.push_back (root + "Cymbals\\909 Ride.wav");
    else if (has ("crash"))
        both ("Cymbals", "Crash", "Crash");
    else if (has ("tom"))
        both ("Toms", "Tom", "Tom");
    else if (has ("cowbell"))
        out.push_back (root + "Percussion\\808 Cowbell.wav");
    else if (has ("clave"))
        both ("Percussion", "Clav", "Clav");
    else if (has ("bongo") || has ("conga"))
        out.push_back (root + "Percussion\\808 Conga.wav");
    else if (has ("maracas"))
        out.push_back (root + "Hats\\808 CH.wav");
    return out;
}
// ---- Kepler (REA_rodada13 §2) ----

bool synthFromKepler (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    // u32 0, u32 tamanho, u32 0, u32 9 + "Kepler-1\0", u32 versão, u32 n, n floats
    if (state.size() < 0x21 + 34 * 4 || u32At (state.data(), 12) != 9 || std::memcmp (state.data() + 16, "Kepler-1", 8) != 0)
        return false;
    const auto count = (int) u32At (state.data(), 0x1D);
    if (count < 34 || count > 64 || 0x21 + (std::size_t) count * 4 > state.size())
        return false;
    float k[64] {};
    std::memcpy (k, state.data() + 0x21, (std::size_t) count * 4);
    for (auto& v : k)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;

    // tabelas de 11 pontos do motor, interpoladas em x·10 (REA_rodada17 §6)
    auto table = [] (const double (&t)[11], float x) {
        const auto pos = std::clamp ((double) x, 0.0, 1.0) * 10.0;
        const auto i = std::min (9, (int) pos);
        return t[i] + (t[i + 1] - t[i]) * (pos - i);
    };
    static constexpr double attackTable[11] = { 0.003, 0.004, 0.013, 0.05, 0.128, 0.243, 0.401, 0.7, 1.3, 2.403, 3.049 };
    static constexpr double decayTable[11] = { 0.0015, 0.0027, 0.043, 0.141, 0.328, 0.651, 1.144, 2.053, 3.956, 8.466, 10.124 };
    static constexpr double releaseTable[11] = { 0.001, 0.002, 0.02, 0.112, 0.321, 0.572, 1.063, 1.964, 3.38, 6.91, 10.967 };
    static constexpr double cutoffTable[11] = { 2.0, 2.4594, 3.3219, 4.3219, 5.6837, 6.9542, 8.543, 10.4717, 12.388, 15.1635, 15.6096 };
    static constexpr double envAmountTable[11] = { 0.0, 0.389, 1.681, 2.888, 4.017, 5.039, 6.069, 7.288, 8.475, 8.957, 8.972 };

    SynthSettings s;
    const auto octave = std::clamp ((int) std::lround (2.0 * k[32]) - 1, -1, 1); // −1, 0, +1
    const auto saw = k[14] >= 0.5f, pulse = k[13] >= 0.5f, sub = k[15] >= 0.5f;
    s.osc = { SynthOscillator { Waveform::saw, octave, 0.0f, saw ? 1.0f : 0.0f },
              SynthOscillator { Waveform::square, octave, 0.0f, pulse ? 1.0f : 0.0f },
              SynthOscillator {} };
    if (! saw && ! pulse)
        s.osc[0].level = 1.0f;
    if (k[17] > k[16] * (sub ? 1.0f : 0.0f) && k[17] > 0.05f) // ruído mais alto que o sub
        s.osc[2] = { Waveform::noise, 0, 0.0f, k[17] };
    else if (sub)
        s.osc[2] = { Waveform::square, std::max (-3, octave - 1), 0.0f, std::max (0.05f, k[16]) };

    // Env1: ataque chega a 1 em T_A (linear no LYNX); decay/release do Kepler são até −20 dB, os do LYNX
    // até −60 dB (exponenciais): × 3
    s.attack = (float) table (attackTable, k[26]);
    s.decay = (float) (3.0 * table (decayTable, k[27]));
    s.sustain = k[28];
    s.release = (float) std::max (0.003, 3.0 * table (releaseTable, k[29]));
    if (k[25] >= 0.5f) // VCA por gate
    {
        s.attack = 0.002f;
        s.decay = 0.0f;
        s.sustain = 1.0f;
        s.release = 0.01f;
    }

    s.cutoff = (float) std::clamp (std::pow (2.0, table (cutoffTable, k[19])), 20.0, 20000.0);
    s.resonance = std::min (0.95f, k[20]);
    if (const auto octaves = table (envAmountTable, k[22]); octaves > 0.01)
    {
        const auto sign = k[21] >= 0.5f ? -1.0f : 1.0f; // polaridade
        s.filterEnv = { sign * (float) octaves, s.attack, 0.0f, s.decay, s.sustain, s.release };
    }
    // LFO1: a taxa é confirmada (21,739·2^(−T(x)) Hz), mas as profundidades LFO→DCO/corte não: ficam de fora
    // chorus I/II do Juno: as taxas não foram confirmadas; sem chorus

    // Main Volume: m(v) = 0,34968v − 0,32754v² + 1,39229v³ (v = 1 -> 0 dB)
    const auto v = (double) k[33];
    gain = (float) std::clamp (0.34968 * v - 0.32754 * v * v + 1.39229 * v * v * v, 0.0, 2.0);
    synth = s;
    return true;
}

// ---- Wave Traveller (§4): o sample e a nota-raiz ----

std::optional<std::pair<std::string, int>> waveTravellerSample (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 24 || u32At (state.data(), 0) != 1000)
        return std::nullopt;
    const auto root = (int) u32At (state.data(), 12);
    const auto length = (std::size_t) u32At (state.data(), 16);
    if (length == 0 || length > 4096 || 20 + length > state.size())
        return std::nullopt;
    std::string path (reinterpret_cast<const char*> (state.data() + 20), length);
    path.resize (strnlen (path.c_str(), path.size()));
    if (path.empty())
        return std::nullopt;
    return std::make_pair (path, std::clamp (root, 0, 127));
}

// ---- FLEX (§1.1): só o estado do plugin (os pacotes são protegidos e não são abertos) ----

std::optional<std::pair<std::string, std::string>> flexPresetName (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 0x411 + 1024 || u32At (state.data(), 0) != 1)
        return std::nullopt;
    auto text = [&] (std::size_t offset) {
        const auto* p = reinterpret_cast<const char*> (state.data() + offset);
        return std::string (p, strnlen (p, 1024));
    };
    auto pack = text (0x10), preset = text (0x411);
    if (preset.empty() && pack.empty())
        return std::nullopt;
    return std::make_pair (pack, preset);
}
// ---- BooBass (REA_rodada10 §5): baixo com Bass/Mid/Treble ----

bool synthFromBooBass (const std::vector<std::uint8_t>& state, SynthSettings& synth)
{
    if (state.size() < 16 || u32At (state.data(), 0) != 1)
        return false;
    auto knob = [&] (std::size_t i) { return std::clamp ((int) u32At (state.data(), 4 + 4 * i), 0, 65535) / 65535.0; };
    const auto bass = knob (0), mid = knob (1), treble = knob (2);

    // REA_rodada18 §11: o som do BooBass é uma amostra (ataque + laço) que não está nos dados do projeto;
    // aqui a fonte é uma serra uma oitava abaixo com decaimento (aproximação). Os três knobs são o EQ de 3
    // bandas confirmado: abaixo de 0,1 a banda cala; Bass 10^((12x − 12)/20), Mid 10^((10x − 10)/20),
    // Treble 10^((12x − 9)/20). A 2ª etapa (saturação no grave, ×3, limitador) fica de fora.
    auto band = [] (double x, double range, double top) { return x < 0.1 ? 0.0f : (float) std::pow (10.0, (range * x - range + top) / 20.0); };
    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::saw, -1, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    s.cutoff = 20000.0f;
    s.bandGain = { band (bass, 12.0, 0.0), band (mid, 10.0, 0.0), band (treble, 12.0, 3.0) };
    s.attack = 0.002f;
    s.decay = 0.6f;
    s.sustain = 0.5f;
    s.release = 0.08f;
    synth = s;
    return true;
}

// ---- Drumpad (§6): só o nome do patch; o modelo físico não é reproduzido ----

std::optional<std::string> drumpadPatchName (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 0x40 || std::memcmp (state.data(), "SSLF", 4) != 0 || std::memcmp (state.data() + 8, "3PMD", 4) != 0)
        return std::nullopt;
    const auto* p = reinterpret_cast<const char*> (state.data() + 12);
    std::string name (p, strnlen (p, 0x30));
    if (name.empty())
        return std::nullopt;
    return name;
}

std::optional<std::array<float, 38>> drumpadParams (const std::vector<std::uint8_t>& state)
{
    // "SSLF" + tamanho + "3PMD" + 128 B de nome + 38 floats (parâmetros 0…37 na ordem), REA_rodada18 §12
    if (state.size() < 140 + 38 * 4 || std::memcmp (state.data(), "SSLF", 4) != 0 || std::memcmp (state.data() + 8, "3PMD", 4) != 0)
        return std::nullopt;
    std::array<float, 38> p {};
    std::memcpy (p.data(), state.data() + 140, sizeof (float) * 38);
    for (auto& v : p)
        if (! std::isfinite (v))
            return std::nullopt;
    return p;
}

SynthSettings drumToneForName (const std::string& patchName)
{
    const auto name = lower (patchName);
    auto has = [&] (const char* w) { return name.find (w) != std::string::npos; };

    // tambor "genérico": seno com queda de afinação + um pouco de ruído para o ataque
    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator { Waveform::noise, 0, 0.0f, 0.12f }, SynthOscillator {} };
    s.attack = 0.0f;
    s.decay = 0.3f;
    s.sustain = 0.0f;
    s.release = 0.1f;
    s.cutoff = 6000.0f;
    s.pitchEnv = { 5.0f, 0.0f, 0.0f, 0.06f, 0.0f, 0.05f };

    if (has ("war") || has ("taiko") || has ("lo") || has ("floor"))
    {
        s.osc[0].octave = -1;
        s.decay = 0.6f;
    }
    if (has ("bongo") || has ("hi "))
    {
        s.osc[0].octave = 1;
        s.decay = 0.15f;
    }
    if (has ("conga"))
        s.decay = 0.25f;
    if (has ("wood") || has ("block") || has ("clave") || has ("rim"))
    {
        s.osc[0] = { Waveform::triangle, 1, 0.0f, 1.0f };
        s.osc[1].level = 0.05f;
        s.decay = 0.06f;
        s.pitchEnv.amount = 2.0f;
    }
    return s;
}
// ---- PoiZone, BassDrum, Toxic Biohazard (REA_rodada15 §12) ----

namespace
{
bool sslfFloats (const std::vector<std::uint8_t>& state, const char* tag, std::size_t offset, float* out, int count)
{
    if (state.size() < offset + (std::size_t) count * 4 || std::memcmp (state.data(), "SSLF", 4) != 0 || std::memcmp (state.data() + 8, tag, 4) != 0)
        return false;
    std::memcpy (out, state.data() + offset, (std::size_t) count * 4);
    for (int i = 0; i < count; ++i)
        out[i] = std::isfinite (out[i]) ? std::clamp (out[i], 0.0f, 1.0f) : 0.0f;
    return true;
}

double ms4 (double lo, double span, float x) { return (lo + span * std::pow ((double) x, 4.0)) / 1000.0; }
} // namespace

bool synthFromPoiZone (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    float p[74];
    if (! sslfFloats (state, "4reV", 12, p, 74))
        return false;

    SynthSettings s;
    const auto transpose = std::floor (48.0f * p[17]) - 24.0 + (200.0 * p[16] - 100.0) / 100.0;
    const auto waveA = p[0] > 0.0f ? Waveform::square : Waveform::saw;
    const auto waveB = p[4] > 0.0f ? Waveform::square : Waveform::saw;
    s.osc[0] = { waveA, 0, 0.0f, std::clamp (1.0f - p[8], 0.0f, 1.0f) };
    s.osc[1] = { waveB, 0, (float) (50.0 * p[7]), std::clamp (p[8], 0.0f, 1.0f) };
    s.osc[2] = { Waveform::noise, 0, 0.0f, p[9] * 0.5f };
    setPitch (s.osc[0], transpose);
    setPitch (s.osc[1], transpose + std::floor (48.0f * p[6]) - 24.0);
    s.osc[1].detune = (float) std::clamp (s.osc[1].detune + 50.0 * p[7], -100.0, 100.0);

    s.attack = (float) ms4 (1.0, 4999.0, p[22]);
    s.decay = (float) ms4 (1.0, 19999.0, p[23]);
    s.sustain = p[24];
    s.release = (float) ms4 (15.0, 9985.0, p[25]);
    // corte (REA_rodada17 §8): 2^(10·v + 6) Hz com v = 1,1·x recortado em 0,91 (64 Hz … 35 kHz)
    s.cutoff = (float) std::clamp (std::pow (2.0, 10.0 * std::min (0.91, 1.1 * (double) p[18]) + 6.0), 20.0, 20000.0);
    s.resonance = std::min (0.95f, p[19]);
    s.filterType = std::clamp ((int) (2.0f * p[66]), 0, 2) == 0 ? 0 : (int) (2.0f * p[66]) == 1 ? 1 : 2;
    // envelope do filtro no corte: ±10·a² oitavas, a = 2x − 1 (REA_rodada17 §8)
    if (const auto a = 2.0f * p[10] - 1.0f; (int) (4.0f * p[65]) == 0 && std::abs (a) > 0.01f)
        s.filterEnv = { (a < 0.0f ? -10.0f : 10.0f) * a * a, (float) ms4 (5.0, 49995.0, p[11]), 0.0f, (float) ms4 (5.0, 49995.0, p[12]), p[13],
                        (float) ms4 (5.0, 49995.0, p[14]) };
    if (std::abs (p[26] - 0.5f) > 0.01f && p[28] > 0.0f)
    {
        ChannelLfo lfo;
        lfo.period = (float) (1.0 / std::max (0.02, 30.0 * std::pow ((double) p[28], 5.0)));
        const auto shape = std::clamp ((int) (4.0f * p[68]), 0, 4);
        lfo.shape = shape == 1 ? 2 : shape == 2 ? 1 : 0;
        const auto amount = 2.0f * p[26] - 1.0f;
        switch (std::clamp ((int) (5.0f * p[67]), 0, 5))
        {
            case 0: lfo.amount = (amount < 0.0f ? -10.0f : 10.0f) * amount * amount; s.lfo[lfoCutoff] = lfo; break; // ±10·l² oit
            case 1: case 2: lfo.amount = std::floor (24.0f * amount); s.lfo[lfoPitch] = lfo; break;               // até ±24 st
            default: break; // PW e pan: escalas não confirmadas
        }
    }
    s.mono = p[44] >= 0.5f;
    s.porta = p[46] > 0.0f;
    gain = (float) std::pow (10.0, (30.0 * p[15] - 20.0) / 20.0);
    synth = s;
    return true;
}

bool synthFromBassDrum (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    float p[23];
    if (! sslfFloats (state, "1kiK", 0x50C, p, 23) && ! sslfFloats (state, "2kiK", 0x50C, p, 23))
        return false;

    // seno na frequência "Base" (2^(4x + 4) Hz). Confirmados (REA_rodada17 §7 + rodada18 §9): duração = rampa
    // linear de (x³ + 0,001) s, sem release; Peak f = base·(1 + 100·peak³·s⁴), com s caindo por
    // (1 − 0,0002/T_s) a cada amostra (a 44,1 kHz), T_s = 20·slide³ + 0,001. O clique e a camada de amostra
    // ficam de fora (a tabela do oscilador é quase senoidal).
    const auto baseHz = std::pow (2.0, 4.0 * p[6] + 4.0);
    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    setPitch (s.osc[0], 12.0 * std::log2 (baseHz / 261.63));
    const auto duration = (float) (std::pow ((double) p[2], 3.0) + 0.001);
    s.attack = 0.0005f;
    s.decay = duration;   // o decay do LYNX chega a −60 dB no tempo dado (a rampa do BassDrum chega a zero)
    s.sustain = 0.0f;
    s.release = duration; // sem release: soltar a nota não corta
    s.cutoff = 20000.0f;
    s.sweep = (float) (100.0 * std::pow ((double) p[7], 3.0));
    const auto slide = 20.0 * std::pow ((double) p[8], 3.0) + 0.001;
    s.sweepTime = (float) (-1.0 / (44100.0 * std::log (1.0 - 0.0002 / slide))); // constante de tempo de s
    gain = (float) std::pow (10.0, 40.0 * (p[0] - 0.5) / 20.0);
    synth = s;
    return true;
}

bool synthFromToxic (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain)
{
    float p[294];
    if (! sslfFloats (state, "2iBT", 0x204C, p, 294))
        return false;

    // portadora = operador ligado com mais "Mix" (REA_rodada17 §9) + o modulador ligado com a maior célula
    // da matriz para ela (REA_rodada18 §8: o operador alvo t recebe da fonte j a célula 109 + 6j + t,
    // desvio de fase = 2·célula·saída ciclos). O filtro fica aberto (o modo do filtro não foi localizado).
    int carrier = -1;
    for (int i = 0; i < 6; ++i)
        if (p[25 + i] >= 0.5f && (carrier < 0 || p[103 + i] > p[103 + carrier]))
            carrier = i;
    if (carrier < 0)
        carrier = 0;

    auto steps = [] (float x) { return (double) std::clamp ((int) (198.0f * x - 99.0f), -99, 99); }; // trunc(198x − 99)
    const auto semis = steps (p[31 + carrier]) + steps (p[37 + carrier]) * 0.01 + (double) (int) (48.0f * p[2] - 24.0f);
    auto seconds = [] (float x) { return (float) std::max (50.0 * std::pow ((double) x, 4.0), 0.00101); };

    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    setPitch (s.osc[0], semis);

    int modulator = -1;
    for (int j = 0; j < 6; ++j)
        if (j != carrier && p[25 + j] >= 0.5f && p[109 + 6 * j + carrier] > 0.005f
            && (modulator < 0 || p[109 + 6 * j + carrier] > p[109 + 6 * modulator + carrier]))
            modulator = j;
    if (modulator >= 0)
    {
        const auto cell = std::floor (100.0f * p[109 + 6 * modulator + carrier]) * 0.01f;
        s.osc[2] = SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f };
        setPitch (s.osc[2], steps (p[31 + modulator]) + steps (p[37 + modulator]) * 0.01 + (double) (int) (48.0f * p[2] - 24.0f));
        s.fm = 2.0f * cell * cell;
        // o envelope do modulador escala a saída dele: decay até o sustain (o ataque fica de fora)
        s.fmDecay = seconds (p[79 + modulator]);
        s.fmSustain = p[85 + modulator] * p[85 + modulator];
    }

    s.attack = seconds (p[3]);
    s.decay = seconds (p[4]);
    s.sustain = p[5] * p[5];
    s.release = 0.35f * seconds (p[6]) + 0.02f;
    s.cutoff = 20000.0f;
    gain = 1.0f;
    synth = s;
    return true;
}
// ---- Morphine (REA_rodada16 §3): aditivo de até 4 geradores; cada um vira um oscilador pela forma do
// espectro médio (quadros ponderados por duração × ganho). Envelope, nível e afinação: o bloco mestre ----

namespace
{
std::uint32_t tagAt (const std::uint8_t* d, std::size_t o) { return u32At (d, o); }
constexpr std::uint32_t morTag (const char (&t)[5]) { return (std::uint32_t) t[3] << 24 | (std::uint32_t) t[2] << 16 | (std::uint32_t) t[1] << 8 | (std::uint32_t) t[0]; }

struct MorphineGenerator
{
    std::array<double, 128> energy {}; // energia média de cada harmônico
    double weight = 0.0;
    float mix = 0.0f;
};

// MBP0: 3 vetores de 128 valores em 16 grupos de 8 (máscara + um f32 por bit ligado); o 1º é a amplitude
bool readPartials (const std::vector<std::uint8_t>& d, std::size_t& o, std::size_t end, std::array<float, 128>& amplitude)
{
    if (o + 4 > end || tagAt (d.data(), o) != morTag ("0PBM"))
        return false;
    o += 4;
    for (int a = 0; a < 3; ++a)
        for (int g = 0; g < 16; ++g)
        {
            if (o >= end)
                return false;
            const auto mask = d[o++];
            for (int b = 0; b < 8; ++b)
                if ((mask >> b) & 1)
                {
                    if (o + 4 > end)
                        return false;
                    float v = 0.0f;
                    std::memcpy (&v, d.data() + o, 4);
                    o += 4;
                    if (a == 0)
                        amplitude[(std::size_t) (g * 8 + b)] = std::isfinite (v) ? v : 0.0f;
                }
        }
    return true;
}

bool readGenerator (const std::vector<std::uint8_t>& d, std::size_t o, std::size_t end, MorphineGenerator& gen)
{
    // MVC0 · i32 · MZH0 · i32 zonas · { i32, f32 nível, i32 n, MSP0 } · i32 n · 6 × i32 (o 1º é o Mix, f32)
    if (o + 8 > end || tagAt (d.data(), o) != morTag ("0CVM"))
        return false;
    o += 8;
    if (o + 8 > end || tagAt (d.data(), o) != morTag ("0HZM"))
        return false;
    const auto zones = (int) u32At (d.data(), o + 4);
    o += 8;
    for (int z = 0; z < zones; ++z)
    {
        if (o + 12 > end)
            return false;
        const auto zoneEnd = o + 12 + (std::size_t) u32At (d.data(), o + 8); // inclui o próprio campo de tamanho [DADOS]
        o += 12;
        if (o + 8 > end || tagAt (d.data(), o) != morTag ("0PSM"))
            return false;
        const auto frames = (int) u32At (d.data(), o + 4);
        o += 8;
        for (int f = 0; f < frames; ++f)
        {
            if (o + 12 > end)
                return false;
            const auto duration = (double) (std::int32_t) u32At (d.data(), o);
            float frameGain = 0.0f;
            std::memcpy (&frameGain, d.data() + o + 4, 4);
            const auto next = o + 12 + (std::size_t) u32At (d.data(), o + 8);
            o += 12;
            std::array<float, 128> amplitude {};
            if (! readPartials (d, o, end, amplitude) || o != next)
                return false;
            const auto w = std::max (0.0, duration) * std::abs ((double) frameGain);
            for (std::size_t n = 0; n < 128; ++n)
                gen.energy[n] += w * (double) amplitude[n] * amplitude[n];
            gen.weight += w;
        }
        o = zoneEnd > o ? zoneEnd : o;
        if (o > end)
            return false;
    }
    if (o + 8 > end)
        return false;
    std::memcpy (&gen.mix, d.data() + o + 4, 4);
    gen.mix = std::isfinite (gen.mix) ? std::clamp (gen.mix, 0.0f, 1.0f) : 0.0f;
    return true;
}

// forma de onda pelo espectro: fundamental dominante -> seno; só ímpares -> quadrada (queda lenta) ou
// triângulo (queda rápida, ~1/n²); o resto -> serra
Waveform waveFromSpectrum (const std::array<double, 128>& e)
{
    double total = 0.0, odd = 0.0, peak = 0.0;
    for (std::size_t n = 0; n < e.size(); ++n)
    {
        total += e[n];
        if (n % 2 == 0)
            odd += e[n];
        peak = std::max (peak, e[n]);
    }
    if (total <= 0.0)
        return Waveform::sine;
    int strong = 0;
    for (auto v : e)
        strong += std::sqrt (v / peak) > 0.05 ? 1 : 0;
    if (e[0] / total >= 0.7 && strong <= 6)
        return Waveform::sine;
    if (odd / total >= 0.8)
    {
        // inclinação log-log das amplitudes dos harmônicos ímpares presentes
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        int count = 0;
        for (std::size_t n = 0; n < e.size(); n += 2)
            if (e[n] > peak * 1e-6)
            {
                const auto x = std::log ((double) n + 1.0), y = 0.5 * std::log (e[n]);
                sx += x, sy += y, sxx += x * x, sxy += x * y, ++count;
            }
        const auto slope = count >= 2 ? (count * sxy - sx * sy) / std::max (1e-12, count * sxx - sx * sx) : -1.0;
        return slope >= -1.4 ? Waveform::square : Waveform::triangle;
    }
    return Waveform::saw;
}
} // namespace

bool synthFromMorphine (const std::vector<std::uint8_t>& d, SynthSettings& synth, float& gain)
{
    if (d.size() < 32 || std::memcmp (d.data(), "SSLF", 4) != 0 || tagAt (d.data(), 8) != morTag ("0roM") || tagAt (d.data(), 12) != morTag ("0RPM"))
        return false;

    std::size_t o = 16;
    std::array<MorphineGenerator, 4> gens {};
    for (auto& g : gens)
    {
        if (o + 4 > d.size())
            return false;
        const auto n = (std::size_t) u32At (d.data(), o);
        if (n > d.size() - o - 4 || ! readGenerator (d, o + 4, o + 4 + n, g))
            return false;
        o += 4 + n;
    }
    for (int block = 0; block < 2; ++block) // MML0 (linha de morph) e MMD0 (envelopes de modulação)
    {
        if (o + 8 > d.size())
            return false;
        o += 4 + (std::size_t) u32At (d.data(), o);
    }
    if (o + 4 + 40 * 4 > d.size() || u32At (d.data(), o) != 0x5C4)
        return false;
    float a[40];
    std::memcpy (a, d.data() + o + 4, sizeof (a));
    for (auto& v : a)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;

    // os geradores com som, pelo Mix
    std::vector<int> active;
    for (int g = 0; g < 4; ++g)
        if (gens[(std::size_t) g].weight > 0.0 && std::any_of (gens[(std::size_t) g].energy.begin(), gens[(std::size_t) g].energy.end(), [] (double v) { return v > 0.0; }))
            active.push_back (g);
    std::stable_sort (active.begin(), active.end(), [&] (int x, int y) { return gens[(std::size_t) x].mix > gens[(std::size_t) y].mix; });

    SynthSettings s;
    s.osc = { SynthOscillator { Waveform::sine, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    const auto transpose = (double) (int) ((a[5] - 0.5f) * 48.0f) + (double) (int) ((a[6] - 0.5f) * 200.0f) / 100.0;
    for (std::size_t k = 0; k < active.size() && k < 3; ++k)
    {
        const auto& g = gens[(std::size_t) active[k]];
        s.osc[k] = { waveFromSpectrum (g.energy), 0, 0.0f, std::max (0.05f, g.mix) };
        setPitch (s.osc[k], transpose);
    }
    if (active.empty())
        setPitch (s.osc[0], transpose);

    // envelope mestre (s): A 15·v^3,5, D 30·v³, S v², R 10·v³ (mín. 3 ms)
    s.attack = (float) (15.0 * std::pow ((double) a[9], 3.5));
    s.decay = (float) (30.0 * std::pow ((double) a[10], 3.0));
    s.sustain = a[11] * a[11];
    s.release = (float) std::max (0.003, 10.0 * std::pow ((double) a[12], 3.0));
    s.cutoff = 20000.0f;
    gain = (float) std::pow (10.0, (48.0 * a[0] - 24.0) / 20.0);
    synth = s;
    return true;
}
} // namespace lynx
