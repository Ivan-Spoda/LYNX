#include "ui/ParametricEqEditor.h"

#include <map>

#include "core/Effects.h"
#include "engine/Text.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
constexpr double minHz = 20.0, maxHz = 20000.0;
constexpr double rangeDb = 24.0; // o gráfico vai de -24 a +24 dB

juce::Colour bandColour (int band)
{
    static const juce::Colour colours[] = { juce::Colour (0xffff6b6b), juce::Colour (0xffffa94d), juce::Colour (0xffffe066),
                                            juce::Colour (0xff8ce99a), juce::Colour (0xff66d9e8), juce::Colour (0xff74c0fc),
                                            juce::Colour (0xffd0bfff) };
    return colours[band % 7];
}

float xForHz (juce::Rectangle<float> area, double hz)
{
    return area.getX() + area.getWidth() * (float) (std::log (hz / minHz) / std::log (maxHz / minHz));
}

double hzForX (juce::Rectangle<float> area, float x)
{
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0, 1.0, (double) ((x - area.getX()) / area.getWidth())));
}

float yForDb (juce::Rectangle<float> area, double db)
{
    return area.getCentreY() - area.getHeight() * 0.5f * (float) (juce::jlimit (-rangeDb, rangeDb, db) / rangeDb);
}

double dbForY (juce::Rectangle<float> area, float y)
{
    return juce::jlimit (-18.0, 18.0, (double) ((area.getCentreY() - y) / (area.getHeight() * 0.5f)) * rangeDb);
}

bool hasGain (peq::BandType t)
{
    return t == peq::BandType::peak || t == peq::BandType::lowShelf || t == peq::BandType::highShelf;
}

// --- o editor -------------------------------------------------------------------------------

class ParametricEqEditor final : public juce::Component,
                                 private juce::ChangeListener
{
public:
    ParametricEqEditor (AudioEngine& e, int t, int s)
        : engine (e),
          track (t),
          slot (s)
    {
        engine.addChangeListener (this);
        setSize (760, 380);
    }

    ~ParametricEqEditor() override { engine.removeChangeListener (this); }

    // o slot ainda é um EQ paramétrico? (pode ter sido removido ou trocado)
    const EffectSlot* current() const
    {
        const auto& mixer = engine.getSong().mixer;

        if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
            return nullptr;

        const auto& s = mixer[(std::size_t) track].effects[(std::size_t) slot];
        return s.kind == EffectKind::parametricEq ? &s : nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::window);
        const auto* s = current();

        if (s == nullptr)
        {
            g.setColour (theme::text);
            g.drawText (utf8 ("O EQ foi removido deste slot."), getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto area = graphArea();
        drawParametricEqCurve (g, area, s->params, sampleRate(), s->enabled, selected);

        // alças
        for (int b = 0; b < peq::bands; ++b)
        {
            const auto t = peq::type (s->params[(std::size_t) peq::param (b, 0)]);

            if (t == peq::BandType::off)
                continue;

            const auto centre = handlePosition (*s, b);
            const auto r = juce::Rectangle<float> (16.0f, 16.0f).withCentre (centre);
            g.setColour (bandColour (b).withAlpha (b == selected ? 1.0f : 0.75f));
            g.fillEllipse (r);
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (b + 1), r, juce::Justification::centred);
        }

        // rodapé: a banda selecionada e a saída
        auto footer = getLocalBounds().removeFromBottom (54).reduced (12, 6);
        g.setFont (juce::FontOptions (13.0f));

        if (selected >= 0)
        {
            const auto& p = s->params;
            const auto t = peq::type (p[(std::size_t) peq::param (selected, 0)]);
            juce::String text;
            text << "Banda " << (selected + 1) << ": " << utf8 (peq::typeName (t)) << "   "
                 << juce::String::fromUTF8 (formatEffectParam (EffectKind::parametricEq, peq::param (selected, 1), p[(std::size_t) peq::param (selected, 1)], 120.0).c_str());

            if (hasGain (t))
                text << "   " << juce::String::fromUTF8 (formatEffectParam (EffectKind::parametricEq, peq::param (selected, 2), p[(std::size_t) peq::param (selected, 2)], 120.0).c_str());

            if (t == peq::BandType::lowCut || t == peq::BandType::highCut)
                text << "   " << juce::String::fromUTF8 (formatEffectParam (EffectKind::parametricEq, peq::slopeParam + selected, p[(std::size_t) (peq::slopeParam + selected)], 120.0).c_str());
            else
                text << "   " << juce::String::fromUTF8 (formatEffectParam (EffectKind::parametricEq, peq::param (selected, 3), p[(std::size_t) peq::param (selected, 3)], 120.0).c_str());

            g.setColour (bandColour (selected));
            g.drawText (text, footer.removeFromTop (20), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (theme::text.withAlpha (0.6f));
            g.drawText (utf8 ("Arraste uma alça (frequência e ganho); roda do mouse = Q; clique direito = tipo; duplo clique = 0 dB"),
                        footer.removeFromTop (20), juce::Justification::centredLeft);
        }

        g.setColour (theme::text.withAlpha (0.8f));
        g.drawText (utf8 ("Saída: ") + juce::String::fromUTF8 (formatEffectParam (EffectKind::parametricEq, peq::outputParam, s->params[peq::outputParam], 120.0).c_str())
                        + utf8 ("   (arraste aqui na vertical)"),
                    outputArea(), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* s = current();

        if (s == nullptr)
            return;

        dragOutput = outputArea().contains (e.getPosition());

        if (dragOutput)
        {
            dragStartValue = s->params[peq::outputParam];
            return;
        }

        selected = bandAt (*s, e.position);

        if (selected < 0)
        {
            repaint();
            return;
        }

        if (e.mods.isPopupMenu())
        {
            showBandMenu (selected);
            return;
        }

        dragOffset = handlePosition (*s, selected) - e.position;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto* s = current();

        if (s == nullptr)
            return;

        if (dragOutput)
        {
            const auto v = juce::jlimit (0.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() / 200.0f);
            setParams ({ { peq::outputParam, v } });
            return;
        }

        if (selected < 0 || e.mods.isPopupMenu())
            return;

        const auto area = graphArea();
        const auto p = e.position + dragOffset;
        const auto t = peq::type (s->params[(std::size_t) peq::param (selected, 0)]);
        std::vector<std::pair<int, float>> changes { { peq::param (selected, 1), peq::hzValue (hzForX (area, p.x)) } };

        if (hasGain (t))
            changes.push_back ({ peq::param (selected, 2), peq::gainValue (dbForY (area, p.y)) });

        setParams (changes);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto* s = current();

        if (s == nullptr)
            return;

        if (const auto b = bandAt (*s, e.position); b >= 0)
            setParams ({ { peq::param (b, 2), 0.5f } });
        else if (outputArea().contains (e.getPosition()))
            setParams ({ { peq::outputParam, 0.5f } });
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto* s = current();

        if (s == nullptr)
            return;

        auto b = bandAt (*s, e.position);
        if (b < 0)
            b = selected;
        if (b < 0)
            return;

        selected = b;
        const auto q = s->params[(std::size_t) peq::param (b, 3)];
        setParams ({ { peq::param (b, 3), juce::jlimit (0.0f, 1.0f, q + wheel.deltaY * 0.15f) } });
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    double sampleRate() const { return juce::jmax (8000.0, engine.getTransport().sampleRate.load()); }

    juce::Rectangle<float> graphArea() const { return getLocalBounds().reduced (12).withTrimmedBottom (54).toFloat(); }
    juce::Rectangle<int> outputArea() const { return getLocalBounds().removeFromBottom (30).reduced (12, 4); }

    juce::Point<float> handlePosition (const EffectSlot& s, int band) const
    {
        const auto area = graphArea();
        const auto t = peq::type (s.params[(std::size_t) peq::param (band, 0)]);
        const auto hz = peq::hz (s.params[(std::size_t) peq::param (band, 1)]);
        const auto db = hasGain (t) ? peq::gainDb (s.params[(std::size_t) peq::param (band, 2)]) : 0.0;
        return { xForHz (area, hz), yForDb (area, db) };
    }

    int bandAt (const EffectSlot& s, juce::Point<float> p) const
    {
        int best = -1;
        float bestDistance = 12.0f;

        for (int b = 0; b < peq::bands; ++b)
            if (peq::type (s.params[(std::size_t) peq::param (b, 0)]) != peq::BandType::off)
                if (const auto d = handlePosition (s, b).getDistanceFrom (p); d < bestDistance)
                {
                    best = b;
                    bestDistance = d;
                }

        return best;
    }

    void setParams (const std::vector<std::pair<int, float>>& changes)
    {
        engine.editSong ([t = track, sl = slot, changes] (Song& song) {
            if (t >= (int) song.mixer.size() || sl >= (int) song.mixer[(std::size_t) t].effects.size())
                return;

            auto& params = song.mixer[(std::size_t) t].effects[(std::size_t) sl].params;
            for (auto [p, v] : changes)
                params[(std::size_t) p] = v;
        });
    }

    void showBandMenu (int band)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Banda " + juce::String (band + 1));
        const auto* s = current();
        const auto currentType = peq::type (s->params[(std::size_t) peq::param (band, 0)]);

        for (int t = 0; t < peq::bandTypes; ++t)
            menu.addItem (utf8 (peq::typeName ((peq::BandType) t)), true, (int) currentType == t, [this, band, t] {
                setParams ({ { peq::param (band, 0), peq::typeValue ((peq::BandType) t) } });
            });

        if (currentType == peq::BandType::lowCut || currentType == peq::BandType::highCut)
        {
            menu.addSeparator();
            for (int stages = 1; stages <= 4; ++stages)
                menu.addItem (juce::String (12 * stages) + " dB/oit", true, peq::slopeStages (s->params[(std::size_t) (peq::slopeParam + band)]) == stages,
                              [this, band, stages] { setParams ({ { peq::slopeParam + band, peq::slopeValue (stages) } }); });
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    AudioEngine& engine;
    int track, slot;
    int selected = -1;
    juce::Point<float> dragOffset;
    bool dragOutput = false;
    float dragStartValue = 0.5f;
};

class ParametricEqWindow final : public juce::DocumentWindow
{
public:
    ParametricEqWindow (AudioEngine& engine, int track, int slot, const juce::String& title)
        : DocumentWindow (title, theme::window, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ParametricEqEditor (engine, track, slot), true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override;
};

std::map<std::pair<int, int>, std::unique_ptr<ParametricEqWindow>>& windows()
{
    static std::map<std::pair<int, int>, std::unique_ptr<ParametricEqWindow>> open;
    return open;
}

void ParametricEqWindow::closeButtonPressed()
{
    for (auto& [key, window] : windows())
        if (window.get() == this)
        {
            juce::MessageManager::callAsync ([k = key] { windows().erase (k); });
            return;
        }
}
} // namespace

void drawParametricEqCurve (juce::Graphics& g, juce::Rectangle<float> area, const EffectParams& params, double sampleRate,
                            bool enabled, int highlightBand)
{
    g.setColour (theme::knobBackground);
    g.fillRoundedRectangle (area, 4.0f);

    // grade: décadas de frequência e ±6/12/18 dB
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        g.drawVerticalLine ((int) xForHz (area, hz), area.getY(), area.getBottom());
    for (double db : { -18.0, -12.0, -6.0, 6.0, 12.0, 18.0 })
        g.drawHorizontalLine ((int) yForDb (area, db), area.getX(), area.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine ((int) yForDb (area, 0.0), area.getX(), area.getRight());

    if (area.getWidth() > 300.0f)
    {
        g.setColour (theme::text.withAlpha (0.4f));
        g.setFont (juce::FontOptions (10.0f));
        for (auto [hz, label] : { std::pair { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
            g.drawText (label, (int) xForHz (area, hz) + 3, (int) area.getBottom() - 14, 30, 12, juce::Justification::centredLeft);
    }

    // curva
    juce::Path curve;
    const auto steps = juce::jmax (32, (int) area.getWidth() / 2);

    for (int i = 0; i <= steps; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) steps;
        const auto y = yForDb (area, peq::responseDb (params, sampleRate, hzForX (area, x)));

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    auto fill = curve;
    fill.lineTo (area.getRight(), area.getCentreY());
    fill.lineTo (area.getX(), area.getCentreY());
    fill.closeSubPath();
    const auto colour = enabled ? (highlightBand >= 0 ? bandColour (highlightBand) : theme::accentOrange) : juce::Colours::grey;
    g.setColour (colour.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

std::unique_ptr<juce::Component> makeParametricEqEditor (AudioEngine& engine, int track, int slot)
{
    return std::make_unique<ParametricEqEditor> (engine, track, slot);
}

namespace ParametricEqWindows
{
void open (AudioEngine& engine, int track, int slot)
{
    auto& open = windows();
    const auto key = std::make_pair (track, slot);

    if (auto it = open.find (key); it != open.end())
    {
        it->second->toFront (true);
        return;
    }

    const auto& mixer = engine.getSong().mixer;
    const auto name = track < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str()) : juce::String();
    const auto title = utf8 ("EQ paramétrico - ") + (track == 0 ? juce::String ("Master") : name.isNotEmpty() ? name : "Insert " + juce::String (track))
                       + " (slot " + juce::String (slot + 1) + ")";
    open[key] = std::make_unique<ParametricEqWindow> (engine, track, slot, title);
}

void closeAll()
{
    windows().clear();
}
} // namespace ParametricEqWindows
} // namespace lynx
