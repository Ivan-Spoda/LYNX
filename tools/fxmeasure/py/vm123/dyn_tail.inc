
struct CompLaw
{
    double thresholdOffset, knee, slope, attackFastMs, attackSlowMs, releaseMs, release1Ms, release1Fraction, detectorAttackMs, detectorReleaseMs, makeup;
};

double knobInterp (const double* xs, const double* ys, int n, double x) noexcept
{
    if (x <= xs[0]) return ys[0];
    for (int i = 0; i < n - 1; ++i)
        if (x <= xs[i + 1])
            return ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys[n - 1];
}

CompLaw compLaw (const Settings& s) noexcept
{
    CompLaw c {};
    const double r = std::max (1.0, s.compRatio);
    const double u = r > 1e6 ? 1.0 : 1.0 - 1.0 / r;
    const double thr = std::clamp (roundTo (s.compThreshold, 0.1), -20.0, 10.0);
    // bilinear interpolation on the grid (thresholds descend, u ascends); below ratio 1.2 the slope and make-up
    // fade to 0 at 1:1 (the console's make-up follows the compression it is set up for)
    int ti = 0;
    constexpr int nT = (int) std::size (compThr), nU = (int) std::size (compU);
    while (ti < nT - 2 && thr < compThr[ti + 1]) ++ti;
    const double tt = std::clamp ((compThr[ti] - thr) / (compThr[ti] - compThr[ti + 1]), 0.0, 1.0);
    double v[6];
    const double uu = std::max (u, compU[0]);
    int ui = 0;
    while (ui < nU - 2 && uu > compU[ui + 1]) ++ui;
    const double tu = std::clamp ((uu - compU[ui]) / (compU[ui + 1] - compU[ui]), 0.0, 1.0);
    for (int k = 0; k < 6; ++k)
    {
        auto at = [&] (int a, int b) { return compGrid[(a * nU + b) * 6 + k]; };
        v[k] = (1 - tt) * ((1 - tu) * at (ti, ui) + tu * at (ti, ui + 1)) + tt * ((1 - tu) * at (ti + 1, ui) + tu * at (ti + 1, ui + 1));
    }
    const double fade = u < compU[0] ? u / compU[0] : 1.0;
    c.thresholdOffset = v[0];
    c.knee = std::max (0.2, v[1]);
    c.slope = v[2] * fade;
    c.makeup = v[5] * fade;
    c.attackFastMs = v[3];
    const double rel = std::clamp (s.compRelease, 0.1, 4.0);
    const double relScale = v[4] / refReleaseFactor;
    // two-part release (measured): a fraction releases with a short time constant, the rest with the knob's
    c.releaseMs = (s.compFast ? knobInterp (relKnob, relSlowFast, 7, rel) : knobInterp (relKnob, relSlowSlow, 7, rel)) * relScale;
    c.release1Ms = (s.compFast ? knobInterp (relKnob, relTau1Fast, 7, rel) : knobInterp (relKnob, relTau1Slow, 7, rel)) * relScale;
    c.release1Fraction = s.compFast ? knobInterp (relKnob, relFracFast, 7, rel) : knobInterp (relKnob, relFracSlow, 7, rel);
    c.attackSlowMs = knobInterp (relKnob, attSlowMs, 7, rel) * v[3] / refAttackFast;
    c.detectorAttackMs = 0.021;
    c.detectorReleaseMs = 0.064;
    return c;
}

struct GateLaw
{
    double range, threshold, attackMs, releaseMs, detectorReleaseMs;
    bool gate;
    // reduction (dB, >= 0) for a detector level in dBFS
    double reduction (double levelDb) const noexcept
    {
        if (gate)
            return levelDb < threshold ? range : 0.0;
        return std::clamp (threshold - levelDb, 0.0, range);
    }
};

GateLaw gateLaw (const Settings& s) noexcept
{
    GateLaw g {};
    g.range = s.expRange;
    g.threshold = s.expThreshold - 16.0;
    g.attackMs = s.expFast ? 0.1 : 1.5;
    g.releaseMs = std::max (0.1, s.expRelease) * 1000.0 * 0.25;
    g.detectorReleaseMs = 30.0;
    g.gate = s.gate;
    return g;
}
