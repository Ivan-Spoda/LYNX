#include "core/SynthPresets.h"

#include <algorithm>
#include <cctype>

namespace lynx
{
std::vector<std::pair<std::string, SynthSettings>> synthPresets()
{
    using W = Waveform;
    auto make = [] (std::array<SynthOscillator, 3> osc, float a, float d, float s, float r, float cutoff, float res) {
        SynthSettings p;
        p.osc = osc;
        p.attack = a;
        p.decay = d;
        p.sustain = s;
        p.release = r;
        p.cutoff = cutoff;
        p.resonance = res;
        return p;
    };

    return {
        { "Padrão", SynthSettings {} },
        { "Baixo 808", make ({ SynthOscillator { W::sine, -1, 0, 1.0f }, { W::triangle, 0, 0, 0.15f }, {} }, 0.002f, 1.2f, 0.0f, 0.3f, 4000.0f, 0.0f) },
        { "Baixo Reese", make ({ SynthOscillator { W::saw, -1, -12, 0.7f }, { W::saw, -1, 12, 0.7f }, { W::square, -2, 0, 0.3f } }, 0.005f, 0.3f, 1.0f, 0.15f, 900.0f, 0.3f) },
        { "Lead", make ({ SynthOscillator { W::saw, 0, -7, 0.6f }, { W::saw, 0, 7, 0.6f }, { W::square, 1, 0, 0.2f } }, 0.01f, 0.2f, 0.8f, 0.2f, 6000.0f, 0.2f) },
        { "Supersaw", make ({ SynthOscillator { W::saw, 0, -18, 0.6f }, { W::saw, 0, 0, 0.6f }, { W::saw, 0, 18, 0.6f } }, 0.02f, 0.4f, 0.8f, 0.4f, 9000.0f, 0.1f) },
        { "Pad", make ({ SynthOscillator { W::saw, 0, -9, 0.5f }, { W::triangle, 1, 9, 0.5f }, { W::sine, -1, 0, 0.4f } }, 0.8f, 1.0f, 0.7f, 1.5f, 2200.0f, 0.15f) },
        { "Pluck", make ({ SynthOscillator { W::square, 0, 0, 0.7f }, { W::saw, 1, 5, 0.3f }, {} }, 0.001f, 0.25f, 0.0f, 0.2f, 3500.0f, 0.35f) },
        { "Sino", make ({ SynthOscillator { W::sine, 0, 0, 0.8f }, { W::sine, 2, 0, 0.35f }, { W::sine, 3, 3, 0.15f } }, 0.001f, 1.5f, 0.0f, 1.2f, 20000.0f, 0.0f) },
        { "Ruído (risers)", make ({ SynthOscillator { W::noise, 0, 0, 0.8f }, {}, {} }, 0.5f, 0.5f, 0.8f, 0.6f, 3000.0f, 0.5f) },
    };
}


SynthSettings presetForChannelName (const std::string& channelName)
{
    std::string name = channelName;
    std::transform (name.begin(), name.end(), name.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    auto has = [&name] (const char* word) { return name.find (word) != std::string::npos; };
    auto preset = [] (const char* wanted) {
        for (const auto& [n, s] : synthPresets())
            if (n == wanted)
                return s;
        return SynthSettings {};
    };

    if (has ("808") || has ("sub"))
        return preset ("Baixo 808");
    if (has ("reese") || has ("bass") || has ("baixo"))
        return preset ("Baixo Reese");
    if (has ("pad") || has ("string") || has ("choir") || has ("atmos") || has ("ambient"))
        return preset ("Pad");
    if (has ("pluck") || has ("arp") || has ("stab"))
        return preset ("Pluck");
    if (has ("bell") || has ("key") || has ("piano") || has ("chord") || has ("rhodes"))
        return preset ("Sino");
    if (has ("supersaw") || has ("saw") || has ("trance"))
        return preset ("Supersaw");
    if (has ("lead") || has ("synth"))
        return preset ("Lead");
    if (has ("riser") || has ("sweep") || has ("noise") || has ("fx") || has ("uplifter") || has ("downlifter"))
        return preset ("Ruído (risers)");

    return SynthSettings {};
}
} // namespace lynx
