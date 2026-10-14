#pragma once

// Browser search queries (docs/WORKFLOW_FL.md P2: browser search). Case-insensitive (ASCII) matching of
// file names against a small query language:
//   kick snare        both words (AND; "AND" may also be written)
//   kick OR snare     either side of OR (OR binds looser than AND)
//   NOT loop / -loop  excludes names containing the word
//   "808 kick"        the exact phrase
//   kick*.wav, h?t    wildcards: * any run of characters, ? any one character (the whole name must match)
// An empty query matches everything.

#include <string>
#include <vector>

namespace lynx::search
{
struct Term
{
    std::string text;      // lower-case
    bool negated = false;
    bool wildcard = false; // contains * or ?: matched against the whole name
};

// Groups of AND-ed terms, OR-ed together.
using Query = std::vector<std::vector<Term>>;

Query parse (const std::string& query);
bool matches (const Query& query, const std::string& name);
bool matches (const std::string& query, const std::string& name);

// Wildcard match of the whole text (* and ?), case-insensitive.
bool wildcardMatch (const std::string& pattern, const std::string& text);
} // namespace lynx::search
