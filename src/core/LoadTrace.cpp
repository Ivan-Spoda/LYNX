#include "core/LoadTrace.h"

#include <algorithm>
#include <cstdio>

namespace lynx::loadtrace
{
namespace
{
struct State
{
    std::mutex lock;
    std::string name;
    bool running = false;
    std::chrono::steady_clock::time_point started {};
    double finalMs = -1.0;
    std::vector<Phase> phases;
    double progress = 0.0, stageFrom = 0.0, stageTo = 1.0;
    Status status;
};

State& state()
{
    static State s;
    return s;
}

std::string formatMs (double ms)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%.0f", ms);
    return buffer;
}

void raise (State& s, double p)
{
    s.progress = std::max (s.progress, std::clamp (p, 0.0, 1.0));
}
} // namespace

void begin (const std::string& what)
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    s.name = what;
    s.running = true;
    s.started = std::chrono::steady_clock::now();
    s.finalMs = -1.0;
    s.phases.clear();
    s.progress = 0.0;
    s.stageFrom = 0.0;
    s.stageTo = 1.0;
    s.status = {};
}

void end()
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    if (! s.running)
        return;
    s.running = false;
    s.finalMs = msSince (s.started);
    s.progress = 1.0;
}

bool active()
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    return s.running;
}

void add (const std::string& phase, double ms, int count)
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    auto it = std::find_if (s.phases.begin(), s.phases.end(), [&] (const Phase& p) { return p.name == phase; });
    if (it == s.phases.end())
        it = s.phases.insert (s.phases.end(), Phase { phase, 0.0, 0 });
    it->ms += ms;
    it->count += count;
}

Scope::Scope (std::string phaseName) : phase (std::move (phaseName)), started (std::chrono::steady_clock::now()) {}

Scope::~Scope()
{
    add (phase, msSince (started));
}

double msSince (std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
}

void stage (double from, double to, const std::string& text, const std::string& itemTemplate)
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    s.stageFrom = std::clamp (from, 0.0, 1.0);
    s.stageTo = std::clamp (std::max (from, to), 0.0, 1.0);
    raise (s, s.stageFrom);
    s.status = { text, itemTemplate, {}, 0, 0 };
}

void step (int index, int count, const std::string& item, int shown)
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    const auto f = count > 0 ? std::clamp ((double) index / (double) count, 0.0, 1.0) : 0.0;
    raise (s, s.stageFrom + (s.stageTo - s.stageFrom) * f);
    if (! item.empty())
    {
        s.status.item = item;
        s.status.index = shown >= 0 ? shown : index;
        s.status.count = count;
    }
}

double progress()
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    return s.progress;
}

Status status()
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    return s.status;
}

std::string fill (const std::string& pattern, const std::string& item, int index, int count)
{
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && pattern[i + 1] >= '0' && pattern[i + 1] <= '2')
        {
            const auto k = pattern[i + 1] - '0';
            out += k == 0 ? item : std::to_string (k == 1 ? index : count);
            i += 2;
            continue;
        }
        out += pattern[i];
    }
    return out;
}

std::string describe (const Status& st, const std::function<std::string (const std::string&)>& tr)
{
    auto translate = [&tr] (const std::string& t) { return tr ? tr (t) : t; };
    if (st.item.empty() || st.itemTemplate.empty())
        return translate (st.text);
    return fill (translate (st.itemTemplate), st.item, st.index, st.count);
}

std::vector<Phase> phases()
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    return s.phases;
}

double totalMs()
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    if (s.finalMs >= 0.0)
        return s.finalMs;
    return s.running ? msSince (s.started) : 0.0;
}

std::string name()
{
    auto& s = state();
    const std::scoped_lock l (s.lock);
    return s.name;
}

std::string reportLines()
{
    std::string out = "load_total_ms=" + formatMs (totalMs()) + "\n";
    for (const auto& p : phases())
        out += "load_" + p.name + "_ms=" + formatMs (p.ms) + "\nload_" + p.name + "_count=" + std::to_string (p.count) + "\n";
    return out;
}

std::string table()
{
    auto list = phases();
    std::stable_sort (list.begin(), list.end(), [] (const Phase& a, const Phase& b) { return a.ms > b.ms; });
    std::string out = "open: " + name() + "\ntotal: " + formatMs (totalMs()) + " ms\n";
    for (const auto& p : list)
    {
        char line[160];
        std::snprintf (line, sizeof (line), "  %-28s %9.0f ms  x%d\n", p.name.c_str(), p.ms, p.count);
        out += line;
    }
    return out;
}

std::string pathRoot (const std::string& path)
{
    std::string p = path;
    std::replace (p.begin(), p.end(), '/', '\\');
    // drive: "K:\..." (letters only; "K:" alone counts as that drive)
    if (p.size() >= 2 && p[1] == ':' && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')))
    {
        auto drive = p.substr (0, 2);
        if (drive[0] >= 'a' && drive[0] <= 'z')
            drive[0] = (char) (drive[0] - 'a' + 'A');
        return drive + "\\";
    }
    // network: "\\server\share\..."
    if (p.size() > 2 && p[0] == '\\' && p[1] == '\\' && p[2] != '?' && p[2] != '.')
    {
        const auto server = p.find ('\\', 2);
        if (server == std::string::npos || server == 2)
            return {};
        const auto share = p.find ('\\', server + 1);
        auto root = share == std::string::npos ? p + "\\" : p.substr (0, share + 1);
        if (root.size() <= server + 2) // "\\server\" with no share
            return {};
        std::transform (root.begin(), root.end(), root.begin(), [] (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c; });
        return root;
    }
    // POSIX absolute paths (tests, Linux tools): the first folder
    if (! p.empty() && path[0] == '/')
    {
        const auto second = path.find ('/', 1);
        return second == std::string::npos ? std::string ("/") : path.substr (0, second + 1);
    }
    return {};
}

RootProbe::RootProbe (std::function<bool (const std::string&)> rootExists) : exists (std::move (rootExists)) {}

bool RootProbe::rootAvailable (const std::string& path)
{
    const auto root = pathRoot (path);
    if (root.empty() || ! exists)
        return true;
    {
        const std::scoped_lock l (lock);
        if (const auto it = roots.find (root); it != roots.end())
        {
            if (! it->second)
                ++skippedCount;
            return it->second;
        }
    }
    const auto ok = exists (root); // outside the lock: it may take a while
    const std::scoped_lock l (lock);
    if (const auto [it, inserted] = roots.emplace (root, ok); inserted)
        ++probeCount;
    else if (! it->second)
        ++skippedCount;
    return roots[root];
}

std::vector<std::string> RootProbe::missingRoots() const
{
    const std::scoped_lock l (lock);
    std::vector<std::string> out;
    for (const auto& [root, ok] : roots)
        if (! ok)
            out.push_back (root);
    return out;
}
} // namespace lynx::loadtrace
