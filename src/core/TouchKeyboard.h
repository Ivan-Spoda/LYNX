#pragma once

// The Touch keyboard's geometry (FL's "Touch keyboard", interpreted; REA_rodada84): which note is under a point, how
// hard it is played (velocity from how far down the key it is touched: near the top soft, near the bottom loud), and
// octave shifting. The notes themselves go through HeldNotes (core/HeldNotes.h), tagged by the pointer that plays
// them, so every note-on gets its note-off whatever ends the gesture. No JUCE.

#include <algorithm>
#include <cmath>

namespace lynx::touchkb
{
inline bool isBlack (int note) noexcept
{
    const auto n = ((note % 12) + 12) % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// Index of the white key at or below `note` counted from C-1 (note 0).
inline int whiteIndex (int note) noexcept
{
    static constexpr int inOctave[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const auto octave = note >= 0 ? note / 12 : (note - 11) / 12;
    return octave * 7 + inOctave[((note % 12) + 12) % 12];
}

struct Rect
{
    float x = 0, y = 0, w = 0, h = 0;
    bool contains (float px, float py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct Layout
{
    int lowest = 48;         // the leftmost key, always a C (48 = C3 in LYNX's naming, middle C = 60)
    int whiteKeys = 21;      // white keys shown (3 octaves)
    float width = 700.0f, height = 120.0f;
    float blackLength = 0.62f; // black keys' length, part of the height
    float blackWidth = 0.62f;  // black keys' width, part of a white key

    float whiteWidth() const noexcept { return width / (float) std::max (1, whiteKeys); }
    int highest() const noexcept
    {
        // the last white key shown, then a black key right after it is still drawn only if it fits: keep it simple
        auto note = lowest;
        for (int w = 1; w < whiteKeys; ++w)
            note += isBlack (note + 1) ? 2 : 1;
        return note;
    }

    Rect keyRect (int note) const noexcept
    {
        const auto ww = whiteWidth();
        if (! isBlack (note))
            return { (float) (whiteIndex (note) - whiteIndex (lowest)) * ww, 0.0f, ww, height };
        const auto left = (float) (whiteIndex (note) - whiteIndex (lowest) + 1) * ww; // the line between the two white keys
        const auto bw = ww * blackWidth;
        return { left - bw * 0.5f, 0.0f, bw, height * blackLength };
    }

    // The note under (x, y): black keys are on top. -1 outside the keyboard.
    int noteAt (float x, float y) const noexcept
    {
        if (x < 0.0f || y < 0.0f || x >= width || y >= height)
            return -1;
        const auto ww = whiteWidth();
        const auto white = (int) std::floor (x / ww);
        // the white key's note
        auto note = lowest;
        for (int w = 0; w < white; ++w)
            note += isBlack (note + 1) ? 2 : 1;

        if (y < height * blackLength)
            for (const auto candidate : { note - 1, note + 1 })
                if (candidate >= lowest && candidate <= highest() + 1 && isBlack (candidate) && keyRect (candidate).contains (x, y))
                    return candidate >= 0 && candidate <= 127 ? candidate : -1;

        return note >= 0 && note <= 127 ? note : -1;
    }

    // Velocity for a touch at `y` on `note`'s key: 0.2 at its top, 1 at its bottom (a curve that reaches ~0.8 at
    // two thirds, so most of the key plays at a useful level).
    float velocityAt (int note, float y) const noexcept
    {
        const auto r = keyRect (note);
        const auto t = r.h > 0.0f ? std::clamp ((y - r.y) / r.h, 0.0f, 1.0f) : 1.0f;
        return 0.2f + 0.8f * std::sqrt (t);
    }
};

// Shifts the keyboard by `octaves`, keeping every key shown inside MIDI's 0..127 and the lowest key a C.
inline int shiftOctave (int lowest, int octaves, int whiteKeys) noexcept
{
    Layout probe;
    probe.whiteKeys = whiteKeys;
    auto best = lowest - lowest % 12;
    for (int step = 0; step < std::abs (octaves); ++step)
    {
        const auto next = best + (octaves > 0 ? 12 : -12);
        probe.lowest = next;
        if (next < 0 || probe.highest() > 127)
            break;
        best = next;
    }
    probe.lowest = best;
    while (best >= 12 && probe.highest() > 127) // a keyboard wider than what is left above: back down
        probe.lowest = best -= 12;
    return best;
}
} // namespace lynx::touchkb
