#include "ui/I18n.h"

#include "engine/PluginHost.h"
#include "ui/I18nData.h"

namespace lynx
{
namespace
{
juce::String& currentId()
{
    static juce::String id { "en" };
    return id;
}

juce::File settingsFile()
{
    return PluginHost::settingsFolder().getChildFile ("language.txt");
}

bool isKnown (const juce::String& id)
{
    for (const auto& l : i18n::languages())
        if (id == l.id)
            return true;
    return false;
}
} // namespace

juce::String tr (const juce::String& english)
{
    return juce::translate (english);
}

juce::String tr (const char* englishUtf8)
{
    return juce::translate (juce::String::fromUTF8 (englishUtf8));
}

juce::String tr (const char* englishUtf8, std::initializer_list<juce::String> args)
{
    auto text = tr (englishUtf8);
    int i = 0;
    for (const auto& a : args)
        text = text.replace ("{" + juce::String (i++) + "}", a);
    return text;
}

namespace i18n
{
const std::vector<Language>& languages()
{
    static const std::vector<Language> list { { "en", "English" }, { "pt-BR", "Portugu\xc3\xaas (Brasil)" } };
    return list;
}

juce::String current()
{
    return currentId();
}

juce::String saved()
{
    const auto id = settingsFile().loadFileAsString().trim();
    return isKnown (id) ? id : juce::String ("en");
}

void save (const juce::String& id)
{
    if (isKnown (id))
        settingsFile().replaceWithText (id);
}

juce::String tableText (const juce::String& id)
{
    if (id == "pt-BR")
        return juce::String::fromUTF8 ((const char*) data::ptBR, (int) sizeof (data::ptBR));
    return {};
}

void initialise (const juce::StringArray& args)
{
    auto id = saved();
    const auto flag = args.indexOf ("--lang");

    if (flag >= 0 && isKnown (args[flag + 1].unquoted()))
        id = args[flag + 1].unquoted();

    currentId() = id;
    const auto table = tableText (id);
    juce::LocalisedStrings::setCurrentMappings (table.isEmpty() ? nullptr : new juce::LocalisedStrings (table, false));
}
} // namespace i18n
} // namespace lynx
