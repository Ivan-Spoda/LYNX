#include "core/PluginSubstitutes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

#include "core/Effects.h"
#include "core/FlPluginDb.h"
#include "core/FlWrapper.h"

namespace lynx
{
namespace
{
// The built-in table (format: docs/PLUGIN_FALLBACK.md). Seeded with the third-party plugins of tmp/gf/PLUGIN_QUEUE.md
// and common mixing staples, mapped to EXISTING LYNX effects. Identifiers marked [DATA] were read from real projects'
// FL wrapper records; the others match by name. Parameter maps only where the saved state shows the layout and a
// public manual gives the meaning (each one says so). Modules added later (pitch corrector, vocal tools...) append
// their own entries here or through registerPluginSubstitutes.
constexpr std::string_view builtInTable = R"TABLE(
# ---------------------------------------------------------------- reverbs -> Reeverb 2 / LuxeVerb
[valhalla-vintageverb]
product = ValhallaVintageVerb
vendor = Valhalla DSP
lynx = Reeverb 2
# [DATA] class ID from FL wrapper records of real projects
vst3 = 7654535665657633616c68616c6c6176
names = ValhallaVintageVerb
# the reverb runs 100 % wet at unity (Reeverb 2: dry 0, wet 128 of 160); the balance is the slot's dry/wet
preset = Dry=0; Wet=0.8
mix = 0.25
# [DATA] the saved state is a JUCE XML block with a "Mix" attribute 0..1; [DOC] the vendor manual describes Mix as
# the wet/dry balance 0-100 %. Read as a linear wet/dry crossfade [model].
map = xml:Mix -> mix

[valhalla-room]
product = ValhallaRoom
vendor = Valhalla DSP
lynx = Reeverb 2
names = ValhallaRoom
preset = Dry=0; Wet=0.8
mix = 0.25

[valhalla-plate]
product = ValhallaPlate
vendor = Valhalla DSP
lynx = Reeverb 2
names = ValhallaPlate
preset = Dry=0; Wet=0.8
mix = 0.25

[valhalla-supermassive]
product = Valhalla Supermassive
vendor = Valhalla DSP
lynx = LuxeVerb
names = ValhallaSupermassive; ValhallaShimmer
mix = 1

[fabfilter-pro-r]
product = FabFilter Pro-R
vendor = FabFilter
lynx = LuxeVerb
names = FabFilter Pro-R; FabFilter Pro-R 2; Pro-R; Pro-R 2
mix = 1

[soundtoys-littleplate]
product = Little Plate
vendor = Soundtoys
lynx = Reeverb 2
names = LittlePlate; Little Plate
preset = Dry=0; Wet=0.8
mix = 0.25

[waves-reverbs]
product = Waves reverb (RVerb / H-Reverb / TrueVerb)
vendor = Waves
lynx = Reeverb 2
waves = RVerb; H-Reverb; TrueVerb
preset = Dry=0; Wet=0.8
mix = 0.25

# ---------------------------------------------------------------- delays -> Fruity Delay 3 (FL's "Default" preset)
[waves-h-delay]
product = H-Delay
vendor = Waves
lynx = Fruity Delay 3
# [DATA] class ID (stereo component) and <PluginName> from real projects
vst3 = 48545356444268532d64656c61792073
waves = H-Delay

[waves-supertap]
product = SuperTap
vendor = Waves
lynx = Fruity Delay 3
waves = SuperTap

[soundtoys-echoboy]
product = EchoBoy
vendor = Soundtoys
lynx = Fruity Delay 3
# [DATA] VST2 unique ID from real projects
vst2 = StEB
names = EchoBoy; EchoBoyJr; EchoBoy Jr

[soundtoys-primaltap]
product = PrimalTap
vendor = Soundtoys
lynx = Fruity Delay 3
names = PrimalTap; Little PrimalTap; LittlePrimalTap

[valhalla-delay]
product = ValhallaDelay
vendor = Valhalla DSP
lynx = Fruity Delay 3
names = ValhallaDelay; ValhallaFreqEcho

[fabfilter-timeless]
product = FabFilter Timeless
vendor = FabFilter
lynx = Fruity Delay 3
names = FabFilter Timeless 2; FabFilter Timeless 3; Timeless 2; Timeless 3

# ---------------------------------------------------------------- limiters -> Limiter (ceiling -0.3 dB)
[waves-l-series]
product = Waves L1 / L2 / L3
vendor = Waves
lynx = Limiter
waves = L1; L2; L3; L3-16; L3-LL Multi; L4

[fabfilter-pro-l]
product = FabFilter Pro-L
vendor = FabFilter
lynx = Apex Limiter
names = FabFilter Pro-L; FabFilter Pro-L 2; Pro-L; Pro-L 2

# ---------------------------------------------------------------- compressors -> Compressor
[waves-rcomp]
product = Renaissance Compressor
vendor = Waves
lynx = Compressor
# [DATA] class ID (stereo component) and <PluginName> from real projects; its RealWorld values are readable but
# their meaning is not confirmed, so the default preset is used
vst3 = 5554535643527253636f6d7072657373
waves = RComp

[waves-cla-2a]
product = CLA-2A
vendor = Waves
lynx = Glow Leveler
# [DATA] class ID (stereo component) and <PluginName> from real projects
vst3 = 4c545356324163536c612d3261207374
waves = CLA-2A
# LYNX's optical leveler (round 96); its state is not translated: moderate leveling (+7 dB into the cell, +3 dB gain)
preset = Peak reduction=0.45; Gain=0.1

[waves-compressors]
product = Waves compressor (CLA-76 / CLA-3A / C1 / SSLComp / API-2500)
vendor = Waves
lynx = Compressor
waves = CLA-76; CLA-3A; C1 comp; C1comp; SSLComp; API-2500

[fabfilter-pro-c]
product = FabFilter Pro-C
vendor = FabFilter
lynx = Compressor
names = FabFilter Pro-C 2; FabFilter Pro-C 3; Pro-C 2; Pro-C 3

# ---------------------------------------------------------------- EQs and channel strips -> Parametric EQ (flat)
[waves-ssl-channel]
product = SSL E-Channel
vendor = Waves
lynx = Ridge Strip
# [DATA] class ID (stereo component) and <PluginName> from real projects; LYNX's channel strip (round 96) starts
# neutral: the EQ curve and dynamics settings are not translated
vst3 = 5354535648437353736c6368616e6e65
waves = SSLChannel

[ssl-native-channel]
product = SSL Native Channel Strip / 4K E
vendor = Solid State Logic
lynx = Ridge Strip
names = SSL Native Channel Strip 2; SSL Native Channel Strip; SSL 4K E; SSL 4K B; Vocalstrip 2; SSL Native Vocalstrip 2

[waves-eqs]
product = Waves EQ (Q10 / Renaissance EQ / SSL EQ)
vendor = Waves
lynx = Parametric EQ
waves = Q10; REQ; SSLEQ

[fabfilter-pro-q]
product = FabFilter Pro-Q
vendor = FabFilter
lynx = Facet EQ
names = FabFilter Pro-Q 2; FabFilter Pro-Q 3; FabFilter Pro-Q 4; Pro-Q 2; Pro-Q 3; Pro-Q 4

# ---------------------------------------------------------------- vocal tools -> LYNX's mixing suite (round 96)
[waves-deesser]
product = DeEsser
vendor = Waves
lynx = Hush De-esser
waves = DeEsser; DeEsser Stereo

[fabfilter-pro-ds]
product = FabFilter Pro-DS
vendor = FabFilter
lynx = Hush De-esser
names = FabFilter Pro-DS; Pro-DS

[harrison-deesser]
product = Harrison De-Esser
vendor = Harrison
lynx = Hush De-esser
names = Harrison De-Esser; Harrison DeEsser

[slate-fresh-air]
product = Fresh Air
vendor = Slate Digital
lynx = Aether
names = Fresh Air; FreshAir

[soundtoys-decapitator]
product = Decapitator
vendor = Soundtoys
lynx = Ember Saturator
names = Decapitator
# a light drive (+5.4 dB, tube) with auto gain, so the slot colours without jumping in level
preset = Drive=0.15

[fabfilter-saturn]
product = FabFilter Saturn
vendor = FabFilter
lynx = Ember Saturator
names = FabFilter Saturn; FabFilter Saturn 2; Saturn 2
preset = Drive=0.15

# ---------------------------------------------------------------- modulation
[soundtoys-panman]
product = PanMan
vendor = Soundtoys
lynx = Auto-pan
# [DATA] VST2 unique ID from real projects
vst2 = StPn
names = PanMan

# ---------------------------------------------------------------- vocal pitch correction -> LYNX Tune (round 95)
[antares-auto-tune]
product = Auto-Tune Pro
vendor = Antares
lynx = LYNX Tune
# [DATA] VST3 class ID from FL wrapper records of real projects (round 98); its settings are not translated
vst3 = 415453563854413175746f2d54756e65
names = Auto-Tune Pro; Auto-Tune Pro X; Auto-Tune Artist; Auto-Tune Access; Auto-Tune EFX+; Auto-Tune Hybrid; Auto-Tune
mix = 1

[waves-tune]
product = Waves Tune Real-Time
vendor = Waves
lynx = LYNX Tune
waves = WavesTune Real-Time; Waves Tune Real-Time; WavesTune
names = WavesTune Real-Time; Waves Tune Real-Time
mix = 1
)TABLE";

std::string lowerAscii (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trim (std::string_view s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a]))
        ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1]))
        --b;
    return std::string (s.substr (a, b - a));
}

std::vector<std::string> splitList (std::string_view s, char separator)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == separator)
        {
            if (auto item = trim (s.substr (start, i - start)); ! item.empty())
                out.push_back (std::move (item));
            start = i + 1;
        }
    return out;
}

// a name key without the channel-layout words Waves and others append ("RComp Stereo", "L2 Mono")
std::string matchKey (const std::string& name)
{
    auto key = pluginNameKey (name);
    for (bool again = true; again;)
    {
        again = false;
        for (const std::string suffix : { "stereo", "mono" })
            if (key.size() >= suffix.size() + 2 && key.compare (key.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                key.resize (key.size() - suffix.size());
                again = true;
            }
    }
    return key;
}

std::optional<EffectKind> effectKindNamed (const std::string& name)
{
    const auto wanted = lowerAscii (trim (name));
    for (int k = 0; k <= (int) EffectKind::lynxTune; ++k) // the last EffectKind: extend when one is appended
    {
        const auto kind = (EffectKind) k;
        if (kind == EffectKind::plugin || kind == EffectKind::parallel)
            continue;
        if (lowerAscii (effectInfo (kind).name) == wanted)
            return kind;
    }
    return std::nullopt;
}

int paramNamed (EffectKind kind, const std::string& name)
{
    const auto wanted = lowerAscii (trim (name));
    const auto& params = effectInfo (kind).params;
    for (std::size_t i = 0; i < params.size(); ++i)
        if (lowerAscii (params[i].name) == wanted)
            return (int) i;
    return -1;
}

bool parseFloat (const std::string& s, float& out)
{
    try
    {
        std::size_t used = 0;
        out = std::stof (s, &used);
        return used == s.size() && std::isfinite (out);
    }
    catch (...)
    {
        return false;
    }
}

bool parseHex16 (const std::string& s, std::array<std::uint8_t, 16>& out)
{
    std::string hex;
    for (auto c : s)
        if (std::isxdigit ((unsigned char) c))
            hex += c;
        else if (c != '-' && c != ' ')
            return false;
    if (hex.size() != 32)
        return false;
    for (std::size_t i = 0; i < 16; ++i)
        out[i] = (std::uint8_t) std::stoul (hex.substr (i * 2, 2), nullptr, 16);
    return true;
}

// "StEB" (four characters, as VST2 IDs are usually written) or a number ("0x53744542", "1400128834")
bool parseVst2Id (const std::string& s, std::uint32_t& out)
{
    if (s.size() == 4 && ! (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
    {
        out = ((std::uint32_t) (unsigned char) s[0] << 24) | ((std::uint32_t) (unsigned char) s[1] << 16)
              | ((std::uint32_t) (unsigned char) s[2] << 8) | (std::uint32_t) (unsigned char) s[3];
        return true;
    }
    try
    {
        std::size_t used = 0;
        const auto v = std::stoull (s, &used, 0);
        if (used != s.size() || v > 0xFFFFFFFFull)
            return false;
        out = (std::uint32_t) v;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// "<source> -> <target> [| inLo inHi outLo outHi]"; source = vst2:<n> | xml:<attribute> | waves:<n>; target =
// "mix" or a parameter name of the LYNX effect
bool parseMap (const std::string& text, EffectKind kind, SubstituteParamMap& out)
{
    const auto arrow = text.find ("->");
    if (arrow == std::string::npos)
        return false;
    const auto source = trim (std::string_view (text).substr (0, arrow));
    auto rest = std::string (text.substr (arrow + 2));
    std::string range;
    if (const auto bar = rest.find ('|'); bar != std::string::npos)
    {
        range = rest.substr (bar + 1);
        rest = rest.substr (0, bar);
    }
    const auto target = trim (rest);

    const auto colon = source.find (':');
    if (colon == std::string::npos)
        return false;
    const auto type = lowerAscii (source.substr (0, colon));
    const auto arg = trim (std::string_view (source).substr (colon + 1));
    if (arg.empty())
        return false;
    if (type == "xml")
    {
        out.source = SubstituteParamMap::Source::xml;
        out.attribute = arg;
    }
    else if (type == "vst2" || type == "waves")
    {
        out.source = type == "vst2" ? SubstituteParamMap::Source::vst2 : SubstituteParamMap::Source::waves;
        float index = 0;
        if (! parseFloat (arg, index) || index < 0 || index != std::floor (index))
            return false;
        out.index = (int) index;
    }
    else
        return false;

    if (lowerAscii (target) == "mix")
        out.toSlotMix = true;
    else if ((out.target = paramNamed (kind, target)) < 0)
        return false;

    if (! range.empty())
    {
        std::vector<float> v;
        for (const auto& token : splitList (range, ' '))
        {
            float f = 0;
            if (! parseFloat (token, f))
                return false;
            v.push_back (f);
        }
        if (v.size() != 4 || v[0] == v[1])
            return false;
        out.inLo = v[0];
        out.inHi = v[1];
        out.outLo = v[2];
        out.outHi = v[3];
    }
    return true;
}

std::vector<SubstituteEntry>& registry()
{
    static std::vector<SubstituteEntry> entries = parseSubstituteTable (builtInTable);
    return entries;
}

std::size_t findBytes (const std::vector<std::uint8_t>& data, std::string_view what, std::size_t from = 0)
{
    if (what.empty() || data.size() < what.size())
        return std::string::npos;
    for (std::size_t i = from; i + what.size() <= data.size(); ++i)
        if (std::memcmp (data.data() + i, what.data(), what.size()) == 0)
            return i;
    return std::string::npos;
}

std::string xmlUnescape (const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '&')
        {
            out += s[i];
            continue;
        }
        const auto end = s.find (';', i);
        if (end == std::string::npos)
        {
            out += s[i];
            continue;
        }
        const auto entity = s.substr (i + 1, end - i - 1);
        if (entity == "amp") out += '&';
        else if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else
        {
            out += s.substr (i, end - i + 1);
        }
        i = end;
    }
    return out;
}

// the text of the active setup's <Parameters> block of a Waves preset chunk
std::string wavesParametersText (const std::vector<std::uint8_t>& state)
{
    const auto start = findBytes (state, "<PresetChunkXMLTree");
    if (start == std::string::npos)
        return {};
    const std::string xml (state.begin() + (std::ptrdiff_t) start, state.end());
    std::string setup = "SETUP_A";
    if (const auto a = xml.find ("<ActiveSetup>"); a != std::string::npos)
        if (const auto b = xml.find ("</ActiveSetup>", a); b != std::string::npos)
            setup = trim (std::string_view (xml).substr (a + 13, b - a - 13));
    const auto data = xml.find ("<PresetData Setup=\"" + setup + "\"");
    if (data == std::string::npos)
        return {};
    const auto params = xml.find ("<Parameters", data);
    if (params == std::string::npos)
        return {};
    const auto open = xml.find ('>', params);
    const auto close = xml.find ("</Parameters>", params);
    if (open == std::string::npos || close == std::string::npos || close < open)
        return {};
    return xml.substr (open + 1, close - open - 1);
}

float mapValue (const SubstituteParamMap& m, double value)
{
    auto u = (value - (double) m.inLo) / ((double) m.inHi - (double) m.inLo);
    u = std::clamp (u, 0.0, 1.0);
    return (float) ((double) m.outLo + u * ((double) m.outHi - (double) m.outLo));
}

// --- binary writer / reader for foreignToText ----------------------------------------------------------------------
void putU32 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (v >> (8 * i)));
}

void putBytes (std::vector<std::uint8_t>& out, const std::uint8_t* p, std::size_t n)
{
    putU32 (out, (std::uint32_t) n);
    out.insert (out.end(), p, p + n);
}

void putString (std::vector<std::uint8_t>& out, const std::string& s)
{
    putBytes (out, (const std::uint8_t*) s.data(), s.size());
}

struct BinReader
{
    const std::vector<std::uint8_t>& d;
    std::size_t pos = 0;
    bool ok = true;

    std::uint32_t u32()
    {
        if (pos + 4 > d.size())
        {
            ok = false;
            return 0;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= (std::uint32_t) d[pos + (std::size_t) i] << (8 * i);
        pos += 4;
        return v;
    }

    std::vector<std::uint8_t> bytes()
    {
        const auto n = u32();
        if (! ok || n > d.size() - pos)
        {
            ok = false;
            return {};
        }
        std::vector<std::uint8_t> v (d.begin() + (std::ptrdiff_t) pos, d.begin() + (std::ptrdiff_t) (pos + n));
        pos += n;
        return v;
    }

    std::string str()
    {
        const auto b = bytes();
        return std::string (b.begin(), b.end());
    }
};

constexpr std::uint32_t foreignTextMagic = 0x31464C4Cu; // "LLF1"
} // namespace

PluginIdentity pluginIdentity (const ForeignPlugin& plugin)
{
    PluginIdentity id;
    id.format = plugin.format;
    id.name = plugin.name;
    id.path = plugin.path;
    id.vst2Id = plugin.vst2Id;
    id.classId = plugin.classId;
    id.hasClassId = plugin.hasClassId;
    id.clapId = plugin.clapId;
    if (plugin.format == ForeignPlugin::Format::vst3)
    {
        std::vector<std::uint8_t> component, controller;
        if (splitFlVst3State (plugin.state, component, controller))
            id.state = std::move (component);
    }
    else if (plugin.format == ForeignPlugin::Format::clap)
    {
        if (plugin.state.size() > 4) // u32 0 + the plugin's own state (REA_rodada22 §2.5)
            id.state.assign (plugin.state.begin() + 4, plugin.state.end());
    }
    else
        id.state = plugin.state;
    return id;
}

std::vector<SubstituteEntry> parseSubstituteTable (std::string_view text, int* errors)
{
    std::vector<SubstituteEntry> entries;
    int bad = 0;
    std::optional<SubstituteEntry> current;
    std::string lynxName;
    std::vector<std::string> presetItems, mapItems;
    bool entryBad = false;

    auto finish = [&] {
        if (! current)
            return;
        const auto kind = effectKindNamed (lynxName);
        if (! kind || entryBad || current->key.empty())
        {
            ++bad;
            current.reset();
            return;
        }
        current->effect = *kind;
        for (const auto& item : presetItems)
        {
            const auto eq = item.rfind ('=');
            float v = 0;
            const auto index = eq == std::string::npos ? -1 : paramNamed (*kind, item.substr (0, eq));
            if (index < 0 || ! parseFloat (trim (item.substr (eq + 1)), v))
            {
                ++bad;
                continue;
            }
            current->preset.push_back ({ index, std::clamp (v, 0.0f, 1.0f) });
        }
        for (const auto& item : mapItems)
        {
            SubstituteParamMap m;
            if (parseMap (item, *kind, m))
                current->params.push_back (std::move (m));
            else
                ++bad;
        }
        if (current->product.empty())
            current->product = current->key;
        entries.push_back (std::move (*current));
        current.reset();
    };

    std::size_t start = 0;
    while (start <= text.size())
    {
        auto end = text.find ('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const auto line = trim (text.substr (start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        if (line.front() == '[' && line.back() == ']')
        {
            finish();
            current = SubstituteEntry {};
            current->key = trim (std::string_view (line).substr (1, line.size() - 2));
            lynxName.clear();
            presetItems.clear();
            mapItems.clear();
            entryBad = false;
            continue;
        }
        const auto eq = line.find ('=');
        if (! current || eq == std::string::npos)
        {
            ++bad;
            continue;
        }
        const auto key = lowerAscii (trim (std::string_view (line).substr (0, eq)));
        const auto value = trim (std::string_view (line).substr (eq + 1));
        auto& e = *current;
        if (key == "product")
            e.product = value;
        else if (key == "vendor")
            e.vendor = value;
        else if (key == "role")
        {
            if (lowerAscii (value) == "instrument")
                e.instrument = true;
            else if (lowerAscii (value) != "effect")
                entryBad = true;
        }
        else if (key == "lynx")
            lynxName = value;
        else if (key == "vst2")
        {
            for (const auto& item : splitList (value, ';'))
            {
                std::uint32_t id = 0;
                if (parseVst2Id (item, id))
                    e.vst2Ids.push_back (id);
                else
                    ++bad;
            }
        }
        else if (key == "vst3")
        {
            for (const auto& item : splitList (value, ';'))
            {
                std::array<std::uint8_t, 16> id {};
                if (parseHex16 (item, id))
                    e.vst3ClassIds.push_back (id);
                else
                    ++bad;
            }
        }
        else if (key == "clap")
            for (const auto& item : splitList (value, ';'))
                e.clapIds.push_back (item);
        else if (key == "names")
            for (const auto& item : splitList (value, ';'))
            {
                if (auto k = matchKey (item); ! k.empty())
                    e.names.push_back (std::move (k));
            }
        else if (key == "waves")
            for (const auto& item : splitList (value, ';'))
            {
                if (auto k = matchKey (item); ! k.empty())
                    e.wavesNames.push_back (std::move (k));
            }
        else if (key == "preset")
            for (auto& item : splitList (value, ';'))
                presetItems.push_back (std::move (item));
        else if (key == "mix")
        {
            float v = 1;
            if (parseFloat (value, v))
                e.mix = std::clamp (v, 0.0f, 1.0f);
            else
                ++bad;
        }
        else if (key == "map")
            mapItems.push_back (value);
        else if (key == "vst2params")
        {
            float v = 0;
            if (parseFloat (value, v) && v >= 1 && v <= 100000 && v == std::floor (v))
                e.vst2Params = (int) v;
            else
                ++bad;
        }
        else
            ++bad;
    }
    finish();
    if (errors != nullptr)
        *errors = bad;
    return entries;
}

const std::vector<SubstituteEntry>& pluginSubstitutes()
{
    return registry();
}

int registerPluginSubstitutes (std::string_view tableText)
{
    auto added = parseSubstituteTable (tableText);
    auto& entries = registry();
    for (auto& entry : added)
    {
        entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const SubstituteEntry& e) { return e.key == entry.key; }), entries.end());
        // later registrations win: they go first, so findPluginSubstitute meets them before the built-in ones
        entries.insert (entries.begin(), std::move (entry));
    }
    return (int) added.size();
}

const SubstituteEntry* findPluginSubstitute (const PluginIdentity& plugin, bool instrument)
{
    const auto& entries = registry();
    auto candidates = [&] (auto&& matches) -> const SubstituteEntry* {
        for (const auto& e : entries)
            if (e.instrument == instrument && matches (e))
                return &e;
        return nullptr;
    };
    // 1) format identifiers
    if (plugin.vst2Id != 0 && (plugin.format == ForeignPlugin::Format::vst2 || plugin.format == ForeignPlugin::Format::none))
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.vst2Ids.begin(), x.vst2Ids.end(), plugin.vst2Id) != x.vst2Ids.end(); }))
            return e;
    if (plugin.hasClassId)
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.vst3ClassIds.begin(), x.vst3ClassIds.end(), plugin.classId) != x.vst3ClassIds.end(); }))
            return e;
    if (! plugin.clapId.empty())
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.clapIds.begin(), x.clapIds.end(), plugin.clapId) != x.clapIds.end(); }))
            return e;
    // 2) a WaveShell sub-plugin, by the name its own state carries
    const auto waves = matchKey (wavesPluginName (plugin.state));
    if (! waves.empty())
        if (auto* e = candidates ([&] (const SubstituteEntry& x) { return std::find (x.wavesNames.begin(), x.wavesNames.end(), waves) != x.wavesNames.end(); }))
            return e;
    // 3) names (never the shell's file name: it holds many plugins)
    std::vector<std::string> keys;
    const auto nameKey = matchKey (plugin.name);
    if (nameKey.rfind ("waveshell", 0) == 0)
        return nullptr;
    if (! nameKey.empty())
        keys.push_back (nameKey);
    {
        auto file = plugin.path;
        std::replace (file.begin(), file.end(), '\\', '/');
        if (const auto slash = file.rfind ('/'); slash != std::string::npos)
            file = file.substr (slash + 1);
        if (const auto dot = file.rfind ('.'); dot != std::string::npos)
            file = file.substr (0, dot);
        if (auto k = matchKey (file); ! k.empty() && k.rfind ("waveshell", 0) != 0)
            keys.push_back (std::move (k));
    }
    for (const auto& key : keys)
        if (auto* e = candidates ([&] (const SubstituteEntry& x) {
                return std::find (x.names.begin(), x.names.end(), key) != x.names.end()
                       || (! waves.empty() && std::find (x.wavesNames.begin(), x.wavesNames.end(), key) != x.wavesNames.end());
            }))
            return e;
    return nullptr;
}

EffectSlot makeSubstituteSlot (const SubstituteEntry& entry, const PluginIdentity* original, int* translated)
{
    auto slot = makeEffectSlot (entry.effect);
    for (const auto& [index, value] : entry.preset)
        if (index >= 0 && index < maxEffectParams)
            slot.params[(std::size_t) index] = value;
    slot.mix = entry.mix;

    int count = 0;
    if (original != nullptr && ! original->state.empty() && ! entry.params.empty())
    {
        std::optional<FlVst2State> vst2;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<double> waves;
        bool attributesRead = false, wavesRead = false;
        for (const auto& m : entry.params)
        {
            std::optional<double> value;
            switch (m.source)
            {
                case SubstituteParamMap::Source::vst2:
                {
                    if (! vst2)
                    {
                        FlVst2State s;
                        vst2 = parseFlVst2State (original->state, s) ? s : FlVst2State {}; // a chunk / unreadable: no params
                    }
                    if (vst2->params.empty())
                        break;
                    // all programs saved: the current one's block (needs the plugin's parameter count)
                    std::size_t base = 0, perProgram = vst2->params.size();
                    if (vst2->allPrograms())
                    {
                        const auto n = (std::size_t) entry.vst2Params;
                        if (n > 0 && vst2->params.size() % n == 0 && (std::size_t) vst2->currentProgram < vst2->params.size() / n)
                        {
                            perProgram = n;
                            base = (std::size_t) std::max (0, vst2->currentProgram) * n;
                        }
                        else if (n > 0 && vst2->params.size() >= n && vst2->currentProgram == 0)
                            perProgram = n;
                        else
                            break; // which block is current is not known
                    }
                    if ((std::size_t) m.index < perProgram)
                        value = (double) vst2->params[base + (std::size_t) m.index];
                    break;
                }
                case SubstituteParamMap::Source::xml:
                {
                    if (! attributesRead)
                    {
                        attributes = juceXmlStateAttributes (original->state);
                        attributesRead = true;
                    }
                    for (const auto& [name, text] : attributes)
                        if (name == m.attribute)
                        {
                            float f = 0;
                            if (parseFloat (text, f))
                                value = (double) f;
                            break;
                        }
                    break;
                }
                case SubstituteParamMap::Source::waves:
                {
                    if (! wavesRead)
                    {
                        waves = wavesRealWorldParams (original->state);
                        wavesRead = true;
                    }
                    if ((std::size_t) m.index < waves.size() && std::isfinite (waves[(std::size_t) m.index]))
                        value = waves[(std::size_t) m.index];
                    break;
                }
            }
            if (! value)
                continue;
            const auto v = mapValue (m, *value);
            if (m.toSlotMix)
                slot.mix = v;
            else if (m.target >= 0 && m.target < maxEffectParams)
                slot.params[(std::size_t) m.target] = v;
            ++count;
        }
    }
    if (translated != nullptr)
        *translated = count;
    return slot;
}

bool substituteEffectSlot (EffectSlot& slot, const PluginIdentity& identity)
{
    if (slot.kind != EffectKind::plugin)
        return false;
    const auto* entry = findPluginSubstitute (identity, false);
    if (entry == nullptr)
        return false;
    auto replacement = makeSubstituteSlot (*entry, &identity);
    replacement.enabled = slot.enabled;
    replacement.mix = std::clamp (slot.mix * replacement.mix, 0.0f, 1.0f); // nested dry/wet crossfades multiply
    replacement.sidechain = slot.sidechain;
    replacement.sidechain2 = slot.sidechain2;
    replacement.substituteFor = slot.plugin;
    if (replacement.substituteFor.name.empty())
        replacement.substituteFor.name = identity.name;
    slot = std::move (replacement);
    return true;
}

bool restoreOriginalPlugin (EffectSlot& slot)
{
    if (! slot.isSubstitute())
        return false;
    EffectSlot original;
    original.kind = EffectKind::plugin;
    original.enabled = slot.enabled;
    original.mix = 1.0f; // the substitute's own dry/wet doesn't belong to the plugin; FL's slot mix is not kept
    original.sidechain = slot.sidechain;
    original.sidechain2 = slot.sidechain2;
    original.plugin = std::move (slot.substituteFor);
    slot = std::move (original);
    return true;
}

std::string substituteOriginalName (const EffectSlot& slot)
{
    if (! slot.isSubstitute())
        return {};
    if (! slot.substituteFor.name.empty())
        return slot.substituteFor.name;
    if (! slot.substituteFor.foreign.name.empty())
        return slot.substituteFor.foreign.name;
    return slot.substituteFor.id;
}

std::vector<std::pair<std::string, std::string>> juceXmlStateAttributes (const std::vector<std::uint8_t>& state)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    auto start = findBytes (state, "VC2!");
    start = findBytes (state, "<?xml", start == std::string::npos ? 0 : start);
    if (start == std::string::npos)
        return attributes;
    // the first element after the declaration
    std::size_t i = start + 5;
    while (i < state.size() && state[i] != '>')
        ++i;
    while (i < state.size() && state[i] != '<')
        ++i;
    if (i >= state.size())
        return attributes;
    std::size_t end = i;
    char quote = 0;
    for (; end < state.size(); ++end)
    {
        const auto c = (char) state[end];
        if (quote != 0)
        {
            if (c == quote)
                quote = 0;
        }
        else if (c == '"' || c == '\'')
            quote = c;
        else if (c == '>')
            break;
        else if (c == 0)
            return attributes;
    }
    const std::string tag (state.begin() + (std::ptrdiff_t) i + 1, state.begin() + (std::ptrdiff_t) end);
    // skip the element name, then name="value" pairs
    std::size_t p = 0;
    while (p < tag.size() && ! std::isspace ((unsigned char) tag[p]) && tag[p] != '/')
        ++p;
    while (p < tag.size())
    {
        while (p < tag.size() && (std::isspace ((unsigned char) tag[p]) || tag[p] == '/'))
            ++p;
        const auto nameStart = p;
        while (p < tag.size() && tag[p] != '=' && ! std::isspace ((unsigned char) tag[p]))
            ++p;
        const auto name = tag.substr (nameStart, p - nameStart);
        while (p < tag.size() && std::isspace ((unsigned char) tag[p]))
            ++p;
        if (p >= tag.size() || tag[p] != '=')
            break;
        ++p;
        while (p < tag.size() && std::isspace ((unsigned char) tag[p]))
            ++p;
        if (p >= tag.size() || (tag[p] != '"' && tag[p] != '\''))
            break;
        const auto q = tag[p++];
        const auto close = tag.find (q, p);
        if (close == std::string::npos)
            break;
        attributes.push_back ({ name, xmlUnescape (tag.substr (p, close - p)) });
        p = close + 1;
    }
    return attributes;
}

std::string wavesPluginName (const std::vector<std::uint8_t>& state)
{
    const auto chunk = findBytes (state, "<PresetChunkXMLTree");
    if (chunk == std::string::npos)
        return {};
    const auto a = findBytes (state, "<PluginName>", chunk);
    if (a == std::string::npos)
        return {};
    const auto b = findBytes (state, "</PluginName>", a);
    if (b == std::string::npos)
        return {};
    return xmlUnescape (trim (std::string (state.begin() + (std::ptrdiff_t) a + 12, state.begin() + (std::ptrdiff_t) b)));
}

std::vector<double> wavesRealWorldParams (const std::vector<std::uint8_t>& state)
{
    std::vector<double> values;
    const auto text = wavesParametersText (state);
    std::size_t p = 0;
    while (p < text.size())
    {
        while (p < text.size() && std::isspace ((unsigned char) text[p]))
            ++p;
        const auto start = p;
        while (p < text.size() && ! std::isspace ((unsigned char) text[p]))
            ++p;
        if (p == start)
            break;
        const auto token = text.substr (start, p - start);
        if (token == "*")
        {
            values.push_back (std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        try
        {
            std::size_t used = 0;
            const auto v = std::stod (token, &used);
            values.push_back (used == token.size() ? v : std::numeric_limits<double>::quiet_NaN());
        }
        catch (...)
        {
            values.push_back (std::numeric_limits<double>::quiet_NaN());
        }
    }
    return values;
}

std::string foreignToText (const ForeignPlugin& plugin)
{
    std::vector<std::uint8_t> out;
    putU32 (out, foreignTextMagic);
    putU32 (out, (std::uint32_t) plugin.format);
    putU32 (out, plugin.vst2Id);
    out.insert (out.end(), plugin.classId.begin(), plugin.classId.end());
    putU32 (out, plugin.hasClassId ? 1u : 0u);
    putString (out, plugin.clapId);
    putString (out, plugin.name);
    putString (out, plugin.path);
    putBytes (out, plugin.state.data(), plugin.state.size());
    putU32 (out, (std::uint32_t) plugin.midiInPort);
    putU32 (out, (std::uint32_t) plugin.midiOutPort);
    return base64Encode (out.data(), out.size());
}

bool foreignFromText (const std::string& text, ForeignPlugin& out)
{
    std::vector<std::uint8_t> data;
    if (! base64Decode (text, data))
        return false;
    BinReader r { data };
    if (r.u32() != foreignTextMagic)
        return false;
    ForeignPlugin p;
    const auto format = r.u32();
    if (format > (std::uint32_t) ForeignPlugin::Format::other)
        return false;
    p.format = (ForeignPlugin::Format) format;
    p.vst2Id = r.u32();
    if (r.pos + 16 > data.size())
        return false;
    std::copy (data.begin() + (std::ptrdiff_t) r.pos, data.begin() + (std::ptrdiff_t) r.pos + 16, p.classId.begin());
    r.pos += 16;
    p.hasClassId = r.u32() != 0;
    p.clapId = r.str();
    p.name = r.str();
    p.path = r.str();
    p.state = r.bytes();
    p.midiInPort = (int) r.u32();
    p.midiOutPort = (int) r.u32();
    if (! r.ok)
        return false;
    out = std::move (p);
    return true;
}

std::string base64Encode (const std::uint8_t* data, std::size_t size)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve ((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3)
    {
        const std::uint32_t b0 = data[i], b1 = i + 1 < size ? data[i + 1] : 0, b2 = i + 2 < size ? data[i + 2] : 0;
        const auto v = (b0 << 16) | (b1 << 8) | b2;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += i + 1 < size ? table[(v >> 6) & 63] : '=';
        out += i + 2 < size ? table[v & 63] : '=';
    }
    return out;
}

bool base64Decode (const std::string& text, std::vector<std::uint8_t>& out)
{
    out.clear();
    std::uint32_t buffer = 0;
    int bits = 0, padding = 0;
    for (const auto c : text)
    {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') { ++padding; continue; }
        else if (std::isspace ((unsigned char) c)) continue;
        else return false;
        if (padding > 0)
            return false; // data after padding
        buffer = (buffer << 6) | (std::uint32_t) v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back ((std::uint8_t) (buffer >> bits));
        }
    }
    return padding <= 2;
}
} // namespace lynx
