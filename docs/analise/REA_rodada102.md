# Round 102 — the Linux CI ("Tests" workflow) back to green

The GitHub Actions workflow `.github/workflows/tests.yml` (Ubuntu 24.04, `scripts/build-local.sh` with the system
g++ 13.3 and clang++ 18.1, glibc 2.39, no FL Data dir) had failed on every push, while the local build (Zig/clang,
glibc 2.43) passed. No FL binary was studied in this round; nothing in the engines changed.

## 1. Failures and root causes [EXEC]

Reproduced in WSL by building the test binary with the system g++ (per-file objects, `tmp/r102/bt.sh`) and running it
under Ubuntu 24.04's own glibc 2.39 (`libc6_2.39-0ubuntu8.9` unpacked, run through its `ld-linux-x86-64.so.2
--library-path`; the binary was linked with `.symver` pins to the 2.2.5 versions of `sqrtf`/`atan2f`/`log10f`), plus a
draft PR run with diagnostic prints.

| Test (line before the fix) | Compilers | Cause | Fix |
|---|---|---|---|
| `morphine_engine` golden samples (:12544), 7 failures | both | The test fixture `morphineTestState()` built its spectra with `std::cbrt (float)` = `cbrtf`. glibc 2.43 has a correctly rounded `cbrtf` (CORE-MATH); glibc 2.39's differs in the last bit for ~14 % of the inputs, so the *input state* differed and the output drifted by up to 7e-5. Engine and goldens were right. | Cube root in double, rounded to float: identical to the correctly rounded value with both glibc versions (checked on all inputs the fixture uses). |
| `fruity_parametric_eq_exact` linear-phase goldens (:11081), 2–6 failures | g++ 13 only | Undefined behaviour in the test: `const std::initializer_list<Gold> gold = hq ? std::initializer_list<Gold> {…} : std::initializer_list<Gold> {…};` keeps a pointer to a temporary backing array that dies at the end of the declaration; GCC 13 reused that stack slot (the log showed "golden" indices like −900 and 40000 and `-Wdangling-pointer` warnings). The engine's real error is ≤ 3e-7. | Two named `static const Gold[]` arrays chosen with `?:` (an lvalue of array type, no temporary). |
| `fl_sampler_interpolators_match_fl` (:2487), 3 failures, intermittent | either | The sinc kernels take 1/x from `RCPPS` + one Newton step, as FLEngine does. RCPPS is implementation-defined (Intel's and AMD's tables differ); the goldens are FL's own output on the capture machine (AMD Zen 2). The job fails only when GitHub schedules it on a CPU with a different RCPPS table. | The test hashes RCPPS over all mantissas in [1, 2); on a CPU whose table matches the capture machine's the sinc cases stay bit-exact, elsewhere they must be within 5e-7 (a simulated different estimate moved them by ≤ 1.2e-7). The linear and 6-point cases stay bit-exact everywhere. |

Double-precision libm (`sin`, `cos`, `sincos`, `sinh`, `pow`, `log`, `atan2`, `exp`, `tanh`, `cosh`, `tan`, `atan`)
gives identical bits in glibc 2.39 and 2.43 on 2–3 M random arguments [EXEC]. Float functions that differ between
the two: `tanf`, `tanhf`, `expm1f`, `cbrtf`, `atan2f`, `log10f` [EXEC] — engine code that needs bits across platforms
should not use them (none of the currently bit-exact tests do after this fix).

## 2. Where I stopped

Fixes in `tests/tests.cpp` only. Verified: local g++ 15 / glibc 2.43, g++ 15 / glibc 2.39, build-local (Zig) with the
FL Data dir, and the PR's CI run on Ubuntu g++ 13 and clang++ 18. The release workflow does not run the tests and is
untouched.
