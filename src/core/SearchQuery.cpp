#include "core/SearchQuery.h"

#include <cctype>

namespace lynx::search
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// splits into tokens; a quoted phrase is one token (kept with a leading '"' marker)
std::vector<std::string> tokens (const std::string& q)
{
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;

    auto flush = [&] {
        if (! current.empty())
            out.push_back (current);
        current.clear();
    };

    for (std::size_t i = 0; i < q.size(); ++i)
    {
        const auto c = q[i];

        if (c == '"')
        {
            if (quoted)
            {
                out.push_back ("\"" + current);
                current.clear();
                quoted = false;
            }
            else
            {
                flush();
                quoted = true;
            }
            continue;
        }

        if (! quoted && std::isspace ((unsigned char) c))
        {
            flush();
            continue;
        }

        current += c;
    }

    if (quoted)
        out.push_back ("\"" + current);
    else
        flush();

    return out;
}
} // namespace

bool wildcardMatch (const std::string& pattern, const std::string& text)
{
    const auto p = lower (pattern), t = lower (text);
    std::size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;

    while (ti < t.size())
    {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti]))
        {
            ++pi;
            ++ti;
        }
        else if (pi < p.size() && p[pi] == '*')
        {
            star = pi++;
            mark = ti;
        }
        else if (star != std::string::npos)
        {
            pi = star + 1;
            ti = ++mark;
        }
        else
            return false;
    }

    while (pi < p.size() && p[pi] == '*')
        ++pi;

    return pi == p.size();
}

Query parse (const std::string& query)
{
    Query out (1);
    bool negateNext = false;

    for (const auto& token : tokens (query))
    {
        const auto phrase = ! token.empty() && token[0] == '"';

        if (! phrase && token == "OR")
        {
            if (! out.back().empty())
                out.emplace_back();
            continue;
        }

        if (! phrase && token == "AND")
            continue;

        if (! phrase && token == "NOT")
        {
            negateNext = true;
            continue;
        }

        Term term;
        term.negated = negateNext;
        negateNext = false;
        auto text = phrase ? token.substr (1) : token;

        if (! phrase && text.size() > 1 && text[0] == '-')
        {
            term.negated = true;
            text = text.substr (1);
        }

        if (text.empty())
            continue;

        term.text = lower (text);
        term.wildcard = ! phrase && text.find_first_of ("*?") != std::string::npos;
        out.back().push_back (term);
    }

    if (out.back().empty() && out.size() > 1)
        out.pop_back();

    return out;
}

bool matches (const Query& query, const std::string& name)
{
    const auto lowered = lower (name);
    bool anyGroup = false;

    for (const auto& group : query)
    {
        if (group.empty())
            continue;

        anyGroup = true;
        bool all = true;

        for (const auto& term : group)
        {
            const auto hit = term.wildcard ? wildcardMatch (term.text, lowered) : lowered.find (term.text) != std::string::npos;

            if (hit == term.negated)
            {
                all = false;
                break;
            }
        }

        if (all)
            return true;
    }

    return ! anyGroup;
}

bool matches (const std::string& query, const std::string& name)
{
    return matches (parse (query), name);
}
} // namespace lynx::search
