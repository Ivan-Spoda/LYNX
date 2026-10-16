#pragma once

// The double-precision math of the Delphi (Win64) run-time library that FL Studio's Delphi plugins call (Exp, Ln,
// SinCos / Sin / Cos, the exp-minus-one behind Tanh, and Power / Sinh / Tanh on top of them), with the plugins' exact
// results. The C library's functions differ from it in the last bit for a few per cent of arguments; engines that must
// match a plugin bit for bit (Fruity Vocoder, Love Philter) use these instead, so their output does not depend on the
// platform's libm.
//
// One replica for every plugin (docs/analise/REA_rodada78.md §2 merged the round-66 and round-70 copies; the routines
// themselves are described in REA_rodada66.md §7 and REA_rodada70.md §2). Clean-room: the algorithms were observed in
// the plugin binaries and rewritten here; the polynomial coefficients are numbers read from them, the exp table is
// recomputed (e^(j/64) − 1 to 106 bits). Only arguments the run-time itself rejects or that lie beyond the handled
// ranges (trigonometric |x| ≥ 4194304) fall back to the C library.

namespace lynx::dmath
{
double exp (double x) noexcept;   // exact over the whole range, incl. overflow and subnormal results
double expm1 (double x) noexcept; // exp(x) − 1: own kernel for |x| < ln 2, exp − 1 beyond
double ln (double x) noexcept;    // exact for every positive argument, incl. subnormals
void sinCos (double x, double& s, double& c) noexcept; // exact for |x| < 4194304, C library beyond
double sin (double x) noexcept;
double cos (double x) noexcept;
double intPower (double x, long long n) noexcept; // repeated squaring (1/r for n < 0)
double power (double x, double y) noexcept;       // integral y by intPower, otherwise exp(y·ln x)
double sinh (double x) noexcept;                   // (exp(x) − exp(−x)) / 2, and 0 for |x| ≤ 1e−12
double tanh (double x) noexcept;
} // namespace lynx::dmath
