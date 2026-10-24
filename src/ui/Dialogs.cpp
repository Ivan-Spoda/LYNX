#include "ui/Dialogs.h"

#include "core/RackOps.h"
#include "engine/MidiFileIo.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
// a row of swatches: "automatic" first, then the palette
class Swatches final : public juce::Component
{
public:
    explicit Swatches (std::uint32_t c) : colour (c) { setSize (13 * (size + gap), size + 4); }

    std::uint32_t colour;

    void paint (juce::Graphics& g) override
    {
        const auto& palette = lynxPalette();

        for (int i = 0; i <= (int) palette.size(); ++i)
        {
            const auto r = cell (i).toFloat();
            const auto value = i == 0 ? 0u : palette[(std::size_t) i - 1];

            if (i == 0)
            {
                g.setColour (theme::colour::bg4);
                g.fillRoundedRectangle (r, theme::radius::sm);
                g.setColour (theme::colour::textSecondary);
                g.drawLine (r.getX() + 5.0f, r.getBottom() - 5.0f, r.getRight() - 5.0f, r.getY() + 5.0f, 1.2f);
            }
            else
            {
                g.setColour (juce::Colour (value));
                g.fillRoundedRectangle (r, theme::radius::sm);
            }

            if (value == colour)
            {
                g.setColour (theme::colour::textPrimary);
                g.drawRoundedRectangle (r.expanded (1.5f), theme::radius::md, 2.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& palette = lynxPalette();

        for (int i = 0; i <= (int) palette.size(); ++i)
            if (cell (i).expanded (1).contains (e.getPosition()))
            {
                colour = i == 0 ? 0u : palette[(std::size_t) i - 1];
                repaint();
            }
    }

    void pickRandom()
    {
        colour = randomPaletteColour (colour, (std::uint32_t) juce::Time::getMillisecondCounter());
        repaint();
    }

private:
    static constexpr int size = 22, gap = 6;
    juce::Rectangle<int> cell (int i) const { return { 2 + i * (size + gap), 2, size, size }; }
};

class NameAndColourWindow final : public juce::AlertWindow
{
public:
    NameAndColourWindow (const juce::String& title, const juce::String& name, std::uint32_t colour)
        : juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon), swatches (colour)
    {
        addTextEditor ("name", name, tr ("Name"));
        addCustomComponent (&swatches);
        hint.setText (tr ("Click a color, or press F2 for a random one"), juce::dontSendNotification);
        hint.setFont (theme::font (theme::type::small));
        hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hint.setSize (swatches.getWidth(), 18);
        addCustomComponent (&hint);
        addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* editor = getTextEditor ("name"))
            editor->selectAll();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::F2Key)
        {
            swatches.pickRandom();
            return true;
        }

        return juce::AlertWindow::keyPressed (key);
    }

    std::uint32_t colour() const { return swatches.colour; }

private:
    Swatches swatches;
    juce::Label hint;
};

// One big choice of the record dialog: a title line and a note below it.
class RecordChoiceButton final : public juce::Button
{
public:
    RecordChoiceButton (const juce::String& title, const juce::String& note)
        : juce::Button ({}), noteText (note) // (no component name: the alert window would print it above)
    {
        setButtonText (title);
        setSize (380, 46);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (down ? colour::bg5 : over ? colour::bg4 : colour::bg3);
        g.fillRoundedRectangle (r, radius::md);
        g.setColour (over ? colour::accent : colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius::md, 1.0f);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        g.drawText (getButtonText(), getLocalBounds().reduced (12, 0).withTrimmedTop (5).withHeight (18), juce::Justification::centredLeft, true);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (noteText, getLocalBounds().reduced (12, 0).withTrimmedTop (24).withHeight (16), juce::Justification::centredLeft, true);
    }

private:
    juce::String noteText;
};

class RecordChoiceWindow final : public juce::AlertWindow
{
public:
    RecordChoiceWindow()
        : juce::AlertWindow (tr ("Recording"), tr ("What would you like to record?"), juce::MessageBoxIconType::NoIcon)
    {
        int result = recordChoiceAudio;
        for (auto* b : { &audio, &notes, &everything, &editor })
        {
            b->onClick = [this, result] { exitModalState (result); };
            addCustomComponent (b);
            ++result;
        }
        dontAsk.setButtonText (tr ("Don't ask again (right-click the record button for the recording filter)"));
        dontAsk.setSize (380, 24);
        addCustomComponent (&dontAsk);
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    bool dontAskAgain() const { return dontAsk.getToggleState(); }

private:
    RecordChoiceButton audio { tr ("Audio, into the Playlist as an audio clip"),
                               tr ("Arms the selected mixer insert; choose its audio input (microphone) if it has none") },
        notes { tr ("Notes and automation"), tr ("Notes played on a MIDI keyboard or the PC keyboard, and the controls you move") },
        everything { tr ("Everything"), tr ("Notes, automation, and audio from the armed inserts") },
        editor { tr ("Audio, into the audio editor"), tr ("Records the audio input into a new audio editor while the song plays") };
    juce::ToggleButton dontAsk;
};
} // namespace

std::unique_ptr<juce::AlertWindow> makeRecordChoiceDialog()
{
    return std::make_unique<RecordChoiceWindow>();
}

namespace
{
// FL's "Import MIDI data" window (REA round 143, TMIDIImportForm): its options as LYNX can apply them
class MidiImportWindow final : public juce::AlertWindow
{
public:
    MidiImportWindow (const juce::String& fileName, const MidiFileIo::ImportOptions& o)
        : juce::AlertWindow (tr ("Import MIDI data"), fileName, juce::MessageBoxIconType::NoIcon)
    {
        auto add = [this] (juce::ToggleButton& b, const juce::String& text, const juce::String& hint, bool on) {
            b.setButtonText (text);
            b.setTooltip (hint);
            b.setToggleState (on, juce::dontSendNotification);
            b.setSize (360, 24);
            addCustomComponent (&b);
        };
        add (newProject, tr ("Start new project"), tr ("Start a blank project, or add to the open one"), o.newProject);
        add (perTrack, tr ("Create one channel per track"), tr ("One channel per track, or per MIDI channel"), o.perTrack);
        add (realign, tr ("Realign events"), tr ("Removes any silent part at the beginning"), o.realign);
        add (timeSignatures, tr ("Import time signatures"), tr ("The file's time signature becomes the new project's"), o.timeSignatures);
        add (mixerTracks, tr ("Set mixer tracks for new channels"), tr ("Route new channels to consecutive mixer tracks"), o.mixerTracks);
        add (midiOut, tr ("Channel type: MIDI Out (else LYNX's synth)"), tr ("MIDI Out channels play the plugins on their MIDI port, on the note's MIDI channel"),
             o.midiOut);
        addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    MidiFileIo::ImportOptions options() const
    {
        MidiFileIo::ImportOptions o;
        o.newProject = newProject.getToggleState();
        o.perTrack = perTrack.getToggleState();
        o.realign = realign.getToggleState();
        o.timeSignatures = timeSignatures.getToggleState();
        o.mixerTracks = mixerTracks.getToggleState();
        o.midiOut = midiOut.getToggleState();
        return o;
    }

private:
    juce::ToggleButton newProject, perTrack, realign, timeSignatures, mixerTracks, midiOut;
};
} // namespace

void showMidiImportDialog (const juce::String& fileName, const MidiFileIo::ImportOptions& current,
                           std::function<void (const MidiFileIo::ImportOptions&)> onAccept)
{
    auto* window = new MidiImportWindow (fileName, current);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onAccept] (int result) {
                                 if (result == 1)
                                     onAccept (window->options());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

std::unique_ptr<juce::AlertWindow> makeMidiImportDialog()
{
    return std::make_unique<MidiImportWindow> ("song.mid", MidiFileIo::ImportOptions {});
}

void showRecordChoiceDialog (std::function<void (int, bool)> onChoice)
{
    auto* window = new RecordChoiceWindow();
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onChoice] (int result) {
                                 if (result > 0)
                                     onChoice (result, window->dontAskAgain());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

std::unique_ptr<juce::AlertWindow> makeNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour)
{
    return std::make_unique<NameAndColourWindow> (title, name, colour);
}

void showNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour,
                              std::function<void (const juce::String&, std::uint32_t)> onDone)
{
    auto* window = new NameAndColourWindow (title, name, colour);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onDone] (int result) {
                                 const auto typed = window->getTextEditorContents ("name").trim();

                                 if (result == 1 && typed.isNotEmpty())
                                     onDone (typed, window->colour());
                             }),
                             true);
    keepAboveMainWindow (*window);
}
} // namespace lynx
