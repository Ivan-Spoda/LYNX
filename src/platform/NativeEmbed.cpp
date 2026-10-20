#include "platform/NativeEmbed.h"

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

namespace lynx::platform
{
#if JUCE_WINDOWS
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component& o) : owner (o) { owner.addAndMakeVisible (holder); }
    ~Impl()
    {
        holder.setHWND (nullptr);
        if (container != nullptr)
            DestroyWindow ((HWND) container);
    }

    bool create()
    {
        if (container != nullptr)
            return true;
        // the child window needs a real window as parent: only once on screen
        auto* peer = owner.getPeer();
        if (peer == nullptr)
            return false;
        const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
        container = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                     juce::roundToInt (owner.getWidth() * scale), juce::roundToInt (owner.getHeight() * scale),
                                     (HWND) peer->getNativeHandle(), nullptr, nullptr, nullptr);
        holder.setHWND (container);
        holder.setBounds (owner.getLocalBounds());
        return container != nullptr;
    }

    void* handle() const noexcept { return container; }
    unsigned long x11() const noexcept { return 0; }
    void resized() { holder.setBounds (owner.getLocalBounds()); }

    juce::Component& owner;
    juce::HWNDComponent holder;
    void* container = nullptr;
};
#elif JUCE_MAC
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component& o) : owner (o) { owner.addAndMakeVisible (holder); }
    ~Impl() { holder.setView (nullptr); }

    bool create()
    {
        if (view != nullptr)
            return true;
        if (owner.getPeer() == nullptr)
            return false;
        // [[NSView alloc] init]; NSViewComponent retains it and keeps its frame on the component's bounds
        using Send = id (*) (id, SEL);
        auto* cls = (id) objc_getClass ("NSView");
        auto obj = ((Send) objc_msgSend) (((Send) objc_msgSend) (cls, sel_registerName ("alloc")), sel_registerName ("init"));
        if (obj == nullptr)
            return false;
        holder.setView (obj);
        ((void (*) (id, SEL)) objc_msgSend) (obj, sel_registerName ("release"));
        view = obj;
        holder.setBounds (owner.getLocalBounds());
        return true;
    }

    void* handle() const noexcept { return view; }
    unsigned long x11() const noexcept { return 0; }
    void resized() { holder.setBounds (owner.getLocalBounds()); }

    juce::Component& owner;
    juce::NSViewComponent holder;
    void* view = nullptr;
};
#elif JUCE_LINUX || JUCE_BSD
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component& o) : owner (o) { owner.addAndMakeVisible (holder); }
    ~Impl() { holder.removeClient(); }

    bool create()
    {
        if (window != 0)
            return true;
        if (owner.getPeer() == nullptr)
            return false;
        holder.setBounds (owner.getLocalBounds());
        window = holder.getHostWindowID();
        return window != 0;
    }

    void* handle() const noexcept { return (void*) (std::uintptr_t) window; }
    unsigned long x11() const noexcept { return window; }
    void resized()
    {
        holder.setBounds (owner.getLocalBounds());
        holder.updateEmbeddedBounds();
    }

    juce::Component& owner;
    juce::XEmbedComponent holder { true, false };
    unsigned long window = 0;
};
#else
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component&) {}
    bool create() { return false; }
    void* handle() const noexcept { return nullptr; }
    unsigned long x11() const noexcept { return 0; }
    void resized() {}
};
#endif

NativeEmbed::NativeEmbed() : impl (std::make_unique<Impl> (*this)) {}
NativeEmbed::~NativeEmbed() = default;

bool NativeEmbed::create() { return impl->create(); }
bool NativeEmbed::isCreated() const noexcept { return impl->handle() != nullptr; }
void* NativeEmbed::nativeHandle() const noexcept { return impl->handle(); }
unsigned long NativeEmbed::x11Window() const noexcept { return impl->x11(); }
void NativeEmbed::resized() { impl->resized(); }

const char* NativeEmbed::clapWindowApi() noexcept
{
   #if JUCE_WINDOWS
    return "win32";
   #elif JUCE_MAC
    return "cocoa";
   #else
    return "x11";
   #endif
}
} // namespace lynx::platform
