#include "core/FlEffects.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Effects.h"
#include "core/FlpImport.h"

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

bool reeverb2 (const std::vector<std::uint8_t>& d, EffectSlot& slot)
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
} // namespace

std::optional<EffectSlot> effectFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state)
{
    const auto kind = effectForFlPlugin (pluginName);

    if (! kind)
        return std::nullopt;

    auto slot = makeEffectSlot (*kind);

    if (*kind == EffectKind::filter)
        slot.params[1] = 1.0f; // aberto: sem os ajustes do FL, melhor não abafar

    if (pluginName == "Fruity Soft Clipper")
        slot.params[0] = 0.15f; // drive baixo

    if (pluginName == "Fruity Parametric EQ 2")
        parametricEq2 (state, slot);
    else if (pluginName == "Fruity Limiter")
        limiter (state, slot);
    else if (pluginName == "Fruity Reeverb 2")
        reeverb2 (state, slot);
    else if (pluginName == "Fruity Delay 2")
        delay2 (state, slot);
    else if (pluginName == "Fruity Delay 3")
        delay3 (state, slot);
    else if (pluginName == "Fruity Compressor")
        compressor (state, slot);

    return slot;
}
} // namespace lynx
