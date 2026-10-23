#pragma once

// One-sentence descriptions of the controls of LYNX's own engines (round 135, docs/PARITY.md "description line"):
// the reference reverbs (and other originals) explain the control under the mouse in a strip at the bottom of their
// window. LYNX's editors draw the same strip; the texts are LYNX's own words, English, marked for translation
// (resources/i18n/pt-BR.txt). No JUCE: tested in tests/Parity135vTests.inc.

#include <string>

#include "core/Song.h"

namespace lynx::paramhelp
{
// What the control does; nullptr when there is no text. `param` is the parameter's EffectInfo name. The lookup tries
// the engine's own wording first, then the shared one, then the name without a band / voice / side / section prefix
// ("B2 ratio", "V1 pan", "Time L", "Early size") or suffix.
const char* describe (EffectKind kind, const char* param);

// The line shown while no control is under the mouse: the engine's mode (or style / type) and what it is.
struct Summary
{
    int param = -1;               // the mode parameter (−1: the engine has none)
    const char* text = nullptr;   // what this mode is (English; nullptr = nothing known about it)
    const char* detail = nullptr; // a second clause (a variant: "brighter", "with a drifting pitch"), or nullptr
};
Summary summary (EffectKind kind, const EffectParams& params);

// The parameter that names an engine's mode / type / style (−1 = none).
int modeParam (EffectKind kind);
} // namespace lynx::paramhelp
