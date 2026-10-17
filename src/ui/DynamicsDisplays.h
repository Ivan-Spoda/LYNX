#pragma once

// The live graphs of LYNX's dynamics suite (REA round 108) inside the native effect editor: transfer curves with the
// live input level, a scrolling gain-reduction history over the input / output levels, level bars, Prism's band
// display (crossovers, per-band gain change, the resulting response) and Ember's multiband split.

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"

namespace lynx::dynui
{
// Recent values of a slot's meters (pushed at the editor's 30 Hz refresh).
struct History
{
    static constexpr int length = 180; // 6 s
    std::array<float, length> gr {}, in {}, out {};
    int pos = 0;
    void push (float reduction01, float inPeak, float outPeak) noexcept
    {
        gr[(std::size_t) pos] = reduction01;
        in[(std::size_t) pos] = inPeak;
        out[(std::size_t) pos] = outPeak;
        pos = (pos + 1) % length;
    }
};

// Displays that change with the audio (the editor refreshes them at 30 Hz).
bool isLive (const char* id) noexcept;

// Paints display `id` when it is one of the suite's (false otherwise). `meter (k)` reads the slot's live output k.
bool paint (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> area, float scale, const EffectSlot& fx,
            const std::function<float (int)>& meter, const History& history);
} // namespace lynx::dynui
