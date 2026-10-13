#include "ui/WindowOwner.h"

#include "ui/LogoData.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx
{
namespace
{
juce::Component::SafePointer<juce::Component>& mainWindow()
{
    static juce::Component::SafePointer<juce::Component> window;
    return window;
}
} // namespace

void setMainWindowForOwnership (juce::Component* window)
{
    mainWindow() = window;
}

juce::Image logoImage()
{
    return juce::ImageCache::getFromMemory (logo::png256, (int) sizeof (logo::png256));
}

void keepAboveMainWindow (juce::Component& window)
{
    if (auto* top = dynamic_cast<juce::TopLevelWindow*> (&window))
        if (auto* peer = top->getPeer())
            peer->setIcon (logoImage());
   #if JUCE_WINDOWS
    auto* owner = mainWindow().getComponent();
    if (owner == nullptr || owner == &window || window.getWindowHandle() == nullptr || owner->getWindowHandle() == nullptr)
        return;
    // janela "possuída": o Windows a mantém acima da dona e a minimiza junto
    SetWindowLongPtrW ((HWND) window.getWindowHandle(), GWLP_HWNDPARENT, (LONG_PTR) owner->getWindowHandle());
   #else
    juce::ignoreUnused (window);
   #endif
}
} // namespace lynx
