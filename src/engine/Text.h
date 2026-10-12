#pragma once

#include <juce_core/juce_core.h>

namespace daw
{
// O JUCE trata literais char* como ASCII. Todo texto com acento passa por aqui.
inline juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}
} // namespace daw
