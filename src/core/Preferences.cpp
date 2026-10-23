#include "core/Preferences.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace lynx::prefs
{
namespace
{
std::string trim (const std::string& s)
{
    auto a = s.find_first_not_of (" \t\r\n");
    if (a == std::string::npos)
        return {};
    auto b = s.find_last_not_of (" \t\r\n");
    return s.substr (a, b - a + 1);
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

int toInt (const std::string& s, int fallback)
{
    try
    {
        std::size_t used = 0;
        const auto v = std::stoi (s, &used);
        return used > 0 ? v : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}
} // namespace

std::map<std::string, std::string> parseKeyValues (const std::string& text)
{
    std::map<std::string, std::string> out;
    std::istringstream in (text);
    std::string line;

    while (std::getline (in, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = trim (line.substr (0, eq));
        if (! key.empty())
            out[key] = trim (line.substr (eq + 1));
    }

    return out;
}

// --- autosave ---------------------------------------------------------------------------------------------------

const std::vector<int>& autosaveIntervals()
{
    static const std::vector<int> list { 0, 1, 2, 5, 10, 15, 30 };
    return list;
}

int clampAutosaveMinutes (int minutes)
{
    if (minutes <= 0)
        return 0;
    auto best = autosaveIntervals()[1];
    for (auto m : autosaveIntervals())
        if (m > 0 && std::abs (m - minutes) < std::abs (best - minutes))
            best = m;
    return best;
}

int clampAutosaveKeep (int keep) { return std::clamp (keep, 1, 100); }

std::string backupName (const std::string& stem, int year, int month, int day, int hour, int minute, int second)
{
    char stamp[48];
    std::snprintf (stamp, sizeof (stamp), " (%04d-%02d-%02d %02d.%02d.%02d)", year, month, day, hour, minute, second);
    return stem + stamp;
}

bool isBackupOf (const std::string& name, const std::string& stem)
{
    if (name == stem)
        return true;
    // stem + " (YYYY-MM-DD HH.MM.SS)"
    constexpr std::size_t stampLength = 22;
    if (name.size() != stem.size() + stampLength || name.compare (0, stem.size(), stem) != 0)
        return false;
    const auto stamp = name.substr (stem.size());
    const char* pattern = " (dddd-dd-dd dd.dd.dd)";
    for (std::size_t i = 0; i < stampLength; ++i)
        if (pattern[i] == 'd' ? ! std::isdigit ((unsigned char) stamp[i]) : stamp[i] != pattern[i])
            return false;
    return true;
}

std::vector<std::string> backupsToRemove (const std::vector<std::string>& names, const std::string& stem, int keep)
{
    std::vector<std::string> mine;
    for (const auto& n : names)
        if (isBackupOf (n, stem))
            mine.push_back (n);

    // the plain "stem" (an older version's single backup) counts as the oldest
    std::sort (mine.begin(), mine.end(), [&stem] (const std::string& a, const std::string& b) {
        if ((a == stem) != (b == stem))
            return a == stem;
        return a < b;
    });

    const auto k = (std::size_t) clampAutosaveKeep (keep);
    if (mine.size() <= k)
        return {};
    mine.resize (mine.size() - k);
    return mine;
}

int clampUndoLevels (int levels) { return std::clamp (levels, 10, 1000); }

double bufferLatencyMs (int bufferSamples, double sampleRate)
{
    return sampleRate > 0.0 ? 1000.0 * bufferSamples / sampleRate : 0.0;
}

DriverKind driverKind (const std::string& typeName)
{
    if (typeName == "Windows Audio")
        return DriverKind::wasapiShared;
    if (typeName == "Windows Audio (Low Latency Mode)")
        return DriverKind::wasapiLowLatency;
    if (typeName == "Windows Audio (Exclusive Mode)")
        return DriverKind::wasapiExclusive;
    if (typeName == "DirectSound")
        return DriverKind::directSound;
    if (typeName == "ASIO")
        return DriverKind::asio;
    if (typeName == "LYNX ASIO")
        return DriverKind::lynxAsio;
    if (typeName == "CoreAudio")
        return DriverKind::coreAudio;
    if (typeName == "ALSA")
        return DriverKind::alsa;
    if (typeName == "JACK")
        return DriverKind::jack;
    return DriverKind::other;
}

std::vector<int> usableBufferSizes (DriverKind kind, const std::vector<int>& offered, int devicePeriod)
{
    if (kind != DriverKind::wasapiShared || devicePeriod <= 0)
        return offered;
    std::vector<int> kept;
    for (const auto size : offered)
        if (size >= devicePeriod)
            kept.push_back (size);
    return kept.empty() ? offered : kept;
}

int safeBufferSize (const std::vector<int>& available, int current, double sampleRate)
{
    const int target = sampleRate > 50000.0 ? (sampleRate > 100000.0 ? 2048 : 1024) : 512;
    if (current >= target)
        return current;
    int best = 0, largest = 0;
    for (const auto b : available)
    {
        largest = std::max (largest, b);
        if (b >= target && (best == 0 || b < best))
            best = b;
    }
    if (best == 0)
        best = std::max (largest, current);
    return best;
}

// --- MIDI -------------------------------------------------------------------------------------------------------

float mapVelocity (float velocity, VelocityCurve curve) noexcept
{
    const auto v = std::clamp (velocity, 0.0f, 1.0f);
    if (v <= 0.0f)
        return 0.0f;
    switch (curve)
    {
        case VelocityCurve::soft: return std::pow (v, 0.6f);
        case VelocityCurve::hard: return std::pow (v, 1.7f);
        case VelocityCurve::fixed: return 1.0f;
        case VelocityCurve::linear: break;
    }
    return v;
}

MidiInputPlan planMidiInputs (const MidiPrefs& prefs, const std::vector<MidiPort>& available,
                              const std::vector<std::string>& previous, const std::vector<std::string>& openIds)
{
    auto contains = [] (const std::vector<std::string>& list, const std::string& id) {
        return std::find (list.begin(), list.end(), id) != list.end();
    };

    MidiInputPlan plan;
    for (const auto& port : available)
    {
        const auto wanted = prefs.input (port.name).enabled;
        const auto isOpen = contains (openIds, port.id);

        if (wanted && ! isOpen)
            plan.open.push_back (port.id);
        else if (wanted && ! contains (previous, port.id))
            plan.reopen.push_back (port.id);
        else if (! wanted && isOpen)
            plan.close.push_back (port.id);
    }
    return plan;
}

MidiInputPrefs MidiPrefs::input (const std::string& name) const
{
    for (const auto& i : inputs)
        if (i.name == name)
            return i;
    MidiInputPrefs d;
    d.name = name;
    return d;
}

void MidiPrefs::setInput (const MidiInputPrefs& in)
{
    MidiInputPrefs defaults;
    defaults.name = in.name;
    inputs.erase (std::remove_if (inputs.begin(), inputs.end(), [&] (const MidiInputPrefs& i) { return i.name == in.name; }), inputs.end());
    if (! (in == defaults)) // only the changed devices are kept
        inputs.push_back (in);
}

bool MidiPrefs::accepts (const std::string& device, int channel) const
{
    const auto i = input (device);
    return i.enabled && (i.midiChannel == 0 || i.midiChannel == channel);
}

namespace
{
const char* curveName (VelocityCurve c)
{
    switch (c)
    {
        case VelocityCurve::soft: return "soft";
        case VelocityCurve::hard: return "hard";
        case VelocityCurve::fixed: return "fixed";
        case VelocityCurve::linear: break;
    }
    return "linear";
}

// device names may hold '=' or '|'; they are written last on the line
std::vector<std::string> split (const std::string& s, char sep, std::size_t maxParts)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (parts.size() + 1 < maxParts)
    {
        const auto p = s.find (sep, start);
        if (p == std::string::npos)
            break;
        parts.push_back (s.substr (start, p - start));
        start = p + 1;
    }
    parts.push_back (s.substr (start));
    return parts;
}
} // namespace

std::string MidiPrefs::toText() const
{
    std::string out = "velocity=" + std::string (curveName (velocity)) + "\npickup=" + (pickup ? "1" : "0") + "\n";
    // input=<enabled>|<midi channel>|<target>|<device name>
    for (const auto& i : inputs)
        out += "input=" + std::string (i.enabled ? "1" : "0") + "|" + std::to_string (i.midiChannel) + "|" + std::to_string (i.target) + "|" + i.name + "\n";
    return out;
}

MidiPrefs MidiPrefs::fromText (const std::string& text)
{
    MidiPrefs p;
    std::istringstream in (text);
    std::string line;

    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = trim (line.substr (0, eq));
        const auto value = line.substr (eq + 1);

        if (key == "velocity")
        {
            const auto v = trim (value);
            p.velocity = v == "soft" ? VelocityCurve::soft : v == "hard" ? VelocityCurve::hard : v == "fixed" ? VelocityCurve::fixed : VelocityCurve::linear;
        }
        else if (key == "pickup")
            p.pickup = trim (value) == "1";
        else if (key == "input")
        {
            const auto parts = split (value, '|', 4);
            if (parts.size() != 4 || parts[3].empty())
                continue;
            MidiInputPrefs i;
            i.enabled = trim (parts[0]) != "0";
            i.midiChannel = std::clamp (toInt (trim (parts[1]), 0), 0, 16);
            i.target = std::max (-1, toInt (trim (parts[2]), -1));
            i.name = parts[3];
            p.setInput (i);
        }
    }

    return p;
}

bool pickupReached (float parameter, float previous, float incoming, float tolerance) noexcept
{
    if (std::abs (incoming - parameter) <= tolerance)
        return true;
    if (previous < 0.0f)
        return false;
    // crossed it between the last value and this one
    return (previous - parameter) * (incoming - parameter) < 0.0f;
}

bool pickupUpdate (PickupState& state, float parameter, float incoming, float tolerance) noexcept
{
    if (state.caught && std::abs (parameter - state.lastSet) > tolerance)
        state.caught = false; // moved by the mouse, automation or an undo
    if (! state.caught)
        state.caught = pickupReached (parameter, state.lastController, incoming, tolerance);
    state.lastController = incoming;
    if (state.caught)
        state.lastSet = incoming;
    return state.caught;
}

// --- plugin manager ---------------------------------------------------------------------------------------------

std::vector<int> visiblePlugins (const std::vector<PluginRow>& rows, const std::string& query, PluginFilter filter, PluginSort sort, bool descending)
{
    std::vector<std::string> words;
    {
        std::istringstream in (lower (query));
        std::string w;
        while (in >> w)
            words.push_back (w);
    }

    std::vector<int> out;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& r = rows[(std::size_t) i];
        switch (filter)
        {
            case PluginFilter::instruments: if (! r.instrument || r.status == PluginStatus::failed) continue; break;
            case PluginFilter::effects: if (r.instrument || r.status == PluginStatus::failed) continue; break;
            case PluginFilter::favourites: if (! r.favourite) continue; break;
            case PluginFilter::failed: if (r.status == PluginStatus::ok) continue; break;
            case PluginFilter::all: break;
        }

        const auto hay = lower (r.name + "\n" + r.vendor + "\n" + r.format + "\n" + r.category + "\n" + r.file);
        if (std::all_of (words.begin(), words.end(), [&hay] (const std::string& w) { return hay.find (w) != std::string::npos; }))
            out.push_back (i);
    }

    auto key = [&rows, sort] (int i) -> std::string {
        const auto& r = rows[(std::size_t) i];
        switch (sort)
        {
            case PluginSort::vendor: return lower (r.vendor);
            case PluginSort::format: return lower (r.format);
            case PluginSort::type: return r.instrument ? "0" : "1";
            case PluginSort::status: return std::to_string ((int) r.status);
            case PluginSort::name: break;
        }
        return lower (r.name);
    };

    std::stable_sort (out.begin(), out.end(), [&] (int a, int b) {
        const auto ka = key (a), kb = key (b);
        if (ka != kb)
            return descending ? ka > kb : ka < kb;
        return lower (rows[(std::size_t) a].name) < lower (rows[(std::size_t) b].name);
    });
    return out;
}

std::set<std::string> parseFavourites (const std::string& text)
{
    std::set<std::string> out;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
        if (const auto t = trim (line); ! t.empty())
            out.insert (t);
    return out;
}

std::string formatFavourites (const std::set<std::string>& ids)
{
    std::string out;
    for (const auto& id : ids)
        out += id + "\n";
    return out;
}
} // namespace lynx::prefs
