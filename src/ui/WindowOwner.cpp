#include "ui/WindowOwner.h"

#include "ui/LogoData.h"
#include "ui/Theme.h"

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

void applyWindowChrome (juce::Component& window)
{
   #if JUCE_WINDOWS
    // dark native title bar (Windows 10 1809+), tinted with the LYNX background on Windows 11
    auto* hwnd = (HWND) window.getWindowHandle();
    if (hwnd == nullptr)
        return;

    using SetAttribute = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
    static const auto setAttribute = [] {
        auto* dwm = LoadLibraryW (L"dwmapi.dll");
        return dwm != nullptr ? (SetAttribute) (void*) GetProcAddress (dwm, "DwmSetWindowAttribute") : nullptr;
    }();

    if (setAttribute == nullptr)
        return;

    const BOOL dark = TRUE;
    if (FAILED (setAttribute (hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof (dark))))
        setAttribute (hwnd, 19 /* same attribute before Windows 10 20H1 */, &dark, sizeof (dark));

    auto colorRef = [] (juce::Colour c) { return (DWORD) (c.getRed() | (c.getGreen() << 8) | (c.getBlue() << 16)); };
    const auto caption = colorRef (theme::colour::bg1);
    const auto text = colorRef (theme::colour::textPrimary);
    const auto border = colorRef (theme::colour::border);
    setAttribute (hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof (caption));
    setAttribute (hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof (text));
    setAttribute (hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof (border));
   #else
    juce::ignoreUnused (window);
   #endif
}

void keepAboveMainWindow (juce::Component& window)
{
    applyWindowChrome (window);

    if (auto* top = dynamic_cast<juce::TopLevelWindow*> (&window))
        if (auto* peer = top->getPeer())
            peer->setIcon (logoImage());
   #if JUCE_WINDOWS
    auto* owner = mainWindow().getComponent();
    if (owner == nullptr || owner == &window || window.getWindowHandle() == nullptr || owner->getWindowHandle() == nullptr)
        return;
    // "owned" window: Windows keeps it above its owner and minimizes it along with it
    SetWindowLongPtrW ((HWND) window.getWindowHandle(), GWLP_HWNDPARENT, (LONG_PTR) owner->getWindowHandle());
   #else
    juce::ignoreUnused (window);
   #endif
}
} // namespace lynx
