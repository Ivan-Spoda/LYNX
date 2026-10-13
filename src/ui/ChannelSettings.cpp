#include "ui/ChannelSettings.h"

#include "core/SynthPresets.h"
#include "engine/Text.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class SettingsPanel final : public juce::Component,
                            private juce::ChangeListener
{
public:
    SettingsPanel (AudioEngine& e, int c)
        : engine (e),
          channel (c)
    {
        setup (pitch, pitchLabel, "Afinação", -24.0, 24.0, 0.01, " st");
        setup (attack, attackLabel, "Ataque", 0.0, 2.0, 0.001, " s");
        setup (release, releaseLabel, "Release", 0.0, 4.0, 0.001, " s");
        setup (decay, decayLabel, "Decaimento", 0.0, 10.0, 0.001, " s");
        setup (sustain, sustainLabel, "Sustentação", 0.0, 1.0, 0.01, "");
        setup (stretch, stretchLabel, "Esticar", 0.0, 64.0, 0.25, " tempos");
        setup (regionStart, regionStartLabel, "Início", 0.0, 1.0, 0.001, "");
        setup (regionEnd, regionEndLabel, "Fim", 0.0, 1.0, 0.001, "");
        setup (cutoff, cutoffLabel, "Corte", 20.0, 20000.0, 1.0, " Hz");
        setup (resonance, resonanceLabel, "Ressonância", 0.0, 1.0, 0.01, "");
        cutoff.setSkewFactorFromMidPoint (1000.0);
        cutoff.setDoubleClickReturnValue (true, 20000.0);
        cutoff.setTooltip (utf8 ("Passa-baixas do canal (no máximo e sem ressonância fica desligado)"));
        regionEnd.setDoubleClickReturnValue (true, 1.0);
        regionStart.setTooltip (utf8 ("De onde o sample começa a tocar (fração do arquivo)"));
        sustain.setDoubleClickReturnValue (true, 1.0);
        decay.setSkewFactorFromMidPoint (0.5);
        stretch.setTooltip (utf8 ("0 = desligado. Senão o sample inteiro dura este tanto de tempos e acompanha o BPM (reamostrando, como o modo Resample do FL)"));
        attack.setSkewFactorFromMidPoint (0.2);
        release.setSkewFactorFromMidPoint (0.4);
        release.setTooltip (utf8 ("0 = o sample toca inteiro (one-shot), como os steps do FL"));

        keepPitch.setButtonText (utf8 ("Esticar mantendo a altura"));
        keepPitch.setTooltip (utf8 ("Com \"Esticar\": muda a duração sem mudar a altura (como os modos Stretch do FL)"));
        keepPitch.onClick = [this] { apply(); };
        addAndMakeVisible (keepPitch);

        reverse.setButtonText (utf8 ("Ao contrário"));
        cut.setButtonText ("Cut itself");
        cut.setTooltip (utf8 ("Uma nota nova corta a anterior (bom para hi-hats e 808)"));

        for (auto* b : { &reverse, &cut })
        {
            addAndMakeVisible (b);
            b->onClick = [this] { apply(); };
        }

        refresh();
        engine.addChangeListener (this);
        setSize (420, 536);
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
        place (area.removeFromTop (110), { { &cutoff, &cutoffLabel }, { &resonance, &resonanceLabel } });
        area.removeFromTop (6);
        reverse.setBounds (area.removeFromLeft (150).removeFromTop (24));
        cut.setBounds (area.removeFromLeft (150).removeFromTop (24));
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
        label.setText (utf8 (name), juce::dontSendNotification);
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
            release.setValue (c->sampler.release, juce::dontSendNotification);
            reverse.setToggleState (c->sampler.reverse, juce::dontSendNotification);
            cut.setToggleState (c->sampler.cut, juce::dontSendNotification);
            updating = false;

            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                w->setName (utf8 ("Ajustes do sampler - ") + juce::String::fromUTF8 (c->name.c_str()));
        }
    }

    void apply()
    {
        if (updating || current() == nullptr)
            return;

        auto s = current()->sampler; // mantém o que o painel não mostra (fatias)
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
        s.cutoff = (float) cutoff.getValue();
        s.resonance = (float) resonance.getValue();

        if (s == current()->sampler)
            return;

        engine.editSong ([c = channel, s] (Song& song) { song.channels[(std::size_t) c].sampler = s; });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr) // canal removido
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
    juce::ToggleButton reverse, cut, keepPitch;
};

// Página do synth interno: 3 osciladores, envelope e filtro.
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
            row.title.setText ("Osc " + juce::String (o + 1), juce::dontSendNotification);
            row.wave.addItemList ({ "Seno", utf8 ("Triângulo"), "Serra", "Quadrada", utf8 ("Ruído") }, 1);
            row.wave.onChange = [this] { apply(); };
            knob (row.octave, -3.0, 3.0, 1.0, " oit");
            knob (row.semitones, -11.0, 11.0, 1.0, " st");
            knob (row.detune, -100.0, 100.0, 1.0, " ct");
            knob (row.pan, -1.0, 1.0, 0.01, "");
            knob (row.level, 0.0, 1.0, 0.01, "");
            addAndMakeVisible (row.title);
            addAndMakeVisible (row.wave);
        }

        knob (attack, 0.0, 5.0, 0.001, " s");
        knob (decay, 0.0, 5.0, 0.001, " s");
        knob (sustain, 0.0, 1.0, 0.01, "");
        knob (release, 0.0, 5.0, 0.001, " s");
        knob (cutoff, 20.0, 20000.0, 1.0, " Hz");
        knob (resonance, 0.0, 1.0, 0.01, "");

        const juce::String envTitles[] = { "Env. corte", utf8 ("Env. afinação") };
        for (int e = 0; e < 2; ++e)
        {
            auto& row = envRows[(std::size_t) e];
            row.title.setText (envTitles[e], juce::dontSendNotification);
            row.title.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (row.title);
            knob (row.amount, e == 0 ? -10.0 : -48.0, e == 0 ? 10.0 : 48.0, 0.01, e == 0 ? " oit" : " st");
            row.amount.setDoubleClickReturnValue (true, 0.0);
            row.amount.setTooltip (utf8 ("0 = desligado"));
            for (auto* s : { &row.attack, &row.hold, &row.decay, &row.release })
            {
                knob (*s, 0.0, 10.0, 0.001, " s");
                s->setSkewFactorFromMidPoint (0.5);
            }
            knob (row.sustain, 0.0, 1.0, 0.01, "");
        }

        const char* envNames[] = { "Quantidade", "Ataque", "Hold", "Decaimento", "Sustentação", "Release" };
        for (int i = 0; i < 6; ++i)
        {
            envLabels[(std::size_t) i].setText (utf8 (envNames[i]), juce::dontSendNotification);
            envLabels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            envLabels[(std::size_t) i].setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (envLabels[(std::size_t) i]);
        }

        for (auto* s : { &attack, &decay, &release })
            s->setSkewFactorFromMidPoint (0.5);
        cutoff.setSkewFactorFromMidPoint (1000.0);

        const char* names[] = { "Oitava", "Semitom", "Afinação", "Pan", "Nível", "Ataque", "Decaimento", "Sustentação", "Release", "Corte", "Ressonância" };

        for (int i = 0; i < (int) labels.size(); ++i)
        {
            labels[(std::size_t) i].setText (utf8 (names[i]), juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (labels[(std::size_t) i]);
        }

        presetBox.setTextWhenNothingSelected ("Presets...");
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

        mono.setButtonText ("Mono");
        mono.setTooltip (utf8 ("Uma voz só: a nota nova assume a que está soando (baixos e leads)"));
        mono.onClick = [this] { apply(); };
        addAndMakeVisible (mono);

        refresh();
        engine.addChangeListener (this);
        setSize (620, 590);
    }

    ~SynthPanel() override { engine.removeChangeListener (this); }

    int getChannel() const noexcept { return channel; }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto topRow = area.removeFromTop (24);
        presetBox.setBounds (topRow.removeFromLeft (200));
        mono.setBounds (topRow.removeFromLeft (100).withTrimmedLeft (16));
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
        const auto w = area.getWidth() / 6;

        for (int i = oscKnobs; i < (int) labels.size(); ++i)
            labels[(std::size_t) i].setBounds (names.removeFromLeft (w));

        auto knobsRow = area.removeFromTop (80);
        for (auto* s : { &attack, &decay, &sustain, &release, &cutoff, &resonance })
            s->setBounds (knobsRow.removeFromLeft (w));

        area.removeFromTop (8);
        auto envHeader = area.removeFromTop (16);
        envHeader.removeFromLeft (80);
        const auto ew = envHeader.getWidth() / 6;
        for (auto& l : envLabels)
            l.setBounds (envHeader.removeFromLeft (ew));

        for (auto& row : envRows)
        {
            auto line = area.removeFromTop (62);
            row.title.setBounds (line.removeFromLeft (80).withSizeKeepingCentre (80, 24));
            for (auto* s : { &row.amount, &row.attack, &row.hold, &row.decay, &row.sustain, &row.release })
                s->setBounds (line.removeFromLeft (ew));
        }
    }

private:
    struct Row
    {
        juce::Label title;
        juce::ComboBox wave;
        juce::Slider octave, semitones, detune, pan, level;
    };

    static constexpr int oscKnobs = 5; // rótulos dos knobs de oscilador vêm primeiro

    struct EnvRow
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

    std::array<EnvRow, 2> envRows; // corte, afinação
    juce::ToggleButton mono;
    std::array<juce::Label, 6> envLabels;

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
        envRows[0].show (s.filterEnv);
        envRows[1].show (s.pitchEnv);
        updating = false;

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName ("Ajustes do synth - " + juce::String::fromUTF8 (c->name.c_str()));
    }

    void apply()
    {
        if (updating || current() == nullptr)
            return;

        SynthSettings s;

        for (std::size_t o = 0; o < 3; ++o)
            s.osc[o] = { (Waveform) juce::jlimit (0, 4, rows[o].wave.getSelectedId() - 1), (int) rows[o].octave.getValue(),
                         (float) rows[o].detune.getValue(), (float) rows[o].level.getValue(), (int) rows[o].semitones.getValue(),
                         (float) rows[o].pan.getValue() };

        s.attack = (float) attack.getValue();
        s.decay = (float) decay.getValue();
        s.sustain = (float) sustain.getValue();
        s.release = (float) release.getValue();
        s.cutoff = (float) cutoff.getValue();
        s.resonance = (float) resonance.getValue();
        s.filterEnv = envRows[0].read();
        s.pitchEnv = envRows[1].read();
        s.mono = mono.getToggleState();

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
    juce::Slider attack, decay, sustain, release, cutoff, resonance;
    std::array<juce::Label, 11> labels;
    juce::ComboBox presetBox;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow (AudioEngine& engine, int c)
        : juce::DocumentWindow ("Ajustes do canal", theme::panel, DocumentWindow::closeButton),
          channel (c)
    {
        setUsingNativeTitleBar (true);

        if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::synth)
            setContentOwned (new SynthPanel (engine, channel), true);
        else
            setContentOwned (new SettingsPanel (engine, channel), true);

        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    int getChannel() const { return channel; }
    void closeButtonPressed() override;

private:
    int channel;
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

void openChannelSettings (AudioEngine& engine, int channel)
{
    for (auto& w : windows())
        if (w->getChannel() == channel)
        {
            w->toFront (true);
            return;
        }

    windows().push_back (std::make_unique<SettingsWindow> (engine, channel));
}

void closeChannelSettings()
{
    windows().clear();
}

std::unique_ptr<juce::Component> makeChannelSettingsPanel (AudioEngine& engine, int channel)
{
    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::synth)
        return std::make_unique<SynthPanel> (engine, channel);

    return std::make_unique<SettingsPanel> (engine, channel);
}
} // namespace lynx
