#pragma once

// Built-in instruments: synthesized drums and synth, and a sampler. They let the
// DAW make sound before plugin hosting exists and play imported projects.
// Everything has fixed capacity: noteOn/render never allocate.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void noteOn (int key, float velocity) noexcept = 0;
    // Starts the note already `startSeconds` in (only meaningful for samples; others ignore it).
    virtual void noteOnAt (int key, float velocity, double /*startSeconds*/) noexcept { noteOn (key, velocity); }
    virtual void noteOff (int key) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;

    // Adds (does not overwrite) n samples into left/right, with the gains already applied.
    virtual void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept = 0;
    // Automated parameter (plugins). 0..1. Audio thread.
    virtual void setParameter (int /*index*/, float /*value*/) noexcept {}
    // Latency the instrument introduces (plugins), in samples. For PDC.
    virtual int latencySamples() const noexcept { return 0; }
    // "Slide" note: sounding notes glide to `key` over `samples`, without retriggering. Ignored if unsupported.
    virtual void slideTo (int /*key*/, float /*cents*/, float /*samples*/) noexcept {}
    // Audio clip: fades and in-clip position of the next note (before noteOn), in samples.
    virtual void setNextClipFades (float /*elapsed*/, float /*remaining*/, float /*fadeIn*/, float /*fadeOut*/) noexcept {}
    // Pan, fine tuning and "porta" (inverts the channel's porta) of the next note (called right before
    // noteOn). Ignored if unsupported.
    virtual void setNextNoteExpression (float /*pan*/, float /*cents*/, bool /*porta*/ = false) noexcept {}
    // Channel settings (sampler, synth). UI thread, on every snapshot; the instrument stores them
    // so the audio thread can read without waiting.
    virtual void configure (const ChannelDef&, double /*bpm*/) noexcept {}
    // Control output ("controller" instruments), 0..1. Audio thread.
    virtual float controlOutput (int /*output*/) const noexcept { return 0.0f; }
    // Song position (ticks) at the start of the block, for the global LFOs. Audio thread.
    virtual void setSongPosition (double /*ticks*/) noexcept {}
    // No voice sounding: render would only produce silence (the mixer skips the channel until a note arrives).
    virtual bool idle() const noexcept { return false; }
};

// Audio of an already-decoded sample (loaded off the audio thread). Short samples
// live in the vectors; long ones live in a memory-mapped cache file (the OS reads
// from disk on demand) and use the `mapped*` pointers.
struct SampleData
{
    std::vector<float> left, right; // empty right = mono
    double sampleRate = 44100.0;

    const float* mappedLeft = nullptr;
    const float* mappedRight = nullptr; // nullptr = mono
    std::size_t mappedFrames = 0;
    std::shared_ptr<const void> mapping; // owner of the mapping

    bool isMapped() const noexcept { return mappedLeft != nullptr; }
    std::size_t frames() const noexcept { return isMapped() ? mappedFrames : left.size(); }
    const float* leftData() const noexcept { return isMapped() ? mappedLeft : left.data(); }
    const float* rightData() const noexcept
    {
        if (isMapped())
            return mappedRight != nullptr ? mappedRight : mappedLeft;
        return right.empty() ? left.data() : right.data();
    }
    bool isStereo() const noexcept { return isMapped() ? mappedRight != nullptr : ! right.empty(); }
};

// `sample` is only used by the sampler; without a sample the sampler is silent.
std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind, std::shared_ptr<const SampleData> sample = nullptr);

// Multi-sample sampler: each zone with its sample already loaded (nullptr = missing file).
struct LoadedZone
{
    SampleZone zone;
    std::shared_ptr<const SampleData> sample;
};
std::unique_ptr<Instrument> makeZonedSampler (std::vector<LoadedZone> zones);

// Cheap, deterministic white noise (xorshift), identical on every platform.
struct Noise
{
    std::uint32_t state = 0x12345678u;

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
    }
};
} // namespace lynx
