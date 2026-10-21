#include "core/ModuleWarmup.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (ui/I18n.h defines the same)
#endif

namespace lynx::modulewarm
{
namespace
{
std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

std::string baseName (const std::string& file)
{
    auto s = file;
    while (! s.empty() && (s.back() == '/' || s.back() == '\\'))
        s.pop_back();
    const auto slash = s.find_last_of ("/\\");
    return slash == std::string::npos ? s : s.substr (slash + 1);
}

double steadySeconds()
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

bool worthStartingAhead (const std::string& formatName, const std::string& file)
{
    if (formatName != "VST3")
        return false;
    const auto name = lower (baseName (file));
    return name.rfind ("waveshell", 0) == 0 && name.size() > 5 && name.substr (name.size() - 5) == ".vst3";
}

std::string displayName (const std::string& file)
{
    auto name = baseName (file);
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos && dot > 0)
        name = name.substr (0, dot);
    for (const char* suffix : { "_x64", "_x86", "_arm64", "_arm64ec" })
        if (const auto n = std::string (suffix).size(); name.size() > n && lower (name.substr (name.size() - n)) == suffix)
        {
            name = name.substr (0, name.size() - n);
            break;
        }
    return name;
}

Registry::Registry (Loader l, std::function<double()> c, bool t)
    : load (std::move (l)), clock (c ? std::move (c) : std::function<double()> (steadySeconds)), threaded (t),
      entries (std::make_shared<std::map<std::string, Entry>>()), entriesLock (std::make_shared<std::mutex>())
{
}

Registry::~Registry() = default; // (a thread still loading keeps the map alive through its shared_ptr)

State Registry::start (const std::string& file)
{
    {
        const std::scoped_lock l (*entriesLock);
        auto& e = (*entries)[file];
        if (e.state == State::warming || e.state == State::ready || e.state == State::skipped)
            return e.state;
        e.state = State::warming;
        e.started = clock();
    }
    auto run = [loader = load, map = entries, mapLock = entriesLock, file] {
        bool ok = false;
        try
        {
            ok = loader (file);
        }
        catch (...)
        {
        }
        const std::scoped_lock l (*mapLock);
        auto& e = (*map)[file];
        // a skipped module stays skipped for this open; its result counts from the next one
        e.state = e.state == State::skipped ? (ok ? State::skipped : State::failed) : (ok ? State::ready : State::failed);
        if (e.state == State::skipped && ok)
            e.started = -1.0; // (marks "ready once the skip is forgotten")
    };
    if (threaded)
        std::thread (std::move (run)).detach();
    else
        run();
    return state (file);
}

State Registry::state (const std::string& file) const
{
    const std::scoped_lock l (*entriesLock);
    const auto it = entries->find (file);
    return it == entries->end() ? State::none : it->second.state;
}

Waiting Registry::waiting() const
{
    Waiting w;
    const auto now = clock();
    const std::scoped_lock l (*entriesLock);
    for (const auto& [file, e] : *entries)
        if (e.state == State::warming)
        {
            ++w.modules;
            if (const auto s = std::max (0.0, now - e.started); w.file.empty() || s > w.seconds)
            {
                w.file = file;
                w.seconds = s;
            }
        }
    return w;
}

void Registry::skipWaiting()
{
    const std::scoped_lock l (*entriesLock);
    for (auto& [file, e] : *entries)
        if (e.state == State::warming)
            e.state = State::skipped;
}

bool Registry::skipped (const std::string& file) const
{
    return state (file) == State::skipped;
}

void Registry::newOpen()
{
    const std::scoped_lock l (*entriesLock);
    for (auto it = entries->begin(); it != entries->end();)
    {
        auto& e = it->second;
        if (e.state == State::skipped && e.started < 0.0)
            e.state = State::ready; // finished after the skip
        else if (e.state == State::skipped)
            e.state = State::warming; // its thread is still running
        if (e.state == State::failed)
            it = entries->erase (it);
        else
            ++it;
    }
}

std::string waitTemplate (const Waiting& w)
{
    if (w.file.empty())
        return {};
    return w.seconds >= Registry::slowSeconds ? TR_NOOP ("{0} is slow to start ({1} s): its vendor's service is starting. You can wait or continue without it.")
                                              : TR_NOOP ("Starting {0}...");
}
} // namespace lynx::modulewarm
