#include "ui/WindowOwner.h"
#include "ui/ChannelSettings.h"

#include <cstring>
#include <functional>

#include "core/DrumSynthLive.h"
#include "core/FlSynths.h"
#include "core/FlpImport.h"
#include "core/Harmor.h"
#include "core/SynthPresets.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// Cutoff and pitch envelopes (FL's channel envelopes), used by the synth and the sampler.
class ModEnvelopeEditor final : public juce::Component
{
public:
    std::function<void()> onChange;
    static constexpr int preferredHeight = 16 + 2 * 62;

    ModEnvelopeEditor()
    {
        const juce::String titles[] = { tr ("Cutoff env."), tr ("Pitch env.") };

        for (int e = 0; e < 2; ++e)
        {
            auto& row = rows[(std::size_t) e];
            row.title.setText (titles[e], juce::dontSendNotification);
            row.title.setFont (theme::font (theme::type::small));
            addAndMakeVisible (row.title);
            knob (row.amount, e == 0 ? -10.0 : -48.0, e == 0 ? 10.0 : 48.0, 0.01, e == 0 ? " oct" : " st");
            row.amount.setDoubleClickReturnValue (true, 0.0);
            row.amount.setTooltip (tr ("0 = off"));

            for (auto* s : { &row.attack, &row.hold, &row.decay, &row.release })
            {
                knob (*s, 0.0, 10.0, 0.001, " s");
                s->setSkewFactorFromMidPoint (0.5);
            }

            knob (row.sustain, 0.0, 1.0, 0.01, "");
        }

        const char* names[] = { TR_NOOP ("Amount"), TR_NOOP ("Attack"), TR_NOOP ("Hold"), TR_NOOP ("Decay"), TR_NOOP ("Sustain"), TR_NOOP ("Release") };

        for (int i = 0; i < 6; ++i)
        {
            labels[(std::size_t) i].setText (tr (names[i]), juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (theme::font (theme::type::caption));
            addAndMakeVisible (labels[(std::size_t) i]);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (16);
        header.removeFromLeft (80);
        const auto w = header.getWidth() / 6;

        for (auto& l : labels)
            l.setBounds (header.removeFromLeft (w));

        for (auto& row : rows)
        {
            auto line = area.removeFromTop (62);
            row.title.setBounds (line.removeFromLeft (80).withSizeKeepingCentre (80, 24));
            for (auto* s : { &row.amount, &row.attack, &row.hold, &row.decay, &row.sustain, &row.release })
                s->setBounds (line.removeFromLeft (w));
        }
    }

    void show (const ModEnvelope& filter, const ModEnvelope& pitch)
    {
        rows[0].show (filter);
        rows[1].show (pitch);
    }

    ModEnvelope filter() const { return rows[0].read(); }
    ModEnvelope pitch() const { return rows[1].read(); }

private:
    struct Row
    {
        juce::Label title;
        juce::Slider amount, attack, hold, decay, sustain, release;

        void show (const ModEnvelope& e)
        {
            amount.setValue (e.amount, juce::dontSendNotification);
            attack.setValue (e.attack, juce::dontSendNotification);
            hold.setValue (e.hold, juce::dontSendNotification);
            decay.setValue (e.decay, juce::dontSendNotification);
            sustain.setValue (e.sustain, juce::dontSendNotification);
            release.setValue (e.release, juce::dontSendNotification);
        }

        ModEnvelope read() const
        {
            return { (float) amount.getValue(), (float) attack.getValue(), (float) hold.getValue(),
                     (float) decay.getValue(), (float) sustain.getValue(), (float) release.getValue() };
        }
    };

    void knob (juce::Slider& s, double lo, double hi, double step, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] {
            if (onChange)
                onChange();
        };
        addAndMakeVisible (s);
    }

    std::array<Row, 2> rows;
    std::array<juce::Label, 6> labels;
};

// Channel LFOs (pan, volume, cutoff, pitch), like FL's.
class LfoEditor final : public juce::Component
{
public:
    std::function<void()> onChange;
    static constexpr int preferredHeight = 18 + lfoTargets * 60;

    LfoEditor()
    {
        const juce::String titles[] = { tr ("Pan LFO"), tr ("Volume LFO"), tr ("Cutoff LFO"), tr ("Pitch LFO") };
        const double amountRange[] = { 1.0, 1.0, 8.0, 24.0 };
        const char* amountSuffix[] = { "", "", " oct", " st" };

        for (int i = 0; i < lfoTargets; ++i)
        {
            auto& row = rows[(std::size_t) i];
            row.title.setText (titles[i], juce::dontSendNotification);
            row.title.setFont (theme::font (theme::type::small));
            addAndMakeVisible (row.title);
            knob (row.amount, -amountRange[i], amountRange[i], 0.01, amountSuffix[i]);
            row.amount.setDoubleClickReturnValue (true, 0.0);
            row.amount.setTooltip (tr ("0 = off"));
            knob (row.period, 0.01, 16.0, 0.001, "");
            row.period.setSkewFactorFromMidPoint (0.5);
            knob (row.delay, 0.0, 8.0, 0.001, "");
            row.delay.setSkewFactorFromMidPoint (0.5);
            knob (row.attack, 0.0, 8.0, 0.001, "");
            row.attack.setSkewFactorFromMidPoint (0.5);
            row.shape.addItemList ({ tr ("Sine"), tr ("Triangle"), tr ("Pulse") }, 1);
            row.shape.onChange = [this] { changed(); };
            addAndMakeVisible (row.shape);
            row.sync.setButtonText (tr ("Beats"));
            row.sync.setTooltip (tr ("Period, delay and fade-in in beats (follows the BPM) instead of seconds"));
            row.sync.onClick = [this] { changed(); };
            addAndMakeVisible (row.sync);
            row.global.setButtonText (tr ("Global"));
            row.global.setTooltip (tr ("Phase follows the song position instead of restarting on each note"));
            row.global.onClick = [this] { changed(); };
            addAndMakeVisible (row.global);
        }

        const juce::String headers[] = { tr ("Amount"), tr ("Period"), tr ("Shape"), tr ("Delay"), tr ("Fade-in"), tr ("Unit") };

        for (int i = 0; i < 6; ++i)
        {
            labels[(std::size_t) i].setText (headers[i], juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (theme::font (theme::type::caption));
            addAndMakeVisible (labels[(std::size_t) i]);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (18);
        header.removeFromLeft (80);
        const auto w = header.getWidth() / 6;

        for (auto& l : labels)
            l.setBounds (header.removeFromLeft (w));

        for (auto& row : rows)
        {
            auto line = area.removeFromTop (60);
            row.title.setBounds (line.removeFromLeft (80).withSizeKeepingCentre (80, 24));
            row.amount.setBounds (line.removeFromLeft (w));
            row.period.setBounds (line.removeFromLeft (w));
            row.shape.setBounds (line.removeFromLeft (w).withSizeKeepingCentre (w - 6, 24));
            row.delay.setBounds (line.removeFromLeft (w));
            row.attack.setBounds (line.removeFromLeft (w));
            auto toggles = line.removeFromLeft (w).withSizeKeepingCentre (w - 6, 50);
            row.sync.setBounds (toggles.removeFromTop (24));
            row.global.setBounds (toggles.removeFromBottom (24));
        }
    }

    void show (const std::array<ChannelLfo, lfoTargets>& lfo)
    {
        for (std::size_t i = 0; i < lfoTargets; ++i)
        {
            auto& row = rows[i];
            const auto& l = lfo[i];
            row.amount.setValue (l.amount, juce::dontSendNotification);
            row.period.setValue (l.period, juce::dontSendNotification);
            row.delay.setValue (l.delay, juce::dontSendNotification);
            row.attack.setValue (l.attack, juce::dontSendNotification);
            row.shape.setSelectedId (l.shape + 1, juce::dontSendNotification);
            row.sync.setToggleState (l.sync, juce::dontSendNotification);
            row.global.setToggleState (l.global, juce::dontSendNotification);
            const auto unit = l.sync ? " t" : " s";
            for (auto* s : { &row.period, &row.delay, &row.attack })
                s->setTextValueSuffix (unit);
        }
    }

    std::array<ChannelLfo, lfoTargets> read() const
    {
        std::array<ChannelLfo, lfoTargets> lfo {};

        for (std::size_t i = 0; i < lfoTargets; ++i)
        {
            const auto& row = rows[i];
            lfo[i] = { (float) row.amount.getValue(), (float) row.period.getValue(), row.sync.getToggleState(),
                       juce::jlimit (0, 2, row.shape.getSelectedId() - 1), (float) row.delay.getValue(), (float) row.attack.getValue(),
                       row.global.getToggleState() };
        }

        return lfo;
    }

private:
    struct Row
    {
        juce::Label title;
        juce::Slider amount, period, delay, attack;
        juce::ComboBox shape;
        juce::ToggleButton sync, global;
    };

    void knob (juce::Slider& s, double lo, double hi, double step, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] { changed(); };
        addAndMakeVisible (s);
    }

    void changed()
    {
        if (onChange)
            onChange();
    }

    std::array<Row, lfoTargets> rows;
    std::array<juce::Label, 6> labels;
};

// Channel modulation window: cutoff/pitch envelopes and LFOs of the synth or the sampler.
class ModulationPanel final : public juce::Component,
                              private juce::ChangeListener
{
public:
    ModulationPanel (AudioEngine& e, int c)
        : engine (e),
          channel (c)
    {
        envelopes.onChange = [this] { apply(); };
        lfos.onChange = [this] { apply(); };
        addAndMakeVisible (envelopes);
        addAndMakeVisible (lfos);
        refresh();
        engine.addChangeListener (this);
        setSize (660, 20 + ModEnvelopeEditor::preferredHeight + 16 + LfoEditor::preferredHeight);
    }

    ~ModulationPanel() override { engine.removeChangeListener (this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        envelopes.setBounds (area.removeFromTop (ModEnvelopeEditor::preferredHeight));
        area.removeFromTop (16);
        lfos.setBounds (area.removeFromTop (LfoEditor::preferredHeight));
    }

    int getChannel() const noexcept { return channel; }

private:
    const ChannelDef* current() const
    {
        const auto& channels = engine.getSong().channels;
        return channel >= 0 && channel < (int) channels.size() ? &channels[(std::size_t) channel] : nullptr;
    }

    bool isSynth() const { return current() != nullptr && current()->instrument == InstrumentKind::synth; }

    void refresh()
    {
        const auto* c = current();

        if (c == nullptr)
            return;

        if (isSynth())
        {
            envelopes.show (c->synth.filterEnv, c->synth.pitchEnv);
            lfos.show (c->synth.lfo);
        }
        else
        {
            envelopes.show (c->sampler.filterEnv, c->sampler.pitchEnv);
            lfos.show (c->sampler.lfo);
        }

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName (tr ("Modulation - {0}", { juce::String::fromUTF8 (c->name.c_str()) }));
    }

    void apply()
    {
        if (current() == nullptr)
            return;

        const auto filter = envelopes.filter(), pitch = envelopes.pitch();
        const auto lfo = lfos.read();
        const auto synth = isSynth();

        engine.editSong ([c = channel, synth, filter, pitch, lfo] (Song& song) {
            auto& ch = song.channels[(std::size_t) c];

            if (synth)
            {
                ch.synth.filterEnv = filter;
                ch.synth.pitchEnv = pitch;
                ch.synth.lfo = lfo;
            }
            else
            {
                ch.sampler.filterEnv = filter;
                ch.sampler.pitchEnv = pitch;
                ch.sampler.lfo = lfo;
            }
        });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    AudioEngine& engine;
    int channel;
    ModEnvelopeEditor envelopes;
    LfoEditor lfos;
};

// "Controller" channel (FL's Envelope Controller): shows the 8 articulators (envelope, base, mode and
// key range) and which automations each output drives. Read-only.
class ControllerPanel final : public juce::Component
{
public:
    ControllerPanel (AudioEngine& e, int c)
        : engine (e),
          channel (c)
    {
        setSize (720, 470);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        const auto& song = engine.getSong();
        if (channel >= (int) song.channels.size())
            return;
        const auto parsed = parseEnvelopeController (song.channels[(std::size_t) channel].controllerState);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::title));
        g.drawText (tr ("Envelope controller: each note on this channel triggers the envelopes; output k drives the linked targets"),
                    10, 6, getWidth() - 20, 20, juce::Justification::centredLeft);
        if (! parsed)
            return;

        for (int k = 0; k < 8; ++k)
        {
            const auto& a = (*parsed)[(std::size_t) k];
            const auto area = juce::Rectangle<float> (10.0f + (float) (k % 4) * 177.0f, 34.0f + (float) (k / 4) * 215.0f, 170.0f, 205.0f);
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (area, theme::radius::lg);

            // targets of this output
            juce::StringArray targets;
            for (const auto& automation : song.automations)
                if (automation.source.channel == channel && automation.source.output == k)
                    targets.add (juce::String::fromUTF8 (automation.name.c_str()));

            g.setColour (targets.isEmpty() ? theme::colour::textSecondary : theme::colour::accent);
            g.setFont (theme::font (theme::type::body, true));
            g.drawText (tr ("Output {0}", { juce::String (k) }) + (targets.isEmpty() ? juce::String() : tr (" (in use)")), area.withHeight (20.0f).reduced (6.0f, 0.0f),
                        juce::Justification::centredLeft);

            const auto graph = area.withTrimmedTop (22.0f).withHeight (110.0f).reduced (6.0f, 2.0f);
            g.setColour (theme::colour::bg0);
            g.fillRect (graph);
            if (a.envOn && a.count > 0)
            {
                const auto total = std::max (0.001f, a.time[(std::size_t) a.count - 1]);
                juce::Path path;
                for (int i = 0; i < a.count; ++i)
                {
                    const auto x = graph.getX() + graph.getWidth() * a.time[(std::size_t) i] / total;
                    const auto y = graph.getBottom() - graph.getHeight() * a.level[(std::size_t) i];
                    if (i == 0)
                        path.startNewSubPath (x, y);
                    else
                        path.lineTo (x, y);
                }
                g.setColour (theme::colour::info);
                g.strokePath (path, juce::PathStrokeType (2.0f));
                if (a.sustain >= 0)
                {
                    g.setColour (theme::colour::textDisabled);
                    g.drawVerticalLine ((int) (graph.getX() + graph.getWidth() * a.time[(std::size_t) a.sustain] / total), graph.getY(), graph.getBottom());
                }
            }
            else
            {
                g.setColour (theme::colour::textDisabled);
                g.drawText (tr ("envelope off"), graph, juce::Justification::centred);
            }

            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption));
            auto line = graph.getBottom() + 4.0f;
            auto text = [&] (const juce::String& t) {
                g.drawText (t, (int) area.getX() + 6, (int) line, (int) area.getWidth() - 12, 14, juce::Justification::centredLeft, true);
                line += 14.0f;
            };
            text (tr ("base {0}, env {1}", { juce::String::formatted ("%.2f", a.base), juce::String::formatted ("%+.2f", a.envLevel) }) + (a.bipolar ? tr (", bipolar") : tr (", unipolar")));
            if (a.envOn && a.count > 0)
                text (tr ("length {0} ", { juce::String::formatted ("%.2f", a.time[(std::size_t) a.count - 1]) }) + (a.tempo ? tr ("beats") : "s")
                      + (a.sustain >= 0 ? tr (", with sustain") : tr (", no sustain")));
            if (a.lowKey > 0 || a.highKey < 127)
                text (tr ("keys {0} to {1}", { juce::String (a.lowKey), juce::String (std::min (a.highKey, 127)) }));
            for (const auto& t : targets)
                text (utf8 ("-> ") + t);
        }
    }

private:
    AudioEngine& engine;
    int channel;
};

// Physical-model channels (Sakura, Drumpad; REA_rodada28): knobs for the main raw FL parameters (0..1)
class ModelPanel final : public juce::Component
{
public:
    ModelPanel (AudioEngine& e, int c)
        : engine (e),
          channel (c)
    {
        const auto& def = engine.getSong().channels[(std::size_t) channel];
        sakura = def.instrument == InstrumentKind::sakura;
        boobass = def.instrument == InstrumentKind::boobass;
        drumsynth = def.instrument == InstrumentKind::drumsynth;
        harmless = def.instrument == InstrumentKind::harmless;
        harmor = def.instrument == InstrumentKind::harmor;
        // DrumSynth Live: the patch on the key selected in FL's editor (raw FL values)
        if (drumsynth)
        {
            if (lynx::drumSynthEditorKeyIndex < (int) def.modelParams.size())
                editorKey = (int) def.modelParams[(std::size_t) lynx::drumSynthEditorKeyIndex];
            if (drumSynthKeyToSlot (editorKey) < 0)
                editorKey = 60;
            wavesFound = ! def.samplePath.empty();
        }
        const auto offset = drumsynth ? drumSynthPatchParams * drumSynthKeyToSlot (editorKey) : 0;
        static const std::pair<int, const char*> sakuraKnobs[] = {
            { 0, "Master" }, { 1, "Tune" }, { 2, "Attack" }, { 3, "Decay" }, { 4, "Sustain" }, { 5, "Release" },
            { 6, "Str1 decay" }, { 9, "Str1 damp" }, { 11, "Str2 decay" }, { 14, "Str2 damp" }, { 16, "Saturation" },
            { 17, "String mix" }, { 18, "Stereo sep" }, { 19, "Comb ratio" }, { 22, "Comb level" }, { 24, "Lo-cut" },
            { 29, "Hi-cut" }, { 30, "Hi-cut res" }, { 31, "Noise level" }, { 32, "Noise rate" } };
        static const std::pair<int, const char*> drumpadKnobs[] = {
            { 2, "Mallet amp" }, { 3, "Mallet decay" }, { 4, "Mallet noise" }, { 6, "Decay" }, { 7, "Cutoff" },
            { 8, "Tension" }, { 9, "Phase" }, { 10, "Material" }, { 11, "Size" }, { 12, "Shape" }, { 13, "Low freq" },
            { 18, "Mid freq" }, { 19, "Mid level" }, { 24, "High freq" }, { 25, "High level" }, { 27, "Lo-fi" },
            { 28, "Hold" }, { 29, "Pitch" } };
        auto add = [&] (int index, const char* name, double low = 0.0, double high = -1.0) {
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            index += offset;
            if (high > low)
                knob->setRange (low, high, 1.0);
            else
                knob->setRange (0.0, boobass ? 65535.0 : 1.0, boobass ? 1.0 : 0.001);
            knob->setValue (index < (int) def.modelParams.size() ? def.modelParams[(std::size_t) index] : 0.0, juce::dontSendNotification);
            knob->setName (name);
            knob->onValueChange = [this, index, k = knob.get()] {
                engine.editSong ([c = channel, index, v = (float) k->getValue()] (Song& song) {
                    auto& params = song.channels[(std::size_t) c].modelParams;
                    if (index < (int) params.size())
                        params[(std::size_t) index] = v;
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        static const std::pair<int, const char*> booBassKnobs[] = { { 0, "Bass" }, { 1, "Mid" }, { 2, "Treble" } };
        struct Ranged { int index; const char* name; double low, high; };
        static const Ranged drumSynthKnobs[] = {
            { 0, "Level", 0, 128 }, { 1, "Pan", -64, 64 }, { 2, "Tone", -50, 50 }, { 3, "Harmonics", -50, 50 },
            { 4, "Osc1 freq", 0, 1000 }, { 7, "Osc1 decay", 0, 127 }, { 8, "Osc1 level", 0, 127 }, { 10, "Osc2 freq", 0, 1000 },
            { 13, "Osc2 decay", 0, 127 }, { 14, "Osc2 level", 0, 127 }, { 17, "Wave mix", 0, 127 }, { 21, "Release", 0, 127 },
            { 23, "Click", 0, 100 }, { 33, "Ring mod", 0, 127 } };
        // Harmless: raw FL values (REA_rodada21/32 ranges)
        static const Ranged harmlessKnobs[] = {
            { 36, "Volume", 0, 2000 }, { 26, "Attack", 0, 1000 }, { 27, "Decay", 0, 1000 }, { 28, "Release", 0, 1000 },
            { 54, "Cutoff", 0, 8192 }, { 59, "Resonance", 0, 1000 }, { 52, "Filter env", -1000, 1000 },
            { 31, "Pluck", 0, 1000 }, { 23, "Mask mix", 0, 1000 }, { 65, "Unison", 1, 9 }, { 67, "Uni detune", 0, 256 },
            { 25, "Tremolo", 0, 1000 }, { 71, "Phaser mix", 0, 1000 }, { 79, "Harmonizer", 0, 1000 } };
        // Harmor: raw FL integers inside the saved state (part A, and the global master / A–B mix; REA_rodada34)
        struct HarmorKnob { int part, index; const char* name; double low, high; };
        static const HarmorKnob harmorKnobs[] = {
            { -1, 12, "Master vol", 0, 2000 }, { -1, 10, "A/B mix", 0, 1000 }, { 0, 11, "A volume", -1000, 1000 },
            { 0, 4, "Timbre mix", 0, 1000 }, { 0, 52, "Cutoff", 0, 8192 }, { 0, 57, "Resonance", 0, 1000 },
            { 0, 75, "Pluck", 0, 1000 }, { 0, 34, "Unison", 1, 9 }, { 0, 38, "Uni detune", 0, 256 },
            { 0, 27, "Harmonizer", 0, 1000 }, { 0, 76, "Phaser mix", 0, 1000 }, { 0, 23, "Harm. blur", 0, 10000 },
            { 0, 45, "Vibrato", 0, 1000 }, { 0, 17, "Tremolo", 0, 1000 } };
        auto addHarmor = [&] (const HarmorKnob& k) {
            const auto at = harmorParamOffset (def.pluginState, k.part, k.index);
            if (at < 0)
                return;
            std::int32_t value = 0;
            std::memcpy (&value, def.pluginState.data() + at, 4);
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            knob->setRange (k.low, k.high, 1.0);
            knob->setValue (value, juce::dontSendNotification);
            knob->setName (k.name);
            knob->onValueChange = [this, k, s = knob.get()] {
                engine.editSong ([c = channel, k, v = (std::int32_t) s->getValue()] (Song& song) {
                    auto& st = song.channels[(std::size_t) c].pluginState;
                    if (const auto o = harmorParamOffset (st, k.part, k.index); o >= 0)
                        std::memcpy (st.data() + o, &v, 4);
                });
            };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        };
        if (harmor)
            for (const auto& k : harmorKnobs)
                addHarmor (k);
        else if (harmless)
            for (const auto& k : harmlessKnobs)
                add (k.index, k.name, k.low, k.high);
        else if (drumsynth)
            for (const auto& k : drumSynthKnobs)
                add (k.index, k.name, k.low, k.high);
        else if (boobass)
            for (const auto& [index, name] : booBassKnobs)
                add (index, name);
        else if (sakura)
            for (const auto& [index, name] : sakuraKnobs)
                add (index, name);
        else
            for (const auto& [index, name] : drumpadKnobs)
                add (index, name);
        setSize (720, 384);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::title));
        g.drawText (drumsynth ? tr ("DrumSynth Live — FL's engine, patch on key {0}", { juce::String (editorKey) })
                                    + (wavesFound ? tr (" (next hit; recorded waves from your FL installation)")
                                                  : tr (" (next hit; recorded waves not found: silent)"))
                    : harmless ? tr ("Harmless — FL's additive engine (chorus/delay/reverb/compressor rack off)")
                    : harmor ? tr ("Harmor — FL's additive engine (effects rack off; image synthesis not decoded)")
                    : boobass ? tr ("BooBass — FL's engine, its sound read from your FL installation (knobs: 3-band EQ)")
                    : sakura ? tr ("Sakura — physical strings (FL's engine; new parameters apply to the next note)")
                             : tr ("Drumpad — physical drum (FL's engine; new parameters apply to the next hit)"),
                    10, 6, getWidth() - 20, 20, juce::Justification::centredLeft);
        g.setFont (theme::font (theme::type::small));
        for (const auto& k : knobs)
            g.drawText (k->getName(), k->getBounds().withHeight (14).translated (0, -14), juce::Justification::centred);
    }

    void resized() override
    {
        constexpr int columns = 7, w = 96, h = 92;
        for (std::size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (12 + (int) (i % columns) * (w + 4), 46 + (int) (i / columns) * (h + 18), w, h);
    }

private:
    AudioEngine& engine;
    int channel;
    bool sakura = true, boobass = false, drumsynth = false, harmless = false, harmor = false, wavesFound = false;
    int editorKey = 60;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
};

class SettingsPanel final : public juce::Component,
                            private juce::ChangeListener
{
public:
    SettingsPanel (AudioEngine& e, int c)
        : engine (e),
          channel (c)
    {
        setup (pitch, pitchLabel, TR_NOOP ("Pitch"), -24.0, 24.0, 0.01, " st");
        setup (attack, attackLabel, TR_NOOP ("Attack"), 0.0, 2.0, 0.001, " s");
        setup (release, releaseLabel, TR_NOOP ("Release"), 0.0, 4.0, 0.001, " s");
        setup (decay, decayLabel, TR_NOOP ("Decay"), 0.0, 10.0, 0.001, " s");
        setup (sustain, sustainLabel, TR_NOOP ("Sustain"), 0.0, 1.0, 0.01, "");
        setup (stretch, stretchLabel, TR_NOOP ("Stretch"), 0.0, 64.0, 0.25, " beats");
        setup (regionStart, regionStartLabel, TR_NOOP ("Start"), 0.0, 1.0, 0.001, "");
        setup (regionEnd, regionEndLabel, TR_NOOP ("End"), 0.0, 1.0, 0.001, "");
        setup (cutoff, cutoffLabel, TR_NOOP ("Cutoff"), 20.0, 20000.0, 1.0, " Hz");
        setup (resonance, resonanceLabel, TR_NOOP ("Resonance"), 0.0, 1.0, 0.01, "");
        cutoff.setSkewFactorFromMidPoint (1000.0);
        cutoff.setDoubleClickReturnValue (true, 20000.0);
        cutoff.setTooltip (tr ("Channel filter cutoff (low-pass at maximum with no resonance = off)"));
        filterType.addItemList ({ tr ("Low-pass"), tr ("Band-pass"), tr ("High-pass"), tr ("Notch") }, 1);
        filterType.onChange = [this] { apply(); };
        addAndMakeVisible (filterType);
        modulationButton.setButtonText (tr ("Modulation (envelopes and LFOs)..."));
        modulationButton.onClick = [this] { openChannelModulation (engine, channel); };
        addAndMakeVisible (modulationButton);
        regionEnd.setDoubleClickReturnValue (true, 1.0);
        regionStart.setTooltip (tr ("Where the sample starts playing (fraction of the file)"));
        sustain.setDoubleClickReturnValue (true, 1.0);
        decay.setSkewFactorFromMidPoint (0.5);
        stretch.setTooltip (tr ("0 = off. Otherwise the whole sample lasts this many beats and follows the BPM (by resampling, like FL's Resample mode)"));
        attack.setSkewFactorFromMidPoint (0.2);
        release.setSkewFactorFromMidPoint (0.4);
        release.setTooltip (tr ("0 = the whole sample plays (one-shot), like FL's steps"));

        keepPitch.setButtonText (tr ("Stretch keeping pitch"));
        keepPitch.setTooltip (tr ("With \"Stretch\": changes the length without changing the pitch (like FL's Stretch modes)"));
        keepPitch.onClick = [this] { apply(); };
        addAndMakeVisible (keepPitch);

        reverse.setButtonText (tr ("Reverse"));
        cut.setButtonText (tr ("Cut itself"));
        porta.setButtonText ("Porta");
        porta.setTooltip (tr ("Portamento: the note starts from the previous note's pitch (with Cut itself, the 808 glide)"));
        cut.setTooltip (tr ("A new note cuts the previous one (good for hi-hats and 808s)"));

        for (auto* b : { &reverse, &cut, &porta })
        {
            addAndMakeVisible (b);
            b->onClick = [this] { apply(); };
        }

        refresh();
        engine.addChangeListener (this);
        setSize (420, 576);
    }

    ~SettingsPanel() override { engine.removeChangeListener (this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto place = [] (juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Slider*, juce::Label*>> items) {
            const auto w = row.getWidth() / 3;

            for (auto [slider, label] : items)
            {
                auto column = row.removeFromLeft (w);
                label->setBounds (column.removeFromTop (18));
                slider->setBounds (column);
            }
        };

        auto top = area.removeFromTop (110);
        keepPitch.setBounds (top.removeFromRight (top.getWidth() / 3).withSizeKeepingCentre (130, 40));
        place (top, { { &pitch, &pitchLabel }, { &stretch, &stretchLabel } });
        area.removeFromTop (6);
        auto envelope = area.removeFromTop (110);
        const auto w = envelope.getWidth() / 4;

        for (auto [slider, label] : { std::pair { &attack, &attackLabel }, { &decay, &decayLabel }, { &sustain, &sustainLabel }, { &release, &releaseLabel } })
        {
            auto column = envelope.removeFromLeft (w);
            label->setBounds (column.removeFromTop (18));
            slider->setBounds (column);
        }

        area.removeFromTop (8);
        area.removeFromTop (6);
        place (area.removeFromTop (110), { { &regionStart, &regionStartLabel }, { &regionEnd, &regionEndLabel } });
        area.removeFromTop (6);
        {
            auto filterRow = area.removeFromTop (110);
            filterType.setBounds (filterRow.removeFromRight (filterRow.getWidth() / 3).withSizeKeepingCentre (124, 24));
            place (filterRow.withWidth (filterRow.getWidth() * 3 / 2), { { &cutoff, &cutoffLabel }, { &resonance, &resonanceLabel } });
        }
        modulationButton.setBounds (area.removeFromBottom (26).withSizeKeepingCentre (260, 26));
        area.removeFromTop (6);
        reverse.setBounds (area.removeFromLeft (150).removeFromTop (24));
        cut.setBounds (area.removeFromLeft (130).removeFromTop (24));
        porta.setBounds (area.removeFromLeft (100).removeFromTop (24));
    }

    int getChannel() const noexcept { return channel; }

private:
    void setup (juce::Slider& s, juce::Label& label, const char* name, double lo, double hi, double step, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setDoubleClickReturnValue (true, 0.0);
        s.onValueChange = [this] { apply(); };
        label.setText (tr (name), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (s);
        addAndMakeVisible (label);
    }

    const ChannelDef* current() const
    {
        const auto& channels = engine.getSong().channels;
        return channel >= 0 && channel < (int) channels.size() ? &channels[(std::size_t) channel] : nullptr;
    }

    void refresh()
    {
        if (const auto* c = current())
        {
            updating = true;
            pitch.setValue (c->sampler.pitch, juce::dontSendNotification);
            attack.setValue (c->sampler.attack, juce::dontSendNotification);
            decay.setValue (c->sampler.decay, juce::dontSendNotification);
            sustain.setValue (c->sampler.sustain, juce::dontSendNotification);
            stretch.setValue (c->sampler.stretchBeats, juce::dontSendNotification);
            keepPitch.setToggleState (c->sampler.keepPitch, juce::dontSendNotification);
            regionStart.setValue (c->sampler.regionStart, juce::dontSendNotification);
            regionEnd.setValue (c->sampler.regionEnd, juce::dontSendNotification);
            cutoff.setValue (c->sampler.cutoff, juce::dontSendNotification);
            resonance.setValue (c->sampler.resonance, juce::dontSendNotification);
            filterType.setSelectedId (c->sampler.filterType + 1, juce::dontSendNotification);
            release.setValue (c->sampler.release, juce::dontSendNotification);
            reverse.setToggleState (c->sampler.reverse, juce::dontSendNotification);
            cut.setToggleState (c->sampler.cut, juce::dontSendNotification);
            porta.setToggleState (c->sampler.porta, juce::dontSendNotification);
            updating = false;

            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                w->setName (tr ("Sampler settings - {0}", { juce::String::fromUTF8 (c->name.c_str()) })
                            + (c->zones.empty() ? juce::String() : tr (" (multi-sample: {0} zones)", { juce::String ((int) c->zones.size()) })));
        }
    }

    void apply()
    {
        if (updating || current() == nullptr)
            return;

        auto s = current()->sampler; // keeps what the panel does not show (slices)
        s.pitch = (float) pitch.getValue();
        s.attack = (float) attack.getValue();
        s.decay = (float) decay.getValue();
        s.sustain = (float) sustain.getValue();
        s.stretchBeats = (float) stretch.getValue();
        s.keepPitch = keepPitch.getToggleState();
        s.regionStart = (float) regionStart.getValue();
        s.regionEnd = (float) regionEnd.getValue();
        s.release = (float) release.getValue();
        s.reverse = reverse.getToggleState();
        s.cut = cut.getToggleState();
        s.porta = porta.getToggleState();
        s.cutoff = (float) cutoff.getValue();
        s.resonance = (float) resonance.getValue();
        s.filterType = juce::jlimit (0, 3, filterType.getSelectedId() - 1);
        if (s == current()->sampler)
            return;

        engine.editSong ([c = channel, s] (Song& song) { song.channels[(std::size_t) c].sampler = s; });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr) // channel removed
        {
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::DocumentWindow> (w)] {
                    if (safe != nullptr)
                        safe->closeButtonPressed();
                });
            return;
        }

        refresh();
    }

    AudioEngine& engine;
    int channel;
    bool updating = false;
    juce::Slider pitch, attack, release, decay, sustain, stretch, regionStart, regionEnd, cutoff, resonance;
    juce::Label pitchLabel, attackLabel, releaseLabel, decayLabel, sustainLabel, stretchLabel, regionStartLabel, regionEndLabel,
        cutoffLabel, resonanceLabel;
    juce::ToggleButton reverse, cut, keepPitch, porta;
    juce::ComboBox filterType;
    juce::TextButton modulationButton;
};

// Built-in synth page: 3 oscillators, envelope and filter.
class SynthPanel final : public juce::Component,
                         private juce::ChangeListener
{
public:
    SynthPanel (AudioEngine& e, int c)
        : engine (e),
          channel (c)
    {
        for (int o = 0; o < 3; ++o)
        {
            auto& row = rows[(std::size_t) o];
            row.title.setText (tr ("Osc {0}", { juce::String (o + 1) }), juce::dontSendNotification);
            row.wave.addItemList ({ tr ("Sine"), tr ("Triangle"), tr ("Saw"), tr ("Square"), tr ("Noise"), tr ("Harmonics") }, 1);
            row.wave.onChange = [this] { apply(); };
            knob (row.octave, -3.0, 3.0, 1.0, " oct");
            knob (row.semitones, -11.0, 11.0, 1.0, " st");
            knob (row.detune, -100.0, 100.0, 1.0, " ct");
            knob (row.pan, -1.0, 1.0, 0.01, "");
            knob (row.level, 0.0, 4.0, 0.01, "");
            row.level.setSkewFactorFromMidPoint (1.0); // above 1: gain (Harmor goes up to +12 dB)
            addAndMakeVisible (row.title);
            addAndMakeVisible (row.wave);
        }

        knob (attack, 0.0, 5.0, 0.001, " s");
        knob (decay, 0.0, 5.0, 0.001, " s");
        knob (sustain, 0.0, 1.0, 0.01, "");
        knob (release, 0.0, 5.0, 0.001, " s");
        knob (cutoff, 20.0, 20000.0, 1.0, " Hz");
        knob (resonance, 0.0, 1.0, 0.01, "");
        knob (fm, 0.0, 8.0, 0.01, "");
        fm.setSkewFactorFromMidPoint (1.0);
        fm.setTooltip (tr ("FM: osc 3 (which stops sounding) modulates the phase of osc 1 and 2; 0 = off"));
        knob (fmDecay, 0.0, 5.0, 0.001, " s");
        fmDecay.setSkewFactorFromMidPoint (0.5);
        fmDecay.setTooltip (tr ("Time for the FM index to decay (0 = constant); the brightness fades like an electric piano"));
        knob (keyTrack, -1.0, 2.0, 0.01, "");
        keyTrack.setTooltip (tr ("Cutoff follows the note: cutoff octaves per note octave, relative to C3 (130.8 Hz)"));
        knob (sweep, 0.0, 100.0, 0.01, "");
        sweep.setSkewFactorFromMidPoint (2.0);
        sweep.setTooltip (tr ("Kick pitch sweep: the note starts (1 + value) times higher and falls"));
        knob (sweepTime, 0.001, 2.0, 0.001, " s");
        sweepTime.setSkewFactorFromMidPoint (0.1);
        sweepTime.setTooltip (tr ("Sweep time constant"));
        for (auto* b : { &bandLow, &bandMid, &bandHigh })
        {
            knob (*b, 0.0, 2.0, 0.01, "");
            b->setTooltip (tr ("3-band tone after the filter (low < 178 Hz < mid < 1.18 kHz < high); 1 = unchanged"));
        }
        ring.setButtonText (tr ("Ring"));
        ring.setTooltip (tr ("With AM on: ring modulation, (osc 1 + 2) × (1 − level + osc 3), like Sytrus's RM"));
        ring.onClick = [this] { apply(); };
        addAndMakeVisible (ring);

        modulationButton.setButtonText (tr ("Modulation (envelopes and LFOs)..."));
        modulationButton.onClick = [this] { openChannelModulation (engine, channel); };
        addAndMakeVisible (modulationButton);

        for (auto* s : { &attack, &decay, &release })
            s->setSkewFactorFromMidPoint (0.5);
        cutoff.setSkewFactorFromMidPoint (1000.0);

        const char* names[] = { TR_NOOP ("Octave"), TR_NOOP ("Semitone"), TR_NOOP ("Fine"), TR_NOOP ("Pan"), TR_NOOP ("Level"), TR_NOOP ("Attack"), TR_NOOP ("Decay"), TR_NOOP ("Sustain"), TR_NOOP ("Release"), TR_NOOP ("Cutoff"), TR_NOOP ("Resonance"), TR_NOOP ("Porta time"), TR_NOOP ("FM"), TR_NOOP ("FM decay"),
                                TR_NOOP ("Keytrack"), TR_NOOP ("Sweep"), TR_NOOP ("Sweep time"), TR_NOOP ("Low"), TR_NOOP ("Mid"), TR_NOOP ("High") };

        for (int i = 0; i < (int) labels.size(); ++i)
        {
            labels[(std::size_t) i].setText (tr (names[i]), juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (theme::font (theme::type::caption));
            addAndMakeVisible (labels[(std::size_t) i]);
        }

        presetBox.setTextWhenNothingSelected (tr ("Presets..."));
        const auto presets = synthPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            presetBox.addItem (juce::String::fromUTF8 (presets[(std::size_t) i].first.c_str()), i + 1);
        presetBox.onChange = [this] {
            const auto presets = synthPresets();
            const auto index = presetBox.getSelectedId() - 1;

            if (juce::isPositiveAndBelow (index, (int) presets.size()) && current() != nullptr)
                engine.editSong ([c = channel, s = presets[(std::size_t) index].second] (Song& song) { song.channels[(std::size_t) c].synth = s; });

            presetBox.setSelectedId (0, juce::dontSendNotification);
        };
        addAndMakeVisible (presetBox);

        flPresets.setButtonText (tr ("FL presets..."));
        flPresets.setTooltip (tr ("The 3x Osc channel presets that ship with FL Studio (Patches\\Channel presets\\3x Osc)"));
        flPresets.onClick = [this] { showFlPresets(); };
        addAndMakeVisible (flPresets);

        filterTypeBox.addItemList ({ tr ("Low-pass"), tr ("Band-pass"), tr ("High-pass"), tr ("Notch") }, 1);
        filterTypeBox.setTooltip (tr ("Filter type"));
        filterTypeBox.onChange = [this] { apply(); };
        addAndMakeVisible (filterTypeBox);

        am.setButtonText ("AM");
        am.setTooltip (tr ("Osc 3 modulates the amplitude of osc 1 and 2 (like 3x Osc's AM); its level is the depth"));
        am.onClick = [this] { apply(); };
        addAndMakeVisible (am);

        knob (porta, 0.0, 2.0, 0.005, " t");
        porta.setSkewFactorFromMidPoint (0.25);
        porta.setTooltip (tr ("Portamento time, in beats (applies to the Porta button and to notes marked porta)"));
        portaOn.setButtonText ("Porta");
        portaOn.setTooltip (tr ("Portamento: the new note glides from the previous one (notes marked porta invert this)"));
        portaOn.onClick = [this] { apply(); };
        addAndMakeVisible (portaOn);

        mono.setButtonText ("Mono");
        mono.setTooltip (tr ("Single voice: the new note takes over the one currently sounding (basses and leads)"));
        mono.onClick = [this] { apply(); };
        addAndMakeVisible (mono);

        refresh();
        engine.addChangeListener (this);
        setSize (720, 510);
    }

    ~SynthPanel() override { engine.removeChangeListener (this); }

    int getChannel() const noexcept { return channel; }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto topRow = area.removeFromTop (24);
        presetBox.setBounds (topRow.removeFromLeft (200));
        mono.setBounds (topRow.removeFromLeft (90).withTrimmedLeft (16));
        am.setBounds (topRow.removeFromLeft (52));
        ring.setBounds (topRow.removeFromLeft (60));
        flPresets.setBounds (topRow.removeFromLeft (130).reduced (4, 0));
        portaOn.setBounds (topRow.removeFromLeft (70));
        filterTypeBox.setBounds (topRow.removeFromRight (140));
        area.removeFromTop (6);
        auto header = area.removeFromTop (16);
        header.removeFromLeft (150);

        for (int i = 0; i < oscKnobs; ++i)
            labels[(std::size_t) i].setBounds (header.removeFromLeft (88));

        for (auto& row : rows)
        {
            auto line = area.removeFromTop (64);
            row.title.setBounds (line.removeFromLeft (50).withSizeKeepingCentre (50, 24));
            row.wave.setBounds (line.removeFromLeft (100).withSizeKeepingCentre (96, 24));
            for (auto* s : { &row.octave, &row.semitones, &row.detune, &row.pan, &row.level })
                s->setBounds (line.removeFromLeft (88));
        }

        area.removeFromTop (10);
        auto names = area.removeFromTop (16);
        const auto w = area.getWidth() / 9;

        for (int i = oscKnobs; i < oscKnobs + 9; ++i)
            labels[(std::size_t) i].setBounds (names.removeFromLeft (w));

        auto knobsRow = area.removeFromTop (80);
        for (auto* s : { &attack, &decay, &sustain, &release, &cutoff, &resonance, &porta, &fm, &fmDecay })
            s->setBounds (knobsRow.removeFromLeft (w));

        area.removeFromTop (6);
        auto names2 = area.removeFromTop (16);
        for (int i = oscKnobs + 9; i < (int) labels.size(); ++i)
            labels[(std::size_t) i].setBounds (names2.removeFromLeft (w));
        auto knobsRow2 = area.removeFromTop (80);
        for (auto* s : { &keyTrack, &sweep, &sweepTime, &bandLow, &bandMid, &bandHigh })
            s->setBounds (knobsRow2.removeFromLeft (w));

        area.removeFromTop (12);
        modulationButton.setBounds (area.removeFromTop (26).withSizeKeepingCentre (260, 26));
    }

private:
    struct Row
    {
        juce::Label title;
        juce::ComboBox wave;
        juce::Slider octave, semitones, detune, pan, level;
    };

    static constexpr int oscKnobs = 5; // oscillator knob labels come first

    juce::TextButton modulationButton;
    juce::ToggleButton mono, am, portaOn;
    juce::TextButton flPresets;

    // menu with the presets from the FL install for the synths LYNX converts: the 3x Osc channel .fst
    // files (by letter) and, per generator, the "Plugin presets" folders (Sytrus, Harmor, GMS...)
    void showFlPresets()
    {
        const auto roots = findFlStudioRoots();
        const auto patches = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches");
        auto sorted = [] (juce::Array<juce::File> files) {
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
                return a.getFileName().compareNatural (b.getFileName()) < 0;
            });
            return files;
        };

        juce::PopupMenu menu;

        // 3x Osc: ~170 presets grouped by first letter
        {
            juce::PopupMenu threeOsc, sub;
            juce::juce_wchar letter = 0;
            for (const auto& f : sorted (patches.getChildFile ("Channel presets/3x Osc").findChildFiles (juce::File::findFiles, false, "*.fst")))
            {
                const auto first = juce::CharacterFunctions::toUpperCase (f.getFileName()[0]);
                if (first != letter && sub.getNumItems() > 0)
                {
                    threeOsc.addSubMenu (juce::String::charToString (letter), sub);
                    sub = {};
                }
                letter = first;
                sub.addItem (f.getFileNameWithoutExtension(), [this, f] { loadFlPreset (f); });
            }
            if (sub.getNumItems() > 0)
                threeOsc.addSubMenu (juce::String::charToString (letter), sub);
            if (threeOsc.getNumItems() > 0)
                menu.addSubMenu ("3x Osc", threeOsc);
        }

        // a preset folder as a menu: subfolders become submenus
        std::function<juce::PopupMenu (const juce::File&, int)> folderMenu = [&] (const juce::File& folder, int depth) {
            juce::PopupMenu m;
            if (depth < 3)
                for (const auto& dir : sorted (folder.findChildFiles (juce::File::findDirectories, false)))
                    if (auto sub = folderMenu (dir, depth + 1); sub.getNumItems() > 0)
                        m.addSubMenu (dir.getFileName(), sub);
            for (const auto& f : sorted (folder.findChildFiles (juce::File::findFiles, false, "*.fst;*.gmsynth")))
                m.addItem (f.getFileNameWithoutExtension(), [this, f] { loadFlPreset (f); });
            return m;
        };

        const auto generators = patches.getChildFile ("Plugin presets/Generators");
        for (const auto* name : { "Sytrus", "Harmor", "GMS", "Harmless", "MiniSynth", "Fruity DX10", "Fruit Kick", "Ogun", "Sakura", "Kepler",
                                 "PoiZone", "Toxic Biohazard", "BassDrum" })
            if (auto sub = folderMenu (generators.getChildFile (name), 0); sub.getNumItems() > 0)
                menu.addSubMenu (name, sub);

        if (menu.getNumItems() == 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("FL presets"),
                                                    tr ("FL presets not found (install FL Studio or check the Data\\Patches folder)."));
            return;
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&flPresets));
    }

    void loadFlPreset (const juce::File& file)
    {
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data) || current() == nullptr)
            return;

        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());

        if (auto synth = synthFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize())))
            engine.editSong ([c = channel, s = *synth] (Song& song) { song.channels[(std::size_t) c].synth = s; });
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("FL presets"),
                                                    tr ("This preset is not for a synth LYNX converts, or it could not be read."));
    }
    juce::Slider porta;
    juce::ComboBox filterTypeBox;

    void knob (juce::Slider& s, double lo, double hi, double step, const char* suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] { apply(); };
        addAndMakeVisible (s);
    }

    const ChannelDef* current() const
    {
        const auto& channels = engine.getSong().channels;
        return channel >= 0 && channel < (int) channels.size() ? &channels[(std::size_t) channel] : nullptr;
    }

    void refresh()
    {
        const auto* c = current();

        if (c == nullptr)
            return;

        updating = true;
        const auto& s = c->synth;

        for (std::size_t o = 0; o < 3; ++o)
        {
            rows[o].wave.setSelectedId ((int) s.osc[o].wave + 1, juce::dontSendNotification);
            rows[o].octave.setValue (s.osc[o].octave, juce::dontSendNotification);
            rows[o].detune.setValue (s.osc[o].detune, juce::dontSendNotification);
            rows[o].semitones.setValue (s.osc[o].semitones, juce::dontSendNotification);
            rows[o].pan.setValue (s.osc[o].pan, juce::dontSendNotification);
            rows[o].level.setValue (s.osc[o].level, juce::dontSendNotification);
        }

        attack.setValue (s.attack, juce::dontSendNotification);
        decay.setValue (s.decay, juce::dontSendNotification);
        sustain.setValue (s.sustain, juce::dontSendNotification);
        release.setValue (s.release, juce::dontSendNotification);
        cutoff.setValue (s.cutoff, juce::dontSendNotification);
        resonance.setValue (s.resonance, juce::dontSendNotification);
        mono.setToggleState (s.mono, juce::dontSendNotification);
        am.setToggleState (s.am, juce::dontSendNotification);
        porta.setValue (s.portaBeats, juce::dontSendNotification);
        portaOn.setToggleState (s.porta, juce::dontSendNotification);
        filterTypeBox.setSelectedId (s.filterType + 1, juce::dontSendNotification);
        fm.setValue (s.fm, juce::dontSendNotification);
        fmDecay.setValue (s.fmDecay, juce::dontSendNotification);
        keyTrack.setValue (s.keyTrack, juce::dontSendNotification);
        sweep.setValue (s.sweep, juce::dontSendNotification);
        sweepTime.setValue (s.sweepTime, juce::dontSendNotification);
        bandLow.setValue (s.bandGain[0], juce::dontSendNotification);
        bandMid.setValue (s.bandGain[1], juce::dontSendNotification);
        bandHigh.setValue (s.bandGain[2], juce::dontSendNotification);
        ring.setToggleState (s.ring, juce::dontSendNotification);

        updating = false;

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName (tr ("Synth settings - {0}", { juce::String::fromUTF8 (c->name.c_str()) }));
    }

    void apply()
    {
        if (updating || current() == nullptr)
            return;

        auto s = current()->synth; // keeps what this window does not show (envelopes and LFOs)

        for (std::size_t o = 0; o < 3; ++o)
            s.osc[o] = { (Waveform) juce::jlimit (0, 5, rows[o].wave.getSelectedId() - 1), (int) rows[o].octave.getValue(),
                         (float) rows[o].detune.getValue(), (float) rows[o].level.getValue(), (int) rows[o].semitones.getValue(),
                         (float) rows[o].pan.getValue() };

        s.attack = (float) attack.getValue();
        s.decay = (float) decay.getValue();
        s.sustain = (float) sustain.getValue();
        s.release = (float) release.getValue();
        s.cutoff = (float) cutoff.getValue();
        s.resonance = (float) resonance.getValue();
        s.mono = mono.getToggleState();
        s.am = am.getToggleState();
        s.portaBeats = (float) porta.getValue();
        s.porta = portaOn.getToggleState();
        s.filterType = juce::jlimit (0, 3, filterTypeBox.getSelectedId() - 1);
        s.fm = (float) fm.getValue();
        s.fmDecay = (float) fmDecay.getValue();
        if (s.fmDecay <= 0.0f)
            s.fmSustain = 1.0f;
        s.keyTrack = (float) keyTrack.getValue();
        s.sweep = (float) sweep.getValue();
        s.sweepTime = (float) sweepTime.getValue();
        s.bandGain = { (float) bandLow.getValue(), (float) bandMid.getValue(), (float) bandHigh.getValue() };
        s.ring = ring.getToggleState();

        if (s == current()->synth)
            return;

        engine.editSong ([c = channel, s] (Song& song) { song.channels[(std::size_t) c].synth = s; });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr)
        {
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::DocumentWindow> (w)] {
                    if (safe != nullptr)
                        safe->closeButtonPressed();
                });
            return;
        }

        refresh();
    }

    AudioEngine& engine;
    int channel;
    bool updating = false;
    std::array<Row, 3> rows;
    juce::Slider attack, decay, sustain, release, cutoff, resonance, fm, fmDecay, keyTrack, sweep, sweepTime, bandLow, bandMid, bandHigh;
    juce::ToggleButton ring;
    std::array<juce::Label, 20> labels;
    juce::ComboBox presetBox;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow (AudioEngine& engine, int c, bool modulationWindow)
        : juce::DocumentWindow (tr ("Channel settings"), theme::colour::bg2, DocumentWindow::closeButton),
          channel (c),
          modulation (modulationWindow)
    {
        setUsingNativeTitleBar (true);

        if (modulation)
            setContentOwned (new ModulationPanel (engine, channel), true);
        else if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::synth)
            setContentOwned (new SynthPanel (engine, channel), true);
        else if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::controller)
            setContentOwned (new ControllerPanel (engine, channel), true);
        else if (const auto kind = engine.getSong().channels[(std::size_t) channel].instrument;
                 kind == InstrumentKind::sakura || kind == InstrumentKind::drumpad || kind == InstrumentKind::boobass
                 || kind == InstrumentKind::drumsynth || kind == InstrumentKind::harmless || kind == InstrumentKind::harmor)
            setContentOwned (new ModelPanel (engine, channel), true);
        else
            setContentOwned (new SettingsPanel (engine, channel), true);

        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }

    int getChannel() const { return channel; }
    bool isModulation() const { return modulation; }
    void closeButtonPressed() override;

private:
    int channel;
    bool modulation;
};

std::vector<std::unique_ptr<SettingsWindow>>& windows()
{
    static std::vector<std::unique_ptr<SettingsWindow>> open;
    return open;
}

void SettingsWindow::closeButtonPressed()
{
    auto* self = this;
    juce::MessageManager::callAsync ([self] {
        auto& open = windows();
        open.erase (std::remove_if (open.begin(), open.end(), [self] (auto& w) { return w.get() == self; }), open.end());
    });
}
} // namespace

namespace
{
void openWindow (AudioEngine& engine, int channel, bool modulation)
{
    for (auto& w : windows())
        if (w->getChannel() == channel && w->isModulation() == modulation)
        {
            w->toFront (true);
            return;
        }

    windows().push_back (std::make_unique<SettingsWindow> (engine, channel, modulation));
}
} // namespace

void openChannelSettings (AudioEngine& engine, int channel)
{
    openWindow (engine, channel, false);
}

void openChannelModulation (AudioEngine& engine, int channel)
{
    openWindow (engine, channel, true);
}

void closeChannelSettings()
{
    windows().clear();
}

std::unique_ptr<juce::Component> makeChannelModulationPanel (AudioEngine& engine, int channel)
{
    return std::make_unique<ModulationPanel> (engine, channel);
}

std::unique_ptr<juce::Component> makeChannelSettingsPanel (AudioEngine& engine, int channel)
{
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::synth)
        return std::make_unique<SynthPanel> (engine, channel);
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::controller)
        return std::make_unique<ControllerPanel> (engine, channel);
    if (const auto kind = engine.getSong().channels[(std::size_t) channel].instrument;
        kind == InstrumentKind::sakura || kind == InstrumentKind::drumpad || kind == InstrumentKind::boobass
                 || kind == InstrumentKind::drumsynth || kind == InstrumentKind::harmless || kind == InstrumentKind::harmor)
        return std::make_unique<ModelPanel> (engine, channel);

    return std::make_unique<SettingsPanel> (engine, channel);
}
} // namespace lynx
