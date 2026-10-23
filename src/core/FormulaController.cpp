#include "core/FormulaController.h"

#include "core/Effects.h"
#include "core/Song.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>

namespace lynx::formula
{
namespace
{
constexpr double eps = 1e-30; // the tolerance of =, <>, >= and <= in FL's compiler
constexpr double pi = 3.14159265358979323846;

enum Op : int
{
    num, varA, varB, varC, songTime, getTempo, timeMs, date, mouseX, mouseY, rand,
    neg, factorial, percent,
    power, mul, divide, idiv, imod, fmodOp, add, sub,
    gt, ge, le, lt, ne, eq,
    lnot, lor, land, lxor, shr, shl,
    // functions
    fExp, fIf, fSqr, fSqrt, fAbs, fRound, fTrunc, fFloor, fCeil, fLn, fLog10, fLogN, fIntPower, fMax, fMin, fSin, fCos, fTan,
    fArcSin, fArcCos, fArcTan2, fSinh, fCosh, fTanh, fArcSinh, fArcCosh, fArcTanh, fDegToRad, fRadToDeg, fInter, fInterNoSwap,
    fFold, fWarp, fMap, fSeededRand, fInt, fFrac, fIfE, fIfL, fIfG, fIfLE, fIfGE, fCase, fSum, fLog2, fCot, fArcTan, fSec,
    fCoSec, fTension
};

struct Function
{
    int op, args;
};

const std::map<std::string, Function>& functions()
{
    static const std::map<std::string, Function> table {
        { "exp", { fExp, 1 } }, { "if", { fIf, 3 } }, { "sqr", { fSqr, 1 } }, { "sqrt", { fSqrt, 1 } }, { "abs", { fAbs, 1 } },
        { "round", { fRound, 1 } }, { "trunc", { fTrunc, 1 } }, { "floor", { fFloor, 1 } }, { "ceil", { fCeil, 1 } },
        { "ln", { fLn, 1 } }, { "log10", { fLog10, 1 } }, { "logn", { fLogN, 2 } }, { "power", { power, 2 } }, { "pow", { power, 2 } },
        { "intpower", { fIntPower, 2 } }, { "max", { fMax, 2 } }, { "min", { fMin, 2 } }, { "sin", { fSin, 1 } }, { "cos", { fCos, 1 } },
        { "tan", { fTan, 1 } }, { "arcsin", { fArcSin, 1 } }, { "arccos", { fArcCos, 1 } }, { "arctan2", { fArcTan2, 2 } },
        { "sinh", { fSinh, 1 } }, { "cosh", { fCosh, 1 } }, { "tanh", { fTanh, 1 } }, { "arcsinh", { fArcSinh, 1 } },
        { "arccosh", { fArcCosh, 1 } }, { "arctanh", { fArcTanh, 1 } }, { "degtorad", { fDegToRad, 1 } }, { "radtodeg", { fRadToDeg, 1 } },
        { "atan2", { fArcTan2, 2 } }, { "inter", { fInter, 3 } }, { "internoswap", { fInterNoSwap, 3 } }, { "fold", { fFold, 1 } },
        { "warp", { fWarp, 2 } }, { "map", { fMap, 3 } }, { "seededrand", { fSeededRand, 1 } }, { "int", { fInt, 1 } },
        { "frac", { fFrac, 1 } }, { "ife", { fIfE, 2 } }, { "ifl", { fIfL, 2 } }, { "ifg", { fIfG, 2 } }, { "ifle", { fIfLE, 2 } },
        { "ifge", { fIfGE, 2 } }, { "case", { fCase, 3 } }, { "sum", { fSum, 2 } }, { "neg", { neg, 1 } }, { "log2", { fLog2, 1 } },
        { "tg", { fTan, 1 } }, { "ctg", { fCot, 1 } }, { "arctg", { fArcTan, 1 } }, { "sec", { fSec, 1 } }, { "cosec", { fCoSec, 1 } },
        { "tension", { fTension, 2 } },
    };
    return table;
}

// identifiers that are values (callable with or without "()")
const std::map<std::string, int>& values()
{
    static const std::map<std::string, int> table {
        { "a", varA }, { "b", varB }, { "c", varC }, { "songtime", songTime }, { "gettempo", getTempo }, { "time", timeMs },
        { "date", date }, { "mousex", mouseX }, { "mousey", mouseY }, { "rand", rand },
    };
    return table;
}

double factorialOf (double x) noexcept
{
    double r = 1.0;
    for (int guard = 0; x > 1.1 && guard < 200; ++guard, x -= 1.0)
        r *= x;
    return x > 1.1 ? HUGE_VAL : r;
}

std::int64_t roundInt (double v) noexcept
{
    const auto r = roundHalfEven (v);
    return std::isfinite (r) && std::abs (r) < 9.0e18 ? (std::int64_t) r : 0;
}

std::string lower (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::tolower ((unsigned char) ch);
    return s;
}
} // namespace

double roundHalfEven (double v) noexcept
{
    return std::nearbyint (v); // (the default rounding mode: to nearest, ties to even)
}

float seededRand (double seed) noexcept
{
    static constexpr std::uint32_t c1[4] { 0xbaa96887u, 0x1e17d32cu, 0x03bcdc3cu, 0x0f33d1b2u };
    static constexpr std::uint32_t c2[4] { 0x4b0f3b58u, 0xe874f0c3u, 0x6955c5a6u, 0x55a7ca46u };
    std::uint64_t bits = 0;
    std::memcpy (&bits, &seed, 8);
    auto low = (std::uint32_t) bits, high = (std::uint32_t) (bits >> 32);
    for (int i = 0; i < 4; ++i)
    {
        const auto swap = low;
        const auto x = low ^ c1[i];
        const auto lo = x & 0xffffu, hi = x >> 16;
        const auto y = lo * lo + ~(hi * hi);
        low = high ^ (((y >> 16) | (y << 16)) ^ c2[i]) + lo * hi;
        high = swap;
    }
    const auto mantissa = (low & 0x7fffffu) | 0x3f800000u;
    float f = 0.0f;
    std::memcpy (&f, &mantissa, 4);
    return f - 1.0f;
}

double tension (double value, double rate) noexcept
{
    // the plugin computes in single precision
    if (rate == 0.0)
        return value;
    if (rate < 0.0)
    {
        const auto s = -rate;
        const auto g = std::log ((double) ((float) s + 1.0f));
        const auto e = std::exp ((double) (float) value * g);
        return (double) (float) (e - 1.0) / s;
    }
    const auto g = std::log ((double) ((float) rate + 1.0f));
    const auto e = std::exp ((double) (float) (1.0 - value) * g);
    return (rate - (double) (float) (e - 1.0)) / rate;
}

double fold (double value) noexcept
{
    const auto x = value * 2.0 - 1.0;
    auto t = std::abs (x) * 0.25;
    t -= std::trunc (t);
    if (t >= 0.25)
        t = t >= 0.75 ? (t - 0.75) * 4.0 - 1.0 : 1.0 - (t - 0.25) * 4.0;
    else
        t *= 4.0;
    if (x < 0.0)
        t = -t;
    return t / 2.0 + 0.5;
}

double warp (double value, double amount) noexcept
{
    if (value > 1.0 || value < 0.0)
        value += amount;
    return value - std::trunc (value / 1.0) * 1.0;
}

struct Compiler
{
    std::string text;
    std::size_t pos = 0;
    std::string error;
    Program& program;

    explicit Compiler (const std::string& t, Program& p) : text (t), program (p) {}

    int node (int op, int a = -1, int b = -1, int c = -1, double value = 0.0)
    {
        Program::Node n;
        n.op = op;
        n.value = value;
        n.args[0] = a;
        n.args[1] = b;
        n.args[2] = c;
        program.nodes.push_back (n);
        return (int) program.nodes.size() - 1;
    }

    void skip()
    {
        while (pos < text.size() && std::isspace ((unsigned char) text[pos]))
            ++pos;
    }

    bool fail (const std::string& message)
    {
        if (error.empty())
            error = message;
        return false;
    }

    // the next word (letters, digits, _), lowercase, without consuming it
    std::string peekWord()
    {
        skip();
        auto end = pos;
        while (end < text.size() && (std::isalnum ((unsigned char) text[end]) || text[end] == '_'))
            ++end;
        if (end == pos || std::isdigit ((unsigned char) text[pos]))
            return {};
        return lower (text.substr (pos, end - pos));
    }

    bool takeWord (const char* word)
    {
        if (peekWord() != word)
            return false;
        pos += std::strlen (word);
        return true;
    }

    bool takeSymbol (const char* s)
    {
        skip();
        if (text.compare (pos, std::strlen (s), s) != 0)
            return false;
        pos += std::strlen (s);
        return true;
    }

    int level70()
    {
        auto left = level60();
        while (left >= 0)
        {
            int op = -1;
            if (takeWord ("or")) op = lor;
            else if (takeWord ("and")) op = land;
            else if (takeWord ("xor")) op = lxor;
            else if (takeWord ("shr")) op = shr;
            else if (takeWord ("shl")) op = shl;
            else break;
            const auto right = level60();
            if (right < 0)
                return -1;
            left = node (op, left, right);
        }
        return left;
    }

    int level60()
    {
        if (takeWord ("not"))
        {
            const auto operand = level60();
            return operand < 0 ? -1 : node (lnot, operand);
        }
        return level50();
    }

    int level50()
    {
        auto left = level40();
        while (left >= 0)
        {
            int op = -1;
            if (takeSymbol (">=")) op = ge;
            else if (takeSymbol ("<=")) op = le;
            else if (takeSymbol ("<>")) op = ne;
            else if (takeSymbol (">")) op = gt;
            else if (takeSymbol ("<")) op = lt;
            else if (takeSymbol ("=")) op = eq;
            else if (takeWord ("in")) op = eq;
            else break;
            const auto right = level40();
            if (right < 0)
                return -1;
            left = node (op, left, right);
        }
        return left;
    }

    int level40()
    {
        auto left = level30();
        while (left >= 0)
        {
            int op = -1;
            if (takeSymbol ("+")) op = add;
            else if (takeSymbol ("-")) op = sub;
            else break;
            const auto right = level30();
            if (right < 0)
                return -1;
            left = node (op, left, right);
        }
        return left;
    }

    int level30()
    {
        auto left = level20();
        while (left >= 0)
        {
            int op = -1;
            if (takeSymbol ("*")) op = mul;
            else if (takeSymbol ("/")) op = divide;
            else if (takeWord ("div")) op = idiv;
            else if (takeWord ("mod")) op = imod;
            else if (takeWord ("fmod")) op = fmodOp;
            else break;
            const auto right = level20();
            if (right < 0)
                return -1;
            left = node (op, left, right);
        }
        return left;
    }

    int level20()
    {
        auto left = level10();
        while (left >= 0 && takeSymbol ("^"))
        {
            const auto right = level10();
            if (right < 0)
                return -1;
            left = node (power, left, right);
        }
        return left;
    }

    int level10()
    {
        if (takeSymbol ("-"))
        {
            const auto operand = level10();
            return operand < 0 ? -1 : node (neg, operand);
        }
        if (takeSymbol ("+"))
            return level10();
        auto value = primary();
        while (value >= 0)
        {
            if (takeSymbol ("!"))
                value = node (factorial, value);
            else if (takeSymbol ("%"))
                value = node (percent, value);
            else
                break;
        }
        return value;
    }

    int primary()
    {
        skip();
        if (pos >= text.size())
            return (fail ("Unexpected end of formula"), -1);

        const auto ch = text[pos];
        if (std::isdigit ((unsigned char) ch) || ch == '.')
        {
            const char* begin = text.c_str() + pos;
            char* end = nullptr;
            const auto v = std::strtod (begin, &end);
            if (end == begin)
                return (fail ("Bad number"), -1);
            pos += (std::size_t) (end - begin);
            return node (num, -1, -1, -1, v);
        }
        if (takeSymbol ("("))
        {
            const auto inner = level70();
            if (inner < 0)
                return -1;
            if (! takeSymbol (")"))
                return (fail ("Missing )"), -1);
            return inner;
        }

        const auto word = peekWord();
        if (word.empty())
            return (fail (std::string ("Unexpected '") + ch + "'"), -1);
        pos += word.size();

        if (word == "pi")
            return node (num, -1, -1, -1, pi);
        if (const auto v = values().find (word); v != values().end())
        {
            if (takeSymbol ("(") && ! takeSymbol (")"))
                return (fail ("Missing )"), -1);
            if (v->second >= songTime && v->second <= rand)
                program.timed = true;
            if (v->second == date)
                program.dated = true;
            return node (v->second);
        }
        const auto f = functions().find (word);
        if (f == functions().end())
            return (fail ("Unknown identifier '" + word + "'"), -1);
        if (! takeSymbol ("("))
            return (fail ("Missing ( after " + word), -1);
        int args[3] { -1, -1, -1 };
        for (int k = 0; k < f->second.args; ++k)
        {
            if (k > 0 && ! takeSymbol (","))
                return (fail (word + " takes " + std::to_string (f->second.args) + " arguments"), -1);
            args[k] = level70();
            if (args[k] < 0)
                return -1;
        }
        if (! takeSymbol (")"))
            return (fail ("Missing )"), -1);
        if (f->second.op == fSeededRand)
            program.timed = true; // (only through its argument, but cheap to re-evaluate)
        return node (f->second.op, args[0], args[1], args[2]);
    }
};

std::shared_ptr<const Program> Program::compile (const std::string& text, std::string* error)
{
    auto program = std::make_shared<Program>();
    Compiler compiler (text, *program);
    program->root = compiler.level70();
    compiler.skip();
    if (program->root >= 0 && compiler.pos < text.size())
        compiler.fail ("Unexpected '" + text.substr (compiler.pos, 8) + "'");
    if (program->root < 0 || ! compiler.error.empty())
    {
        if (error != nullptr)
            *error = compiler.error.empty() ? std::string ("Empty formula") : compiler.error;
        return nullptr;
    }
    return program;
}

double Program::evaluate (const Inputs& in) const noexcept
{
    return root >= 0 ? eval (root, in) : 0.0;
}

double Program::eval (int index, const Inputs& in) const noexcept
{
    const auto& n = nodes[(std::size_t) index];
    auto arg = [&] (int k) { return n.args[k] >= 0 ? eval (n.args[k], in) : 0.0; };

    switch (n.op)
    {
        case num: return n.value;
        case varA: return in.a;
        case varB: return in.b;
        case varC: return in.c;
        case songTime: return in.songTime;
        case getTempo: return in.tempo;
        case timeMs: return in.timeMs;
        case date: return in.date;
        case mouseX: return in.mouseX;
        case mouseY: return in.mouseY;
        case rand:
        {
            if (in.randSeed == nullptr)
                return 0.5;
            *in.randSeed = *in.randSeed * 134775813u + 1u; // FL's Random (a Delphi LCG)
            return (double) *in.randSeed * (1.0 / 4294967296.0);
        }
        case neg: return -arg (0);
        case factorial: return factorialOf ((double) roundInt (arg (0)));
        case percent: return arg (0) * 0.01;
        case power: return std::pow (arg (0), arg (1));
        case mul: return arg (0) * arg (1);
        case divide: return arg (0) / arg (1);
        case idiv: { const auto d = roundInt (arg (1)); return d == 0 ? 0.0 : (double) (roundInt (arg (0)) / d); }
        case imod: { const auto d = roundInt (arg (1)); return d == 0 ? 0.0 : (double) (roundInt (arg (0)) % d); }
        case fmodOp: { const auto x = arg (0), d = arg (1); return d == 0.0 ? 0.0 : x - std::trunc (x / d) * d; }
        case add: return arg (0) + arg (1);
        case sub: return arg (0) - arg (1);
        case gt: { const auto x = arg (0), y = arg (1); return x > y ? 1.0 : 0.0; }
        case ge: { const auto x = arg (0), y = arg (1); return y <= x + eps ? 1.0 : 0.0; }
        case le: { const auto x = arg (0), y = arg (1); return x <= y + eps ? 1.0 : 0.0; }
        case lt: { const auto x = arg (0), y = arg (1); return x < y ? 1.0 : 0.0; }
        case ne: return std::abs (arg (0) - arg (1)) > eps ? 1.0 : 0.0;
        case eq: return std::abs (arg (0) - arg (1)) < eps ? 1.0 : 0.0;
        case lnot: return roundInt (arg (0)) == 0 ? 1.0 : 0.0;
        case lor: return (double) (roundInt (arg (0)) | roundInt (arg (1)));
        case land: return (double) (roundInt (arg (0)) & roundInt (arg (1)));
        case lxor: return (double) (roundInt (arg (0)) ^ roundInt (arg (1)));
        case shr: return (double) (std::int64_t) ((std::uint64_t) roundInt (arg (0)) >> (roundInt (arg (1)) & 63));
        case shl: return (double) (std::int64_t) ((std::uint64_t) roundInt (arg (0)) << (roundInt (arg (1)) & 63));

        case fExp: return std::exp (arg (0));
        case fIf: return roundInt (arg (0)) != 0 ? arg (1) : arg (2);
        case fSqr: { const auto x = arg (0); return x * x; }
        case fSqrt: return std::sqrt (arg (0));
        case fAbs: return std::abs (arg (0));
        case fRound: return (double) roundInt (arg (0));
        case fTrunc: return std::trunc (arg (0));
        case fFloor: return std::floor (arg (0));
        case fCeil: return std::ceil (arg (0));
        case fLn: return std::log (arg (0));
        case fLog10: return std::log10 (arg (0));
        case fLogN: return std::log (arg (1)) / std::log (arg (0));
        case fIntPower: return std::pow (arg (0), (double) roundInt (arg (1)));
        case fMax: { const auto x = arg (0), y = arg (1); return y <= x ? x : y; }
        case fMin: { const auto x = arg (0), y = arg (1); return x <= y ? x : y; }
        case fSin: return std::sin (arg (0));
        case fCos: return std::cos (arg (0));
        case fTan: return std::tan (arg (0));
        case fArcSin: return std::asin (arg (0));
        case fArcCos: return std::acos (arg (0));
        case fArcTan2: return std::atan2 (arg (0), arg (1));
        case fSinh: return std::sinh (arg (0));
        case fCosh: return std::cosh (arg (0));
        case fTanh: return std::tanh (arg (0));
        case fArcSinh: return std::asinh (arg (0));
        case fArcCosh: return std::acosh (arg (0));
        case fArcTanh: return std::atanh (arg (0));
        case fDegToRad: return arg (0) * (pi / 180.0);
        case fRadToDeg: return arg (0) * (180.0 / pi);
        case fInter:
        case fInterNoSwap:
        {
            const auto x = roundInt (arg (0) * 1000.0), lo = roundInt (arg (1) * 1000.0), hi = roundInt (arg (2) * 1000.0);
            if (n.op == fInter && hi < lo)
                return x < hi || lo < x ? 0.0 : 1.0;
            return x < lo || hi < x ? 0.0 : 1.0;
        }
        case fFold: return fold (arg (0));
        case fWarp: return warp (arg (0), arg (1));
        case fMap: { const auto x = arg (0), lo = arg (1), hi = arg (2); return x * (hi - lo) + lo; }
        case fSeededRand: return (double) seededRand (arg (0));
        case fInt: return std::trunc (arg (0));
        case fFrac: { const auto x = arg (0); return x - std::trunc (x); }
        case fIfE: return arg (0) == arg (1) ? 1.0 : 0.0;
        case fIfL: return arg (0) < arg (1) ? 1.0 : 0.0;
        case fIfG: return arg (0) > arg (1) ? 1.0 : 0.0;
        case fIfLE: return arg (0) <= arg (1) ? 1.0 : 0.0;
        case fIfGE: return arg (0) >= arg (1) ? 1.0 : 0.0;
        case fCase: return arg (0) == 1.0 ? arg (1) : arg (2);
        case fSum: return arg (0) + arg (1);
        case fLog2: return std::log2 (arg (0));
        case fCot: return 1.0 / std::tan (arg (0));
        case fArcTan: return std::atan (arg (0));
        case fSec: { const auto c = std::cos (arg (0)); return c == 0.0 ? 1.0 : 1.0 / c; }
        case fCoSec: { const auto s = std::sin (arg (0)); return s == 0.0 ? 1.0 : 1.0 / s; }
        case fTension: return tension (arg (0), arg (1));
        default: return 0.0;
    }
}

std::optional<State> parseState (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 17)
        return std::nullopt;
    std::int32_t v[4] {};
    std::memcpy (v, d.data(), 16);
    if (v[0] < 0 || v[0] > 64)
        return std::nullopt;
    State s;
    s.version = v[0];
    auto knob = [] (std::int32_t raw) { return std::clamp ((float) raw / 65536.0f, 0.0f, 1.0f); };
    s.a = knob (v[1]);
    s.b = knob (v[2]);
    s.c = knob (v[3]);
    const auto length = (std::size_t) d[16];
    if (17 + length > d.size())
        return std::nullopt;
    s.formula.assign (reinterpret_cast<const char*> (d.data() + 17), length);
    return s;
}

std::vector<std::uint8_t> writeState (const State& s)
{
    std::vector<std::uint8_t> out;
    auto i32 = [&] (std::int32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*> (&v);
        out.insert (out.end(), p, p + 4);
    };
    auto raw = [] (float k) { return (std::int32_t) std::lround (std::clamp (k, 0.0f, 1.0f) * 65536.0f); };
    i32 (2);
    i32 (raw (s.a));
    i32 (raw (s.b));
    i32 (raw (s.c));
    const auto text = s.formula.substr (0, 255);
    out.push_back ((std::uint8_t) text.size());
    out.insert (out.end(), text.begin(), text.end());
    i32 (0); // no description
    return out;
}
std::string formulaOf (const std::vector<std::uint8_t>& state)
{
    const auto parsed = parseState (state);
    return parsed ? parsed->formula : std::string ("a+b+c");
}

namespace
{
std::atomic<double> mouseXValue { 0.0 }, mouseYValue { 0.0 };

double systemDate() noexcept
{
    // days since 1899-12-30 in local time (Delphi's TDateTime, FL's Date)
    const auto now = std::time (nullptr);
    const auto* g = std::gmtime (&now);
    if (g == nullptr)
        return (double) now / 86400.0 + 25569.0;
    auto utc = *g;
    utc.tm_isdst = -1;
    const auto offset = std::difftime (now, std::mktime (&utc)); // local − UTC, in seconds
    return ((double) now + offset) / 86400.0 + 25569.0;
}

class FormulaEffect final : public Effect
{
public:
    explicit FormulaEffect (std::shared_ptr<const Program> p) : program (std::move (p)) {}

    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void setSongPosition (double ticks) noexcept override { position = ticks; }

    void process (float*, float*, int, const EffectParams& p, double bpm) noexcept override
    {
        if (program == nullptr)
        {
            out = 0.0f;
            return;
        }
        Inputs in;
        in.a = p[0];
        in.b = p[1];
        in.c = p[2];
        in.songTime = std::floor (position) / (double) ppq; // FL counts whole ticks
        in.tempo = bpm;
        in.timeMs = (double) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
        in.date = program->usesDate() ? systemDate() : 0.0;
        in.mouseX = mouseXValue.load (std::memory_order_relaxed);
        in.mouseY = mouseYValue.load (std::memory_order_relaxed);
        in.randSeed = &seed;
        const auto v = program->evaluate (in);
        out = std::isfinite (v) ? (float) std::clamp (v, 0.0, 1.0) : 0.0f;
    }

    float controlOutput (int) const noexcept override { return out; }

private:
    std::shared_ptr<const Program> program;
    double position = 0.0;
    std::uint32_t seed = 0x2545F491u;
    float out = 0.0f;
};
} // namespace

void setMousePosition (double x, double y) noexcept
{
    mouseXValue.store (x, std::memory_order_relaxed);
    mouseYValue.store (y, std::memory_order_relaxed);
}

std::unique_ptr<Effect> makeEffect (const std::vector<std::uint8_t>& state)
{
    return std::make_unique<FormulaEffect> (Program::compile (formulaOf (state)));
}
} // namespace lynx::formula
