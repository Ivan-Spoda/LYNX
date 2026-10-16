#pragma once

// Double-precision exp, ln and sin/cos with the exact results of the Delphi (Win64) run-time library that FL Studio's
// Delphi plugins call (docs/analise/REA_rodada70.md §2). The C library's functions differ from it in the last bit for a
// few per cent of arguments; engines that must match a plugin bit for bit use these instead. Clean-room: the
// algorithms were observed in the plugin binaries and rewritten here; the polynomial coefficients are numbers read
// from them, the exp table is recomputed (e^(j/64) − 1 to 106 bits).

namespace lynx::dmath
{
double exp (double x) noexcept;
double ln (double x) noexcept;
void sinCos (double x, double& s, double& c) noexcept; // exact for |x| < 5π/4, C library beyond
double sinh (double x) noexcept;                        // (exp(x) − exp(−x)) / 2, and 0 for |x| ≤ 1e−12
} // namespace lynx::dmath
