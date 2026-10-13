#include "ui/ChannelSettings.h"

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
        attack.setSkewFactorFromMidPoint (0.2);
        release.setSkewFactorFromMidPoint (0.4);
        release.setTooltip (utf8 ("0 = o sample toca inteiro (one-shot), como os steps do FL"));

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
        setSize (360, 170);
    }

    ~SettingsPanel() override { engine.removeChangeListener (this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto knobs = area.removeFromTop (110);
        const auto w = knobs.getWidth() / 3;

        for (auto [slider, label] : { std::pair { &pitch, &pitchLabel }, { &attack, &attackLabel }, { &release, &releaseLabel } })
        {
            auto column = knobs.removeFromLeft (w);
            label->setBounds (column.removeFromTop (18));
            slider->setBounds (column);
        }

        area.removeFromTop (8);
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

        SamplerSettings s;
        s.pitch = (float) pitch.getValue();
        s.attack = (float) attack.getValue();
        s.release = (float) release.getValue();
        s.reverse = reverse.getToggleState();
        s.cut = cut.getToggleState();

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
    juce::Slider pitch, attack, release;
    juce::Label pitchLabel, attackLabel, releaseLabel;
    juce::ToggleButton reverse, cut;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow (AudioEngine& engine, int channel)
        : juce::DocumentWindow ("Ajustes do sampler", theme::panel, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new SettingsPanel (engine, channel), true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    int getChannel() const { return static_cast<SettingsPanel*> (getContentComponent())->getChannel(); }
    void closeButtonPressed() override;
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
    return std::make_unique<SettingsPanel> (engine, channel);
}
} // namespace lynx
