#include "core/FlPatcher.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>

namespace lynx
{
namespace
{
template <typename T>
T at (const std::uint8_t* p, std::size_t offset)
{
    T v {};
    std::memcpy (&v, p + offset, sizeof (T));
    return v;
}

// chunks "u32 id + u64 tamanho + dados"; id 0 = grupo (o conteúdo são mais chunks)
bool forEachChunk (const std::uint8_t* p, std::size_t size, const std::function<void (std::uint32_t, const std::uint8_t*, std::size_t)>& fn)
{
    std::size_t pos = 0;
    while (pos + 12 <= size)
    {
        const auto id = at<std::uint32_t> (p, pos);
        const auto length = at<std::uint64_t> (p, pos + 4);
        if (length > size - pos - 12)
            return false;
        fn (id, p + pos + 12, (std::size_t) length);
        pos += 12 + (std::size_t) length;
    }
    return pos == size;
}

std::string text (const std::uint8_t* p, std::size_t size, bool wide)
{
    std::string out;
    if (wide)
        for (std::size_t i = 0; i + 1 < size; i += 2)
            out += (char) (p[i] | (p[i + 1] != 0 ? 0x80 : 0)); // nomes de plugin são ASCII
    else
        out.assign (reinterpret_cast<const char*> (p), size);
    while (! out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}

struct Port
{
    int node = -1;      // -1 = pino do Patcher
    bool input = false; // entrada do nó, ou pino que leva o som para fora
    int bus = 0;
    int kind = 0;       // 0 áudio, 1 MIDI, 2 controle
};

struct Connection
{
    std::uint64_t destination = 0, source = 0;
    float gain = 1.0f;
    int kind = 0;
};
} // namespace

std::optional<PatcherGraph> parsePatcher (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 16)
        return std::nullopt;
    const auto version = at<std::uint32_t> (state.data(), 0);
    if (version < 3 || version > 4)
        return std::nullopt;
    const auto wide = version >= 4;

    PatcherGraph graph;
    std::map<std::uint64_t, Port> ports;
    std::vector<Connection> connections;
    std::uint64_t inputPin = 0, outputPin = 0;
    bool hasInput = false, hasOutput = false;

    const auto ok = forEachChunk (state.data() + 4, state.size() - 4, [&] (std::uint32_t id, const std::uint8_t* d, std::size_t n) {
        if (id != 0)
            return;
        std::vector<std::pair<std::uint32_t, std::pair<const std::uint8_t*, std::size_t>>> sub;
        forEachChunk (d, n, [&] (std::uint32_t k, const std::uint8_t* x, std::size_t m) { sub.push_back ({ k, { x, m } }); });
        if (sub.empty())
            return;

        const auto first = sub.front().first;
        if (first == 5000 && sub.front().second.second >= 4) // nó
        {
            PatcherNode node;
            node.generator = at<std::uint32_t> (sub.front().second.first, 0) == 3;
            const auto index = (int) graph.nodes.size();
            for (const auto& [k, x] : sub)
                if (k == 5008)
                    node.plugin = text (x.first, x.second, wide);
                else if (k == 5005)
                    node.state.assign (x.first, x.first + x.second);
                else if (k == 5009 && x.second >= 20)
                    ports[at<std::uint64_t> (x.first, 8)] = { index, (at<std::uint32_t> (x.first, 16) & 1) != 0, (int) at<std::uint32_t> (x.first, 4),
                                                             (int) at<std::uint32_t> (x.first, 0) };
            graph.nodes.push_back (std::move (node));
        }
        else if (first == 1400 && sub.front().second.second >= 21) // pino de E/S
        {
            const auto* x = sub.front().second.first;
            const auto portId = at<std::uint64_t> (x, 0);
            const auto kind = (int) at<std::uint32_t> (x, 8);
            const auto fromOutside = (at<std::uint32_t> (x, 12) & 1) != 0;
            const auto bus = at<std::int32_t> (x, 17);
            ports[portId] = { -1, ! fromOutside, bus, kind };
            if (kind == 0 && bus == 0 && fromOutside)
                inputPin = portId, hasInput = true;
            if (kind == 0 && bus == 0 && ! fromOutside)
                outputPin = portId, hasOutput = true;
        }
        else if (first == 1300 && sub.front().second.second >= 25) // conexão
        {
            const auto* x = sub.front().second.first;
            connections.push_back ({ at<std::uint64_t> (x, 0), at<std::uint64_t> (x, 8), at<float> (x, 16), (int) x[24] });
        }
    });
    if (! ok)
        return std::nullopt;

    // gerador principal: o que chega na saída com mais ganho
    float best = -1.0f;
    for (const auto& c : connections)
        if (c.kind == 0 && hasOutput && c.destination == outputPin)
            if (const auto src = ports.find (c.source); src != ports.end() && src->second.node >= 0
                && graph.nodes[(std::size_t) src->second.node].generator && c.gain > best)
            {
                best = c.gain;
                graph.mainGenerator = src->second.node;
            }

    // cadeia em série: só arestas de áudio no barramento 0, cada nó com uma entrada e uma saída
    if (hasInput && hasOutput)
    {
        std::map<int, std::vector<int>> next; // -1 = IN, -2 = OUT
        int audioEdges = 0;
        bool simple = true;
        for (const auto& c : connections)
        {
            if (c.kind != 0)
                continue;
            const auto src = ports.find (c.source), dst = ports.find (c.destination);
            if (src == ports.end() || dst == ports.end())
                continue;
            ++audioEdges;
            if (src->second.bus != 0 || dst->second.bus != 0 || std::abs (c.gain - 1.0f) > 1e-3f)
                simple = false;
            const auto from = src->second.node >= 0 ? src->second.node : (c.source == inputPin ? -1 : -3);
            const auto to = dst->second.node >= 0 ? dst->second.node : (c.destination == outputPin ? -2 : -3);
            if (from == -3 || to == -3)
                simple = false;
            next[from].push_back (to);
        }

        std::vector<int> chain;
        auto cursor = -1;
        while (simple && next[cursor].size() == 1)
        {
            cursor = next[cursor].front();
            if (cursor == -2)
                break;
            if (cursor < 0 || graph.nodes[(std::size_t) cursor].generator || chain.size() > graph.nodes.size())
            {
                simple = false;
                break;
            }
            chain.push_back (cursor);
        }
        if (simple && cursor == -2 && audioEdges == (int) chain.size() + 1)
            graph.serialChain = std::move (chain);
    }

    // ramos em paralelo: de IN, cada saída segue uma cadeia de nós com 1 entrada e 1 saída até OUT
    if (hasInput && hasOutput)
    {
        struct Edge
        {
            int from, to;
            float gain;
        };
        std::vector<Edge> edges;
        bool ok = true;
        for (const auto& c : connections)
        {
            if (c.kind != 0)
                continue;
            const auto src = ports.find (c.source), dst = ports.find (c.destination);
            if (src == ports.end() || dst == ports.end())
                continue;
            if (src->second.bus != 0 || dst->second.bus != 0)
                ok = false; // M/S, bandas, sidechain: fora
            const auto from = src->second.node >= 0 ? src->second.node : (c.source == inputPin ? -1 : -3);
            const auto to = dst->second.node >= 0 ? dst->second.node : (c.destination == outputPin ? -2 : -3);
            if (from == -3 || to == -3)
                ok = false;
            edges.push_back ({ from, to, std::isfinite (c.gain) ? c.gain : 1.0f });
        }

        auto countFrom = [&] (int n) { return (int) std::count_if (edges.begin(), edges.end(), [n] (const Edge& e) { return e.from == n; }); };
        auto countTo = [&] (int n) { return (int) std::count_if (edges.begin(), edges.end(), [n] (const Edge& e) { return e.to == n; }); };
        std::size_t used = 0;
        for (const auto& start : edges)
        {
            if (! ok || start.from != -1)
                continue;
            PatcherGraph::Branch branch;
            branch.gain = start.gain;
            auto cursor = start.to;
            ++used;
            while (ok && cursor >= 0)
            {
                if (graph.nodes[(std::size_t) cursor].generator || countTo (cursor) != 1 || countFrom (cursor) != 1
                    || branch.nodes.size() > graph.nodes.size())
                {
                    ok = false;
                    break;
                }
                branch.nodes.push_back (cursor);
                const auto next = std::find_if (edges.begin(), edges.end(), [cursor] (const Edge& e) { return e.from == cursor; });
                branch.gain *= next->gain;
                cursor = next->to;
                ++used;
            }
            if (cursor != -2)
                ok = false;
            graph.branches.push_back (std::move (branch));
        }
        if (! ok || used != edges.size() || graph.branches.size() < 2)
            graph.branches.clear();
    }

    return graph;
}
} // namespace lynx
