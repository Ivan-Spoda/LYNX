#pragma once

// FL's Fruity Formula Controller (docs/analise/REA_rodada133.md §3): a mixer "controller" effect whose output (0..1)
// is a formula of its three knobs a, b, c and of the song position, linked to any parameter like the Peak
// Controller. The expression language is FL's (its plugin embeds an expression parser): the operators, their
// precedence and every function below follow the plugin's own definitions [DECOMP], case-insensitive.
//
//   precedence (tightest first): unary - + and postfix ! % · ^ · * / div mod fmod · + - · > >= <= < <> = in · not ·
//                                or and xor shr shl
//   comparisons give 1/0 (= and <> with a 1e-30 tolerance); not/and/or/xor/shl/shr work on Round()ed integers;
//   Round is the banker's rounding of FL's compiler; If(a, b, c) = Round(a) <> 0 ? b : c; Case(a, b, c) = a = 1 ? b : c.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lynx
{
class Effect;
}

namespace lynx::formula
{
// What a formula can read besides its knobs.
struct Inputs
{
    double a = 0.0, b = 0.0, c = 0.0;
    double songTime = 0.0; // song position in quarter notes
    double tempo = 120.0;  // GetTempo
    double timeMs = 0.0;   // Time: system time in ms
    double date = 0.0;     // Date: days since 1899-12-30 (fraction = time of day)
    double mouseX = 0.0, mouseY = 0.0; // 0..1 across the screen
    std::uint32_t* randSeed = nullptr; // Rand's generator state (nullptr = a fixed sequence)
};

// FL's SeededRand: the "psdes" hash of the value's 64 bits, as a float in [0, 1).
float seededRand (double seed) noexcept;
// FL's Tension(value, rate).
double tension (double value, double rate) noexcept;
// FL's Fold (folds a signal back into 0..1) and Warp(a, b).
double fold (double value) noexcept;
double warp (double value, double amount) noexcept;
// The banker's rounding of FL's compiler (0.5 → 0, 1.5 → 2).
double roundHalfEven (double v) noexcept;

class Program
{
public:
    // nullptr and the message ("Unknown identifier 'x'", "Missing )"…) when the formula doesn't compile.
    static std::shared_ptr<const Program> compile (const std::string& text, std::string* error = nullptr);

    double evaluate (const Inputs& in) const noexcept;
    // Whether it reads the song position / the clock (its output moves on its own).
    bool usesTime() const noexcept { return timed; }
    bool usesDate() const noexcept { return dated; }

    struct Node
    {
        int op = 0;
        double value = 0.0;
        int args[3] { -1, -1, -1 };
    };

private:
    std::vector<Node> nodes;
    int root = -1;
    bool timed = false, dated = false;
    double eval (int node, const Inputs& in) const noexcept;
    friend struct Compiler;
};

// FL's saved state (the plugin's 0xD5 record): i32 version, 3 × i32 knobs a, b, c (0…65536), the formula as a
// short string (u8 length + ANSI), then the description (v ≤ 2: i32 length + RTF; v ≥ 4: plain UTF-16 text).
struct State
{
    int version = 2;
    float a = 0.0f, b = 0.0f, c = 0.0f; // 0..1
    std::string formula = "a+b+c";
};
std::optional<State> parseState (const std::vector<std::uint8_t>& data);
// The state LYNX saves (version 2, no description) — what FL's own reader takes.
std::vector<std::uint8_t> writeState (const State& state);
// The formula of a slot's state ("a+b+c" when it has none, as FL's default preset).
std::string formulaOf (const std::vector<std::uint8_t>& state);

// The mixer effect: audio passes through; output 0 = clamp (formula (params[0..2] = a, b, c), 0, 1), evaluated once
// per block. A formula that doesn't compile outputs 0 (FL: "make sure you pressed Compile").
std::unique_ptr<Effect> makeEffect (const std::vector<std::uint8_t>& state);

// The mouse position the formulas see (MouseX / MouseY, 0..1 of the screen); set by the UI.
void setMousePosition (double x, double y) noexcept;
} // namespace lynx::formula
