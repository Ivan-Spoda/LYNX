#pragma once

// Image-Line's "plugin voice processor": the per-voice channel processing that FL's native generators built on its
// plugin framework carry inside their own DLL (Fruity DX10, 3x Osc, …), decoded in docs/analise/REA_rodada42.md §3 and
// REA_rodada47.md §3 (the library code is byte-for-byte the same in both DLLs, addresses aside). Clean-room: our own
// port of our round-42/47 reference models (r42/py/dxproc.py, r47/py/osc3model.py), not Image-Line code.
//
// Per voice: five "INS" articulators (pan, volume, cutoff, resonance, pitch: delay/attack/hold/decay/sustain/release
// envelopes with tensions and delayed, faded LFOs on the sine/triangle/square tables) stepped once per sequencer tick,
// the channel filter (fast LP, 12 dB LP/BP/HP/BS, 24 dB LP, SVF LP ×1/×2 from Mod X/Y), the x87 pan law with its gain
// ramps, the 441-sample end fade; voices are mixed into the output in list order.
//
// The settings come from a 364-byte snapshot: the five 0xDA records (68 bytes each), Mod X at 0x15c, Mod Y at 0x160,
// the filter type at 0x168.

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lynx::flvp
{
constexpr int snapshotSize = 364;

inline std::int32_t bitsOf (float x) noexcept
{
    std::int32_t b;
    std::memcpy (&b, &x, 4);
    return b;
}

// cvtsd2si: round half to even (default rounding mode), 64-bit
std::int64_t roundEven (double x) noexcept;

// The host's WaveTables[0]: 16384 floats sin(2i·π/16384) (FLEngine FUN_009cdfc0)
const std::array<float, 16384>& sineTable() noexcept;

// x87 pan law ((1 + pan)·π/4 through fsincos, 64-bit precision, the FPU's 66-bit π above π/4): the processor's
// FUN_005fad20 and FL's host ComputeLRVol law A (FLEngine 0x6a3990) compute the same
void panLaw (float pan, float vol, float& gl, float& gr) noexcept;

// FUN_005fac70: per-sample gain step towards the target, limited to `limit` per sample
float rampStep (float& target, float prev, int n, float limit = 0.001f) noexcept;

// FUN_005f9760 (SSE path): ramped mix of an interleaved stereo block into l/r
void mixRamp (const float* x, float* l, float* r, int n, float gL, float gR, float sL, float sR) noexcept;

// the plugin's defaults (FUN_00722bc0): neutral records, the volume one with flag 4 and release tension −101
std::array<std::uint8_t, snapshotSize> defaultSnapshot() noexcept;

// true when a snapshot leaves the sound untouched: no envelope/LFO acting and the default "fast LP" fully open
bool snapshotNeutral (const std::uint8_t* s) noexcept;

// The end-fade table (FUN_0071e060 mode 3, 10 ms at the constructor's 44.1 kHz: 441 entries at every rate)
const std::vector<float>& endTable();

struct Tension
{
    float v = 0.0f, inv = 0.0f, L = 0.0f;
};

// One articulator's settings: a 0xDA record (17 ints) converted by FUN_0071ff00
struct Rec
{
    std::int32_t flags = 0, enabled = 0, attackRaw = 0, amountRaw = 0, lfoAmountRaw = 0;
    int delay = 0, hold = 0, lfoDelay = 0;
    float att = 0, sus = 0, dec = 0, rel = 0, amt = 0, lfoFade = 0, lfoAmt = 0;
    std::uint32_t lfoInc = 0, globalPhase = 0;
    const float* table = nullptr;
    Tension tens[3];

    void build (const std::int32_t* r, int ppq, double tempo) noexcept;
};

struct Settings
{
    std::array<Rec, 5> recs {};
    std::int32_t modX = 256, modY = 0, type = 0;
    bool hasGlobal = false;
    // gain/cutoff ramp limit per sample: 0.001, or float(float(44100/fs)·0.001) when the plugin hands the library its rate
    // (FUN_005f8470; 3x Osc does, DX10 does not)
    float rampLimit = 0.001f;
    // the library's rate ratio float(44100/fs) (0x77e6f4, set by FUN_005f8470 like the ramp limit; 1 when never set, as in
    // DX10): the filter coefficients scale with it
    float rateRatio = 1.0f;

    void build (const std::uint8_t* s, int ppq, double tempo) noexcept;
    // ProcessEvent 0 / SetTimeSig: new tick counts, global LFO phases kept (FUN_00722d80)
    void retime (const std::uint8_t* s, int ppq, double tempo) noexcept;
    // NewTick: global LFOs advance (only once the processor has listed them)
    void advanceGlobal() noexcept;
};

// Per-voice articulator state (FUN_007202c0 / FUN_0071f9b0 / FUN_007201e0)
struct Art
{
    int cnt = 0, state = 1, lfoDelay = 0;
    std::uint32_t phase = 0;
    float pos = 0, val = 0, fade = 0, lfo = 0;

    void init (const Rec& s) noexcept;
    void step (const Rec& s) noexcept;
    void release (const Rec& s) noexcept; // to the release stage from the current value (states below 7)
};

// Per-voice processor (FUN_00722070 …)
struct Proc
{
    std::array<Art, 5> arts {};
    int first = 2; // +0x170: 2 = the volume articulator starts above "attack" (gains start at their target)
    bool gainsSet = false, released = false;
    float gL = 0, gR = 0;
    float pan = 0, vol = 1, cut = 0, res = 0, pitch = 0;
    int kind = 0, stage2 = 0;
    bool active = false;
    float fc = 0, fq = 0;
    float b[5] {};
    double fst[4] {};
    float st1[16] {}, st2[16] {};
    int endPos = -1;

    void init (const Settings& S) noexcept;
    // FUN_00722830: one articulator step; pitch (cents) to add to the host's Final.Pitch
    void control (const Settings& S) noexcept;
    // FUN_0072c050: Voice_Release through the processor — volume/cutoff/resonance/pitch articulators released, the end
    // fade at once when the volume envelope is off
    void release (const Settings& S) noexcept;
    // FUN_0072c0d0: released, end fade started (voice stealing)
    void kill() noexcept;
};

// FUN_00722290 and what follows the plugin's Voice_Render: pan-law gains from Final.Vol/Final.Pan and the articulators,
// the channel filter (x may be replaced by `scratch`), the end fade, the ramped mix into l/r (added). x and scratch are
// interleaved stereo blocks of n frames. Returns true when the end fade has finished (the voice is done).
bool mixVoice (const Settings& S, Proc& pr, float* x, float* scratch, int n, float finalVol, float finalPan, float* l, float* r) noexcept;
} // namespace lynx::flvp
