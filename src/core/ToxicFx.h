#pragma once

// FL Studio Toxic Biohazard's output effects rack, rebuilt from docs/analise/REA_rodada45.md (clean-room; a port of our
// own reference model r45/fx/toxfx.py, which was checked bit for bit against the installation's
// Toxic Biohazard_x64.dll by calling the plug-in's own rack on test signals).
//
// Chain on the summed voices, per engine chunk (REA_rodada45 §FX): distortion + DC blocker → effect slot 1 → effect
// slot 2 → 8-band EQ, plus the rack's silence/sleep logic. The engine calls the rack in chunks of at most 64 samples;
// the caller does the chunking. The master gain after the rack is not part of this class.

#include <memory>

namespace lynx
{
// the plug-in's own CRT powf (MSVC SSE2 path at 0x180057f90, not correctly rounded; REA_rodada45 §FX.8), shared with
// the voice engine (core/Toxic.cpp)
float toxicPowf (float x, float y) noexcept;

class ToxicFxRack
{
public:
    ToxicFxRack();
    ~ToxicFxRack();
    ToxicFxRack (const ToxicFxRack&) = delete;
    ToxicFxRack& operator= (const ToxicFxRack&) = delete;

    // fresh state, as after construction (allocates)
    void reset();
    // what the engine does on a sample-rate change (FUN_180040ff0's rack part): the rack's rate set-up FUN_1800384b0
    // followed by the rack's own clear FUN_180038450 (states cleared, sleep state entered); allocates
    void setSampleRate (double sampleRate);
    // tempo in BPM (the delay's tempo sync); the engine pushes it before every chunk, default 120
    void setTempo (double bpm) noexcept;
    // plug-in parameter indices and the values the engine's setter receives (REA_rodada45 §FX.1):
    // 1 Distortion, 227 EQ On, 228–235 EQ bands, 236/237 Fx1/Fx2 enable, 238–293 slot parameters (even index → slot 1,
    // odd → slot 2), 304/305 delay sync (0 off, 1…9), 314/315 slot type (0 delay, 1 chorus, 2 reverb, 3 flanger,
    // 4 phaser, 5 lo-fi; applied at the next process call). Other indices are ignored.
    void setParam (int index, float value) noexcept;
    // in place; n ≥ 0 (the engine uses chunks of ≤ 64 samples). No allocation: a slot-type change re-initialises the
    // delay lines inside storage reserved by setSampleRate (a type set before the first setSampleRate waits for it).
    void process (float* left, float* right, int n) noexcept;
    // false only while the rack sleeps (REA_rodada45 §FX.7: after 2.1 s of quiet output its effects are cleared and
    // skipped) and its DC blocker has settled to 0 — then silent input gives exactly silent output
    bool active() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};
} // namespace lynx
