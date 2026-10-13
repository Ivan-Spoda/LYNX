#pragma once

#include <juce_core/juce_core.h>

namespace lynx
{
// JUCE treats char* literals as ASCII. All non-ASCII text goes through here.
inline juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}
} // namespace lynx
