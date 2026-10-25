#pragma once

#include <algorithm>
#include <cstdint>

namespace lynx::warmup
{
// Export "Plugin warm-up" (round 155). An export builds its own engine: every hosted plugin is a fresh instance whose
// saved state was just restored, then it is re-prepared for the offline render mode. Some plugins finish that work
// later — bridged plugins (yabridge on Linux forwards state, parameter and latency changes over IPC, and the Windows
// side applies some of them on its own GUI thread), samplers that stream their samples in the background, convolution
// reverbs loading their impulse. An offline render runs much faster than real time, so without a wait the first
// second of the file is rendered by half-loaded plugins.
//
// Before the render starts, the engine processes silent blocks that are thrown away (transport stopped at the start
// position, so the plugins also get the song's values at that position) until:
//   - at least `minAudioSeconds` of audio went through the plugins (plugins that settle after a number of blocks),
//   - at least `minWallSeconds` of real time passed (work done on the plugins' own threads),
//   - what the plugins report (latency, parameter values: `signature`) stayed the same for `stableWallSeconds`,
// or `maxWallSeconds` passed (a plugin whose values never stop moving, e.g. a meter exposed as a parameter).
// A plugin that reports nothing while it loads only gets the minimums (1 s), the time a real start gives it in
// practice; one whose parameters / latency move while it loads (a bridge forwarding the restored values one by one)
// is waited for until they stop.
struct Settings
{
    double minAudioSeconds = 1.0;
    double minWallSeconds = 1.0;
    double stableWallSeconds = 0.3;
    double maxWallSeconds = 10.0;
};

struct Result
{
    int blocks = 0;
    double audioSeconds = 0.0;
    double wallSeconds = 0.0;
    int signatureChanges = 0; // how many times what the plugins report changed during the warm-up
    bool timedOut = false;
    bool cancelled = false; // process() returned a negative length
};

// Runs the warm-up. `process()` renders one discarded silent block and returns its length in samples (negative:
// cancel); `signature()`
// is a hash of what the plugins report; `now()` is a clock in seconds; `idle (seconds)` lets other threads run (the
// blocks are paced so the audio processed does not run ahead of real time by more than a block or so: plugins that
// work on their own threads get the time a real-time start would give them).
template <class Process, class Signature, class Now, class Idle>
Result run (const Settings& settings, double sampleRate, Process&& process, Signature&& signature, Now&& now, Idle&& idle)
{
    Result r;
    const auto start = now();
    auto last = signature();
    auto stableSince = start;
    std::int64_t samples = 0;
    const auto rate = std::max (1.0, sampleRate);

    for (;;)
    {
        const auto t = now();
        r.wallSeconds = t - start;
        r.audioSeconds = (double) samples / rate;

        if (r.wallSeconds >= settings.maxWallSeconds)
        {
            r.timedOut = true;
            break;
        }

        const auto sig = signature();
        if (sig != last)
        {
            last = sig;
            stableSince = t;
            ++r.signatureChanges;
        }

        if (r.audioSeconds >= settings.minAudioSeconds && r.wallSeconds >= settings.minWallSeconds
            && t - stableSince >= settings.stableWallSeconds)
            break;

        // ahead of real time: give the plugins' threads (and the message thread) a moment instead of spinning
        if (r.audioSeconds > r.wallSeconds + 0.02 && r.audioSeconds >= settings.minAudioSeconds)
        {
            idle (std::min (0.01, r.audioSeconds - r.wallSeconds));
            continue;
        }

        const auto n = process();
        if (n < 0)
        {
            r.cancelled = true;
            break;
        }
        samples += std::max (1, n);
        ++r.blocks;
    }

    return r;
}

// FNV-1a over the values that make up a signature (latency, quantised parameter values).
struct Hash
{
    std::uint64_t value = 1469598103934665603ull;

    void add (std::uint64_t v) noexcept
    {
        for (int i = 0; i < 8; ++i)
        {
            value ^= (v >> (i * 8)) & 0xffu;
            value *= 1099511628211ull;
        }
    }

    // parameter values are quantised so float noise in a plugin's cache does not count as a change
    void addValue (float v) noexcept { add ((std::uint64_t) (std::int64_t) (v * 100000.0f)); }
};
} // namespace lynx::warmup
