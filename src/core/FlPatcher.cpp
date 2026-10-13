#include "core/FlPatcher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

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
    int kind = -1;         // −1 = classe da porta (bit 1 das flags desligado)
    bool disabled = false; // bit 0 das flags
};

bool isSplitterName (std::string name)
{
    for (auto& ch : name)
        ch = (char) std::tolower ((unsigned char) ch);
    return name == "frequency splitter" || name == "fruity stereo shaper"; // nós que dividem por barramento
}

// Redução série-paralelo (REA_rodada20 §3.3). Vértices: IN, OUT e, para cada nó (ou nó divisor por
// barramento), a entrada e a saída; arestas carregam a expressão. Repete: arestas com as mesmas pontas
// viram paralelo; vértice com uma entrada e uma saída vira série.
struct SpEdge
{
    int from, to;
    PatcherExpr expr;
};

PatcherExpr flattenSeries (PatcherExpr a, PatcherExpr b)
{
    PatcherExpr s;
    s.kind = PatcherExpr::Kind::series;
    s.gain = a.gain * b.gain;
    for (auto* e : { &a, &b })
    {
        if (e->kind == PatcherExpr::Kind::identity)
            continue;
        if (e->kind == PatcherExpr::Kind::series)
            for (auto& c : e->children)
                s.children.push_back (std::move (c));
        else
        {
            e->gain = 1.0f;
            s.children.push_back (std::move (*e));
        }
    }
    if (s.children.empty())
        return { PatcherExpr::Kind::identity, -1, -1, s.gain, {} };
    if (s.children.size() == 1)
    {
        auto only = std::move (s.children.front());
        only.gain = s.gain;
        return only;
    }
    return s;
}

std::optional<PatcherExpr> reduce (std::vector<SpEdge> edges, int source, int sink)
{
    for (int guard = 0; guard < 100000; ++guard)
    {
        if (edges.size() == 1 && edges[0].from == source && edges[0].to == sink)
            return edges[0].expr;
        bool changed = false;

        // paralelo
        for (std::size_t i = 0; i < edges.size() && ! changed; ++i)
            for (std::size_t j = i + 1; j < edges.size(); ++j)
                if (edges[i].from == edges[j].from && edges[i].to == edges[j].to)
                {
                    PatcherExpr p;
                    p.kind = PatcherExpr::Kind::parallel;
                    for (auto* e : { &edges[i].expr, &edges[j].expr })
                        if (e->kind == PatcherExpr::Kind::parallel && e->gain == 1.0f)
                            for (auto& c : e->children)
                                p.children.push_back (std::move (c));
                        else
                            p.children.push_back (std::move (*e));
                    edges[i].expr = std::move (p);
                    edges.erase (edges.begin() + (std::ptrdiff_t) j);
                    changed = true;
                    break;
                }
        if (changed)
            continue;

        // série
        std::map<int, int> inCount, outCount;
        for (const auto& e : edges)
        {
            ++outCount[e.from];
            ++inCount[e.to];
        }
        for (const auto& [v, n] : inCount)
        {
            if (v == source || v == sink || n != 1 || outCount[v] != 1)
                continue;
            const auto a = std::find_if (edges.begin(), edges.end(), [v = v] (const SpEdge& e) { return e.to == v; }) - edges.begin();
            const auto b = std::find_if (edges.begin(), edges.end(), [v = v] (const SpEdge& e) { return e.from == v; }) - edges.begin();
            SpEdge merged { edges[(std::size_t) a].from, edges[(std::size_t) b].to,
                            flattenSeries (std::move (edges[(std::size_t) a].expr), std::move (edges[(std::size_t) b].expr)) };
            edges.erase (edges.begin() + std::max (a, b));
            edges.erase (edges.begin() + std::min (a, b));
            edges.push_back (std::move (merged));
            changed = true;
            break;
        }
        if (! changed)
            return std::nullopt;
    }
    return std::nullopt;
}
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
            // flags (offset 20): bit 0 = cabo desligado; bit 1 = o byte de classe (24) vale (REA_rodada20 §3.1)
            const auto flags = at<std::uint32_t> (x, 20);
            connections.push_back ({ at<std::uint64_t> (x, 0), at<std::uint64_t> (x, 8), at<float> (x, 16), (flags & 2) != 0 ? (int) x[24] : -1,
                                     (flags & 1) != 0 });
        }
    });
    if (! ok)
        return std::nullopt;

    // classe do cabo: a do byte 24 se valer; senão, a da porta de destino. Cabos desligados saem.
    std::erase_if (connections, [] (const Connection& c) { return c.disabled; });
    for (auto& c : connections)
        if (c.kind < 0)
        {
            const auto dst = ports.find (c.destination);
            c.kind = dst != ports.end() ? dst->second.kind : 2;
        }

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

    // ramos em paralelo: trecho comum em série desde IN, depois ramos (do IN, de um nó com várias
    // saídas, ou das bandas de um Frequency Splitter), cada um uma cadeia simples até OUT. Entradas de
    // sidechain (barramento > 0 no destino) ficam de fora.
    if (hasInput && hasOutput)
    {
        struct Edge
        {
            int from, to, fromBus;
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
            if (dst->second.bus != 0)
                continue; // sidechain
            const auto from = src->second.node >= 0 ? src->second.node : (c.source == inputPin ? -1 : -3);
            const auto to = dst->second.node >= 0 ? dst->second.node : (c.destination == outputPin ? -2 : -3);
            if (from == -3 || to == -3)
            {
                ok = false;
                continue;
            }
            edges.push_back ({ from, to, src->second.bus, std::isfinite (c.gain) ? c.gain : 1.0f });
        }

        auto isSplitter = [&] (int n) {
            auto name = graph.nodes[(std::size_t) n].plugin;
            for (auto& ch : name)
                ch = (char) std::tolower ((unsigned char) ch);
            return name == "frequency splitter" || name == "fruity stereo shaper"; // nós que dividem por barramento
        };
        auto outs = [&] (int n) {
            std::vector<std::size_t> r;
            for (std::size_t i = 0; i < edges.size(); ++i)
                if (edges[i].from == n)
                    r.push_back (i);
            return r;
        };
        auto countTo = [&] (int n) { return (int) std::count_if (edges.begin(), edges.end(), [n] (const Edge& e) { return e.to == n; }); };

        // só barramento 0 fora do splitter (outras saídas, como as do Stereo Shaper, não são reproduzíveis)
        for (const auto& e : edges)
            if (e.fromBus != 0 && (e.from < 0 || ! isSplitter (e.from)))
                ok = false;

        std::vector<bool> consumed (edges.size(), false);
        auto cursor = -1;
        float preGain = 1.0f;
        std::vector<int> pre;
        while (ok)
        {
            const auto o = outs (cursor);
            if (o.size() != 1 || edges[o[0]].to < 0 || countTo (edges[o[0]].to) != 1)
                break;
            consumed[o[0]] = true;
            preGain *= edges[o[0]].gain;
            cursor = edges[o[0]].to;
            if (graph.nodes[(std::size_t) cursor].generator)
                ok = false;
            pre.push_back (cursor);
            if (isSplitter (cursor))
                break;
        }

        std::vector<PatcherGraph::Branch> branches;
        const auto splitterHere = cursor >= 0 && isSplitter (cursor);
        for (const auto i : outs (cursor))
        {
            if (! ok)
                break;
            consumed[i] = true;
            PatcherGraph::Branch branch;
            branch.gain = preGain * edges[i].gain;
            branch.band = splitterHere ? edges[i].fromBus : 0;
            auto at = edges[i].to;
            while (ok && at >= 0)
            {
                const auto o = outs (at);
                if (graph.nodes[(std::size_t) at].generator || isSplitter (at) || countTo (at) != 1 || o.size() != 1
                    || branch.nodes.size() > graph.nodes.size())
                {
                    ok = false;
                    break;
                }
                branch.nodes.push_back (at);
                consumed[o[0]] = true;
                branch.gain *= edges[o[0]].gain;
                at = edges[o[0]].to;
            }
            if (at != -2)
                ok = false;
            branches.push_back (std::move (branch));
        }

        if (ok && std::all_of (consumed.begin(), consumed.end(), [] (bool c) { return c; })
            && (branches.size() >= 2 || (splitterHere && ! branches.empty())))
        {
            if (splitterHere)
            {
                graph.splitter = cursor;
                pre.pop_back();
            }
            graph.preChain = std::move (pre);
            graph.branches = std::move (branches);
        }
    }

    // grafo de áudio reduzido a série/paralelo (REA_rodada20 §3.3)
    if (hasInput && hasOutput)
    {
        struct Edge
        {
            int from, to, fromBus;
            float gain;
        };
        std::vector<Edge> edges;
        bool valid = true;
        for (const auto& c : connections)
        {
            if (c.kind != 0)
                continue;
            const auto src = ports.find (c.source), dst = ports.find (c.destination);
            if (src == ports.end() || dst == ports.end() || dst->second.bus != 0)
                continue; // sidechain fica de fora
            const auto from = src->second.node >= 0 ? src->second.node : (c.source == inputPin ? -1 : -3);
            const auto to = dst->second.node >= 0 ? dst->second.node : (c.destination == outputPin ? -2 : -3);
            if (from == -3 || to == -3)
                continue;
            edges.push_back ({ from, to, src->second.bus, std::isfinite (c.gain) ? c.gain : 1.0f });
        }

        // poda: só nós alcançáveis a partir de IN e que chegam a OUT
        auto reach = [&] (int start, bool forward) {
            std::set<int> seen { start };
            std::vector<int> stack { start };
            while (! stack.empty())
            {
                const auto v = stack.back();
                stack.pop_back();
                for (const auto& e : edges)
                    if ((forward ? e.from : e.to) == v && seen.insert (forward ? e.to : e.from).second)
                        stack.push_back (forward ? e.to : e.from);
            }
            return seen;
        };
        const auto fromIn = reach (-1, true), toOut = reach (-2, false);
        std::erase_if (edges, [&] (const Edge& e) { return ! fromIn.count (e.from) || ! toOut.count (e.from) || ! fromIn.count (e.to) || ! toOut.count (e.to); });

        // divisores com saídas por barramento ≠ 0 viram um vértice por barramento usado
        std::map<int, std::set<int>> splitBuses;
        for (const auto& e : edges)
            if (e.from >= 0 && e.fromBus != 0)
            {
                if (! isSplitterName (graph.nodes[(std::size_t) e.from].plugin))
                    valid = false; // saída lateral de um nó comum: não reproduzível
                splitBuses[e.from].insert (e.fromBus);
            }
        for (const auto& e : edges)
            if (e.from >= 0 && splitBuses.count (e.from))
                splitBuses[e.from].insert (e.fromBus);

        // vértices: IN = 0, OUT = 1; nó n (ou n por barramento): entrada/saída
        std::map<std::pair<int, int>, int> vin, vout;
        int next = 2;
        std::vector<SpEdge> sp;
        auto vertexPair = [&] (int n, int bus) {
            const auto key = std::make_pair (n, bus);
            if (! vin.count (key))
            {
                vin[key] = next++;
                vout[key] = next++;
                PatcherExpr e;
                e.kind = PatcherExpr::Kind::node;
                e.node = n;
                e.bus = bus;
                sp.push_back ({ vin[key], vout[key], std::move (e) });
            }
        };
        auto busesOf = [&] (int n) { return splitBuses.count (n) ? std::vector<int> (splitBuses[n].begin(), splitBuses[n].end()) : std::vector<int> { -1 }; };
        for (const auto& e : edges)
        {
            if ((e.from >= 0 && graph.nodes[(std::size_t) e.from].generator) || (e.to >= 0 && graph.nodes[(std::size_t) e.to].generator))
                valid = false;
            const auto fromBus = e.from >= 0 && splitBuses.count (e.from) ? e.fromBus : -1;
            int from = 0;
            if (e.from >= 0)
            {
                vertexPair (e.from, fromBus);
                from = vout[{ e.from, fromBus }];
            }
            // o cabo que entra num divisor alimenta todos os barramentos dele
            for (const auto bus : e.to >= 0 ? busesOf (e.to) : std::vector<int> { -1 })
            {
                int to = 1;
                if (e.to >= 0)
                {
                    vertexPair (e.to, bus);
                    to = vin[{ e.to, bus }];
                }
                sp.push_back ({ from, to, { PatcherExpr::Kind::identity, -1, -1, e.gain, {} } });
            }
        }
        if (valid && ! sp.empty())
            graph.audio = reduce (std::move (sp), 0, 1);
    }

    return graph;
}
} // namespace lynx
