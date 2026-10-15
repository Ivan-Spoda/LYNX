#include "ui/QuickAddPalette.h"

#include "core/Effects.h"
#include "core/SearchQuery.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
struct Entry
{
    juce::String name, detail;
    std::function<void()> add;
};

class EntryList final : public juce::ListBox,
                        private juce::ListBoxModel
{
public:
    EntryList()
    {
        setModel (this);
        setRowHeight (24);
        setColour (juce::ListBox::backgroundColourId, theme::colour::bg1);
        setOutlineThickness (0);
        setWantsKeyboardFocus (false);
    }

    std::vector<Entry> all, shown;
    std::function<void (const Entry&)> onChosen;

    void filter (const juce::String& query)
    {
        const auto q = search::parse (query.toStdString());
        shown.clear();
        for (const auto& e : all)
            if (search::matches (q, (e.name + " " + e.detail).toStdString()))
                shown.push_back (e);
        updateContent();
        selectRow (shown.empty() ? -1 : 0);
        repaint();
    }

    void chooseSelected()
    {
        const auto row = getSelectedRow();
        if (row >= 0 && row < (int) shown.size() && onChosen)
            onChosen (shown[(std::size_t) row]);
    }

private:
    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size())
            return;

        if (selected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
        }

        const auto& e = shown[(std::size_t) row];
        g.setFont (theme::font (theme::type::body));
        g.setColour (theme::colour::textPrimary);
        g.drawText (e.name, 8, 0, width - 16, height, juce::Justification::centredLeft, true);

        if (e.detail.isNotEmpty())
        {
            g.setFont (theme::font (theme::type::caption));
            g.setColour (theme::colour::textSecondary);
            g.drawText (e.detail, 8, 0, width - 16, height, juce::Justification::centredRight, true);
        }
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int) shown.size() && onChosen)
            onChosen (shown[(std::size_t) row]);
    }
};

class QuickAddPalette final : public juce::Component,
                              private juce::TextEditor::Listener,
                              private juce::KeyListener
{
public:
    QuickAddPalette (AudioEngine& engine, QuickAddTargets t) : targets (std::move (t))
    {
        search.setTextToShowWhenEmpty (tr ("Type to filter (e.g. reverb, synth OR bass, -delay)"), theme::colour::textDisabled);
        search.addListener (this);
        search.addKeyListener (this);
        search.setWantsKeyboardFocus (true);
        addAndMakeVisible (search);

        for (auto* l : { &instrumentsTitle, &effectsTitle })
        {
            l->setFont (theme::font (theme::type::small, true));
            l->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (*l);
        }
        instrumentsTitle.setText (tr ("Instruments: new channel"), juce::dontSendNotification);
        effectsTitle.setText (tr ("Effects: into the selected insert"), juce::dontSendNotification);
        hint.setText (tr ("Enter adds the highlighted item, Up/Down move, Tab switches lists, Esc closes"), juce::dontSendNotification);
        hint.setFont (theme::font (theme::type::caption));
        hint.setColour (juce::Label::textColourId, theme::colour::textDisabled);
        addAndMakeVisible (hint);

        // built-in instruments
        const std::pair<InstrumentKind, const char*> builtIn[] = { { InstrumentKind::synth, TR_NOOP ("Synth") }, { InstrumentKind::kick, TR_NOOP ("Kick") },
                                                                   { InstrumentKind::snare, TR_NOOP ("Snare") }, { InstrumentKind::hat, TR_NOOP ("Hat") },
                                                                   { InstrumentKind::clap, TR_NOOP ("Clap") } };
        for (const auto& [kind, name] : builtIn)
            instruments.all.push_back ({ tr (name), tr ("built-in"), [this, kind = kind, english = juce::String (name)] {
                                            ChannelDef c;
                                            c.name = english.toStdString();
                                            c.instrument = kind;
                                            targets.addInstrument (c);
                                        } });

        for (auto kind : addableEffects())
            effects.all.push_back ({ juce::String::fromUTF8 (effectInfo (kind).name), tr ("built-in"), [this, kind] { targets.addEffect (makeEffectSlot (kind)); } });

        auto types = engine.getPluginHost().getKnownPlugins().getTypes();
        std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

        for (const auto& type : types)
        {
            PluginRef ref;
            ref.id = type.createIdentifierString().toStdString();
            ref.name = type.name.toStdString();
            const auto detail = type.manufacturerName + " · " + type.pluginFormatName;

            if (type.isInstrument)
                instruments.all.push_back ({ type.name, detail, [this, ref] {
                                                ChannelDef c;
                                                c.name = ref.name;
                                                c.instrument = InstrumentKind::plugin;
                                                c.plugin = ref;
                                                targets.addInstrument (c);
                                            } });
            else
                effects.all.push_back ({ type.name, detail, [this, ref] {
                                            EffectSlot slot;
                                            slot.kind = EffectKind::plugin;
                                            slot.plugin = ref;
                                            targets.addEffect (slot);
                                        } });
        }

        for (auto* list : { &instruments, &effects })
        {
            list->onChosen = [this] (const Entry& e) { choose (e); };
            addAndMakeVisible (*list);
            list->filter ({});
        }

        active = targets.effectsFirst ? &effects : &instruments;
        setSize (640, 420);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::accent);
        const auto r = active->getBounds().toFloat().expanded (1.0f);
        g.drawRect (r, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        search.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        hint.setBounds (area.removeFromBottom (18));
        auto titles = area.removeFromTop (20);
        instrumentsTitle.setBounds (titles.removeFromLeft (area.getWidth() / 2));
        effectsTitle.setBounds (titles.withTrimmedLeft (6));
        instruments.setBounds (area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (3));
        effects.setBounds (area.withTrimmedLeft (3));
    }

    void parentHierarchyChanged() override
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }

    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override { return handleKey (key); }

    bool handleKey (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::returnKey)
        {
            active->chooseSelected();
            return true;
        }

        if (key == juce::KeyPress::tabKey)
        {
            active = active == &instruments ? &effects : &instruments;
            repaint();
            return true;
        }

        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            const auto count = (int) active->shown.size();
            if (count > 0)
            {
                const auto row = juce::jlimit (0, count - 1, active->getSelectedRow() + (key == juce::KeyPress::upKey ? -1 : 1));
                active->selectRow (row);
                active->scrollToEnsureRowIsOnscreen (row);
            }
            return true;
        }

        return false;
    }

private:
    void textEditorTextChanged (juce::TextEditor&) override
    {
        instruments.filter (search.getText());
        effects.filter (search.getText());

        // the list that has matches when the preferred one has none
        if (active->shown.empty())
        {
            auto* other = active == &instruments ? &effects : &instruments;
            if (! other->shown.empty())
                active = other;
        }
        repaint();
    }

    void textEditorReturnKeyPressed (juce::TextEditor&) override { active->chooseSelected(); }

    void choose (const Entry& e)
    {
        if (e.add)
            e.add();
        closeToolWindowOf (*this);
    }

    QuickAddTargets targets;
    juce::TextEditor search;
    juce::Label instrumentsTitle, effectsTitle, hint;
    EntryList instruments, effects;
    EntryList* active = nullptr;
};
} // namespace

std::unique_ptr<juce::Component> makeQuickAddPalette (AudioEngine& engine, QuickAddTargets targets)
{
    return std::make_unique<QuickAddPalette> (engine, std::move (targets));
}
} // namespace lynx
