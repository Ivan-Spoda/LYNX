#include "ui/ChannelSettings.h"

#include "core/FlpImport.h"
#include "core/SynthPresets.h"
#include "engine/Text.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// Envelopes de corte e de afinação (os envelopes do canal do FL), usados pelo synth e pelo sampler.
class ModEnvelopeEditor final : public juce::Component
{
public:
    std::function<void()> onChange;
    static constexpr int preferredHeight = 16 + 2 * 62;

    ModEnvelopeEditor()
    {
        const juce::String titles[] = { "Env. corte", utf8 ("Env. afinação") };

        for (int e = 0; e < 2; ++e)
        {
            auto& row = rows[(std::size_t) e];
            row.title.setText (titles[e], juce::dontSendNotification);
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

        const char* names[] = { "Quantidade", "Ataque", "Hold", "Decaimento", "Sustentação", "Release" };

        for (int i = 0; i < 6; ++i)
        {
            labels[(std::size_t) i].setText (utf8 (names[i]), juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (juce::FontOptions (11.0f));
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

// LFOs do canal (pan, volume, corte, afinação), como os do FL.
class LfoEditor final : public juce::Component
{
public:
    std::function<void()> onChange;
    static constexpr int preferredHeight = 18 + lfoTargets * 60;

    LfoEditor()
    {
        const juce::String titles[] = { "LFO pan", "LFO volume", "LFO corte", utf8 ("LFO afinação") };
        const double amountRange[] = { 1.0, 1.0, 8.0, 24.0 };
        const char* amountSuffix[] = { "", "", " oit", " st" };

        for (int i = 0; i < lfoTargets; ++i)
        {
            auto& row = rows[(std::size_t) i];
            row.title.setText (titles[i], juce::dontSendNotification);
            row.title.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (row.title);
            knob (row.amount, -amountRange[i], amountRange[i], 0.01, amountSuffix[i]);
            row.amount.setDoubleClickReturnValue (true, 0.0);
            row.amount.setTooltip (utf8 ("0 = desligado"));
            knob (row.period, 0.01, 16.0, 0.001, "");
            row.period.setSkewFactorFromMidPoint (0.5);
            knob (row.delay, 0.0, 8.0, 0.001, "");
            row.delay.setSkewFactorFromMidPoint (0.5);
            knob (row.attack, 0.0, 8.0, 0.001, "");
            row.attack.setSkewFactorFromMidPoint (0.5);
            row.shape.addItemList ({ "Senoide", utf8 ("Triângulo"), "Pulso" }, 1);
            row.shape.onChange = [this] { changed(); };
            addAndMakeVisible (row.shape);
            row.sync.setButtonText ("Tempos");
            row.sync.setTooltip (utf8 ("Período, atraso e fade-in em tempos (segue o BPM) em vez de segundos"));
            row.sync.onClick = [this] { changed(); };
            addAndMakeVisible (row.sync);
        }

        const juce::String headers[] = { "Quantidade", utf8 ("Período"), "Forma", "Atraso", "Fade-in", "Unidade" };

        for (int i = 0; i < 6; ++i)
        {
            labels[(std::size_t) i].setText (headers[i], juce::dontSendNotification);
            labels[(std::size_t) i].setJustificationType (juce::Justification::centred);
            labels[(std::size_t) i].setFont (juce::FontOptions (11.0f));
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
            row.sync.setBounds (line.removeFromLeft (w).withSizeKeepingCentre (w - 6, 24));
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
                       juce::jlimit (0, 2, row.shape.getSelectedId() - 1), (float) row.delay.getValue(), (float) row.attack.getValue() };
        }

        return lfo;
    }

private:
    struct Row
    {
        juce::Label title;
        juce::Slider amount, period, delay, attack;
        juce::ComboBox shape;
        juce::ToggleButton sync;
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

// Janela de modulação do canal: envelopes de corte/afinação e LFOs do synth ou do sampler.
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
            w->setName (utf8 ("Modulação - ") + juce::String::fromUTF8 (c->name.c_str()));
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
        cutoff.setTooltip (utf8 ("Corte do filtro do canal (passa-baixas no máximo e sem ressonância = desligado)"));
        filterType.addItemList ({ "Passa-baixas", "Passa-banda", "Passa-altas", "Rejeita-faixa" }, 1);
        filterType.onChange = [this] { apply(); };
        addAndMakeVisible (filterType);
        modulationButton.setButtonText (utf8 ("Modulação (envelopes e LFOs)..."));
        modulationButton.onClick = [this] { openChannelModulation (engine, channel); };
        addAndMakeVisible (modulationButton);
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
        porta.setButtonText ("Porta");
        porta.setTooltip (utf8 ("Portamento: a nota parte da altura da anterior (com Cut itself, o glide do 808)"));
        cut.setTooltip (utf8 ("Uma nota nova corta a anterior (bom para hi-hats e 808)"));

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
            filterType.setSelectedId (c->sampler.filterType + 1, juce::dontSendNotification);
            release.setValue (c->sampler.release, juce::dontSendNotification);
            reverse.setToggleState (c->sampler.reverse, juce::dontSendNotification);
            cut.setToggleState (c->sampler.cut, juce::dontSendNotification);
            porta.setToggleState (c->sampler.porta, juce::dontSendNotification);
            updating = false;

            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                w->setName (utf8 ("Ajustes do sampler - ") + juce::String::fromUTF8 (c->name.c_str())
                            + (c->zones.empty() ? juce::String() : " (multi-sample: " + juce::String ((int) c->zones.size()) + " zonas)"));
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
    juce::ToggleButton reverse, cut, keepPitch, porta;
    juce::ComboBox filterType;
    juce::TextButton modulationButton;
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

        modulationButton.setButtonText (utf8 ("Modulação (envelopes e LFOs)..."));
        modulationButton.onClick = [this] { openChannelModulation (engine, channel); };
        addAndMakeVisible (modulationButton);

        for (auto* s : { &attack, &decay, &release })
            s->setSkewFactorFromMidPoint (0.5);
        cutoff.setSkewFactorFromMidPoint (1000.0);

        const char* names[] = { "Oitava", "Semitom", "Afinação", "Pan", "Nível", "Ataque", "Decaimento", "Sustentação", "Release", "Corte", "Ressonância", "Tempo porta" };

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

        flPresets.setButtonText ("Presets do FL...");
        flPresets.setTooltip (utf8 ("Os presets de canal do 3x Osc que vêm com o FL Studio (Patches\\Channel presets\\3x Osc)"));
        flPresets.onClick = [this] { showFlPresets(); };
        addAndMakeVisible (flPresets);

        filterTypeBox.addItemList ({ "Passa-baixas", "Passa-banda", "Passa-altas", "Rejeita-faixa" }, 1);
        filterTypeBox.setTooltip ("Tipo do filtro");
        filterTypeBox.onChange = [this] { apply(); };
        addAndMakeVisible (filterTypeBox);

        am.setButtonText ("AM");
        am.setTooltip (utf8 ("O osc 3 modula a amplitude dos osc 1 e 2 (como o AM do 3x Osc); o nível dele é a profundidade"));
        am.onClick = [this] { apply(); };
        addAndMakeVisible (am);

        knob (porta, 0.0, 2.0, 0.005, " t");
        porta.setSkewFactorFromMidPoint (0.25);
        porta.setTooltip (utf8 ("Tempo do portamento, em tempos (vale para o botão Porta e para as notas com porta)"));
        portaOn.setButtonText ("Porta");
        portaOn.setTooltip (utf8 ("Portamento: a nota nova desliza a partir da anterior (notas marcadas com porta invertem)"));
        portaOn.onClick = [this] { apply(); };
        addAndMakeVisible (portaOn);

        mono.setButtonText ("Mono");
        mono.setTooltip (utf8 ("Uma voz só: a nota nova assume a que está soando (baixos e leads)"));
        mono.onClick = [this] { apply(); };
        addAndMakeVisible (mono);

        refresh();
        engine.addChangeListener (this);
        setSize (720, 410);
    }

    ~SynthPanel() override { engine.removeChangeListener (this); }

    int getChannel() const noexcept { return channel; }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto topRow = area.removeFromTop (24);
        presetBox.setBounds (topRow.removeFromLeft (200));
        mono.setBounds (topRow.removeFromLeft (90).withTrimmedLeft (16));
        am.setBounds (topRow.removeFromLeft (60));
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
        const auto w = area.getWidth() / 7;

        for (int i = oscKnobs; i < (int) labels.size(); ++i)
            labels[(std::size_t) i].setBounds (names.removeFromLeft (w));

        auto knobsRow = area.removeFromTop (80);
        for (auto* s : { &attack, &decay, &sustain, &release, &cutoff, &resonance, &porta })
            s->setBounds (knobsRow.removeFromLeft (w));

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

    static constexpr int oscKnobs = 5; // rótulos dos knobs de oscilador vêm primeiro

    juce::TextButton modulationButton;
    juce::ToggleButton mono, am, portaOn;
    juce::TextButton flPresets;

    // menu com os .fst do 3x Osc da instalação do FL, em ordem alfabética
    void showFlPresets()
    {
        const auto roots = findFlStudioRoots();
        const auto folder = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Channel presets/3x Osc");
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.fst");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        });

        if (files.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Presets do FL",
                                                    utf8 ("Não achei os presets do 3x Osc (instale o FL Studio ou confira a pasta Patches\\Channel presets\\3x Osc)."));
            return;
        }

        juce::PopupMenu menu;
        juce::PopupMenu sub;
        juce::juce_wchar letter = 0;

        // agrupa por letra inicial (são ~170)
        for (const auto& f : files)
        {
            const auto first = juce::CharacterFunctions::toUpperCase (f.getFileName()[0]);

            if (first != letter && sub.getNumItems() > 0)
            {
                menu.addSubMenu (juce::String::charToString (letter), sub);
                sub = {};
            }

            letter = first;
            sub.addItem (f.getFileNameWithoutExtension(), [this, f] { loadFlPreset (f); });
        }

        if (sub.getNumItems() > 0)
            menu.addSubMenu (juce::String::charToString (letter), sub);

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
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Presets do FL",
                                                    utf8 ("Este preset não é de 3x Osc ou não pôde ser lido."));
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

        updating = false;

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName ("Ajustes do synth - " + juce::String::fromUTF8 (c->name.c_str()));
    }

    void apply()
    {
        if (updating || current() == nullptr)
            return;

        auto s = current()->synth; // mantém o que esta janela não mostra (envelopes e LFOs)

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
        s.mono = mono.getToggleState();
        s.am = am.getToggleState();
        s.portaBeats = (float) porta.getValue();
        s.porta = portaOn.getToggleState();
        s.filterType = juce::jlimit (0, 3, filterTypeBox.getSelectedId() - 1);

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
    std::array<juce::Label, 12> labels;
    juce::ComboBox presetBox;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow (AudioEngine& engine, int c, bool modulationWindow)
        : juce::DocumentWindow ("Ajustes do canal", theme::panel, DocumentWindow::closeButton),
          channel (c),
          modulation (modulationWindow)
    {
        setUsingNativeTitleBar (true);

        if (modulation)
            setContentOwned (new ModulationPanel (engine, channel), true);
        else if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::synth)
            setContentOwned (new SynthPanel (engine, channel), true);
        else
            setContentOwned (new SettingsPanel (engine, channel), true);

        centreWithSize (getWidth(), getHeight());
        setVisible (true);
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

    return std::make_unique<SettingsPanel> (engine, channel);
}
} // namespace lynx
