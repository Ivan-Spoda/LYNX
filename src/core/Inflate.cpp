#include "core/Inflate.h"

#include <algorithm>
#include <array>

namespace lynx
{
namespace
{
struct BitReader
{
    const std::uint8_t* data;
    std::size_t size, pos = 0;
    std::uint32_t bitBuf = 0;
    int bitCount = 0;
    bool overrun = false;

    int bit()
    {
        if (bitCount == 0)
        {
            if (pos >= size)
            {
                overrun = true;
                return 0;
            }
            bitBuf = data[pos++];
            bitCount = 8;
        }
        const auto b = (int) (bitBuf & 1u);
        bitBuf >>= 1;
        --bitCount;
        return b;
    }

    int bits (int n) // n <= 16, LSB first
    {
        int v = 0;
        for (int i = 0; i < n; ++i)
            v |= bit() << i;
        return v;
    }
};

// canonical Huffman: count per length + symbols in order
struct Huffman
{
    std::array<std::uint16_t, 16> count {};
    std::array<std::uint16_t, 320> symbol {};

    bool build (const std::uint8_t* lengths, int n)
    {
        count.fill (0);
        for (int i = 0; i < n; ++i)
            ++count[lengths[i]];
        count[0] = 0;

        int left = 1;
        for (int len = 1; len < 16; ++len)
        {
            left = (left << 1) - count[(std::size_t) len];
            if (left < 0)
                return false; // too many codes
        }

        std::array<std::uint16_t, 16> offs {};
        for (int len = 1; len < 15; ++len)
            offs[(std::size_t) len + 1] = (std::uint16_t) (offs[(std::size_t) len] + count[(std::size_t) len]);
        for (int i = 0; i < n; ++i)
            if (lengths[i] != 0)
                symbol[offs[lengths[i]]++] = (std::uint16_t) i;
        return true;
    }

    int decode (BitReader& in) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len)
        {
            code |= in.bit();
            const int c = count[(std::size_t) len];
            if (code - c < first)
                return symbol[(std::size_t) (index + (code - first))];
            index += c;
            first = (first + c) << 1;
            code <<= 1;
            if (in.overrun)
                return -1;
        }
        return -1;
    }
};

constexpr std::uint16_t lengthBase[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
constexpr std::uint8_t lengthExtra[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
constexpr std::uint16_t distBase[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
constexpr std::uint8_t distExtra[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool inflateBlock (BitReader& in, std::vector<std::uint8_t>& out, const Huffman& lit, const Huffman& dist, std::size_t maxSize)
{
    for (;;)
    {
        const auto sym = lit.decode (in);
        if (sym < 0 || in.overrun)
            return false;
        if (sym < 256)
        {
            if (out.size() >= maxSize)
                return false;
            out.push_back ((std::uint8_t) sym);
            continue;
        }
        if (sym == 256)
            return true;

        const auto li = sym - 257;
        if (li >= 29)
            return false;
        const auto length = (std::size_t) (lengthBase[li] + in.bits (lengthExtra[li]));
        const auto di = dist.decode (in);
        if (di < 0 || di >= 30)
            return false;
        const auto distance = (std::size_t) (distBase[di] + in.bits (distExtra[di]));
        if (distance > out.size() || out.size() + length > maxSize || in.overrun)
            return false;
        const auto from = out.size() - distance;
        for (std::size_t i = 0; i < length; ++i)
            out.push_back (out[from + i]);
    }
}
} // namespace

std::optional<std::vector<std::uint8_t>> inflateZlib (const std::uint8_t* data, std::size_t size, bool raw, std::size_t maxSize)
{
    if (! raw)
    {
        if (size < 2 || (data[0] & 0x0f) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20) != 0)
            return std::nullopt;
        data += 2;
        size -= 2;
    }

    BitReader in { data, size };
    std::vector<std::uint8_t> out;

    for (bool last = false; ! last;)
    {
        last = in.bit() != 0;
        const auto type = in.bits (2);

        if (type == 0) // uncompressed
        {
            in.bitCount = 0;
            if (in.pos + 4 > size)
                return std::nullopt;
            const auto len = (std::size_t) (data[in.pos] | (data[in.pos + 1] << 8));
            const auto nlen = (std::size_t) (data[in.pos + 2] | (data[in.pos + 3] << 8));
            in.pos += 4;
            if ((len ^ 0xffffu) != nlen || in.pos + len > size || out.size() + len > maxSize)
                return std::nullopt;
            out.insert (out.end(), data + in.pos, data + in.pos + len);
            in.pos += len;
        }
        else if (type == 1) // fixed Huffman
        {
            std::array<std::uint8_t, 288> l {};
            for (int i = 0; i < 288; ++i)
                l[(std::size_t) i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            std::array<std::uint8_t, 30> d {};
            d.fill (5);
            Huffman lit, dist;
            lit.build (l.data(), 288);
            dist.build (d.data(), 30);
            if (! inflateBlock (in, out, lit, dist, maxSize))
                return std::nullopt;
        }
        else if (type == 2) // dynamic Huffman
        {
            const auto hlit = in.bits (5) + 257, hdist = in.bits (5) + 1, hclen = in.bits (4) + 4;
            if (hlit > 286 || hdist > 30)
                return std::nullopt;
            constexpr std::uint8_t order[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            std::array<std::uint8_t, 19> cl {};
            for (int i = 0; i < hclen; ++i)
                cl[order[i]] = (std::uint8_t) in.bits (3);
            Huffman clh;
            if (! clh.build (cl.data(), 19))
                return std::nullopt;

            std::array<std::uint8_t, 320> lengths {};
            for (int n = 0; n < hlit + hdist;)
            {
                const auto sym = clh.decode (in);
                if (sym < 0 || in.overrun)
                    return std::nullopt;
                if (sym < 16)
                {
                    lengths[(std::size_t) n++] = (std::uint8_t) sym;
                    continue;
                }
                int repeat = 0;
                std::uint8_t value = 0;
                if (sym == 16)
                {
                    if (n == 0)
                        return std::nullopt;
                    value = lengths[(std::size_t) n - 1];
                    repeat = 3 + in.bits (2);
                }
                else if (sym == 17)
                    repeat = 3 + in.bits (3);
                else
                    repeat = 11 + in.bits (7);
                if (n + repeat > hlit + hdist)
                    return std::nullopt;
                while (repeat-- > 0)
                    lengths[(std::size_t) n++] = value;
            }

            Huffman lit, dist;
            if (lengths[256] == 0 || ! lit.build (lengths.data(), hlit) || ! dist.build (lengths.data() + hlit, hdist))
                return std::nullopt;
            if (! inflateBlock (in, out, lit, dist, maxSize))
                return std::nullopt;
        }
        else
            return std::nullopt;

        if (in.overrun)
            return std::nullopt;
    }

    return out;
}

// zlib stream with stored (uncompressed) blocks
std::vector<std::uint8_t> zlibStored (const std::vector<std::uint8_t>& data)
{
    std::vector<std::uint8_t> out { 0x78, 0x01 };
    std::size_t i = 0;
    do
    {
        const auto len = std::min<std::size_t> (65535, data.size() - i);
        const bool last = i + len >= data.size();
        out.push_back (last ? 1 : 0);
        out.push_back ((std::uint8_t) (len & 0xff));
        out.push_back ((std::uint8_t) (len >> 8));
        out.push_back ((std::uint8_t) (~len & 0xff));
        out.push_back ((std::uint8_t) ((~len >> 8) & 0xff));
        out.insert (out.end(), data.begin() + (std::ptrdiff_t) i, data.begin() + (std::ptrdiff_t) (i + len));
        i += len;
    } while (i < data.size());
    std::uint32_t a = 1, b = 0;
    for (const auto c : data)
    {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    const auto ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8)
        out.push_back ((std::uint8_t) (ad >> s));
    return out;
}

} // namespace lynx
