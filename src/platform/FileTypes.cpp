#include "platform/FileTypes.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shlobj.h>
#endif

namespace lynx
{
void registerFileTypes()
{
   #if JUCE_WINDOWS
    using juce::WindowsRegistry;
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    const juce::String classes = "HKEY_CURRENT_USER\\Software\\Classes\\";
    const juce::String progId = "LYNX.Project";
    const auto command = "\"" + exe + "\" \"%1\"";
    const auto icon = "\"" + exe + "\",-2"; // resource id 2 in resources/LYNX.rc: the project-file icon

    if (WindowsRegistry::getValue (classes + ".lynx\\") == progId
        && WindowsRegistry::getValue (classes + progId + "\\shell\\open\\command\\") == command
        && WindowsRegistry::getValue (classes + progId + "\\DefaultIcon\\") == icon)
        return;

    WindowsRegistry::setValue (classes + ".lynx\\", progId);
    WindowsRegistry::setValue (classes + ".lynx\\Content Type", juce::String ("application/x-lynx-project"));
    WindowsRegistry::setValue (classes + progId + "\\", juce::String ("LYNX Project"));
    WindowsRegistry::setValue (classes + progId + "\\DefaultIcon\\", icon);
    WindowsRegistry::setValue (classes + progId + "\\shell\\open\\command\\", command);
    // Explorer caches icons per type: tell it the association changed
    SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
   #endif
}
} // namespace lynx
