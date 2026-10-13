#pragma once

// FL's Patcher (REA_rodada14 §1): the plugin graph stored in the state (0xD5). LYNX handles
// the simple cases: a serial chain of effects (becomes consecutive mixer slots) and the main generator
// of a Patcher used as an instrument.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lynx
{
struct PatcherNode
{
    std::string plugin;               // internal name (same as 0xC9)
    std::vector<std::uint8_t> state;  // same format as the plugin's 0xD5 in a slot/channel
    bool generator = false;
};

// Series-parallel expression of the IN ⇒ OUT audio path (REA_rodada20 §3.3): node (with the output bus
// used, for splitters), serial chain or parallel sum; `gain` = product of the cable gains.
struct PatcherExpr
{
    enum class Kind { identity, node, series, parallel };
    Kind kind = Kind::identity;
    int node = -1;
    int bus = -1; // splitter node: output bus of this branch (−1 = ordinary node)
    float gain = 1.0f;
    std::vector<PatcherExpr> children;
};

struct PatcherGraph
{
    std::vector<PatcherNode> nodes;
    std::vector<int> serialChain; // nodes from input to output, if the audio is a simple chain

    // Parallel branches from input to output (all on bus 0, each a simple chain; empty
    // branch = dry signal), with the cable gains multiplied. Empty if the graph is anything else.
    struct Branch
    {
        std::vector<int> nodes;
        float gain = 1.0f;
        int band = 0; // output bus of the `splitter` node the branch comes out of
    };
    std::vector<Branch> branches;
    std::vector<int> preChain; // serial nodes before the split into branches
    int splitter = -1;         // Frequency Splitter / Stereo Shaper that splits (the branches start at its outputs)
    int mainGenerator = -1;       // generator connected to the output with the most gain
    std::optional<PatcherExpr> audio; // the whole reduced audio graph (empty if it does not reduce or has a generator)
};

std::optional<PatcherGraph> parsePatcher (const std::vector<std::uint8_t>& state);
} // namespace lynx
