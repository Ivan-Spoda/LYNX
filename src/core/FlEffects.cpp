#include "core/FlEffects.h"
#include "core/FlSynths.h"

#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstring>

#include "core/Effects.h"
#include "core/FlpImport.h"
#include "flp/Flp.h"

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

// Lk(x) = (k^x − 1)/(k − 1): as curvas de ganho dos plugins Fruity
double curve (double k, double x)
{
    return (std::pow (k, std::max (0.0, x)) - 1.0) / (k - 1.0);
}

double toDb (double gain)
{
    return 20.0 * std::log10 (std::max (gain, 1.0e-6));
}

// inverso de logRange (lo·(hi/lo)^v)
float fromLog (double value, double lo, double hi)
{
    return (float) std::clamp (std::log (std::max (value, lo) / lo) / std::log (hi / lo), 0.0, 1.0);
}

float clamp01 (double v)
{
    return (float) std::clamp (v, 0.0, 1.0);
}

// Q de um pico/passa-banda pela largura em oitavas (cookbook)
double qFromOctaves (double octaves)
{
    return 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * std::max (0.01, octaves)));
}

// --- Fruity Parametric EQ 2 (REA_rodada7 §1) ------------------------------------------------

bool parametricEq2 (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 36 * 4)
        return false;

    const auto version = i32At (d, 0);
    const auto mask = version >= 4 && d.size() >= 0x142 ? (std::uint32_t) i32At (d, 0x13E) : 0x7Fu;
    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };

    for (int b = 0; b < peq::bands; ++b)
    {
        const auto gainDb = std::clamp (param (b) / 100.0, -18.0, 18.0);
        const auto hz = 20.0 * std::pow (1000.0, std::clamp (param (7 + b), 0, 65536) / 65536.0);
        const auto w = std::clamp (param (14 + b), 0, 65536) / 65536.0;
        const auto type = param (21 + b);
        const auto order = std::clamp (param (28 + b), -3, 3);
        const auto on = (mask & (1u << b)) != 0;

        peq::BandType t = peq::BandType::off;
        double q = 1.0;

        switch (on ? type : 0)
        {
            case 1: t = peq::BandType::highCut; q = 0.1 + 9.9 * std::pow (1.0 - w, 3.0); break;
            case 2: t = peq::BandType::bandPass; q = qFromOctaves ((0.1 + 4.9 * w) * 0.708 * std::sqrt (std::abs (order) + 1.0)); break;
            case 3: t = peq::BandType::lowCut; q = 0.1 + 9.9 * std::pow (1.0 - w, 3.0); break;
            case 4: t = peq::BandType::notch; q = qFromOctaves ((0.1 + 4.9 * w) / (0.708 * std::sqrt (std::abs (order) + 1.0))); break;
            case 5: t = peq::BandType::lowShelf; q = 1.0 / 3.0 + 1.7193 * (1.0 - w); break; // Q = inclinação S
            case 6: t = peq::BandType::peak; q = qFromOctaves (0.1 + 4.9 * w); break;
            case 7: t = peq::BandType::highShelf; q = 1.0 / 3.0 + 1.7193 * (1.0 - w); break;
            default: break;
        }

        slot.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (t);
        slot.params[(std::size_t) peq::param (b, 1)] = peq::hzValue (hz);
        slot.params[(std::size_t) peq::param (b, 2)] = peq::gainValue (gainDb);
        slot.params[(std::size_t) peq::param (b, 3)] = peq::qValue (q);
        // ordem: |ordem| + 1 biquads (os cortes "Steep 8" = 4)
        slot.params[(std::size_t) (peq::slopeParam + b)] = peq::slopeValue (std::abs (order) + 1);
    }

    slot.params[peq::outputParam] = peq::gainValue (std::clamp (param (35) / 100.0, -18.0, 18.0));
    return true;
}

// --- Fruity Limiter (§2): limiter, ou o compressor dele quando só o compressor trabalha -------

bool limiter (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 18 * 4)
        return false;

    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    auto timeMs = [] (int v) { return 2.0 * (std::pow (501.0, std::clamp (v, 0, 10000) / 10000.0) - 1.0); };
    const auto gainDb = toDb (curve (7.0, param (0) / 1000.0));
    const auto ceilingDb = toDb (curve (3.0, param (2) / 1000.0));
    const auto compThresholdDb = toDb (curve (7.0, param (8) / 1000.0));
    const auto ratioRaw = param (9);
    const auto ratio = ratioRaw > 0 ? 1.0 + 0.38 * (std::pow (51.0, ratioRaw / 1000.0) - 1.0) : 1.0;

    // teto acima de 0 dB praticamente desliga o limitador; aí o que soa é o compressor
    if (ceilingDb > -0.1 && ratio > 1.05 && compThresholdDb < -0.5)
    {
        slot = makeEffectSlot (EffectKind::compressor);
        slot.params[0] = clamp01 ((compThresholdDb + 60.0) / 60.0);
        slot.params[1] = clamp01 ((std::min (ratio, 20.0) - 1.0) / 19.0);
        slot.params[2] = fromLog (std::max (0.1, timeMs (param (11))), 0.1, 100.0);
        slot.params[3] = fromLog (2.0 * timeMs (param (12)), 10.0, 1000.0);
        slot.params[4] = clamp01 (std::max (0.0, gainDb) / 24.0);
        return true;
    }

    slot = makeEffectSlot (EffectKind::limiter);
    slot.params[0] = clamp01 ((std::min (ceilingDb, 0.0) + 12.0) / 12.0);
    slot.params[1] = clamp01 (std::max (0.0, gainDb) / 24.0);
    slot.params[2] = fromLog (2.0 * timeMs (param (5)), 10.0, 1000.0);
    return true;
}

// --- Fruity Reeverb 2 (§3) --------------------------------------------------------------------

bool reeverb2 (const std::vector<std::uint8_t>& d, EffectSlot& slot, double bpm)
{
    const auto version = i32At (d, 0);
    const auto count = version == 10001 ? 15 : version == 10000 ? 13 : 0;

    if (count == 0 || d.size() < 4 + (std::size_t) count * 4)
        return false;

    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    const auto decay = std::clamp (param (5) / 10.0, 0.1, 20.0);
    const auto dampingKhz = param (6) >= 221 ? 22.0 : std::clamp (param (6) / 10.0, 0.5, 22.0);
    const auto separation = std::clamp (param (9) / 64.0, -1.0, 1.0);
    const auto dry = curve (11.0, param (10) / 128.0);
    const auto wet = curve (11.0, param (12) / 256.0);

    // o Freeverb do LYNX: realimentação dos combs para o decaimento pedido (combs de ~30 ms)
    const auto feedback = std::pow (10.0, -3.0 * 0.03 / decay);
    slot.params[0] = clamp01 ((feedback - 0.7) / 0.28);
    slot.params[1] = clamp01 (1.0 - std::log (dampingKhz / 0.5) / std::log (22.0 / 0.5)); // abafa mais com o corte mais baixo
    slot.params[2] = clamp01 (1.0 + std::min (0.0, separation));                           // separação negativa estreita
    slot.params[3] = clamp01 (wet / 0.25);
    slot.params[4] = clamp01 (dry);

    // corte de graves (< 20 Hz = desligado), de agudos (>= 22,1 kHz = desligado) e predelay
    const auto lowCutHz = param (0);
    slot.params[5] = lowCutHz < 20 ? 0.0f : std::max (0.002f, fromLog (lowCutHz, 20.0, 3000.0));
    slot.params[6] = param (1) >= 221 ? 1.0f : std::min (0.998f, fromLog (param (1) * 100.0, 500.0, 22000.0));
    const auto tempoPredelay = d.size() > 4 + (std::size_t) count * 4 && d[4 + (std::size_t) count * 4] != 0;
    const auto predelayMs = tempoPredelay ? param (2) / 48.0 * 60000.0 / std::max (1.0, bpm) : param (2) * 1000.0 / 384.0;
    slot.params[7] = clamp01 (predelayMs / 1000.0);
    return true;
}

// --- Fruity Delay 2 e Delay 3 (§4) ---------------------------------------------------------------

float delayTimeValue (double beats)
{
    // a divisão do delay interno mais próxima (0,25 … 4 tempos, 8 divisões)
    constexpr double divisions[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
    int best = 0;

    for (int i = 1; i < 8; ++i)
        if (std::abs (std::log (divisions[i] / beats)) < std::abs (std::log (divisions[best] / beats)))
            best = i;

    return ((float) best + 0.5f) / 8.0f;
}

bool delay2 (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 32)
        return false;

    slot.params[0] = delayTimeValue (std::clamp (i32At (d, 0x10), 1, 768) / 48.0);
    slot.params[1] = clamp01 (i32At (d, 0x0C) / 127.0 * 0.9);
    slot.params[2] = clamp01 (curve (11.0, i32At (d, 0x04) / 128.0) * 0.5);
    slot.params[3] = i32At (d, 0x18) == 2 ? 1.0f : 0.0f;
    slot.params[4] = clamp01 (1.0 - i32At (d, 0x1C) / 128.0);
    return true;
}

bool delay3 (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 26 * 4)
        return false;

    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    const auto synced = param (2) != 0;
    // sincronizado: ticks de 1/96 de tempo; sem sincronia a unidade não é conhecida: fica 1/4
    slot.params[0] = delayTimeValue (synced ? std::clamp (param (4), 1, 768) / 96.0 : 1.0);
    slot.params[1] = clamp01 (param (14) / 6000.0 * 0.9);
    slot.params[2] = clamp01 (param (23) / 6000.0 * 0.5);
    slot.params[3] = param (1) == 2 ? 1.0f : 0.0f;
    slot.params[4] = clamp01 (1.0 - param (16) / 6000.0);
    return true;
}

// --- Fruity Compressor (§5) ----------------------------------------------------------------------

bool compressor (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 6 * 4)
        return false;

    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    slot.params[0] = clamp01 ((std::clamp (param (0) / 10.0, -60.0, 0.0) + 60.0) / 60.0);
    slot.params[1] = clamp01 ((std::clamp (param (1) / 10.0, 1.0, 20.0) - 1.0) / 19.0);
    slot.params[4] = clamp01 (std::clamp (param (2) / 10.0, 0.0, 24.0) / 24.0);
    slot.params[2] = fromLog (std::max (0.1, param (3) / 10.0), 0.1, 100.0);
    slot.params[3] = fromLog (std::max (10.0, (double) param (4)), 10.0, 1000.0);
    return true;
}
// --- REA_rodada8: Maximus, Soundgoodizer, Fast Dist, Fruity Filter, Flangus, Soft Clipper -------

double timeMs (int v) // T(x) = 2·(501^x − 1) ms
{
    return 2.0 * (std::pow (501.0, std::clamp (v, 0, 10000) / 10000.0) - 1.0);
}

float compressorAttackValue (double ms) { return fromLog (std::max (0.1, ms), 0.1, 100.0); }
float compressorReleaseValue (double ms) { return fromLog (std::max (10.0, ms), 10.0, 1000.0); }
float limiterReleaseValue (double ms) { return fromLog (std::max (10.0, ms), 10.0, 1000.0); }

float utilityGainValue (double gain);

bool maximus (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 59 * 4)
        return false;

    const auto header = (std::uint32_t) i32At (d, 0);
    const auto version = header > 0xFFFFu ? (int) (header >> 16) : (int) header;
    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };

    // a seção master (banda 3) representa o efeito; as bandas são multibanda (o LYNX não tem)
    constexpr int master = 7 + 13 * 3;
    const auto mode = param (master);
    const auto preDb = toDb (curve (7.0, param (master + 1) / 1000.0));
    const auto postDb = toDb (curve (7.0, param (master + 2) / 1000.0));

    if (mode == 3) // master desligado: passa direto (utilitário transparente mantém a posição do slot)
    {
        slot = makeEffectSlot (EffectKind::utility);
        return true;
    }

    if (mode != 0) // sem compressão: só os ganhos
    {
        slot = makeEffectSlot (EffectKind::utility);
        slot.params[0] = utilityGainValue (std::pow (10.0, (preDb + postDb) / 20.0));
        return true;
    }

    // curva de compressão do master: 7º envelope (low, 2º, mid, 2º, high, 2º, master, 2º)
    std::vector<std::pair<double, double>> points;
    std::size_t pos = version >= 15 ? 0x21F : version >= 14 ? 0x21B : 0x1FE;

    for (int env = 0; env < 8 && pos + 12 <= d.size(); ++env)
    {
        const auto count = (std::size_t) std::max (0, i32At (d, pos + 8));

        if (count > 512 || pos + 12 + 24 * count + 20 > d.size())
            break;

        if (env == 6)
        {
            double x = 0.0;

            for (std::size_t k = 0; k < count; ++k)
            {
                double dx = 0.0, y = 0.0;
                std::memcpy (&dx, d.data() + pos + 12 + 24 * k, 8);
                std::memcpy (&y, d.data() + pos + 12 + 24 * k + 8, 8);
                x += dx;
                if (std::isfinite (x) && std::isfinite (y))
                    points.push_back ({ std::clamp (x, 0.0, 1.0), std::clamp (y, 0.0, 1.0) });
            }
        }

        pos += 12 + 24 * count + 20;
    }

    if (points.size() < 2)
        points = { { 0.0, 0.0 }, { 0.5, 0.5 }, { 1.0, 0.5 } }; // a curva padrão: limitador em −0,22 dB

    auto curveAt = [&] (double c) {
        if (c <= points.front().first)
            return points.front().second;
        for (std::size_t k = 1; k < points.size(); ++k)
            if (c <= points[k].first)
            {
                const auto span = points[k].first - points[k - 1].first;
                const auto t = span > 0.0 ? (c - points[k - 1].first) / span : 1.0;
                return points[k - 1].second + t * (points[k].second - points[k - 1].second);
            }
        return points.back().second;
    };
    auto levelDb = [] (double c, double scale) { return toDb ((std::pow (3.0, 2.0 * c) - 1.0) / 2.0 * scale); };

    // teto, joelho e ratio da curva (nível de entrada c: 0,5 = 0 dBFS, 1 = +12 dB)
    double ceilingDb = -120.0, kneeDb = 0.0, kneeOut = 0.0;
    bool knee = false;

    for (int k = 1; k <= 400; ++k)
    {
        const auto c = k / 400.0;
        const auto in = levelDb (c, 1.0), out = levelDb (curveAt (c), 0.975);
        ceilingDb = std::max (ceilingDb, out);

        if (! knee && out < in - 0.5)
        {
            knee = true;
            kneeDb = in;
            kneeOut = out;
        }
    }

    const auto topIn = levelDb (1.0, 1.0), topOut = levelDb (curveAt (1.0), 0.975);
    const auto ratio = knee && topOut - kneeOut > 0.05 ? (topIn - kneeDb) / (topOut - kneeOut) : 100.0;

    if (knee && kneeDb < -0.5 && ratio < 8.0) // compressão de verdade (joelho abaixo de 0 dB)
    {
        slot = makeEffectSlot (EffectKind::compressor);
        slot.params[0] = clamp01 ((kneeDb + 60.0) / 60.0);
        slot.params[1] = clamp01 ((std::clamp (ratio, 1.0, 20.0) - 1.0) / 19.0);
        slot.params[2] = compressorAttackValue (timeMs (param (master + 6)));
        slot.params[3] = compressorReleaseValue (2.0 * timeMs (param (master + 9)));
        slot.params[4] = clamp01 (std::max (0.0, preDb + postDb) / 24.0);
        return true;
    }

    slot = makeEffectSlot (EffectKind::limiter);
    slot.params[0] = clamp01 ((std::min (ceilingDb + postDb, 0.0) + 12.0) / 12.0);
    slot.params[1] = clamp01 (std::max (0.0, preDb) / 24.0);
    slot.params[2] = limiterReleaseValue (2.0 * timeMs (param (master + 9)));
    return true;
}

bool soundgoodizer (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 12)
        return false;

    // preset A–D (cópias completas do Maximus); aproximação de compressor medida com amount 0,6
    struct Preset { double threshold, ratio, makeup; };
    constexpr Preset presets[] = { { -6.0, 2.5, 5.3 }, { -10.5, 2.2, 6.6 }, { -10.8, 2.5, 7.9 }, { -14.0, 1.9, 8.5 } };
    const auto& p = presets[std::clamp (i32At (d, 4), 0, 3)];
    const auto amount = std::clamp (i32At (d, 8), 0, 1000) / 1000.0;
    const auto scale = amount / 0.6;

    slot = makeEffectSlot (EffectKind::compressor);
    slot.params[0] = clamp01 ((p.threshold + 60.0) / 60.0);
    slot.params[1] = clamp01 ((1.0 + (p.ratio - 1.0) * scale - 1.0) / 19.0);
    slot.params[2] = compressorAttackValue (2.0);
    slot.params[3] = compressorReleaseValue (85.0);
    slot.params[4] = clamp01 (p.makeup * scale / 24.0);
    return true;
}

bool fastDist (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 20)
        return false;

    const auto pre = curve (11.0, std::clamp (i32At (d, 0), 64, 192) / 128.0);
    const auto t = std::clamp (i32At (d, 4), 1, 10);
    const auto typeB = i32At (d, 8) != 0;
    const auto mix = curve (11.0, std::clamp (i32At (d, 12), 0, 128) / 128.0);
    const auto post = curve (11.0, std::clamp (i32At (d, 16), 0, 128) / 128.0);

    // drive equivalente do tanh para a curva do FL (ajuste da REA_rodada8 §3.2)
    constexpr double driveA[] = { 0, 0, 0, 0, 2.2, 4.5, 6.8, 9.2, 12.8, 17.8, 22.8 };
    const auto curveDb = typeB ? std::max (0.0, 0.8 + (t - 6) * 0.85) * (t >= 6 ? 1.0 : 0.0) : driveA[t];
    const auto wet = mix * post;
    const auto dry = (post - wet) * pre * post; // o post entra ao quadrado no seco (assim no FL)
    const auto level = typeB ? 1.0 : std::pow (10.0, -0.9 * (t - 1) / 20.0);
    const auto total = wet * level + dry;

    slot.params[0] = clamp01 ((toDb (pre) + curveDb) / 36.0);
    slot.params[1] = 1.0f; // o Fast Dist não filtra
    slot.params[2] = total > 1.0e-6 ? clamp01 (wet * level / total) : 1.0f;
    slot.params[3] = clamp01 (0.5 + toDb (std::max (total, 1.0e-6)) / 24.0);
    return true;
}

bool fruityFilter (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 6 * 4)
        return false;

    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    const auto lp = curve (11.0, std::clamp (param (2), 0, 1024) / 1024.0);
    const auto bp = curve (11.0, std::clamp (param (3), 0, 1024) / 1024.0);
    const auto hp = curve (11.0, std::clamp (param (4), 0, 1024) / 1024.0);

    // tipo do filtro interno: 0 passa-baixas, 1 passa-altas, 2 passa-banda (floor(3v))
    auto type = 0.0f;
    if (hp > lp && hp >= bp && ! (lp >= 0.5 && hp >= 0.5))
        type = 0.4f;
    else if (bp > lp && bp > hp)
        type = 0.7f;

    // SVF de Chamberlin sem correção de taxa: "aberto" ainda corta em ~7,35 kHz (a 44,1 kHz)
    const auto f = std::min (1.0, (std::pow (26.0, std::clamp (param (0), 0, 1024) / 1024.0) - 1.0) * 0.04);
    const auto hz = std::max (20.0, 44100.0 / 3.141592653589793 * std::asin (f / 2.0));
    auto q = 1.0 / (1.0 - 0.8 * std::clamp (param (1), 0, 1024) / 1024.0);
    if (param (5) != 0)
        q *= 1.3; // x2: segundo estágio em série

    slot.params[0] = type;
    slot.params[1] = fromLog (hz, 20.0, 20000.0);
    slot.params[2] = fromLog (q, 0.5, 12.0);
    slot.params[3] = 1.0f;
    return true;
}

bool flangus (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 32)
        return false;

    auto param = [&] (int i) { return i32At (d, 4 * (std::size_t) i); };
    const auto depthMs = 24.8 * curve (11.0, std::clamp (param (1), 0, 1024) / 1024.0);
    const auto voiceDepth = depthMs * 44.1 + 10.0; // amostras a 44,1 kHz
    const auto period = 75000.0 * (std::pow (101.0, 1.0 - std::clamp (param (2), 0, 1024) / 1024.0) - 1.0) + 50000.0;
    const auto hz = 250.0 * 44100.0 / (period * voiceDepth);
    const auto dryGain = curve (11.0, std::abs (std::clamp (param (6), -128, 128)) / 128.0);
    const auto wetGain = curve (11.0, std::abs (std::clamp (param (7), -128, 128)) / 128.0);
    const auto r = dryGain > 1.0e-6 ? wetGain / dryGain : 1000.0;

    slot.params[0] = fromLog (std::max (0.05, hz), 0.05, 5.0);
    slot.params[1] = clamp01 (2.0 * depthMs / 8.0);
    slot.params[2] = clamp01 (2.0 * r / (1.0 + r));
    slot.params[3] = 0.0f; // o Flangus não realimenta
    return true;
}

bool softClipper (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 8)
        return false;

    const auto t = std::clamp (i32At (d, 0), 1, 127);
    const auto postDb = toDb (curve (11.0, std::clamp (i32At (d, 4), 0, 160) / 128.0));
    const auto drop = t >= 100 ? 1.5 : t >= 64 ? 2.0 - (t - 64) / 36.0 * 0.5 : 3.8 - (t - 1) / 63.0 * 1.8; // queda de nível da curva

    slot.params[0] = 0.0f;
    slot.params[1] = 1.0f;
    slot.params[2] = 1.0f;
    slot.params[3] = clamp01 (0.5 + (postDb - drop) / 24.0);
    return true;
}

// --- Balance, PanOMatic, Stereo Shaper (§6) -> Utilitário ---------------------------------------

float utilityGainValue (double gain) { return clamp01 (toDb (gain) / 48.0 + 0.5); }
float utilityMatrixValue (double m) { return clamp01 ((m + 2.0) / 4.0); }

bool balance (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 8)
        return false;

    slot.params[1] = clamp01 (std::clamp (i32At (d, 0), -128, 128) / 256.0 + 0.5);
    slot.params[0] = utilityGainValue (curve (11.0, i32At (d, 4) / 256.0)); // 256 = 0 dB
    return true;
}

bool panOMatic (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 8)
        return false;

    slot.params[1] = clamp01 (std::clamp (i32At (d, 0), 0, 256) / 256.0); // 128 = centro
    slot.params[0] = utilityGainValue (curve (11.0, i32At (d, 4) / 256.0));
    return true; // o LFO do PanOMatic (ligado em poucos demos) fica de fora
}

bool stereoShaper (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 4 * 4)
        return false;

    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i) / 12800.0; }; // 12800 = 1,0
    slot.params[3] = utilityMatrixValue (param (0)); // R -> L
    slot.params[2] = utilityMatrixValue (param (1)); // L
    slot.params[5] = utilityMatrixValue (param (2)); // R
    slot.params[4] = utilityMatrixValue (param (3)); // L -> R
    return true;
}
// --- REA_rodada14: Gross Beat (volume), Frequency Splitter, Effector ----------------------------

// Gross Beat: o slot de volume escolhido (envelope de 4 tempos) vira os 32 passos do gate; o
// "time travel" não tem equivalente. Sem estado legível, o gate fica transparente.
bool grossBeat (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    for (int i = 0; i < 32; ++i)
        slot.params[(std::size_t) i] = 1.0f;

    if (d.size() < 64)
        return false;
    auto version = (std::uint32_t) i32At (d, 0);
    if (version > 0xffff)
        version >>= 16;
    if (version < 3 || version > 9)
        return false;

    const auto paramCount = 9;
    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    std::size_t pos = 4 + 4 * paramCount + (version >= 5 ? 9 : 8);
    const auto volumeSlot = 36 + std::clamp (param (1), 0, 35);

    // os 72 slots (36 de tempo + 36 de volume): nome, 15 B de sincronismo, envelope
    std::vector<std::pair<double, double>> chosen; // (x acumulado em tempos, y)
    std::vector<int> curves;
    for (int s = 0; s < 72; ++s)
    {
        if (pos >= d.size())
            return false;
        pos += 1 + d[pos] + 15;
        if (pos + 12 > d.size())
            return false;
        const auto n = i32At (d, pos + 8);
        if (n < 0 || n > 5000 || pos + 12 + (std::size_t) n * 24 + 20 > d.size())
            return false;
        pos += 12;
        double x = 0.0;
        for (int k = 0; k < n; ++k, pos += 24)
            if (s == volumeSlot)
            {
                double dx = 0.0, y = 0.0;
                std::memcpy (&dx, d.data() + pos, 8);
                std::memcpy (&y, d.data() + pos + 8, 8);
                x += std::isfinite (dx) ? std::max (0.0, dx) : 0.0;
                chosen.emplace_back (x, std::isfinite (y) ? std::clamp (y, 0.0, 1.0) : 1.0);
                curves.push_back (d[pos + 20]);
            }
        pos += 20;
    }

    if (chosen.empty())
        return true;

    // valor no meio de cada passo; "hold" (2) segura o ponto anterior, o resto vira reta
    for (int i = 0; i < 32; ++i)
    {
        const auto t = (i + 0.5) / 32.0 * 4.0;
        auto value = chosen.back().second;
        if (t <= chosen.front().first)
            value = chosen.front().second;
        else
            for (std::size_t k = 1; k < chosen.size(); ++k)
                if (t < chosen[k].first)
                {
                    const auto [x0, y0] = chosen[k - 1];
                    const auto [x1, y1] = chosen[k];
                    value = curves[k] == 2 || x1 <= x0 ? y0 : y0 + (y1 - y0) * (t - x0) / (x1 - x0);
                    break;
                }
        slot.params[(std::size_t) i] = (float) value;
    }

    slot.params[32] = clamp01 (param (3) / 256.0);
    slot.params[33] = clamp01 (param (4) / 10000.0);
    slot.params[34] = clamp01 (param (5) / 10000.0);
    return true;
}

// Frequency Splitter: a saída principal com nível por banda vira prateleiras no EQ paramétrico
bool frequencySplitter (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 15 * 4)
        return false;
    auto word = [&] (int i) { return i32At (d, 4 * (std::size_t) i); };
    auto hz = [] (int v) { return 30.0 * std::pow (16000.0 / 30.0, std::clamp (v, 0, 65536) / 65536.0); };
    auto db = [] (int v) { return std::clamp (v / 100.0, -18.0, 18.0); };
    const auto threeBands = word (6) != 0;
    const auto low = db (word (12)), mid = db (word (13)), high = db (word (14));

    for (int b = 0; b < peq::bands; ++b)
        slot.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (peq::BandType::off);

    auto shelf = [&] (int band, peq::BandType type, double f, double gain) {
        slot.params[(std::size_t) peq::param (band, 0)] = peq::typeValue (type);
        slot.params[(std::size_t) peq::param (band, 1)] = peq::hzValue (f);
        slot.params[(std::size_t) peq::param (band, 2)] = peq::gainValue (gain);
        slot.params[(std::size_t) peq::param (band, 3)] = peq::qValue (1.0);
    };

    if (threeBands)
    {
        shelf (0, peq::BandType::lowShelf, hz (word (1)), low - mid);
        shelf (6, peq::BandType::highShelf, hz (word (2)), high - mid);
        slot.params[peq::outputParam] = peq::gainValue (mid);
    }
    else
    {
        shelf (0, peq::BandType::lowShelf, hz (word (1)), low - mid);
        slot.params[peq::outputParam] = peq::gainValue (mid);
    }
    return true;
}

// Peak Controller (§7.3): base, quantidade, tensão e velocidade de queda
bool peakController (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 40)
        return false;
    slot.params[0] = clamp01 (i32At (d, 0) / 65536.0);
    slot.params[1] = clamp01 ((std::clamp (i32At (d, 4), -256, 256) / 256.0 + 1.0) / 2.0);
    slot.params[2] = clamp01 ((std::clamp (i32At (d, 8), -128, 128) / 128.0 + 1.0) / 2.0);
    slot.params[3] = clamp01 ((std::clamp (i32At (d, 12), 5, 128) - 5) / 123.0);
    slot.params[4] = clamp01 (i32At (d, 16) / 65536.0); // base do LFO
    return true;
}

// --- REA_rodada15: Pitch Shifter, Frequency Shifter, Emphasis, Low Lifter, Send, Transporter -----

bool pitchShifter (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 13 * 4)
        return false;
    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    const auto cents = std::clamp (param (0), -1200, 1200);
    const auto semis = (int) std::lround (cents / 100.0);
    slot.params[0] = clamp01 (0.5 + semis / 48.0);
    slot.params[1] = clamp01 (0.5 + (cents - semis * 100) / 200.0);
    slot.params[2] = clamp01 (std::clamp (param (3), 0, 2400) / 2400.0);
    const auto granular = d.size() >= 65 && i32At (d, 61) == 1;
    const auto grainMs = granular ? 20.0 + 130.0 * (std::clamp (param (4), 2000, 15000) - 2000) / 13000.0 : 60.0;
    slot.params[3] = clamp01 ((grainMs - 20.0) / 100.0);
    return true;
}

bool frequencyShifter (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 10 * 4)
        return false;
    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    const auto s = std::clamp (param (2), -70000, 70000) / 70000.0;
    const auto k = param (8) == 0 ? 17.6096 : 10.9658; // faixa 20 kHz / 200 Hz (a "Tempo" fica como 200 Hz)
    const auto hz = s == 0.0 ? 0.0 : (s < 0.0 ? -1.0 : 1.0) * 0.1 * std::pow (2.0, k * std::abs (s));
    // a nossa escala: 2000·x·|x| Hz
    const auto x = std::clamp ((hz < 0.0 ? -1.0 : 1.0) * std::sqrt (std::min (1.0, std::abs (hz) / 2000.0)), -1.0, 1.0);
    slot.params[0] = clamp01 (0.5 + x / 2.0);
    slot.params[1] = clamp01 (std::clamp (param (0), 0, 10000) / 10000.0);
    slot.params[2] = 0.0f;
    return true;
}

bool emphasis (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 13 * 4)
        return false;
    auto x = [&] (int i) { return std::clamp (i32At (d, 4 + 4 * (std::size_t) i), 0, 10000) / 10000.0; };
    const auto ceilingDb = toDb (curve (11.0, x (9)));
    slot.params[0] = clamp01 ((std::min (ceilingDb, 0.0) + 12.0) / 12.0);
    slot.params[1] = clamp01 (x (6)); // ganho de entrada 24x dB = a nossa escala de 0..24 dB
    slot.enabled = i32At (d, 4) == 0;
    return true;
}

bool lowLifter (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 15 * 4)
        return false;
    auto x = [&] (int i) { return std::clamp (i32At (d, 4 + 4 * (std::size_t) i), 0, 10000) / 10000.0; };
    auto hz = [] (double v) { return 20.0 + 100.0 * v * v; };
    const auto mix = x (1);
    const auto lowDb = std::clamp ((x (2) - 0.5) * 24.0, -18.0, 12.0);
    const auto highDb = std::clamp (100.0 * std::log10 (std::max (1e-3, x (9))), -18.0, 0.0);
    // o gerador de harmônicos (Chebyshev até a 3ª ordem, REA_rodada17 §13) não tem equivalente aqui; o EQ
    // reproduz só o corte de graves e os ganhos das bandas
    const auto boost = 0.0;

    for (int b = 0; b < peq::bands; ++b)
        slot.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (peq::BandType::off);
    if (hz (x (11)) > 21.0)
    {
        slot.params[(std::size_t) peq::param (0, 0)] = peq::typeValue (peq::BandType::lowCut);
        slot.params[(std::size_t) peq::param (0, 1)] = peq::hzValue (hz (x (11)));
        slot.params[(std::size_t) peq::param (0, 3)] = peq::qValue (0.707);
    }
    slot.params[(std::size_t) peq::param (1, 0)] = peq::typeValue (peq::BandType::lowShelf);
    slot.params[(std::size_t) peq::param (1, 1)] = peq::hzValue (hz (x (10)));
    slot.params[(std::size_t) peq::param (1, 2)] = peq::gainValue (std::clamp ((lowDb - highDb) * mix + boost, -18.0, 18.0));
    slot.params[(std::size_t) peq::param (1, 3)] = peq::qValue (1.0);
    slot.params[peq::outputParam] = peq::gainValue (highDb * mix);
    slot.enabled = i32At (d, 4) != 0;
    return true;
}

bool fruitySend (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 12)
        return false;
    slot.params[0] = utilityGainValue (std::clamp (i32At (d, 8), 0, 256) / 256.0); // o que segue na trilha
    slot.params[1] = 0.5f;
    return true;
}

// Squeeze: quantização (poucos bits) + filtro; aproximado por distorção leve, com o mix dele
bool squeeze (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 4 + 12 * 4)
        return false;
    auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    const auto levels = std::pow (32768.0, 1.0 - std::clamp (param (2), 1, 32768) / 32768.0);
    slot.params[0] = clamp01 (0.6 * (1.0 - std::log2 (std::max (2.0, levels)) / 15.0)); // menos níveis, mais "sujeira"
    slot.params[1] = param (3) != 0 ? clamp01 (std::clamp (param (6), 0, 65536) / 65536.0) : 0.8f;
    slot.params[2] = 1.0f;
    slot.params[3] = 0.5f;
    slot.mix = clamp01 (std::clamp (param (1), 0, 128) / 128.0);
    return true;
}

bool transporter (const std::vector<std::uint8_t>& d, EffectSlot& slot)
{
    if (d.size() < 8)
        return false;
    slot.params[0] = utilityGainValue (curve (11.0, std::clamp (i32At (d, 4), 0, 16000) / 12800.0)); // dry level
    return true;
}

// Effector: o efeito Effector do LYNX (core/Effector.h) recebe os 12 parâmetros crus do FL
std::optional<EffectSlot> effector (const std::vector<std::uint8_t>& d, double)
{
    // "SSLF" + tamanho + "10XF" + 64 floats reservados + os 12 parâmetros
    if (d.size() < 12 + 76 * 4 || std::memcmp (d.data(), "SSLF", 4) != 0)
        return std::nullopt;
    float p[12];
    std::memcpy (p, d.data() + 12 + 64 * 4, sizeof (p));
    auto slot = makeEffectSlot (EffectKind::effector);
    for (int i = 0; i < 12; ++i)
        slot.params[(std::size_t) i] = std::isfinite (p[i]) ? std::clamp (p[i], 0.0f, 1.0f) : 0.0f;
    slot.enabled = slot.params[1] == 0.0f; // bypass = qualquer valor ≠ 0
    slot.params[1] = 0.0f;
    slot.mix = 1.0f; // a mistura de cada algoritmo é feita dentro do efeito (Wet Mix = parâmetro 2)
    return slot;
}

// Fruity Vocoder (REA_rodada20 §4.1.1): i32 versão (2), i32 bandas, i32 ordem, i32 listen, u8 troca, N f32 níveis,
// 12 i32 parâmetros crus
std::optional<EffectSlot> fruityVocoder (const std::vector<std::uint8_t>& d, std::vector<std::string>* notes)
{
    if (d.size() < 17 || i32At (d, 0) < 2)
        return std::nullopt;
    const auto n = i32At (d, 4);
    if (n < 4 || n > 128 || d.size() < 17 + (std::size_t) n * 4 + 12 * 4)
        return std::nullopt;
    auto slot = makeEffectSlot (EffectKind::vocoder);
    int index = 0;
    while ((4 << index) < n && index < 5)
        ++index;
    slot.params[0] = (float) index / 5.0f;
    slot.params[1] = (float) (std::clamp (i32At (d, 8), 1, 3) - 1) / 2.0f;
    slot.params[2] = (float) std::clamp (i32At (d, 12), 0, 2) / 2.0f;
    slot.params[3] = d[16] != 0 ? 1.0f : 0.0f;
    bool levelsFlat = true;
    for (int k = 0; k < n; ++k)
    {
        float level = 1.0f;
        std::memcpy (&level, d.data() + 17 + 4 * (std::size_t) k, 4);
        levelsFlat = levelsFlat && std::abs (level - 1.0f) < 1e-4f;
    }
    if (! levelsFlat && notes != nullptr)
        notes->push_back ("Fruity Vocoder: níveis por banda não aplicados");
    auto param = [&] (int t) { return i32At (d, 17 + 4 * (std::size_t) n + 4 * (std::size_t) t); };
    slot.params[4] = (float) clamp01 (param (0) / 65536.0);
    slot.params[5] = (float) clamp01 (param (1) / 65536.0);
    slot.params[6] = (float) clamp01 (param (2) / 128.0);
    slot.params[7] = param (3) != 0 ? 1.0f : 0.0f;
    slot.params[8] = (float) clamp01 ((std::clamp (param (4), -1200, 1200) + 1200) / 2400.0);
    slot.params[9] = (float) clamp01 ((std::clamp (param (5), 5, 200) - 5) / 195.0);
    slot.params[10] = (float) clamp01 ((std::clamp (param (6), 10, 1000) - 10) / 990.0);
    slot.params[11] = (float) clamp01 ((std::clamp (param (7), 10, 1000) - 10) / 990.0);
    slot.params[12] = param (8) != 0 ? 1.0f : 0.0f;
    slot.params[13] = (float) clamp01 (param (9) / 160.0);
    slot.params[14] = (float) clamp01 (param (10) / 160.0);
    slot.params[15] = (float) clamp01 (param (11) / 160.0);
    return slot;
}
} // namespace

std::optional<EffectSlot> effectFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state, double bpm)
{
    if (pluginName == "Effector")
        return effector (state, bpm);
    if (pluginName == "Fruity Vocoder")
        return fruityVocoder (state, nullptr);

    const auto kind = effectForFlPlugin (pluginName);

    if (! kind)
        return std::nullopt;

    auto slot = makeEffectSlot (*kind);

    // compara sem caixa (presets do FL 3: "Fruity delay 2")
    std::string lower = pluginName;
    std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    auto is = [&] (const char* canonical) {
        std::string other = canonical;
        std::transform (other.begin(), other.end(), other.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return lower == other;
    };

    if (*kind == EffectKind::filter)
        slot.params[1] = 1.0f; // aberto: sem os ajustes do FL, melhor não abafar

    if (is ("Fruity Soft Clipper"))
        softClipper (state, slot);

    if (is ("Fruity Parametric EQ 2"))
        parametricEq2 (state, slot);
    else if (is ("Fruity Limiter"))
        limiter (state, slot);
    else if (is ("Fruity Reeverb 2"))
        reeverb2 (state, slot, bpm);
    else if (is ("Fruity Delay 2"))
        delay2 (state, slot);
    else if (is ("Fruity Delay 3"))
        delay3 (state, slot);
    else if (is ("Fruity Compressor"))
        compressor (state, slot);
    else if (is ("Fruity Balance"))
        balance (state, slot);
    else if (is ("Fruity PanOMatic"))
        panOMatic (state, slot);
    else if (is ("Fruity Stereo Shaper"))
        stereoShaper (state, slot);
    else if (is ("Maximus"))
        maximus (state, slot);
    else if (is ("Soundgoodizer"))
        soundgoodizer (state, slot);
    else if (is ("Fruity Fast Dist"))
        fastDist (state, slot);
    else if (is ("Fruity Filter"))
        fruityFilter (state, slot);
    else if (is ("Fruity Flangus"))
        flangus (state, slot);
    else if (is ("Gross Beat"))
        grossBeat (state, slot);
    else if (is ("Fruity Peak Controller"))
        peakController (state, slot);
    else if (is ("Pitch Shifter"))
        pitchShifter (state, slot);
    else if (is ("Frequency Shifter"))
        frequencyShifter (state, slot);
    else if (is ("Emphasis"))
        emphasis (state, slot);
    else if (is ("Low Lifter"))
        lowLifter (state, slot);
    else if (is ("Fruity Send"))
        fruitySend (state, slot);
    else if (is ("Transporter"))
        transporter (state, slot);
    else if (is ("Fruity Squeeze"))
        squeeze (state, slot);
    else if (is ("Frequency Splitter"))
        frequencySplitter (state, slot);
    else if (is ("Fruity Phase Inverter"))
    {
        slot.params[2] = utilityMatrixValue (-1.0); // inverte os dois lados
        slot.params[5] = utilityMatrixValue (-1.0);
    }

    return slot;
}
std::optional<EffectSlot> effectFromFlPreset (const std::vector<std::uint8_t>& bytes, double bpm)
{
    try
    {
        // o preset de efeito tem o mesmo formato do de canal: nome do plugin (0xC9) e estado (0xD5)
        const auto project = flp::parseProject (bytes, true);

        if (project.channels.empty() || project.channels.front().pluginState.empty())
            return std::nullopt;

        return effectFromFl (project.channels.front().pluginName, project.channels.front().pluginState, bpm);
    }
    catch (const flp::Error&)
    {
        return std::nullopt;
    }
}

std::vector<std::string> flPluginsForEffect (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::parametricEq: return { "Fruity Parametric EQ 2" };
        case EffectKind::limiter:
        case EffectKind::compressor: return { "Fruity Limiter", "Fruity Compressor" };
        case EffectKind::reverb: return { "Fruity Reeverb 2" };
        case EffectKind::delay: return { "Fruity Delay 3", "Fruity Delay 2" };
        case EffectKind::utility: return { "Fruity Stereo Shaper" };
        default: return {};
    }
}
std::optional<FlParamMap> effectParamFromFl (const std::string& pluginName, int k)
{
    auto is = [&] (const char* n) { return pluginName == n; };

    if (is ("Fruity Parametric EQ 2"))
    {
        if (k >= 0 && k < 7) // ganho da banda: ±18 dB
            return FlParamMap { peq::param (k, 2), [] (float v) { return peq::gainValue (36.0 * v - 18.0); } };
        if (k >= 7 && k < 14) // frequência: 20 Hz · 1000^v
            return FlParamMap { peq::param (k - 7, 1), [] (float v) { return peq::hzValue (20.0 * std::pow (1000.0, (double) v)); } };
        if (k == 35)
            return FlParamMap { peq::outputParam, [] (float v) { return peq::gainValue (36.0 * v - 18.0); } };
        return std::nullopt;
    }

    if (is ("Fruity Filter"))
    {
        if (k == 0) // corte (SVF de Chamberlin, como no import do estado)
            return FlParamMap { 1, [] (float v) {
                const auto f = std::min (1.0, (std::pow (26.0, (double) v) - 1.0) * 0.04);
                return fromLog (std::max (20.0, 44100.0 / 3.141592653589793 * std::asin (f / 2.0)), 20.0, 20000.0);
            } };
        if (k == 1)
            return FlParamMap { 2, [] (float v) { return fromLog (1.0 / (1.0 - 0.8 * (double) v), 0.5, 12.0); } };
        return std::nullopt;
    }

    if (is ("Fruity Balance") || is ("Fruity PanOMatic"))
    {
        if (k == 0) // pan: a faixa inteira dos dois casa com a nossa
            return FlParamMap { 1, [] (float v) { return v; } };
        if (k == 1) // volume: 0..320 (256 = 0 dB) [HIP no máximo]
            return FlParamMap { 0, [] (float v) { return utilityGainValue (curve (11.0, 1.25 * (double) v)); } };
        return std::nullopt;
    }

    if (is ("Fruity Fast LP") && (k == 0 || k == 1)) // corte / ressonância: o filtro interno tem os padrões, segue o knob
        return FlParamMap { k + 1, [] (float v) { return v; } };

    if (is ("Fruity Reeverb 2"))
    {
        if (k == 5) // decay 0,1..20 s -> realimentação dos combs
            return FlParamMap { 0, [] (float v) {
                const auto decay = 0.1 + 19.9 * (double) v;
                return clamp01 ((std::pow (10.0, -3.0 * 0.03 / decay) - 0.7) / 0.28);
            } };
        if (k == 10)
            return FlParamMap { 4, [] (float v) { return clamp01 (curve (11.0, (double) v)); } };
        if (k == 12)
            return FlParamMap { 3, [] (float v) { return clamp01 (curve (11.0, (double) v) / 0.25); } };
        return std::nullopt;
    }

    if (is ("Pitch Shifter"))
    {
        if (k == 0) // afinação: −12..+12 st (em centésimos) -> os nossos ±24 st
            return FlParamMap { 0, [] (float v) { return (float) clamp01 (0.5 + std::round (24.0 * v - 12.0) / 48.0); } };
        if (k == 3)
            return FlParamMap { 2, [] (float v) { return v; } };
        return std::nullopt;
    }

    if (is ("Fruity Delay 2") && k == 1) // wet
        return FlParamMap { 2, [] (float v) { return clamp01 (curve (11.0, (double) v) * 0.5); } };

    if (is ("Fruity Delay 3"))
    {
        if (k == 14)
            return FlParamMap { 1, [] (float v) { return clamp01 (v * 0.9); } };
        if (k == 23)
            return FlParamMap { 2, [] (float v) { return clamp01 (v * 0.5); } };
        if (k == 16)
            return FlParamMap { 4, [] (float v) { return clamp01 (1.0 - v); } };
    }

    return std::nullopt;
}
std::optional<float> controllerConstantOutput (const std::string& pluginName, const std::vector<std::uint8_t>& d, int output)
{
    if (pluginName == "Fruity X-Y Controller")
    {
        if (d.size() < 44 || output < 0 || output > 1)
            return std::nullopt;
        auto param = [&] (int i) { return std::clamp (i32At (d, 4 + 4 * (std::size_t) i), -65536, 65536) / 65536.0; };
        const auto axis = output * 4;
        const auto level = std::clamp (param (axis), 0.0, 1.0);
        // tensão (REA_rodada17 §4): k = 0,1·(1001^(|t|/128) − 1); t > 0 côncava, t < 0 convexa
        const auto t = std::clamp (i32At (d, 4 + 4 * (std::size_t) (axis + 3)), -128, 128);
        auto shaped = level;
        if (t != 0)
        {
            const auto k = 0.1 * (std::pow (1001.0, std::abs (t) / 128.0) - 1.0);
            shaped = t > 0 ? 1.0 - (std::pow (k + 1.0, 1.0 - level) - 1.0) / k : (std::pow (k + 1.0, level) - 1.0) / k;
        }
        return (float) std::clamp (param (axis + 1) + (param (axis + 2) - param (axis + 1)) * shaped, 0.0, 1.0);
    }

    if (pluginName == "Control Surface")
    {
        if (d.size() < 16)
            return std::nullopt;
        std::optional<float> found;
        // chunks u32 id + u64 tamanho (id 0 = grupo); 0x836 = valor de um controle e a saída publicada
        std::function<void (std::size_t, std::size_t)> walk = [&] (std::size_t pos, std::size_t end) {
            while (pos + 12 <= end && ! found)
            {
                std::uint32_t id = 0;
                std::uint64_t size = 0;
                std::memcpy (&id, d.data() + pos, 4);
                std::memcpy (&size, d.data() + pos + 4, 8);
                if (size > end - pos - 12)
                    return;
                const auto body = pos + 12;
                if (id == 0)
                    walk (body, body + (std::size_t) size);
                else if (id == 0x836 && size >= 12 && i32At (d, body + 8) == output)
                {
                    float v = 0.0f;
                    std::memcpy (&v, d.data() + body, 4);
                    if (std::isfinite (v))
                        found = std::clamp (v, 0.0f, 1.0f);
                }
                pos = body + (std::size_t) size;
            }
        };
        walk (4, d.size());
        return found;
    }

    return std::nullopt;
}
namespace
{
// curva de ganho dos knobs do FL: (11^x − 1)/10, com sinal
double scurve11 (double x) { return (x < 0.0 ? -1.0 : 1.0) * (std::pow (11.0, std::abs (x)) - 1.0) / 10.0; }

struct PhilterBand
{
    peq::BandType type = peq::BandType::off;
    double hz = 1000.0, q = 0.707, gainDb = 0.0;
    int stages = 1;
    bool ok = false;
    std::vector<double> stageHz; // modos alternate: corte de cada estágio
};

// tipo, corte e Q de uma unidade com corte c e ressonância r (0..1), a 44,1 kHz (k = 1)
PhilterBand philterBand (const LovePhilterUnit& u, double c, double r)
{
    PhilterBand b;
    c = std::clamp (c, 0.0, 1.0);
    r = std::clamp (r, 0.0, 1.0);
    const auto mode = std::clamp (u.mode, 0, 5);
    if (mode == 0)
        return b; // a unidade não filtra
    b.stages = mode == 4 ? 2 : mode == 5 ? 3 : mode;
    auto lpQ = [] (double rr) { return 0.1 + 0.02495 * (std::pow (2001.0, rr) - 1.0); };
    const auto lowGain = scurve11 (std::clamp (u.params[3], 0, 128) / 128.0);
    const auto bandGain = scurve11 (std::clamp (u.params[4], 0, 128) / 128.0);
    const auto highGain = scurve11 (std::clamp (u.params[5], 0, 128) / 128.0);
    double x = 0.0;
    std::function<double (double)> hzOf;

    switch (u.type)
    {
        case 0: // SVF de Chamberlin: fc = (x + 0,005)·44100/π; saída = L·LP + B·BP + H·HP
        {
            x = (std::pow (51.0, c) - 1.0) * 0.0159;
            hzOf = [] (double v) { return (v + 0.005) * 44100.0 / 3.141592653589793; };
            b.hz = hzOf (x);
            auto d = r <= 0.0 ? 1.0 : r >= 1.0 ? 0.2 : 1.0 - 0.9 * r;
            if (u.alt)
                d *= d;
            b.q = 1.0 / std::max (0.05, d);
            const auto l = std::abs (lowGain) > 1e-6, bp = std::abs (bandGain) > 1e-6, h = std::abs (highGain) > 1e-6;
            // só as saídas puras têm equivalente: LP, BP, HP ou L+H (rejeita-faixa) com o mesmo ganho
            if (l && ! bp && ! h)
                b.type = peq::BandType::highCut, b.gainDb = 20.0 * std::log10 (std::abs (lowGain));
            else if (h && ! l && ! bp)
                b.type = peq::BandType::lowCut, b.gainDb = 20.0 * std::log10 (std::abs (highGain));
            else if (bp && ! l && ! h)
                b.type = peq::BandType::bandPass, b.gainDb = 20.0 * std::log10 (std::abs (bandGain));
            else if (l && h && ! bp && std::abs (lowGain - highGain) < 1e-6)
                b.type = peq::BandType::notch, b.gainDb = 20.0 * std::log10 (std::abs (lowGain));
            else
                return b;
            b.ok = true;
            break;
        }
        case 1: case 2: // Lime: ω = (x + 0,007)·π/2 -> fc = (x + 0,007)·44100/4
        {
            x = std::min (1.6, (std::pow (76.0, c) - 1.0) * 0.01324);
            hzOf = [] (double v) { return (v + 0.007) * 44100.0 / 4.0; };
            b.hz = hzOf (x);
            b.q = std::min (50.0, std::pow (1001.0, r));
            b.type = u.type == 1 ? peq::BandType::highCut : peq::BandType::lowCut; // o slot L é "drive" aqui: fica de fora
            b.ok = true;
            break;
        }
        case 3: // Vanilla LP: fc = (x + 0,003)·44100
        {
            x = (std::pow (1501.0, c) - 1.0) * 0.000664667;
            hzOf = [] (double v) { return (v + 0.003) * 44100.0; };
            b.hz = hzOf (x);
            b.q = lpQ (r);
            b.type = peq::BandType::highCut;
            b.ok = true;
            break;
        }
        case 6: case 8: case 9: case 10: // família Vanilla 4…10: fc = (x + 0,0001)·44100
        {
            x = std::min (1.0001, (std::pow (1501.0, c) - 1.0) * 0.00039993);
            if (u.type == 6)
                x = std::min (x, 0.6001);
            hzOf = [] (double v) { return (v + 0.0001) * 44100.0; };
            b.hz = hzOf (x);
            if (u.type == 6)
            {
                b.type = peq::BandType::lowCut;
                b.q = lpQ (r);
            }
            else if (u.type == 9)
            {
                b.type = peq::BandType::peak;
                b.q = 0.1 + 0.04995 * (std::pow (2001.0, 1.0 - r) - 1.0);
                b.gainDb = 20.0 * std::log10 (std::max (1e-6, std::abs (lowGain))); // o ganho do pico é o slot low/gain
            }
            else
            {
                b.type = u.type == 8 ? peq::BandType::lowShelf : peq::BandType::highShelf;
                b.q = 0.1 + 0.45 * (std::pow (3.0, 1.0 - r) - 1.0); // inclinação S
                b.gainDb = 20.0 * std::log10 (std::max (1e-6, std::abs (lowGain)));
            }
            b.ok = true;
            break;
        }
        case 4: case 5: // Vanilla passa-faixa / rejeita-faixa: largura em oitavas (REA_rodada19 §7.1)
        {
            x = std::min (1.0001, (std::pow (1501.0, c) - 1.0) * 0.00039993);
            hzOf = [] (double v) { return (v + 0.0001) * 44100.0; };
            b.hz = hzOf (x);
            const auto n = std::max (1, mode);
            auto bw = 0.01 + 0.1199 * (std::pow (101.0, u.alt ? r * (c + 1.0) * 0.5 : r) - 1.0);
            bw = u.type == 4 ? bw * n : bw / n; // por estágio
            const auto w = std::clamp (2.0 * 3.141592653589793 * b.hz / 44100.0, 1e-4, 2.8495);
            b.q = std::clamp (1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * bw * w / std::sin (w))), 0.1, 18.0);
            b.type = u.type == 4 ? peq::BandType::bandPass : peq::BandType::notch;
            b.ok = true;
            if (b.stages > 1) // a largura já foi dividida por estágio: não refazer a raiz do Q abaixo
            {
                b.hz = std::clamp (b.hz, 20.0, 20000.0);
                return b;
            }
            break;
        }
        case 7: // passa-tudo: só muda a fase; sozinho na unidade, o módulo é 1 (REA_rodada19 §7.2)
            b.type = peq::BandType::off;
            b.ok = true;
            return b;
        case 11: // Mango: 4 polos com realimentação; o corte em Hz é inferido do polo (REA_rodada19 §7.3)
        {
            x = std::min (3.0, (std::pow (201.0, c) - 1.0) * 0.01495);
            hzOf = [] (double v) {
                const auto w = std::min ((v + 0.01) * 0.5, 0.65);
                return 22050.0 * w * (1.873 * w * w * w + 0.4955 * w * w - 0.649 * w + 0.9988);
            };
            b.hz = hzOf (x);
            b.q = lpQ (r);
            b.type = peq::BandType::highCut;
            b.stages = std::min (4, 2 * b.stages); // 4 polos (24 dB/oit) por estágio
            b.ok = true;
            break;
        }
        default:
            return b;
    }
    if (b.stages > 1)
        b.q = std::pow (b.q, 1.0 / b.stages);
    // alternate (modos 4/5): a variável de frequência de cada estágio é x, x^E, x^(E²), E = 0,5 / 0,75
    if ((mode == 4 || mode == 5) && hzOf)
    {
        const auto e = mode == 4 ? 0.5 : 0.75;
        auto stageX = x;
        for (int i = 0; i < b.stages; ++i)
        {
            b.stageHz.push_back (std::clamp (hzOf (stageX), 20.0, 20000.0));
            stageX = std::pow (std::max (0.0, stageX), e);
        }
    }
    b.hz = std::clamp (b.hz, 20.0, 20000.0);
    return b;
}

EffectSlot philterSlot (const PhilterBand& band, double unitGain)
{
    auto slot = makeEffectSlot (EffectKind::parametricEq);
    for (int i = 0; i < peq::bands; ++i)
        slot.params[(std::size_t) peq::param (i, 0)] = peq::typeValue (peq::BandType::off);
    double outputDb = 20.0 * std::log10 (std::max (1e-6, unitGain));
    if (band.ok)
    {
        const auto cut = band.type == peq::BandType::highCut || band.type == peq::BandType::lowCut;
        const auto alternate = ! band.stageHz.empty();
        // cortes: estágios = inclinação do EQ; os outros tipos (e os alternate) repetem a banda (cascata)
        const auto copies = cut && ! alternate ? 1 : std::min (band.stages, peq::bands);
        for (int i = 0; i < copies; ++i)
        {
            slot.params[(std::size_t) peq::param (i, 0)] = peq::typeValue (band.type);
            slot.params[(std::size_t) peq::param (i, 1)] = peq::hzValue (alternate ? band.stageHz[(std::size_t) i] : band.hz);
            slot.params[(std::size_t) peq::param (i, 2)] = peq::gainValue (std::clamp (band.gainDb, -18.0, 18.0));
            slot.params[(std::size_t) peq::param (i, 3)] = peq::qValue (band.q);
            slot.params[(std::size_t) (peq::slopeParam + i)] = peq::slopeValue (cut && ! alternate ? std::clamp (band.stages, 1, 4) : 1);
        }
        if (cut) // nos cortes o ganho do slot L/H multiplica a saída
            outputDb += band.gainDb;
    }
    slot.params[peq::outputParam] = peq::gainValue (std::clamp (outputDb, -18.0, 18.0));
    return slot;
}
} // namespace

std::optional<LovePhilterConversion> lovePhilterEffects (const std::vector<std::uint8_t>& stateBytes)
{
    const auto state = parseLovePhilter (stateBytes);
    if (! state)
        return std::nullopt;

    const auto master = scurve11 (std::clamp (state->globals[0], 0, 160) / 128.0);
    std::vector<int> active;
    for (int u = 0; u < 8; ++u)
        if (state->units[(std::size_t) u].enabled && (state->soloUnit < 0 || state->soloUnit == u))
            active.push_back (u);

    LovePhilterConversion out;
    // articulador de volume (multiplicativo, REA_rodada17 §15.3 + rodada18 §4): base × mapaX(x) × mapaY(y);
    // envelope/LFO desligados são neutros (os ligados ficam de fora)
    const auto modX = std::clamp (state->modX, 0, 1024) / 1024.0, modY = std::clamp (state->modY, 0, 1024) / 1024.0;
    auto volumeFactor = [] (const LovePhilterUnit& unit, double x, double y) {
        const auto& a = unit.articulators[1];
        const auto base = std::isfinite (a.base) ? std::clamp ((double) a.base, 0.0, 4.0) : 1.0;
        return std::max (0.0, base * lovePhilterMapAt (a.mapX, x) * lovePhilterMapAt (a.mapY, y));
    };
    auto mapsNeutral = [] (const LovePhilterMap& m) {
        return std::all_of (m.points.begin(), m.points.end(), [] (const auto& pt) { return std::abs (pt.second - 1.0) < 1e-6; });
    };
    for (const auto u : active)
        if (u + 1 < 8 && state->units[(std::size_t) u + 1].enabled && state->units[(std::size_t) u].params[11] != 0)
        {
            out.notes.push_back ("Love Philter: unidades encadeadas (NEXT) tocam em paralelo");
            break;
        }

    auto unitEffects = [&] (int u) {
        const auto& unit = state->units[(std::size_t) u];
        const auto band = philterBand (unit, std::clamp (unit.params[1], 0, 256) / 256.0, std::clamp (unit.params[2], 0, 256) / 256.0);
        if (unit.mode != 0 && ! band.ok)
            out.notes.push_back ("Love Philter: filtro tipo " + std::to_string (unit.type) + " sem equivalente");
        const auto gain = scurve11 (std::clamp (unit.params[9], 0, 160) / 128.0) * master * std::abs (scurve11 (std::clamp (unit.params[10], -160, 160) / 128.0));
        std::vector<EffectSlot> chain { philterSlot (band, gain) };
        const auto& vol = unit.articulators[1];
        if (! mapsNeutral (vol.mapX) || ! mapsNeutral (vol.mapY) || std::abs (vol.base - 1.0f) > 1e-6f)
        {
            auto utility = makeEffectSlot (EffectKind::utility); // volume controlado por Mod X/Y
            utility.params[0] = utilityGainValue (volumeFactor (unit, modX, modY));
            chain.push_back (utility);
        }
        // envelope do pattern no volume (modo 0: ganho × y·d_E), em tempos e em loop (REA_rodada20 §1.2–1.4)
        if (const auto& env = vol.pattern; env.on)
        {
            double period = 0.0;
            for (std::size_t k = 1; k < env.points.size(); ++k)
                period += env.points[k].dt;
            const auto points = env.points.size();
            if (! env.tempo || period <= 1e-6 || points > 12 || points == 0)
                out.notes.push_back ("Love Philter: envelope de volume fora do formato suportado");
            else
            {
                auto gate = makeEffectSlot (EffectKind::rhythmGain);
                gate.params[0] = (float) std::min (1.0, period / 64.0);
                gate.params[1] = (float) points / 12.0f;
                double x = 0.0;
                for (std::size_t k = 0; k < points; ++k)
                {
                    if (k > 0)
                        x += env.points[k].dt;
                    gate.params[2 + 3 * k] = (float) clamp01 (x / period);
                    gate.params[3 + 3 * k] = (float) clamp01 (env.points[k].y * std::clamp ((double) vol.envDepth, 0.0, 1.0));
                    gate.params[4 + 3 * k] = (float) clamp01 ((env.points[k].tension + 1.0) / 2.0);
                    if (env.points[k].curve != 0 && k > 0)
                        out.notes.push_back ("Love Philter: curva de envelope de tipo " + std::to_string (env.points[k].curve) + " tratada como simples");
                }
                chain.push_back (gate);
            }
        }

        const auto pan = std::clamp (unit.params[8], -128, 128) / 128.0;
        if (const auto& lfo = unit.articulators[0].lfo; lfo.on && ! lfo.frozen && lfo.tempo)
        {
            // pan = PAN/128 + 2·A·d_L·W(φ), período 625/(10001^(S/65536) − 1) tempos (REA_rodada20 §1.5)
            auto autoPan = makeEffectSlot (EffectKind::autoPan);
            const auto period = 625.0 / (std::pow (10001.0, std::clamp (lfo.speed, 1, 65536) / 65536.0) - 1.0);
            const auto depth = std::clamp (2.0 * lfo.amplitude * unit.articulators[0].lfoDepth, -1.0, 1.0);
            autoPan.params[0] = (float) std::clamp (period / 64.0, 1e-4, 1.0);
            autoPan.params[1] = (float) clamp01 (depth * 0.5 + 0.5);
            autoPan.params[2] = (float) (lfo.phase / 4294967296.0);
            autoPan.params[3] = (float) clamp01 (pan * 0.5 + 0.5);
            chain.push_back (autoPan);
            if (lfo.skew != 0 || lfo.width != 0)
                out.notes.push_back ("Love Philter: forma do LFO de pan aproximada por triângulo");
        }
        else if (std::abs (pan) > 1e-3)
        {
            auto utility = makeEffectSlot (EffectKind::utility);
            utility.params[1] = (float) clamp01 (pan * 0.5 + 0.5);
            chain.push_back (utility);
        }
        for (const int a : { 2, 3 })
            if (unit.articulators[(std::size_t) a].pattern.on || unit.articulators[(std::size_t) a].lfo.on)
            {
                out.notes.push_back ("Love Philter: envelope/LFO de corte ou ressonância não aplicado");
                break;
            }
        return chain;
    };

    if (active.empty())
    {
        out.slots.push_back (philterSlot ({}, 0.0)); // nenhuma unidade ligada: o Love Philter fica mudo
        return out;
    }

    if (active.size() == 1)
    {
        const auto u = active.front();
        out.slots = unitEffects (u);
        const auto& unit = state->units[(std::size_t) u];
        const auto base = 8 + 44 * u;
        const auto masterGain = master;
        // Mod X (k = 6) / Mod Y (k = 7) movem o utilitário de volume, se houver
        const auto modSlot = out.slots.size() >= 2 && out.slots[1].kind == EffectKind::utility && out.slots[1].params[1] == 0.5f ? 1 : -1;
        out.paramMap = [unit, base, masterGain, modSlot, modX, modY, volumeFactor] (int k) -> std::optional<std::tuple<int, int, std::function<float (float)>>> {
            if ((k == 6 || k == 7) && modSlot >= 0)
                return std::make_tuple (modSlot, 0, std::function<float (float)> ([unit, k, modX, modY, volumeFactor] (float v) {
                                            return utilityGainValue (k == 6 ? volumeFactor (unit, v, modY) : volumeFactor (unit, modX, v));
                                        }));
            if (k == base + 1) // CUT: c = valor
                return std::make_tuple (0, peq::param (0, 1), std::function<float (float)> ([unit] (float v) {
                                            const auto b = philterBand (unit, v, std::clamp (unit.params[2], 0, 256) / 256.0);
                                            return peq::hzValue (b.hz);
                                        }));
            if (k == base + 2) // RES
                return std::make_tuple (0, peq::param (0, 3), std::function<float (float)> ([unit] (float v) {
                                            const auto b = philterBand (unit, std::clamp (unit.params[1], 0, 256) / 256.0, v);
                                            return peq::qValue (b.q);
                                        }));
            if (k == base + 9) // VOL: 0..160
                return std::make_tuple (0, peq::outputParam, std::function<float (float)> ([masterGain] (float v) {
                                            const auto g = scurve11 (160.0 * v / 128.0) * masterGain;
                                            return peq::gainValue (std::clamp (20.0 * std::log10 (std::max (1e-6, g)), -18.0, 18.0));
                                        }));
            return std::nullopt;
        };
        return out;
    }

    auto container = makeEffectSlot (EffectKind::parallel);
    for (const auto u : active)
        container.branches.push_back ({ unitEffects (u), 1.0f });
    out.slots.push_back (std::move (container));
    return out;
}
PatcherBranchHead patcherBranchHead (const std::string& plugin, const std::vector<std::uint8_t>& d, int bus)
{
    PatcherBranchHead head;
    std::string name = plugin;
    std::transform (name.begin(), name.end(), name.begin(), [] (unsigned char c) { return (char) std::tolower (c); });

    if (name == "frequency splitter")
    {
        if (d.size() < 18 * 4)
            return head;
        auto word = [&] (int i) { return i32At (d, 4 * (std::size_t) i); };
        if (bus == 0)
        {
            auto slot = makeEffectSlot (EffectKind::parametricEq);
            if (frequencySplitter (d, slot))
            {
                head.valid = true;
                head.slot = slot;
            }
            return head;
        }
        int band = -1;
        for (int i = 0; i < 3; ++i)
            if (word (15 + i) == bus)
                band = i;
        if (band < 0)
            return head;
        head.valid = true;
        const auto threeBands = word (6) != 0;
        if (! threeBands && band == 1)
        {
            head.gain = 0.0f; // em 2 bandas o barramento da média é mudo
            return head;
        }
        // a banda vira um Crossover com a estrutura exata de cada inclinação (REA_rodada17 §1.2); só a de
        // 18 dB/oit (estrutura com passa-tudo do FL) é aproximada por Butterworth 3
        auto slot = makeEffectSlot (EffectKind::crossover);
        auto value = [] (int v) { return (float) (std::clamp (v, 0, 65536) / 65536.0); };
        auto combo = [] (int c) { return (float) std::clamp (c, 0, 7) / 7.0f; };
        bool approximate = false;
        auto highPass = [&] (int freqWord, int comboWord) {
            slot.params[0] = 1.0f;
            slot.params[1] = value (word (freqWord));
            slot.params[2] = combo (word (comboWord));
            approximate = approximate || word (comboWord) == 2;
        };
        auto lowPass = [&] (int freqWord, int comboWord) {
            slot.params[3] = 1.0f;
            slot.params[4] = value (word (freqWord));
            slot.params[5] = combo (word (comboWord));
            approximate = approximate || word (comboWord) == 2;
        };
        if (band == 0)
            lowPass (1, 3);
        else if (band == 1)
        {
            highPass (1, 3);
            lowPass (2, 4);
        }
        else if (threeBands)
            highPass (2, 4);
        else
            highPass (1, 3);
        head.slot = slot;
        head.gain = (float) std::pow (10.0, std::clamp (word (9 + band), -6000, 1800) / 100.0 / 20.0); // nível de envio (centésimos de dB)
        if (approximate)
            head.note = "Frequency Splitter: inclinação de 18 dB/oit aproximada";
        return head;
    }

    if (name == "fruity stereo shaper")
    {
        if (d.size() < 4 + 8 * 4)
            return head;
        auto param = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
        auto slot = makeEffectSlot (EffectKind::utility);
        if (bus == 0)
        {
            stereoShaper (d, slot);
            head.valid = true;
            head.slot = slot;
            return head;
        }
        if (bus != param (6) || param (4) != 0 || param (5) != 0)
            return head; // com delay/fase a diferença não é uma matriz: fora
        // entrada − saída da matriz M = (I − M)
        const auto rl = param (0) / 12800.0, l = param (1) / 12800.0, r = param (2) / 12800.0, lr = param (3) / 12800.0;
        slot.params[2] = utilityMatrixValue (1.0 - l);
        slot.params[3] = utilityMatrixValue (-rl);
        slot.params[5] = utilityMatrixValue (1.0 - r);
        slot.params[4] = utilityMatrixValue (-lr);
        head.valid = true;
        head.slot = slot;
        return head;
    }

    return head;
}
} // namespace lynx
