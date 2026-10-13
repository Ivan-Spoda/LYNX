#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Paleta e LookAndFeel: tons de cinza-azulado escuros, destaques em laranja,
// verde e azul, no espírito do FL Studio.
namespace lynx::theme
{
inline const juce::Colour window { 0xff1e2226 };
inline const juce::Colour panel { 0xff2b3036 };
inline const juce::Colour toolbar { 0xff32383f };
inline const juce::Colour channelButton { 0xff434a52 };
inline const juce::Colour channelButtonSelected { 0xff56606b };
inline const juce::Colour knobBackground { 0xff1b1e22 };
inline const juce::Colour stepOn { 0xffe8e8e8 };
inline const juce::Colour stepOnOffGrid { 0xff9fd3ff };
inline const juce::Colour accentOrange { 0xffff9a3c };
inline const juce::Colour accentGreen { 0xff7ed957 };
inline const juce::Colour accentBlue { 0xff58b7ff };
inline const juce::Colour playhead { 0xff30c0ff };
inline const juce::Colour text { 0xffe6e6e6 };

inline juce::Colour stepOff (int step)
{
    return (step / 4) % 2 == 0 ? juce::Colour (0xff4a525b) : juce::Colour (0xff5b4f4f); // grupos de 4 alternados
}

inline juce::Colour muteLed (bool muted)
{
    return muted ? juce::Colour (0xff505050) : accentGreen;
}

// Cor de nota no Piano Roll pela velocity (fraca = esverdeada, forte = laranja).
inline juce::Colour noteColour (float velocity)
{
    return juce::Colour (0xff7ed957).interpolatedWith (accentOrange, juce::jlimit (0.0f, 1.0f, velocity));
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              window, panel, toolbar, juce::Colour (0xff4d5560), text, toolbar, juce::Colours::white, accentOrange,
              text })
    {
        setColour (juce::TextButton::buttonOnColourId, accentOrange.darker (0.3f));
        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedButtonBar::tabTextColourId, text.withAlpha (0.6f));
        setColour (juce::TabbedButtonBar::frontTextColourId, accentOrange);
        setColour (juce::Slider::rotarySliderFillColourId, accentOrange);
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff4a525b));
        setColour (juce::Slider::thumbColourId, juce::Colours::white);
    }
};
} // namespace lynx::theme
