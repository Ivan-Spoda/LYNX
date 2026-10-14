#pragma once

// Piano Roll note tools (docs/WORKFLOW_FL.md P2: strum, arpeggiate, flip, randomise, scale levels, limit,
// scale snap) and the per-note "levels" shared by the Piano Roll lane and the step sequencer graph lanes.
// LYNX's own designs of the behaviours the manual describes; pure functions over Pattern, no JUCE.

#include <cstdint>
#include <vector>

#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::notetools
{
// --- per-note levels, normalised to 0..1 ------------------------------------------------------
enum class Level
{
    velocity,  // 0..1
    pan,       // -1..1 -> 0..1
    release,   // 0..1
    finePitch, // -120..120 cents -> 0..1
    modX,      // 0..1
    modY       // 0..1
};

constexpr int levelCount = 6;
const char* levelName (Level level); // English, for tr()
float levelValue (const Note& note, Level level) noexcept;
void setLevelValue (Note& note, Level level, float normalised) noexcept;
float levelDefault (Level level) noexcept; // normalised value of a fresh note
bool levelIsBipolar (Level level) noexcept; // drawn from the centre line (pan, fine pitch)

// --- scales -----------------------------------------------------------------------------------
struct ScaleInfo
{
    const char* name; // English, for tr()
    std::vector<int> intervals;
};

const std::vector<ScaleInfo>& scales(); // [0] = no scale
bool inScale (int key, int root, int scale) noexcept;
// The in-scale key nearest to `key` (a tie goes down); `key` itself without a scale. Stays in 0..127.
int snapToScale (int key, int root, int scale) noexcept;
// Moves every note to the nearest in-scale key.
void snapNotesToScale (Pattern& pattern, const std::vector<int>& indices, int root, int scale);

// --- chords -----------------------------------------------------------------------------------
// Groups the notes into chords: same channel and same start. Each group is sorted by key (low first);
// the groups are in start order.
std::vector<std::vector<int>> chordsOf (const Pattern& pattern, const std::vector<int>& indices);

// --- strum ------------------------------------------------------------------------------------
struct StrumOptions
{
    std::int64_t spread = 40;   // ticks from one chord note to the next
    float curve = 0.0f;         // -1..1: < 0 the strum speeds up, > 0 it slows down
    float velocityStep = 0.0f;  // added to each successive note's velocity (-0.25..0.25)
    bool downward = false;      // high note first
    bool alternate = false;     // every other chord strums the other way
    bool keepEnds = true;       // notes keep their end (they get shorter); otherwise they keep their length
};

void strum (Pattern& pattern, const std::vector<int>& indices, const StrumOptions& options);

// --- arpeggiate (offline) ---------------------------------------------------------------------
struct ArpeggiateOptions
{
    enum Direction { up, down, upDown, downUp };
    std::int64_t step = ticksPerStep; // time between arpeggio notes
    float gate = 0.8f;                // fraction of the step each note sounds (0.05..1)
    int direction = up;
    int octaves = 1;                  // 1..4
};

// Replaces each chord (and, with more than one octave, each single note) by a run of notes over its length.
// Returns the indices of the new notes (not re-sorted).
std::vector<int> arpeggiate (Pattern& pattern, const std::vector<int>& indices, const ArpeggiateOptions& options);

// --- flip -------------------------------------------------------------------------------------
// Horizontal: mirrors the notes in time inside their span. Vertical: mirrors the keys around the middle
// of their range.
void flip (Pattern& pattern, const std::vector<int>& indices, bool horizontal, bool vertical);

// --- randomise --------------------------------------------------------------------------------
struct RandomiseOptions
{
    float velocity = 0.25f;   // maximum change (0..1 of the full range)
    float pan = 0.0f;
    float finePitch = 0.0f;   // fraction of ±120 cents
    float modX = 0.0f, modY = 0.0f;
    std::int64_t timing = 0;  // maximum start shift in ticks (never before 0)
    float length = 0.0f;      // maximum relative length change (0..1)
    bool bipolar = true;      // changes go both ways; otherwise only down (velocity) / only later (timing)
    std::uint32_t seed = 1;
};

void randomise (Pattern& pattern, const std::vector<int>& indices, const RandomiseOptions& options);

// --- scale levels -----------------------------------------------------------------------------
// Each value becomes value * scale + offset (pan and fine pitch around their centre), where `scale` ramps
// from `scaleStart` (first note) to `scaleEnd` (last note) through time: a fade in or out.
void scaleLevels (Pattern& pattern, const std::vector<int>& indices, Level level, float scaleStart, float scaleEnd, float offset);

// --- limit ------------------------------------------------------------------------------------
struct LimitOptions
{
    int lowKey = 36, highKey = 84;   // notes outside move by octaves into the range (or are clamped if it is narrower)
    float minVelocity = 0.0f, maxVelocity = 1.0f;
};

void limit (Pattern& pattern, const std::vector<int>& indices, const LimitOptions& options);
} // namespace lynx::notetools
