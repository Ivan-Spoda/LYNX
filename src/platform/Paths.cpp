#include "platform/Paths.h"

#include "core/FlpImport.h"

namespace lynx::platform
{
namespace
{
juce::File home() { return juce::File::getSpecialLocation (juce::File::userHomeDirectory); }

#if JUCE_LINUX || JUCE_BSD
// An XDG base directory: the variable when it holds an absolute path, else the default under the home folder.
juce::File xdgFolder (const char* variable, const char* fallback)
{
    const auto value = juce::SystemStats::getEnvironmentVariable (variable, {});
    if (value.isNotEmpty() && juce::File::isAbsolutePath (value))
        return juce::File (value);
    return home().getChildFile (fallback);
}
#endif
} // namespace

juce::File settingsFolder()
{
   #if JUCE_WINDOWS
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX");
   #elif JUCE_MAC
    auto folder = home().getChildFile ("Library/Application Support/LYNX");
   #else
    auto folder = xdgFolder ("XDG_CONFIG_HOME", ".config").getChildFile ("LYNX");
   #endif
    folder.createDirectory();
    return folder;
}

juce::File settingsFile (const juce::String& relativePath)
{
    return settingsFolder().getChildFile (relativePath);
}

juce::File cacheFolder (const juce::String& name)
{
   #if JUCE_WINDOWS
    return settingsFolder().getChildFile (name);
   #elif JUCE_MAC
    return home().getChildFile ("Library/Caches/LYNX").getChildFile (name);
   #else
    return xdgFolder ("XDG_CACHE_HOME", ".cache").getChildFile ("LYNX").getChildFile (name);
   #endif
}

juce::FileSearchPath standardPluginFolders()
{
    juce::FileSearchPath paths;
   #if JUCE_WINDOWS
    // the locations Steinberg / the CLAP spec / common installers use
    const auto programs = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    paths.add (programs.getChildFile ("Common Files/VST3"));
    for (const auto* sub : { "VSTPlugins", "Steinberg/VSTPlugins", "Common Files/VST2", "Common Files/Steinberg/VST2" })
        paths.add (programs.getChildFile (sub));
    paths.add (programs.getChildFile ("Common Files/CLAP"));
    paths.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs/Common/CLAP"));
   #elif JUCE_MAC
    // user folders first, then the system-wide ones
    for (const auto& root : { home().getChildFile ("Library/Audio/Plug-Ins"), juce::File ("/Library/Audio/Plug-Ins") })
        for (const auto* sub : { "VST3", "Components", "CLAP", "VST" })
            paths.add (root.getChildFile (sub));
   #else
    // VST3 and CLAP specs' Linux folders, LV2's usual ones and the de-facto VST2 ones; the lib64 variants are where
    // Fedora, openSUSE and other 64-bit-libdir distributions install them
    for (const auto* format : { "vst3", "clap", "lv2", "vst" })
    {
        paths.add (home().getChildFile (juce::String (".") + format));
        for (const auto* root : { "/usr/lib", "/usr/local/lib" })
            paths.add (juce::File (root).getChildFile (format));
        // only where they exist and are not a link to /usr/lib (Arch), which would list every plugin twice
        for (const auto* root : { "/usr/lib64", "/usr/local/lib64" })
            if (const juce::File lib (root); lib.isDirectory() && ! lib.isSymbolicLink() && lib.getChildFile (format).isDirectory()
                && ! lib.getChildFile (format).isSymbolicLink())
                paths.add (lib.getChildFile (format));
    }
   #endif
    paths.add (settingsFolder().getChildFile ("Plugins"));
    return paths;
}

juce::String pluginFormatNames()
{
   #if JUCE_WINDOWS
    return "VST, VST3, CLAP";
   #elif JUCE_MAC
    return "VST3, AU, CLAP";
   #else
    return "VST, VST3, CLAP, LV2";
   #endif
}

juce::File localFile (const juce::String& path)
{
   #if JUCE_WINDOWS
    return juce::File (path);
   #else
    if (! path.containsChar ('\\') && juce::File::isAbsolutePath (path) && juce::File (path).exists())
        return juce::File (path);
    const auto posix = path.replaceCharacter ('\\', '/');
    if (! juce::File::isAbsolutePath (posix))
        return {};
    return juce::File (juce::String::fromUTF8 (existingPathIgnoringCase (posix.toStdString()).c_str()));
   #endif
}

juce::File localFile (const std::string& utf8Path)
{
    return localFile (juce::String::fromUTF8 (utf8Path.c_str()));
}
} // namespace lynx::platform
