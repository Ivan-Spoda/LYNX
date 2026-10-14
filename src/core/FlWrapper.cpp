#include "core/FlWrapper.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace lynx
{
namespace
{
struct Reader
{
    const std::vector<std::uint8_t>& data;
    std::size_t pos = 0;

    std::size_t remaining() const noexcept { return data.size() - pos; }

    template <typename T>
    T read() noexcept
    {
        T v {};
        std::memcpy (&v, data.data() + pos, sizeof (T)); // little-endian, like the x86 that wrote it
        pos += sizeof (T);
        return v;
    }
};

std::uint32_t u32At (const std::vector<std::uint8_t>& d, std::size_t offset)
{
    std::uint32_t v = 0;
    if (offset + 4 <= d.size())
        std::memcpy (&v, d.data() + offset, 4);
    return v;
}

// unterminated text: UTF-8 from version 9 on; before that, ANSI (Windows-1252 ~ Latin-1 in ASCII)
std::string text (const std::uint8_t* p, std::size_t n, bool utf8)
{
    std::string s;

    for (std::size_t i = 0; i < n && p[i] != 0; ++i)
    {
        const auto c = p[i];

        if (utf8 || c < 0x80)
            s += (char) c;
        else // Latin-1 -> UTF-8 (approximates 1252 outside 0x80..0x9F)
        {
            s += (char) (0xC0 | (c >> 6));
            s += (char) (0x80 | (c & 0x3F));
        }
    }

    return s;
}

// walks {u32 id, u64 size, data}; stops at a truncated record
template <typename Fn>
void forEachRecord (Reader& r, Fn&& fn)
{
    while (r.remaining() >= 12)
    {
        const auto id = r.read<std::uint32_t>();
        const auto size = r.read<std::uint64_t>();

        if (size > r.remaining())
            return;

        fn (id, r.data.data() + r.pos, (std::size_t) size);
        r.pos += (std::size_t) size;
    }
}
} // namespace

bool parseFlWrapper (const std::vector<std::uint8_t>& blob, ForeignPlugin& out)
{
    if (blob.size() < 4)
        return false;

    Reader r { blob };
    const auto version = r.read<std::uint32_t>();

    if (version < 5 || version > 12)
        return false;

    const auto utf8 = version >= 9;
    std::uint32_t type = 0xFFFFFFFFu;
    out = {};

    forEachRecord (r, [&] (std::uint32_t id, const std::uint8_t* p, std::size_t n) {
        switch (id)
        {
            case 0x32: if (n >= 4) std::memcpy (&type, p, 4); break;
            case 0x33: if (n >= 4) std::memcpy (&out.vst2Id, p, 4); break;
            case 0x34:
                if (n >= 16)
                {
                    std::copy (p, p + 16, out.classId.begin());
                    out.hasClassId = std::any_of (out.classId.begin(), out.classId.end(), [] (auto b) { return b != 0; });
                }
                break;
            case 0x3A: out.clapId = text (p, n, utf8); break;
            case 0x36: out.name = text (p, n, utf8); break;
            case 0x37: out.path = text (p, n, utf8); break;
            case 0x35: out.state.assign (p, p + n); break;
            case 0x01: // MIDI input and output ports (REA_rodada16 §1.2)
                if (n >= 8)
                {
                    std::memcpy (&out.midiInPort, p, 4);
                    std::memcpy (&out.midiOutPort, p + 4, 4);
                }
                break;
            default: break; // wrapper options, pins: LYNX does not need them
        }
    });

    switch (type)
    {
        case 0:
        case 4: out.format = ForeignPlugin::Format::vst2; break;
        case 7:
        case 8: out.format = ForeignPlugin::Format::vst3; break;
        case 11:
        case 12: out.format = ForeignPlugin::Format::clap; break;
        case 0xFFFFFFFFu: return false; // without the type record the plugin cannot be found
        default: out.format = ForeignPlugin::Format::other; break; // DirectX, AU, native FL
    }

    return true;
}

bool splitFlVst3State (const std::vector<std::uint8_t>& state, std::vector<std::uint8_t>& component,
                       std::vector<std::uint8_t>& controller)
{
    component.clear();
    controller.clear();

    if (state.size() < 4 || u32At (state, 0) > 1)
        return false;

    Reader r { state, 4 };
    forEachRecord (r, [&] (std::uint32_t id, const std::uint8_t* p, std::size_t n) {
        if (id == 3)
            component.assign (p, p + n);
        else if (id == 2)
            controller.assign (p, p + n);
    });

    return ! component.empty() || ! controller.empty();
}

std::vector<std::int32_t> flVst3ParamSlots (const std::vector<std::uint8_t>& state)
{
    std::vector<std::int32_t> slots;

    if (state.size() < 4 || u32At (state, 0) > 1)
        return slots;

    Reader r { state, 4 };
    forEachRecord (r, [&] (std::uint32_t id, const std::uint8_t* p, std::size_t n) {
        if (id != 4 || n < 4)
            return;
        std::uint32_t count = 0;
        std::memcpy (&count, p, 4);
        if (count > (n - 4) / 4)
            return;
        slots.resize (count);
        std::memcpy (slots.data(), p + 4, count * 4);
    });

    return slots;
}

std::vector<int> flVst3SlotPositions (const std::vector<std::int32_t>& savedSlots, const std::vector<std::uint32_t>& paramIds)
{
    std::vector<int> positions (savedSlots.size(), -1);
    std::vector<bool> hasSlot (paramIds.size(), false);

    for (std::size_t slot = 0; slot < savedSlots.size(); ++slot)
    {
        if (savedSlots[slot] == -1)
            continue;
        for (std::size_t i = 0; i < paramIds.size(); ++i)
            if (paramIds[i] == (std::uint32_t) savedSlots[slot] && ! hasSlot[i])
            {
                positions[slot] = (int) i;
                hasSlot[i] = true;
                break;
            }
    }

    for (std::size_t i = 0; i < paramIds.size(); ++i)
        if (! hasSlot[i])
            positions.push_back ((int) i);

    return positions;
}
} // namespace lynx
