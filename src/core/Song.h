#pragma once

// Song data model. Does not depend on JUCE.
// The UI edits a copy and publishes an immutable snapshot to the audio thread
// (see SnapshotExchange.h), so nothing here needs to be thread-safe.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lynx
{
constexpr int ppq = 960;              // ticks per quarter note
constexpr int stepsPerBeat = 4;       // step sequencer in sixteenth notes
constexpr int ticksPerStep = ppq / stepsPerBeat;
constexpr int maxMixerTracks = 128;   // 0 = Master, 1..127 = inserts (FL has 125 + master)
constexpr float unityFader = 0.8f;    // fader position that gives 0 dB, as in FL

// FL mixer fader curve: g = (11^v - 1) / 10 with v = position * 1.25 (0 dB at 0.8;
// +5.6 dB at maximum). docs/analise/REA_rodada2.md §2.2.
float faderGain (float position) noexcept;

// Channel volume with the same curve, but 1.0 = 0 dB (FL's default, 0.78, gives -5.2 dB).
// docs/analise/REA_rodada3.md §2.
float channelGain (float volume) noexcept;

enum class InstrumentKind : std::uint8_t
{
    kick,
    snare,
    hat,
    clap,
    synth,
    sampler,
    audioClip, // sampler without transposition that stops at the end of the clip (audio clip channel, as in FL)
    plugin,    // external instrument (VST3), created by the app
    controller, // makes no sound: notes trigger envelopes that modulate other parameters (FL's Envelope Controller)
    sakura,     // physical string model (FL's Sakura engine, REA_rodada28 §1); parameters in ChannelDef::modelParams
    drumpad,    // physical drum model (FL's Drumpad engine, REA_rodada28 §2); parameters in ChannelDef::modelParams
    boobass,    // FL's BooBass engine (REA_rodada29 §4); multisample read from the user's DLL (samplePath), 3 knobs
    drumsynth,  // FL's Fruity DrumSynth Live (REA_rodada30 §1): 120 patches in modelParams, recorded waves from the user's DLL
    harmless,   // FL's Harmless additive engine (REA_rodada32): raw state in modelParams (layout in core/Harmless.h)
    sytrus,     // FL's Sytrus engine (REA_rodada31): the plugin's raw saved state in ChannelDef::pluginState
    harmor,     // FL's Harmor additive engine (REA_rodada34): the plugin's raw saved state in ChannelDef::pluginState
    gms,        // FL's GMS engine (REA_rodada39 §1): raw saved state in pluginState, wave folder in samplePath
    morphine,   // FL's Morphine engine (REA_rodada39 §2): raw saved state in pluginState, noise sample in samplePath
    ogun,       // FL's Ogun inverse-FFT engine (REA_rodada37): the plugin's raw saved state in ChannelDef::pluginState
    minisynth,  // FL's MiniSynth engine (REA_rodada41): raw saved state in pluginState, path of oscdata.dat in samplePath
    dx10,       // FL's Fruity DX10 FM engine (REA_rodada42): the plugin's raw saved state in ChannelDef::pluginState
    kepler,     // FL's Kepler engine (REA_rodada46): raw saved state in pluginState, the user's engine DLL in samplePath
    poizone,    // FL's PoiZone engine (REA_rodada44): the plugin's raw saved state in ChannelDef::pluginState
    fruitkick,  // FL's Fruit Kick (REA_rodada43 §1): raw saved state (with its envelope snapshot) in pluginState
    bassdrum,   // FL's BassDrum (REA_rodada43 §2): raw saved state in pluginState, the BassDrum folder in samplePath
    toxic,      // FL's Toxic Biohazard engine (REA_rodada45): raw saved state in pluginState, the user's DLL in samplePath
    threeosc    // FL's 3x Osc engine (REA_rodada47): normalised state in pluginState, custom-wave sample in samplePath
};

// External plugin: format identifier (the app knows how to find the file) + saved state.
// Third-party plugin coming from an FL project ("Fruity Wrapper"), before finding the same
// installed plugin (docs/analise/REA_rodada5.md). Not saved in .lynx: the app resolves it on import.
struct ForeignPlugin
{
    enum class Format : std::uint8_t { none, vst2, vst3, clap, other };

    Format format = Format::none;
    std::uint32_t vst2Id = 0;                 // AEffect::uniqueID
    std::array<std::uint8_t, 16> classId {};  // VST3 class TUID, as laid out in memory
    bool hasClassId = false;
    std::string clapId, name, path;           // path: binary on the machine that saved it
    std::vector<std::uint8_t> state;          // raw 0x35 record (format depends on `format`)
    int midiInPort = -1, midiOutPort = -1;     // wrapper MIDI ports (record 0x01; −1 = none)

    bool operator== (const ForeignPlugin&) const = default;
};

struct PluginRef
{
    std::string id;    // unique plugin identifier (JUCE's format-name-hash)
    std::string name;  // to display even when the plugin is not installed
    std::string state; // plugin state in base64 (what the plugin returns when saving)
    ForeignPlugin foreign; // FL import not yet resolved (format == none: nothing pending)

    bool operator== (const PluginRef& o) const { return id == o.id && state == o.state && foreign == o.foreign; }
};

// Channel sampler settings (FL's "Channel settings" window).
// Modulation envelope (cutoff or pitch), like the channel envelopes in FL.
struct ModEnvelope
{
    float amount = 0.0f;  // filter: octaves (±); pitch: semitones (±). 0 = off
    float attack = 0.0f;  // seconds
    float hold = 0.0f;
    float decay = 0.3f;
    float sustain = 0.0f; // 0..1
    float release = 0.2f;
    // 0 = LYNX's linear AHDSR; 1 = Toxic Biohazard's filter envelope (REA_rodada33 §2: exponential stages chosen per
    // 128-sample block, attack towards 5 clipped at 1, elapsed time counted in 1/44100 s)
    int shape = 0;

    bool operator== (const ModEnvelope&) const = default;
};

// Channel LFO (like FL's, REA_rodada6 §2): shape, period, delay and fade-in; the amount
// is in the target's unit. Restarts on every note.
struct ChannelLfo
{
    float amount = 0.0f;  // pan: −1..1; volume: depth 0..1; cutoff: octaves; pitch: semitones. 0 = off
    float period = 0.5f;  // seconds, or beats with `sync`
    bool sync = false;
    int shape = 0;        // 0 sine, 1 triangle, 2 pulse
    float delay = 0.0f;   // seconds (or beats with `sync`) before starting
    float attack = 0.0f;  // fade-in, in the same unit
    bool global = false;  // phase locked to the song position (FL's "Global" button) instead of restarting per note
    bool secondsTiming = false; // delay and fade-in stay in seconds even with `sync` (Kepler's LFO1)

    bool operator== (const ChannelLfo&) const = default;
};

enum LfoTarget { lfoPan, lfoVolume, lfoCutoff, lfoPitch, lfoTargets };

struct SamplerSettings
{
    float pitch = 0.0f;   // semitones, -24..24
    float attack = 0.0f;  // seconds, 0..2
    float decay = 0.0f;   // seconds until sustain (0 = no decay)
    float sustain = 1.0f; // 0..1
    float release = 0.0f; // seconds, 0..4; 0 = plays the whole sample (one-shot)
    bool reverse = false;
    bool cut = false;     // a new note cuts the previous one ("cut itself")
    float stretchBeats = 0.0f; // > 0: the whole sample lasts this long (in beats), following the BPM
    bool keepPitch = false;    // with stretch: stretches without changing pitch (WSOLA); otherwise resamples
    float regionStart = 0.0f;  // played region of the sample (fractions 0..1)
    float regionEnd = 1.0f;
    std::vector<float> slices; // slices (starts as fractions): key 60 + i plays slice i, at the original pitch
    bool porta = false;        // portamento (with "cut", the 808 glide): the note starts from the previous note's pitch
    float portaBeats = 0.125f; // glide time, in beats
    float cutoff = 20000.0f;   // channel filter (Hz); low-pass at 20 kHz with no resonance = off
    float resonance = 0.0f;    // 0..1
    int filterType = 0;        // 0 low-pass, 1 band-pass, 2 high-pass, 3 band-stop
    ModEnvelope filterEnv;     // shifts the cutoff (octaves)
    ModEnvelope pitchEnv;      // shifts the pitch (semitones)
    std::array<ChannelLfo, lfoTargets> lfo {}; // per LfoTarget

    bool operator== (const SamplerSettings&) const = default;
};

// Channel arpeggiator (FL's Channel settings).
struct ArpSettings
{
    int direction = 0;       // 0 off, 1 up, 2 down, 3 up and down, 4 random
    int range = 1;           // octaves, 1..4
    float stepBeats = 0.25f; // duration of each step in beats (0.25 = sixteenth note)
    float gate = 0.5f;       // fraction of the step during which the note sounds
    int repeat = 1;          // times each note plays before the next one

    bool enabled() const noexcept { return direction > 0; }
    bool operator== (const ArpSettings&) const = default;
};

// Built-in 3-oscillator synth (in the spirit of FL's 3xOsc).
enum class Waveform : std::uint8_t
{
    sine,
    triangle,
    saw,
    square,
    noise,
    spectral // channel harmonic table (SynthSettings::spectrum): imported additive timbres (Harmless)
};

struct SynthOscillator
{
    Waveform wave = Waveform::saw;
    int octave = 0;      // -3..3
    float detune = 0.0f; // cents, -100..100
    float level = 0.0f;  // 0..1
    int semitones = 0;   // -11..11, added to the octave (3x Osc's "coarse" goes up to ±24)
    float pan = 0.0f;    // -1..1, linear balance as in FL

    bool operator== (const SynthOscillator&) const = default;
};

struct SynthSettings
{
    std::array<SynthOscillator, 3> osc { SynthOscillator { Waveform::saw, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    float attack = 0.005f; // seconds
    float decay = 0.3f;
    float sustain = 1.0f;  // 0..1
    float release = 0.25f;
    float cutoff = 2500.0f;  // Hz
    float resonance = 0.0f;  // 0..1
    int filterType = 0;      // 0 low-pass, 1 band-pass, 2 high-pass, 3 band-stop
    bool ladder = false;     // 4-pole ladder (Toxic Biohazard's filter, REA_rodada26 §2) instead of the 2-pole SVF
    int chorus = 0;          // Kepler's chorus on the channel output: 0 off, 1 I, 2 II, 3 I+II (REA_rodada26 §3)
    // Harmless (REA_rodada27 §1.1/§1.2): stereo tremolo depth d (2.5 Hz, |1.25·cos|/|1.25·sin| per side) and vibrato depth
    // in cents (6 Hz), both on free-running global phases; a voice fades into them over 0.1 s / 1/3 s (r²)
    float stereoTremolo = 0.0f;
    float vibratoCents = 0.0f;
    float keyTrack = 0.0f;   // cutoff follows the note: octaves per octave from key 48 (C3, 130.81 Hz)
    // kick pitch sweep: f = f_note·(1 + sweep·s⁴), s = e^(−t/sweepTime) (BassDrum's "Peak"/"Slide")
    float sweep = 0.0f;      // 0 = off
    float sweepTime = 0.1f;  // seconds (time constant of s)
    // 3-band tone control after the filter (BooBass's EQ): low < ~178 Hz < mid < ~1.18 kHz < high,
    // split by 1-pole low-passes; linear gains (1, 1, 1 = off)
    std::array<float, 3> bandGain { 1.0f, 1.0f, 1.0f };
    // "spectral" shape: amplitude and phase (in cycles) of harmonics 1…256, as a sum of sines
    static constexpr int spectrumSize = 256;
    std::array<float, spectrumSize> spectrum {};
    std::array<float, spectrumSize> spectrumPhase {};
    // per-harmonic decay: seconds for harmonic h to fall 60 dB from the note start (0 = holds); lets the
    // spectrum evolve during the note (Ogun's per-partial decay)
    std::array<float, spectrumSize> spectrumDecay {};
    // level each harmonic decays towards (0 = silence): A(t) = target + (A₀ − target)·10^(−3t/decay) — Ogun's ALT DECAY
    std::array<float, spectrumSize> spectrumTarget {};
    ModEnvelope filterEnv;   // shifts the cutoff (octaves)
    ModEnvelope pitchEnv;    // shifts the pitch (semitones)
    std::array<ChannelLfo, lfoTargets> lfo {}; // per LfoTarget
    bool mono = false;       // single voice: the new note takes over the sounding voice (no click)
    bool porta = false;       // portamento: the new note glides from the previous one
    float portaBeats = 0.125f; // glide time, in beats (FL's default: half a step)
    bool am = false;
    bool ring = false;       // with `am`: Sytrus ring modulation, (osc 1 + 2)·(1 − g3 + g3·osc 3) instead of 1 − g3·osc 3         // osc 3 modulates the amplitude of osc 1 + osc 2 (3x Osc's "AM")
    float fm = 0.0f;         // FM index: osc 3 (muted) shifts the phase of osc 1 + 2 by fm·output cycles; 0 = off
    float fmDecay = 0.0f;    // seconds for the index to fall to fmSustain (0 = constant index)
    float fmSustain = 1.0f;  // fraction of the index that remains after the decay

    bool operator== (const SynthSettings&) const = default;
};

// Zone of a multi-sample sampler (like FL's DirectWave/FPC zones): the sample plays on the keys and
// velocities in range, tuned relative to the root note.
struct SampleZone
{
    std::string path;
    int rootKey = 60;
    int lowKey = 0, highKey = 127;
    int lowVelocity = 0, highVelocity = 127; // 0..127
    float tune = 0.0f;  // semitones
    float gain = 1.0f;
    std::int64_t loopStart = 0, loopEnd = 0; // in sample frames; loopEnd <= loopStart = no loop
    int loopMode = 0;   // 0 no loop, 1 continuous, 2 while the note is held

    bool operator== (const SampleZone&) const = default;
};

struct ChannelDef
{
    std::string name;
    InstrumentKind instrument = InstrumentKind::synth;
    float volume = 0.78f;  // 0..1 (channelGain curve; 0.78 = FL's default)
    float pan = 0.0f;      // -1..1
    bool muted = false;
    std::string samplePath; // used when instrument == sampler
    std::vector<SampleZone> zones; // multi-sample sampler (empty: samplePath only)
    int mixerTrack = 0;     // 0 = straight to Master
    PluginRef plugin;       // used when instrument == plugin
    std::vector<std::uint8_t> controllerState; // instrument == controller: FL Envelope Controller state
    std::vector<float> modelParams; // instrument == sakura / drumpad / boobass / drumsynth / harmless: FL's raw state parameters
    std::vector<std::uint8_t> pluginState; // instrument == sytrus / harmor / ogun / gms / morphine / fruitkick / bassdrum: the FL plugin's saved state (record 0xD5)
    SamplerSettings sampler; // used when instrument == sampler
    SynthSettings synth;     // used when instrument == synth
    float swingMix = 1.0f;   // how much of the global swing applies to this channel (0..1), as in FL
    ArpSettings arp;
    int cutGroup = 0;        // playing this channel cuts channels with an equal cutBy (0 = none)
    int cutBy = 0;
    std::uint32_t colour = 0; // 0xFFRRGGBB; 0 = theme default
};

enum class EffectKind : std::uint8_t
{
    eq,
    compressor,
    delay,
    reverb,
    plugin, // external effect (VST3/CLAP), created by the app
    limiter,
    filter,
    distortion,
    chorus,
    parametricEq, // 7 bands (like FL's Parametric EQ 2)
    utility,      // gain, pan and stereo matrix (FL's Balance, PanOMatic, Stereo Shaper)
    volumeGate,   // per-step gain synced to the bar (Gross Beat's volume)
    peakController, // does not change the sound: measures the peak and serves as a source for live automation (FL's Peak Controller)
    pitchShift,   // changes pitch without changing duration (two read heads crossfaded)
    freqShift,    // shifts all frequencies by Hz (single sideband, does not preserve harmonics)
    parallel,     // container: parallel effect branches summed (FL's Patcher); see EffectSlot::branches
    lofi,         // sample-rate reduction (sample and hold) and bit reduction (Lo-fi from FL's Effector)
    effector,     // FL's Effector: 12 algorithms with X/Y, LFO and Mod Rate (core/Effector.h)
    crossover,    // one band of a frequency splitter (FL's Frequency Splitter): Linkwitz-Riley high-pass and/or low-pass
    rhythmGain,   // gain following a curve of points in beats, looped, locked to the song (Love Philter envelopes)
    autoPan,      // pan moved by a triangle LFO locked to the song (Love Philter pan LFO)
    vocoder,      // Fruity Vocoder channel vocoder: L modulates, R is the carrier (mono output)
    pitchCorrect, // pitch correction (FL's Pitcher): detects the pitch and pulls it to the nearest allowed note
    squeeze,      // FL's Fruity Squeeze, exact (REA_rodada29 §2): quantiser, periodic gate, filter; raw values / max
    vintagePhaser, // FL's Vintage Phaser (REA_rodada29 §3, core/VintagePhaser.h): 36 normalised engine values
    vocodex,      // FL's Vocodex (REA_rodada35, core/Vocodex.h): built from FL's raw state (EffectSlot::state)
    maximus,      // FL's Maximus / Soundgoodizer engine (REA_rodada36/38, core/MaximusFx.h): built from EffectSlot::state
    fruityLimiter,   // FL's Fruity Limiter, exact (REA_rodada48, core/FruityDynamics.h): FL's raw controls / range
    fruityCompressor, // FL's Fruity Compressor, exact (REA_rodada48, core/FruityDynamics.h): FL's raw controls / range
    fruityWaveShaper, // FL's Fruity WaveShaper, exact (REA_rodada51, core/FruityShapers.h): curve in EffectSlot::state, controls / range
    fruitySoftClipper, // FL's Fruity Soft Clipper, exact (REA_rodada51, core/FruityShapers.h): threshold, post / range
    reeverb2,     // FL's Fruity Reeverb 2, exact (REA_rodada49, core/Reeverb2.h): FL's 15 controls + 2 options, (raw − min) / range
    reeverb1,     // FL's Fruity Reeverb (first version), exact (REA_rodada49 §6, core/Reeverb2.h): FL's 10 controls, raw / max
    fruityDelay2, // FL's Fruity Delay 2, exact (REA_rodada52, core/FruityDelay.h): FL's 8 raw controls / range
    fruityDelay3, // FL's Fruity Delay 3, exact (REA_rodada52, core/FruityDelay.h): FL's 26 raw controls / range
    stereoShaper, // FL's Fruity Stereo Shaper, exact (REA_rodada54, core/StereoFx.h): FL's 8 controls / range + difference flag
    stereoEnhancer, // FL's Fruity Stereo Enhancer, exact (REA_rodada54, core/StereoFx.h): FL's 6 controls / range
    transientProcessor, // FL's Transient Processor, exact (REA_rodada54, core/StereoFx.h): FL's 9 controls / range
    spreader,     // FL's Spreader, exact (REA_rodada54, core/StereoFx.h): FL's 5 controls / range
    fruityChorus, // FL's Fruity Chorus, exact (REA_rodada59 §1, core/ChorusFx.h): controls / range
    hyperChorus,  // FL's Hyper Chorus, exact (REA_rodada59 §2, core/ChorusFx.h): (raw − min) / range; instance in EffectSlot::state
    vintageChorus // FL's Vintage Chorus, exact (REA_rodada59 §3, core/ChorusFx.h): (raw − min) / range; instance in EffectSlot::state
};

constexpr int maxEffectParams = 48; // the 7-band parametric EQ and the Pitcher use most of them
constexpr int maxEffectSlots = 10; // as in FL
using EffectParams = std::array<float, maxEffectParams>;

struct EffectSlot;

// A branch of the `parallel` container: a chain of inner effects and the gain it enters the sum with
// (empty chain = the dry signal).
struct EffectBranch
{
    std::vector<EffectSlot> chain;
    float gain = 1.0f;
};

// Live modulation inside a `parallel` container: the control output of child `source` of branch `branch`
// drives parameter `param` of child `target` of the same branch, through `curve` (33 points over 0..1).
struct EffectLink
{
    int branch = 0, source = 0, target = 0, param = 0;
    std::vector<float> curve;
};

struct EffectSlot
{
    EffectKind kind = EffectKind::eq;
    bool enabled = true;
    EffectParams params {}; // 0..1; meaning depends on the effect
    float mix = 1.0f;       // slot dry/wet (as in FL): 0 = dry signal only
    PluginRef plugin;                             // used when kind == plugin
    int sidechain = -1;   // mixer track whose (post-fader) output feeds the effect's detector (compressor); −1 = none
    int sidechain2 = -1;  // second side input (vocoder carrier); −1 = none
    std::vector<EffectBranch> branches;           // used when kind == parallel
    std::vector<EffectLink> links;                // used when kind == parallel
    std::vector<std::uint8_t> state;              // FL's raw plugin state, for exact effects built from it (vocodex, maximus)
};

// Send from one track to another (post-fader), like FL's routing between inserts.
struct MixerSend
{
    int target = 0;      // destination track (> 0; Master is `toMaster`)
    float level = 1.0f;  // 0..1 (linear gain)

    bool operator== (const MixerSend& o) const noexcept { return target == o.target && level == o.level; }
};

// Built-in EQ of each mixer track (FL's 3 bands: low shelf, peak, high shelf),
// after the effect slots and before the fader (REA_rodada6 §1).
struct TrackEq
{
    std::array<float, 3> gainDb { 0.0f, 0.0f, 0.0f };          // ±18
    std::array<float, 3> hz { 90.0f, 1500.0f, 8000.0f };       // 10 Hz .. 16 kHz
    std::array<float, 3> width { 0.267f, 0.267f, 0.267f };     // 0..1: peak Q / shelf slope

    bool active() const noexcept { return gainDb[0] != 0.0f || gainDb[1] != 0.0f || gainDb[2] != 0.0f; }
    bool operator== (const TrackEq&) const = default;

    // FL conversions: peak Q = 0.0999·(1001^w − 1) + 0.1; shelves S = 1.9·w + 0.1
    static double peakQ (float w) noexcept;
    static double shelfSlope (float w) noexcept;
};

struct MixerTrack
{
    std::string name;
    float volume = unityFader; // 0..1; gain = volume / unityFader
    float pan = 0.0f;          // -1..1
    bool muted = false;
    bool solo = false;
    std::vector<EffectSlot> effects; // up to maxEffectSlots, processed in order
    bool toMaster = true;            // inserts: output to Master (besides the sends)
    float stereo = 0.0f;             // separation: -1 = widest, 0 = normal, +1 = mono (as in FL)
    std::uint32_t colour = 0;        // 0xFFRRGGBB; 0 = theme default
    bool invertPhase = false;
    bool swapChannels = false;
    std::vector<MixerSend> sends;    // to other inserts
    TrackEq eq;
};

struct Note
{
    std::int64_t start = 0;   // ticks
    std::int64_t length = ticksPerStep;
    int channel = 0;
    int key = 60;             // MIDI; 60 = C4 (FL's C5)
    float velocity = 0.78f;   // 0..1
    float pan = 0.0f;         // -1..1 (per note, as in FL)
    float finePitch = 0.0f;   // cents of a semitone, -120..120
    bool muted = false;       // disabled note (does not play, stays in the pattern)
    bool slide = false;       // glides the sounding note to this pitch (without retriggering), as in FL
    bool porta = false;       // inverts the channel's porta on this note (the note glides from the previous one; as in FL)
    float release = 0.5f;     // release velocity 0..1 (Note inspector; stored, not yet used by the instruments)
    float modX = 0.5f, modY = 0.5f; // two free per-note modulation values 0..1 (stored, not yet routed)
};

// Swing delay (FL's formula, docs/analise/REA_rodada3.md §3): only notes whose nearest step
// is odd move, with triangular weight by distance to the grid; half a step at swing 1.
std::int64_t swingDelay (std::int64_t start, float swing, float channelMix = 1.0f) noexcept;

struct Pattern
{
    std::string name;
    std::vector<Note> notes;  // sorted by start (kept by sortNotes)
    std::int64_t lengthTicks = 0; // 0 = automatic (rounds up to bars)
    std::uint32_t colour = 0;     // 0xFFRRGGBB; 0 = automatic (by index)

    void sortNotes();
    std::int64_t effectiveLength (int beatsPerBar) const noexcept;

    bool hasStep (int channel, int step) const noexcept;
    void toggleStep (int channel, int step);
};

enum class ClipKind : std::uint8_t
{
    pattern,
    audio,
    automation
};

// What an automation clip controls. Values always 0..1 (like the model's knobs).
enum class AutomationTargetKind : std::uint8_t
{
    none,          // target removed (the clip goes silent)
    channelVolume, // index = channel
    channelPan,    // index = channel (0..1 -> -1..1)
    channelParam,  // index = channel with plugin; param = plugin parameter index
    mixerVolume,   // index = mixer track (0 = Master)
    mixerPan,      // index = track
    effectParam,   // index = track, slot, param (built-in effect or plugin)
    tempo,         // song tempo: 0..1 = minTempo..maxTempo BPM
    effectMix      // index = track, slot: slot dry/wet (0..1)
};

constexpr double minTempo = 10.0, maxTempo = 522.0; // FL's range
inline double tempoFromValue (float v) noexcept { return minTempo + (maxTempo - minTempo) * (double) v; }
inline float valueFromTempo (double bpm) noexcept { return (float) ((bpm - minTempo) / (maxTempo - minTempo)); }

struct AutomationTarget
{
    AutomationTargetKind kind = AutomationTargetKind::none;
    int index = 0;
    int slot = 0;
    int param = 0;

    bool operator== (const AutomationTarget& o) const noexcept
    {
        return kind == o.kind && index == o.index && slot == o.slot && param == o.param;
    }
};

struct AutomationPoint
{
    std::int64_t tick = 0; // from the start of the automation
    float value = 0.5f;    // 0..1
    float tension = 0.0f;  // curve to the next point: -1..1 (0 = straight)
};

struct Automation
{
    std::string name;
    AutomationTarget target;
    std::vector<AutomationPoint> points; // sorted by tick (kept by the editor)

    // Live source (FL controller link, REA_rodada14 §7): the value comes from output `output` of the
    // controller effect in slot (track, slot), passed through the curve (points evenly spaced over 0..1; empty =
    // direct). Uses neither clips nor points.
    struct LiveSource
    {
        int track = -1, slot = 0, output = 0;
        int channel = -1; // instead of an effect: this channel's "controller" instrument
    } source;
    std::vector<float> curve;
    bool isLive() const noexcept { return source.track >= 0 || source.channel >= 0; }
    float mapLive (float v) const noexcept; // applies the curve

    // Value at `tick` (before the first point = first value; after the last = last).
    float valueAt (double tick) const noexcept;
    std::int64_t length() const noexcept; // tick of the last point (minimum 1)
};

// A block in the Playlist: a section of a pattern, or a section of an audioClip channel's audio.
struct Clip
{
    int pattern = 0;
    int track = 0;
    std::int64_t start = 0;   // ticks in the song
    std::int64_t length = 0;  // ticks
    std::int64_t offset = 0;  // how much of the start of the pattern/audio is skipped (ticks)
    ClipKind kind = ClipKind::pattern;
    int channel = -1;         // audio clips: channel (audioClip) that holds the sample
    int automation = -1;      // automation clips: index into Song::automations
    std::int64_t fadeIn = 0, fadeOut = 0; // audio clips: fades (ticks)
    float gain = 1.0f;        // audio clips: clip volume
    bool muted = false;       // clip-level mute (the Mute tool); independent of the track mute

    std::int64_t end() const noexcept { return start + length; }
};

enum class PlayMode : std::uint8_t
{
    pattern, // loops the current pattern (like FL's "PAT")
    song     // plays the Playlist (like FL's "SONG")
};

// MIDI controller (CC) linked to a parameter (FL's "Link to controller").
struct MidiLink
{
    int cc = 0;       // 0..127
    int channel = -1; // MIDI channel 0..15, -1 = any
    AutomationTarget target;
};

// Header of a Playlist track.
struct PlaylistTrack
{
    std::string name;  // empty = "Track N"
    bool muted = false; // clips on this track don't play
};

// Splits a clip at `tick` (absolute): the original keeps the start and the return value is the rest.
// `tick` must be inside the clip.
Clip splitClip (Clip& clip, std::int64_t tick);

// A Playlist arrangement (FL allows several per project).
// Playlist marker (like FL's "time markers"). With numerator > 0 it is a time signature
// change: a new bar starts there, with numerator/denominator.
struct Marker
{
    std::int64_t tick = 0;
    std::string name;
    int numerator = 0, denominator = 4;

    bool isSignature() const noexcept { return numerator > 0; }
    bool operator== (const Marker&) const = default;
};

// A bar on the ruler: where it starts, how many beats (quarter notes) it has and its number (1, 2, ...).
struct Bar
{
    std::int64_t tick = 0, length = 0;
    int number = 1;
};

struct Arrangement
{
    std::string name;
    std::vector<Clip> clips;               // empty for the current arrangement: its clips live in Song::playlist
    std::int64_t loopStart = 0, loopEnd = 0;
    std::vector<Marker> markers;           // sorted by tick (stay here even for the current arrangement)
};

struct Song
{
    double bpm = 130.0;
    int beatsPerBar = 4;
    float swing = 0.0f; // 0..1: delays off-beat sixteenth notes (like the swing of FL's Channel Rack)
    float masterPitch = 0.0f; // cents, -1200..1200: shifts every note of the built-in instruments (the toolbar's master pitch)
    std::vector<ChannelDef> channels;
    std::vector<Pattern> patterns;
    int currentPattern = 0;

    std::vector<Clip> playlist;
    PlayMode mode = PlayMode::pattern;
    std::int64_t loopStart = 0, loopEnd = 0; // looped section in the Playlist (ticks); loopEnd <= loopStart = off

    // Arrangements: `playlist` and the loop above belong to the current one; the others are stored here.
    std::vector<Arrangement> arrangements { Arrangement { "Arrangement 1", {}, 0, 0, {} } };
    int currentArrangement = 0;

    void selectArrangement (int index);
    int addArrangement (const std::string& name, bool copyCurrent = false); // returns the index (already selected)
    void removeArrangement (int index);
    // Applies `fn` to all targets (automations and MIDI controllers).
    template <typename Fn>
    void forEachTarget (Fn&& fn)
    {
        for (auto& a : automations)
            fn (a.target);
        for (auto& l : midiLinks)
            fn (l.target);
    }
    void removeDeadMidiLinks();

    // Applies `fn` to all clip lists (the current one and the stored ones).
    template <typename Fn>
    void forEachClipList (Fn&& fn)
    {
        fn (playlist);
        for (std::size_t i = 0; i < arrangements.size(); ++i)
            if ((int) i != currentArrangement)
                fn (arrangements[i].clips);
    }

    bool hasLoop() const noexcept { return loopEnd > loopStart; }

    // Markers of the current arrangement.
    std::vector<Marker>& markers();
    const std::vector<Marker>& markers() const;
    void addMarker (Marker marker); // keeps the order

    // Bars that play in [from, to), taking the markers' time signature changes into account
    // (before the first one, beatsPerBar/4 applies).
    std::vector<Bar> barsBetween (std::int64_t from, std::int64_t to) const;
    Bar barAt (std::int64_t tick) const;

    std::vector<MixerTrack> mixer; // [0] = Master; empty = everything straight to the output
    std::vector<Automation> automations;
    std::vector<PlaylistTrack> playlistTracks; // index = track; tracks past the end use the default
    std::vector<MidiLink> midiLinks;

    bool isTrackMuted (int track) const noexcept
    {
        return track >= 0 && track < (int) playlistTracks.size() && playlistTracks[(std::size_t) track].muted;
    }

    static std::vector<MixerTrack> defaultMixer (int inserts = 16);
    // First insert that no channel uses (or the last one, if all are taken).
    int nextFreeInsert() const;

    // End of the last clip, rounded up to a bar (minimum 1 bar).
    std::int64_t songLength() const noexcept;

    // Removes a pattern and fixes the clips and the current index.
    void removePattern (int index);

    // Removes a channel along with its notes and audio clips, and reindexes the others.
    // Automations of that channel lose their target.
    void removeChannel (int index);

    // Routing between inserts. canRoute = valid destination that doesn't create a cycle.
    bool canRoute (int from, int to) const;
    // Enables/disables the send from -> to (to == 0 = Master). Does nothing if it would create a cycle.
    void setRoute (int from, int to, bool enabled, float level = 1.0f);
    bool routes (int from, int to) const noexcept;
    // Inserts (1..N) in processing order: senders come before receivers.
    std::vector<int> mixerOrder() const;
    // Tracks audible with the current solo: the soloed ones, those that feed them and the path to Master.
    // Without solo, all of them. Index = track.
    std::vector<bool> soloAudible() const;

    // Swaps/removes effect slots keeping automations pointing at the right effect.
    void swapEffectSlots (int track, int a, int b);
    void removeEffectSlot (int track, int slot);

    // Removes an automation and its clips.
    void removeAutomation (int index);

    // Current value (from the model, without automation) of the target, 0..1; -1 if the target doesn't exist.
    float targetValue (const AutomationTarget& target) const noexcept;
    // Changes the target's value in the model (0..1). Plugin parameters don't live here: returns false.
    bool setTargetValue (const AutomationTarget& target, float value) noexcept;

    // Value of an automation at a song position (the same one playback uses); NaN without clips.
    float automationValueAt (int automation, double tick) const;
    // Tempo at `tick`, taking tempo automation into account (in song mode).
    double tempoAt (double tick) const;
    // Duration in seconds of the first `ticks` of the song, integrating the tempo automation.
    double secondsForTicks (std::int64_t ticks) const;

    static Song makeDefault();
};

// Copy of the song ready for export: always in song mode (the current pattern becomes a clip
// if `wholeSong` is false or the Playlist is empty), with `tailTicks` of silence at the end
// so releases and reverbs ring out without the song restarting. `musicLength` receives the end of the
// musical part (without the tail), in ticks.
Song makeExportSong (const Song& song, bool wholeSong, std::int64_t tailTicks, std::int64_t& musicLength);

// Copy of the song in which only the (post-fader) output of insert `track` is heard, straight to Master
// without effects or fader: this is what gets rendered to "turn the insert into audio".
Song makeBounceSong (const Song& song, int track);

// After rendering the insert to `samplePath` (`length` ticks from the start): creates an
// audio clip channel on a new insert with the same routing, puts the clip in the Playlist and mutes
// the original insert. Returns the new insert.
int applyBounce (Song& song, int track, const std::string& samplePath, std::int64_t length);
} // namespace lynx
