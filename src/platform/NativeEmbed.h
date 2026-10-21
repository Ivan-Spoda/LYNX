#pragma once

// A native child view for a plugin's own editor (CLAP, VST2): the one place that knows what a "parent window" is on
// each OS (docs/PLATFORMS.md).
//   Windows  a child HWND (STATIC) inside the JUCE window, shown through juce::HWNDComponent
//   macOS    a plain NSView, shown through juce::NSViewComponent
//   Linux    an X11 window created by juce::XEmbedComponent (the plugin reparents its own window into it)

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace lynx::platform
{
class NativeEmbed final : public juce::Component
{
public:
    NativeEmbed();
    ~NativeEmbed() override;

    // Creates the native view once the component is inside a window on screen; false until then (call again from a
    // timer). Safe to call again after it succeeded.
    bool create();
    bool isCreated() const noexcept;

    // The native parent for the plugin: HWND (Windows), NSView* (macOS), X11 Window id (Linux).
    void* nativeHandle() const noexcept;
    unsigned long x11Window() const noexcept;

    // The CLAP window API this platform uses ("win32", "cocoa", "x11"; matches CLAP_WINDOW_API_*).
    static const char* clapWindowApi() noexcept;

    void resized() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lynx::platform
