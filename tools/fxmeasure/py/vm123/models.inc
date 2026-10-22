// ---- SSL-style channel compressor (round 123 lab) ----
// p: 0 Toff, 1 knee, 2 ratioScale, 3 ratioInf, 4 detAtt ms, 5 detRel ms, 6 grAttFast ms, 7 grAttSlow ms, 8 relScale, 9 makeup dB, 10 feedback(0/1), 11 rms(0/1)
static void sslComp (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double thr = kn (k, "thr", 0), ratio = kn (k, "ratio", 1), rel = kn (k, "rel", 0.4); const bool fast = kn (k, "fast", 0) > 0.5;
    const double slope = ratio > 1000 ? p[3] : (ratio - 1.0) * p[2], K = std::max (0.1, p[1]);
    const double dA = coefMs (p[4]), dR = coefMs (p[5]);
    const double gA = coefMs (fast ? p[6] : p[7]), gR = coefMs (rel * 1000.0 * p[8]);
    const bool fb = p[10] > 0.5, rms = p[11] > 0.5;
    double env = 0, gr = 0; double yl = 0, yr = 0; const double M = kn (k, "mk", p[9]);
    for (size_t i = 0; i < l.size(); ++i)
    {
        double s = fb ? std::max (std::abs (yl), std::abs (yr)) : std::max (std::abs ((double) l[i]), std::abs ((double) r[i]));
        if (rms) s = s * s;
        env += (s > env ? dA : dR) * (s - env);
        const double lev = rms ? 10.0 * std::log10 (std::max (env, 1e-24)) : db (env);
        const double o = lev - (thr + p[0]);
        const double target = slope * (o > 30 * K ? o : K * std::log1p (std::exp (o / K)));
        gr += (target > gr ? gA : gR) * (target - gr);
        const double g = undb (-gr);
        yl = l[i] * g; yr = r[i] * g;
        l[i] = (float) (yl * undb (M)); r[i] = (float) (yr * undb (M));
    }
}
// ssl2: feedback, linear-domain detector with attack (fast / auto) and release (knob), static soft knee, no dB smoothing
// p: 0 Toff, 1 knee, 2 ratioScale, 3 slopeInf, 4 attFast ms, 5 attSlow ms, 6 relScale, 7 rms(0/1), 8 grSmooth ms
static void sslComp2 (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double thr = kn (k, "thr", 0), ratio = kn (k, "ratio", 1), rel = kn (k, "rel", 0.4); const bool fast = kn (k, "fast", 0) > 0.5;
    const double slope = ratio > 1000 ? p[3] : (ratio - 1.0) * p[2], K = std::max (0.1, p[1]);
    const double aA = coefMs (fast ? p[4] : p[5]), aR = coefMs (rel * 1000.0 * p[6]), sm = coefMs (p[8]);
    const bool rms = p[7] > 0.5; const double M = kn (k, "mk", 0);
    double env = 0, yl = 0, yr = 0, gr = 0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        double s = std::max (std::abs (yl), std::abs (yr)); if (rms) s *= s;
        env += (s > env ? aA : aR) * (s - env);
        const double lev = rms ? 10.0 * std::log10 (std::max (env, 1e-24)) : db (env);
        const double o = lev - (thr + p[0]);
        const double target = slope * (o > 30 * K ? o : K * std::log1p (std::exp (o / K)));
        gr += sm * (target - gr);
        const double g = undb (-gr);
        yl = l[i] * g; yr = r[i] * g;
        l[i] = (float) (yl * undb (M)); r[i] = (float) (yr * undb (M));
    }
}
// rc: feed-forward peak compressor with look-ahead (RComp-like). knobs thr, ratio, att (ms), rel (ms), arc (0/1), electro(1)/opto(0), mk
// p: 0 Toff, 1 knee W, 2 ratioScale, 3 attScale, 4 relScale, 5 relBelow3 (factor), 6 relAbove3 (factor), 7 detRel ms, 8 lookahead samples, 9 split dB (3)
static void rcomp (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double T = kn (k, "thr", -15) + p[0], R = 1.0 + (kn (k, "ratio", 3) - 1.0) * p[2], W = std::max (0.1, p[1]);
    const double aA = coefMs (kn (k, "att", 0.5) * p[3]);
    const double relMs = kn (k, "rel", 160) * p[4];
    const double dR = coefMs (p[7]);
    const int la = (int) std::lround (p[8]); const double M = kn (k, "mk", 0);
    std::vector<float> dl (l.size() + (size_t) la, 0.0f), dr (r.size() + (size_t) la, 0.0f);
    double env = 0, gr = 0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        const double s = std::max (std::abs ((double) l[i]), std::abs ((double) r[i]));
        env = s > env ? s : env + dR * (s - env);
        const double x = db (env), o = x - T;
        double y;
        if (2 * o < -W) y = x; else if (2 * std::abs (o) <= W) y = x + (1 / R - 1) * (o + W / 2) * (o + W / 2) / (2 * W); else y = T + o / R;
        const double target = x - y;
        double rc = coefMs (relMs * (gr < p[9] ? p[5] : p[6]));
        gr += (target > gr ? aA : rc) * (target - gr);
        const double g = undb (M - gr);
        dl[i + (size_t) la] = (float) (l[i]); dr[i + (size_t) la] = (float) (r[i]);
        l[i] = (float) (dl[i] * g); r[i] = (float) (dr[i] * g);
    }
}
// rc2: two-stage gain smoothing (fast path + slow "program" path with GR-dependent dB-linear release)
// p: 0 Toff, 1 W, 2 ratioScale, 3 attScale, 4 fastRel ms, 5 slowCharge ms, 6 rate0 dB/s (x 160/rel), 7 r1, 8 r2, 9 lookahead, 10 detRel ms, 11 slowAtt mode(0 from GRf,1 from target)
static void rcomp2 (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double T = kn (k, "thr", -15) + p[0], R = 1.0 + (kn (k, "ratio", 3) - 1.0) * p[2], W = std::max (0.1, p[1]);
    const double aA = coefMs (kn (k, "att", 0.5) * p[3]), fR = coefMs (p[4]), sC = coefMs (p[5]);
    const double relK = 160.0 / kn (k, "rel", 160), dt = 1.0 / 48000.0;
    const int la = (int) std::lround (p[9]); const double M = kn (k, "mk", 0), dR = coefMs (p[10]);
    std::vector<float> dl (l.size() + (size_t) la, 0.0f), dr (r.size() + (size_t) la, 0.0f);
    double grf = 0, grs = 0, env = 0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        const double ex = p.size() > 14 ? p[14] : 1.0;
        const double s = std::pow (std::max (std::abs ((double) l[i]), std::abs ((double) r[i])), ex);
        const double dA = p.size() > 13 ? coefMs (p[13]) : 1.0;
        env += (s > env ? dA : dR) * (s - env);
        const double x = db (env) / ex, o = x - T;
        double y;
        if (2 * o < -W) y = x; else if (2 * std::abs (o) <= W) y = x + (1 / R - 1) * (o + W / 2) * (o + W / 2) / (2 * W); else y = T + o / R;
        const double target = std::max (0.0, x - y);
        grf += (target > grf ? aA : fR) * (target - grf);
        const double src = p[11] > 0.5 ? target : grf;
        if (src > grs) grs += sC * (src - grs);
        else if (p.size() > 12 && p[12] > 0.5) { double gs = undb (-grs); gs += p[6] * relK * (1.0 + p[7] * (1.0 - gs)) * dt; grs = gs >= 1.0 ? 0.0 : -db (gs); if (grs < src) grs = src; }
        else { grs -= p[6] * relK * (1.0 + p[7] / (grs + std::max (0.01, p[8]))) * dt; if (grs < src) grs = src; }
        const double gr = std::max (grf, grs);
        const double g = undb (M - gr);
        dl[i + (size_t) la] = l[i]; dr[i + (size_t) la] = r[i];
        l[i] = (float) (dl[i] * g); r[i] = (float) (dr[i] * g);
    }
}
// rc3: Hilbert-envelope detector (analytic magnitude, 129-tap FIR, group delay 64) + two-stage smoothing as rc2
// p: same as rc2 (0..12), 13 detAtt ms, 14 window (0 hann,1 blackman), 15 detector mode (0 max L/R, 1 sum power)
static std::vector<double> hilbertTaps (int N, int win)
{
    std::vector<double> h ((size_t) N, 0.0); const int c = N / 2;
    for (int n = 0; n < N; ++n)
    {
        const int k = n - c; if (k == 0 || (k & 1) == 0) continue;
        double w = win == 1 ? 0.42 - 0.5 * std::cos (2 * M_PI * n / (N - 1)) + 0.08 * std::cos (4 * M_PI * n / (N - 1)) : 0.5 - 0.5 * std::cos (2 * M_PI * n / (N - 1));
        h[(size_t) n] = 2.0 / (M_PI * k) * w;
    }
    return h;
}
static void rcomp3 (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double T = kn (k, "thr", -15) + p[0], R = 1.0 + (kn (k, "ratio", 3) - 1.0) * p[2], W = std::max (0.1, p[1]);
    const double aA = coefMs (kn (k, "att", 0.5) * p[3]), fR = coefMs (p[4]), sC = coefMs (p[5]);
    const double relK = 160.0 / kn (k, "rel", 160), dt = 1.0 / 48000.0;
    const double M = kn (k, "mk", 0), dR = coefMs (p[10]), dA = coefMs (p[13]);
    const int N = 129, c = 64; static std::vector<double> h; if (h.empty()) h = hilbertTaps (N, (int) p[14]);
    const size_t n = l.size(); std::vector<float> L = l, Rr = r;
    double grf = 0, grs = 0, env = 0;
    for (size_t i = 0; i < n; ++i)
    {
        double hl = 0, hr = 0;
        for (int t = 0; t < N; t += 1) { if (h[(size_t) t] == 0.0) continue; const long j = (long) i - t; if (j < 0) break; hl += h[(size_t) t] * L[(size_t) j]; hr += h[(size_t) t] * Rr[(size_t) j]; }
        const double xl = i >= (size_t) c ? L[i - c] : 0.0, xr = i >= (size_t) c ? Rr[i - c] : 0.0;
        const double el = xl * xl + hl * hl, er = xr * xr + hr * hr;
        const double s = std::sqrt (p[15] > 0.5 ? 0.5 * (el + er) : std::max (el, er));
        env += (s > env ? dA : dR) * (s - env);
        const double x = db (env), o = x - T;
        double y;
        if (2 * o < -W) y = x; else if (2 * std::abs (o) <= W) y = x + (1 / R - 1) * (o + W / 2) * (o + W / 2) / (2 * W); else y = T + o / R;
        const double target = std::max (0.0, x - y);
        grf += (target > grf ? aA : fR) * (target - grf);
        const double src = grf;
        if (src > grs) grs += sC * (src - grs);
        else { double gs = undb (-grs); gs += p[6] * relK * (1.0 + p[7] * (1.0 - gs)) * dt; grs = gs >= 1.0 ? 0.0 : -db (gs); if (grs < src) grs = src; }
        const double gr = std::max (grf, grs);
        const double g = undb (M - gr);
        l[i] = (float) (xl * g); r[i] = (float) (xr * g);
    }
}
// de: split de-esser. knobs freq, thr, bp(0/1), wide(0/1)
// p: 0 Toff, 1 ratio, 2 knee, 3 att ms, 4 rel ms, 5 split ratio (fs = freq*p5), 6 sc order (2/4), 7 sc freq scale, 8 detector (0 power,1 peak), 9 split order(1/2)
struct Bq { double b0=1,b1=0,b2=0,a1=0,a2=0, z1[2]={0,0}, z2[2]={0,0};
  double run (double x, int c) { double y = b0*x + z1[c]; z1[c] = b1*x - a1*y + z2[c]; z2[c] = b2*x - a2*y; return y; } };
static Bq rbj (int type, double f, double q, double sr = 48000)
{
    Bq b; const double w = 2 * M_PI * std::min (f, sr * 0.49) / sr, cw = std::cos (w), sw = std::sin (w), al = sw / (2 * q);
    double b0, b1, b2, a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
    if (type == 0) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; }        // LP
    else if (type == 1) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; } // HP
    else { b0 = al; b1 = 0; b2 = -al; }                                 // BP (0 dB peak)
    b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0; return b;
}
static Bq onePole (bool hp, double f, double sr = 48000)
{
    Bq b; const double k = std::tan (M_PI * f / sr);
    if (hp) { b.b0 = 1 / (1 + k); b.b1 = -b.b0; } else { b.b0 = k / (1 + k); b.b1 = b.b0; }
    b.a1 = (k - 1) / (k + 1); return b;
}
static void deess (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double f = kn (k, "freq", 5506), T = kn (k, "thr", -24.2) + p[0], R = std::max (1.0, p[1]), K = std::max (0.01, p[2]);
    const bool bp = kn (k, "bp", 0) > 0.5, wide = kn (k, "wide", 0) > 0.5;
    const double aA = coefMs (p[3]), aR = coefMs (p[4]);
    Bq s1 = bp ? rbj (2, f * p[7], 2.0) : rbj (1, f * p[7], 0.7071), s2 = s1;
    const bool sc4 = p[6] > 3;
    Bq sp = p[9] > 1.5 ? rbj (0, f * p[5], 0.7071) : onePole (false, f * p[5]);
    double env = 0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        double a = s1.run (l[i], 0), b = s1.run (r[i], 1);
        if (sc4) { a = s2.run (a, 0); b = s2.run (b, 1); }
        const double sv = p[8] > 0.5 ? std::max (std::abs (a), std::abs (b)) : 0.5 * (a * a + b * b);
        env += (sv > env ? aA : aR) * (sv - env);
        const double lev = p[8] > 0.5 ? db (env) : 10 * std::log10 (std::max (env, 1e-24));
        const double o = lev - T;
        const double c = (1 - 1 / R) * (o > 30 * K ? o : K * std::log1p (std::exp (o / K)));
        const double g = undb (-c);
        if (wide) { l[i] = (float) (l[i] * g); r[i] = (float) (r[i] * g); }
        else
        {
            const double lo = sp.run (l[i], 0), ro = sp.run (r[i], 1);
            l[i] = (float) (lo + (l[i] - lo) * g); r[i] = (float) (ro + (r[i] - ro) * g);
        }
    }
}
// cla: opto feedback leveler. knobs mk (makeup dB, else p0)
// p: 0 makeup dB, 1 y0 dB, 2 gamma, 3 tauA0 ms, 4 beta, 5 tauRf ms, 6 tauSa ms, 7 tauRs ms, 8 w slow, 9 fz Hz, 10 emph f Hz, 11 emph dB, 12 h2, 13 h3, 14 hp Hz, 15 tauEl ms, 16 mem gain, 17 tauMem s
static void cla2a (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double dt = 1.0 / 48000.0, sr = 48000.0;
    const double mk = undb (kn (k, "mk", p[0]) + kn (k, "dmk", 0)), y0 = undb (p[1] + kn (k, "dy0", 0)), gam = p[2];
    // HF emphasis in the side chain: first-order high shelf (bilinear) gain G above f
    const double G = undb (p[11]), wc = std::tan (M_PI * std::min (p[10], 20000.0) / sr);
    // H(s) = (1 + G s/wc)/(1 + s/wc) -> bilinear (s -> (1-z^-1)/(1+z^-1) with tan prewarp)
    const double eb0 = (1 + G / wc) / (1 + 1 / wc), eb1 = (1 - G / wc) / (1 + 1 / wc), ea1 = (1 - 1 / wc) / (1 + 1 / wc);
    const double hpk = std::tan (M_PI * std::max (0.1, p[14]) / sr), hb0 = 1 / (1 + hpk), ha1 = (hpk - 1) / (hpk + 1);
    const double cEl = coefMs (p[15]), cRf = coefMs (p[5]), cSa = coefMs (p[6]);
    const double wz = 2 * sr * std::tan (M_PI * std::min (p[9], 23000.0) / sr); // analog zero (rad/s) prewarped
    double Cf = 0, Cs = 0, el = 0, mem = 0; double ex1[2] = {0,0}, ey1[2] = {0,0}, yc[2] = {0,0};
    double fx1[2] = {0,0}, fy1[2] = {0,0}, hx1[2] = {0,0}, hy1[2] = {0,0};
    const double cMem = std::exp (-dt / std::max (0.01, p[17]));
    for (size_t i = 0; i < l.size(); ++i)
    {
        // side chain: previous cell output, emphasis, rectify, EL smoothing
        double sm = 0;
        for (int c = 0; c < 2; ++c) { const double e = eb0 * yc[c] + eb1 * ex1[c] - ea1 * ey1[c]; ex1[c] = yc[c]; ey1[c] = e; sm = std::max (sm, std::abs (e)); }
        el += (sm > el ? 1.0 : cEl) * (sm - el);
        const double Ct = std::pow (std::max (0.0, el / y0 - (p.size() > 18 ? p[18] : 0.0)), gam);
        const double C = (1 - p[8]) * Cf + p[8] * Cs;
        if (Ct > Cf) Cf += (1 - std::exp (-dt * 1000.0 / (p[3] / std::pow (1 + Cf, p[4])))) * (Ct - Cf); else Cf += cRf * (Ct - Cf);
        const double tRs = p[7] * (1 + p[16] * mem);
        if (Cf > Cs) Cs += cSa * (Cf - Cs); else Cs += (1 - std::exp (-dt * 1000.0 / tRs)) * (Cf - Cs);
        mem = mem * cMem + (1 - cMem) * std::min (1.0, C);
        const double g = 1.0 / (1.0 + C);
        // cell with HF bypass: H(s) = g (1 + s/wz) / (1 + s g/wz), bilinear K = 2 sr
        const double K = 2 * sr, A0 = 1 + K * g / wz, b0 = g * (1 + K / wz) / A0, b1 = g * (1 - K / wz) / A0, a1 = (1 - K * g / wz) / A0;
        float* ch[2] = { &l[i], &r[i] };
        for (int c = 0; c < 2; ++c)
        {
            const double x = *ch[c];
            const double y = b0 * x + b1 * fx1[c] - a1 * fy1[c]; fx1[c] = x; fy1[c] = y; yc[c] = y;
            double o = y * mk; o = o + p[12] * o * o + p[13] * o * o * o;
            const double h = hb0 * (o - hx1[c]) - ha1 * hy1[c]; hx1[c] = o; hy1[c] = h;
            *ch[c] = (float) h;
        }
    }
}
// ssl3: like ssl (feedback, dB smoothing) with a two-part release: part a releases with tau1 (ms), the rest with the knob
static void sslComp3 (std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    const double thr = kn (k, "thr", 0), ratio = kn (k, "ratio", 1), rel = kn (k, "rel", 0.4); const bool fast = kn (k, "fast", 0) > 0.5;
    const double slope = ratio > 1000 ? p[3] : (ratio - 1.0) * p[2], K = std::max (0.1, p[1]);
    const double dA = coefMs (p[4]), dR = coefMs (p[5]);
    const double gA = coefMs (fast ? p[6] : p[7]), gR = coefMs (rel * 1000.0 * p[8]), gR1 = coefMs (p[13]);
    const double a = std::clamp (p[12], 0.0, 1.0);
    double env = 0, g1 = 0, g2 = 0; double yl = 0, yr = 0; const double M = kn (k, "mk", p[9]);
    for (size_t i = 0; i < l.size(); ++i)
    {
        double s = std::max (std::abs (yl), std::abs (yr));
        env += (s > env ? dA : dR) * (s - env);
        const double o = db (env) - (thr + p[0]);
        const double target = slope * (o > 30 * K ? o : K * std::log1p (std::exp (o / K)));
        g1 += (target > g1 ? gA : gR1) * (target - g1);
        g2 += (target > g2 ? gA : gR) * (target - g2);
        const double gr = a * g1 + (1 - a) * g2;
        const double g = undb (-gr);
        yl = l[i] * g; yr = r[i] * g;
        l[i] = (float) (yl * undb (M)); r[i] = (float) (yr * undb (M));
    }
}
static void runModel (const std::string& m, std::vector<float>& l, std::vector<float>& r, const Knobs& k, const P& p)
{
    if (m == "ssl") sslComp (l, r, k, p);
    if (m == "ssl2") sslComp2 (l, r, k, p);
    if (m == "ssl3") sslComp3 (l, r, k, p);
    if (m == "rc") rcomp (l, r, k, p);
    if (m == "rc2") rcomp2 (l, r, k, p);
    if (m == "rc3") rcomp3 (l, r, k, p);
    if (m == "de") deess (l, r, k, p);
    if (m == "cla") cla2a (l, r, k, p);
}
