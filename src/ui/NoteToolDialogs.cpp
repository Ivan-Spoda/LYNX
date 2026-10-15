#include "ui/NoteToolDialogs.h"

#include "core/NoteTools.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
juce::String keyName (int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12);
}

juce::String percent (double v)
{
    return juce::String (juce::roundToInt (v * 100.0)) + "%";
}

struct Control
{
    enum Kind { slider, choice, toggle } kind = slider;
    juce::String name;
    double minimum = 0.0, maximum = 1.0, interval = 0.01, initial = 0.0;
    std::function<juce::String (double)> format;
    juce::StringArray choices;
};

class NoteToolPanel final : public juce::Component
{
public:
    NoteToolPanel (AudioEngine& e, NoteTool t, std::vector<int> i, std::int64_t g)
        : engine (e), tool (t), indices (std::move (i)), grid (juce::jmax<std::int64_t> (1, g))
    {
        const auto& song = engine.getSong();
        patternIndex = song.currentPattern;
        originals = song.patterns[(std::size_t) patternIndex].notes;
        buildControls();

        title.setText (tr (noteToolTitle (tool)), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);
        info.setText (tr ("{0} notes. Changes are heard and seen right away; Accept keeps them as one undo step.",
                          { juce::String ((int) indices.size()) }),
                      juce::dontSendNotification);
        info.setFont (theme::font (theme::type::small));
        info.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (info);

        for (auto* b : { &acceptButton, &resetButton, &cancelButton })
            addAndMakeVisible (b);

        acceptButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        acceptButton.onClick = [this] { accept(); };
        resetButton.onClick = [this] { resetControls(); };
        cancelButton.onClick = [this] { close(); };

        if (tool == NoteTool::randomise)
        {
            seedButton.onClick = [this] {
                ++seed;
                preview();
            };
            addAndMakeVisible (seedButton);
        }

        setSize (420, 92 + rowHeight * (int) controls.size() + (tool == NoteTool::randomise ? rowHeight : 0) + 44);
        preview();
    }

    ~NoteToolPanel() override
    {
        if (! accepted)
            restore();
    }

    std::function<void()> onFinished;

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 10);
        title.setBounds (area.removeFromTop (24));
        info.setBounds (area.removeFromTop (36));
        area.removeFromTop (6);

        for (std::size_t c = 0; c < controls.size(); ++c)
        {
            auto row = area.removeFromTop (rowHeight).reduced (0, 3);
            labels[c]->setBounds (row.removeFromLeft (130));
            editors[c]->setBounds (row);
        }

        if (tool == NoteTool::randomise)
            seedButton.setBounds (area.removeFromTop (rowHeight).reduced (0, 3).withTrimmedLeft (130).withWidth (160));

        auto buttons = area.removeFromBottom (30);
        acceptButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        resetButton.setBounds (buttons.removeFromRight (90));
    }

private:
    static constexpr int rowHeight = 30;

    void add (Control c)
    {
        auto label = std::make_unique<juce::Label> (juce::String(), c.name);
        label->setFont (theme::font (theme::type::body));
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        std::unique_ptr<juce::Component> editor;

        if (c.kind == Control::slider)
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
            s->setRange (c.minimum, c.maximum, c.interval);
            s->setValue (c.initial, juce::dontSendNotification);
            s->setDoubleClickReturnValue (true, c.initial);
            if (c.format)
                s->textFromValueFunction = c.format;
            s->updateText();
            s->onValueChange = [this] { preview(); };
            editor = std::move (s);
        }
        else if (c.kind == Control::choice)
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (c.choices, 1);
            box->setSelectedItemIndex ((int) c.initial, juce::dontSendNotification);
            box->onChange = [this] { preview(); };
            editor = std::move (box);
        }
        else
        {
            auto toggle = std::make_unique<juce::ToggleButton>();
            toggle->setToggleState (c.initial > 0.5, juce::dontSendNotification);
            toggle->onClick = [this] { preview(); };
            editor = std::move (toggle);
        }

        addAndMakeVisible (*editor);
        editors.push_back (std::move (editor));
        controls.push_back (std::move (c));
    }

    double value (std::size_t i) const
    {
        if (auto* s = dynamic_cast<juce::Slider*> (editors[i].get()))
            return s->getValue();
        if (auto* b = dynamic_cast<juce::ComboBox*> (editors[i].get()))
            return juce::jmax (0, b->getSelectedItemIndex());
        if (auto* t = dynamic_cast<juce::ToggleButton*> (editors[i].get()))
            return t->getToggleState() ? 1.0 : 0.0;
        return 0.0;
    }

    void resetControls()
    {
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            const auto v = controls[i].initial;
            if (auto* s = dynamic_cast<juce::Slider*> (editors[i].get()))
                s->setValue (v, juce::dontSendNotification);
            else if (auto* b = dynamic_cast<juce::ComboBox*> (editors[i].get()))
                b->setSelectedItemIndex ((int) v, juce::dontSendNotification);
            else if (auto* t = dynamic_cast<juce::ToggleButton*> (editors[i].get()))
                t->setToggleState (v > 0.5, juce::dontSendNotification);
        }
        seed = 1;
        preview();
    }

    void buildControls()
    {
        const auto steps = [] (double v) { return juce::String (v, 2) + " " + tr ("steps"); };
        const auto signedPercent = [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; };

        switch (tool)
        {
            case NoteTool::strum:
                add ({ Control::slider, tr ("Spread"), 0.0, 2.0, 0.01, 0.25, steps, {} });
                add ({ Control::slider, tr ("Curve"), -1.0, 1.0, 0.01, 0.0, [] (double v) { return juce::String (v, 2); }, {} });
                add ({ Control::slider, tr ("Velocity per note"), -0.25, 0.25, 0.01, 0.0, signedPercent, {} });
                add ({ Control::choice, tr ("Direction"), 0, 1, 1, 0, {}, { tr ("Low to high"), tr ("High to low") } });
                add ({ Control::toggle, tr ("Alternate"), 0, 1, 1, 0, {}, {} });
                add ({ Control::toggle, tr ("Keep the ends"), 0, 1, 1, 1, {}, {} });
                break;
            case NoteTool::arpeggiate:
                add ({ Control::choice, tr ("Step"), 0, 5, 1, 3, {}, { "1/4", "1/8", tr ("1/8 triplet"), "1/16", tr ("1/16 triplet"), "1/32" } });
                add ({ Control::slider, tr ("Gate"), 0.05, 1.0, 0.01, 0.8, percent, {} });
                add ({ Control::choice, tr ("Direction"), 0, 3, 1, 0, {}, { tr ("Up"), tr ("Down"), tr ("Up and down"), tr ("Down and up") } });
                add ({ Control::slider, tr ("Octaves"), 1.0, 4.0, 1.0, 1.0, [] (double v) { return juce::String (juce::roundToInt (v)); }, {} });
                break;
            case NoteTool::flip:
                add ({ Control::toggle, tr ("Flip in time"), 0, 1, 1, 1, {}, {} });
                add ({ Control::toggle, tr ("Flip in pitch"), 0, 1, 1, 0, {}, {} });
                break;
            case NoteTool::randomise:
                add ({ Control::slider, tr ("Velocity"), 0.0, 1.0, 0.01, 0.2, percent, {} });
                add ({ Control::slider, tr ("Pan"), 0.0, 1.0, 0.01, 0.0, percent, {} });
                add ({ Control::slider, tr ("Fine pitch"), 0.0, 1.0, 0.01, 0.0, percent, {} });
                add ({ Control::slider, tr ("Timing"), 0.0, 0.5, 0.01, 0.0, steps, {} });
                add ({ Control::slider, tr ("Length"), 0.0, 1.0, 0.01, 0.0, percent, {} });
                add ({ Control::toggle, tr ("Both directions"), 0, 1, 1, 1, {}, {} });
                break;
            case NoteTool::scaleLevels:
            {
                juce::StringArray levels;
                for (int l = 0; l < notetools::levelCount; ++l)
                    levels.add (tr (notetools::levelName ((notetools::Level) l)));
                add ({ Control::choice, tr ("Level"), 0, (double) levels.size() - 1, 1, 0, {}, levels });
                add ({ Control::slider, tr ("Scale at the start"), 0.0, 2.0, 0.01, 1.0, percent, {} });
                add ({ Control::slider, tr ("Scale at the end"), 0.0, 2.0, 0.01, 1.0, percent, {} });
                add ({ Control::slider, tr ("Offset"), -1.0, 1.0, 0.01, 0.0, signedPercent, {} });
                break;
            }
            case NoteTool::limit:
                add ({ Control::slider, tr ("Lowest note"), 0.0, 127.0, 1.0, 36.0, [] (double v) { return keyName (juce::roundToInt (v)); }, {} });
                add ({ Control::slider, tr ("Highest note"), 0.0, 127.0, 1.0, 84.0, [] (double v) { return keyName (juce::roundToInt (v)); }, {} });
                add ({ Control::slider, tr ("Lowest velocity"), 0.0, 1.0, 0.01, 0.0, percent, {} });
                add ({ Control::slider, tr ("Highest velocity"), 0.0, 1.0, 0.01, 1.0, percent, {} });
                break;
        }
    }

    // applies the tool with the current control values
    void apply (Pattern& p) const
    {
        using namespace notetools;

        switch (tool)
        {
            case NoteTool::strum:
            {
                StrumOptions o;
                o.spread = (std::int64_t) std::llround (value (0) * ticksPerStep);
                o.curve = (float) value (1);
                o.velocityStep = (float) value (2);
                o.downward = value (3) > 0.5;
                o.alternate = value (4) > 0.5;
                o.keepEnds = value (5) > 0.5;
                strum (p, indices, o);
                break;
            }
            case NoteTool::arpeggiate:
            {
                static const std::int64_t stepTicks[] = { ppq, ppq / 2, ppq / 3, ppq / 4, ppq / 6, ppq / 8 };
                ArpeggiateOptions o;
                o.step = stepTicks[juce::jlimit (0, 5, (int) value (0))];
                o.gate = (float) value (1);
                o.direction = (int) value (2);
                o.octaves = juce::roundToInt (value (3));
                arpeggiate (p, indices, o);
                break;
            }
            case NoteTool::flip:
                flip (p, indices, value (0) > 0.5, value (1) > 0.5);
                break;
            case NoteTool::randomise:
            {
                RandomiseOptions o;
                o.velocity = (float) value (0);
                o.pan = (float) value (1);
                o.finePitch = (float) value (2);
                o.timing = (std::int64_t) std::llround (value (3) * ticksPerStep);
                o.length = (float) value (4);
                o.bipolar = value (5) > 0.5;
                o.seed = seed;
                randomise (p, indices, o);
                break;
            }
            case NoteTool::scaleLevels:
                scaleLevels (p, indices, (Level) (int) value (0), (float) value (1), (float) value (2), (float) value (3));
                break;
            case NoteTool::limit:
            {
                LimitOptions o;
                o.lowKey = juce::roundToInt (value (0));
                o.highKey = juce::roundToInt (value (1));
                o.minVelocity = (float) value (2);
                o.maxVelocity = (float) value (3);
                limit (p, indices, o);
                break;
            }
        }

        p.sortNotes();
    }

    bool patternStillThere() const { return patternIndex < (int) engine.getSong().patterns.size(); }

    void restore()
    {
        if (patternStillThere())
            engine.editSong ([this] (Song& s) { s.patterns[(std::size_t) patternIndex].notes = originals; }, false);
    }

    void preview()
    {
        if (patternStillThere())
            engine.editSong ([this] (Song& s) {
                auto& p = s.patterns[(std::size_t) patternIndex];
                p.notes = originals;
                apply (p);
            }, false);
    }

    void accept()
    {
        if (patternStillThere())
        {
            restore();
            engine.editSong ([this] (Song& s) { apply (s.patterns[(std::size_t) patternIndex]); });
        }

        accepted = true;
        close();
    }

    void close()
    {
        if (! accepted)
            restore();
        accepted = true; // nothing left to undo in the destructor

        if (onFinished)
            onFinished();

        closeToolWindowOf (*this);
    }

    AudioEngine& engine;
    NoteTool tool;
    std::vector<int> indices;
    std::int64_t grid;
    int patternIndex = 0;
    std::vector<Note> originals;
    std::uint32_t seed = 1;
    bool accepted = false;

    std::vector<Control> controls;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::Component>> editors;
    juce::Label title, info;
    juce::TextButton acceptButton { tr ("Accept") }, resetButton { tr ("Reset") }, cancelButton { tr ("Cancel") };
    juce::TextButton seedButton { tr ("New variation") };
};
} // namespace

const char* noteToolTitle (NoteTool tool)
{
    switch (tool)
    {
        case NoteTool::strum: return TR_NOOP ("Strum");
        case NoteTool::arpeggiate: return TR_NOOP ("Arpeggiate");
        case NoteTool::flip: return TR_NOOP ("Flip");
        case NoteTool::randomise: return TR_NOOP ("Randomise");
        case NoteTool::scaleLevels: return TR_NOOP ("Scale levels");
        case NoteTool::limit: return TR_NOOP ("Limit");
    }
    return "";
}

std::unique_ptr<juce::Component> makeNoteToolPanel (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid)
{
    return std::make_unique<NoteToolPanel> (engine, tool, std::move (indices), grid);
}

void showNoteTool (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid, std::function<void()> onFinished)
{
    if (indices.empty())
        return;

    auto panel = std::make_unique<NoteToolPanel> (engine, tool, std::move (indices), grid);
    panel->onFinished = std::move (onFinished);
    openToolWindow (std::move (panel), tr (noteToolTitle (tool)), "notetool");
}
} // namespace lynx
