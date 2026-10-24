// Round 138 (vox138): Auto-Tune Pro measured on the official trial before it expires (docs/analise/REA_rodada138.md)

namespace vox138
{
// an Auto-Tune Pro component state like subst113::autoTuneState, saved by a newer version (<ModernScales> rows)
std::vector<std::uint8_t> modernState (int key, int scale, const char* minorRemove = "010010100101")
{
    const auto xml = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?> <SessionChunkData IsDefaultAAXChunk=\"0\"><ChunkForVersion ChunkVersion=\"11.0.0\">")
                     + "<Parameters ParamsData=\"4.....\" IsUnencrypted=\"1\"/><Custom RAND_ID=\"6334\" ARAProcessorID=\"0\" ScaleGivenKey=\""
                     + std::to_string (key) + "\" ScaleGivenScale=\"" + std::to_string (scale)
                     + "\" ScaleGivenDetune=\"0\"><GraphModeData/><ModernScales>"
                     + "<Scale ScaleIndex=\"0\" Length=\"12\"><Bypass Values=\"000000000000\"/><Remove Values=\"000000000000\"/></Scale>"
                     + "<Scale ScaleIndex=\"1\" Length=\"12\"><Bypass Values=\"000000000000\"/><Remove Values=\"010100101010\"/></Scale>"
                     + "<Scale ScaleIndex=\"2\" Length=\"12\"><Bypass Values=\"000000000000\"/><Remove Values=\"" + minorRemove + "\"/></Scale>"
                     + "</ModernScales></Custom></ChunkForVersion></SessionChunkData>";
    std::vector<std::uint8_t> c { 'V', 'C', '2', '!' };
    const auto size = (std::uint32_t) xml.size();
    for (int i = 0; i < 4; ++i)
        c.push_back ((std::uint8_t) (size >> (8 * i)));
    c.insert (c.end(), xml.begin(), xml.end());
    return c;
}
} // namespace vox138

TEST (auto_tune_pro_modern_scale_states_round138)
{
    using namespace lynx;
    // [EXEC] her states saved with <ModernScales> load into the official trial with the MODERN list's scale: "2" = Minor
    // (42 states, read as Chromatic before), "1" = Major (1 state); the old-format states keep the classic list
    CHECK ((autoTuneProKey (vox138::modernState (0, 2)) == std::optional<std::pair<int, int>> ({ 0, (int) vocal::Scale::minor })));
    CHECK ((autoTuneProKey (vox138::modernState (1, 1)) == std::optional<std::pair<int, int>> ({ 1, (int) vocal::Scale::major })));
    CHECK ((autoTuneProKey (vox138::modernState (3, 0)) == std::optional<std::pair<int, int>> ({ 3, (int) vocal::Scale::chromatic })));
    CHECK ((autoTuneProKey (vox138::modernState (0, 3)) == std::optional<std::pair<int, int>> ({ 0, (int) vocal::Scale::harmonicMinor })));
    CHECK ((autoTuneProKey (vox138::modernState (0, 13)) == std::optional<std::pair<int, int>> ({ 0, (int) vocal::Scale::wholeTone })));
    CHECK (! autoTuneProKey (vox138::modernState (0, 15)));
    CHECK ((autoTuneProKey (subst113::autoTuneState (0, 2)) == std::optional<std::pair<int, int>> ({ 0, (int) vocal::Scale::chromatic })));

    // the substitute: C minor with the notes the modern row removes off (C# E F# A B), Eb minor on its own key
    const auto at = subst113::autoTune (vox138::modernState (0, 2));
    auto slot = subst113::pluginSlot (at);
    CHECK (substituteEffectSlot (slot, pluginIdentity (at)));
    CHECK (slot.kind == EffectKind::lynxTune);
    CHECK (lynxtune::scaleIndex (slot.params[lynxtune::scale]) == (int) vocal::Scale::minor);
    CHECK (lynxtune::keyIndex (slot.params[lynxtune::key]) == 0);
    for (int k : { 1, 4, 6, 9, 11 })
        CHECK (slot.params[(std::size_t) (lynxtune::noteC + k)] == 0.0f);
    for (int k : { 0, 2, 3, 5, 7, 8, 10 })
        CHECK (slot.params[(std::size_t) (lynxtune::noteC + k)] == 1.0f);
    // whole tone: LYNX Tune's "More scales"
    const auto wt = subst113::autoTune (vox138::modernState (2, 13));
    auto s2 = subst113::pluginSlot (wt);
    CHECK (substituteEffectSlot (s2, pluginIdentity (wt)));
    CHECK (lynxtune::effectiveScale (s2.params[lynxtune::scale], s2.params[lynxtune::moreScales]) == (int) vocal::Scale::wholeTone);

    // her real kwe state (when the round's extraction is on this machine): C minor, retune 5 ms, humanize 100, flex 20
    const std::filesystem::path f = "/mnt/d/lynx-work/vox138/at/states/kw0.bin";
    if (std::filesystem::exists (f))
    {
        std::ifstream in (f, std::ios::binary);
        const std::vector<std::uint8_t> st ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        CHECK ((autoTuneProKey (st) == std::optional<std::pair<int, int>> ({ 0, (int) vocal::Scale::minor })));
    }
}

TEST (lynx_tune_humanize_ramp_round138)
{
    using namespace lynx;
    // [EXEC] Auto-Tune Pro, retune 0, humanize 100: a note that moves 30 c after 0.8 s steady is pulled back over ~100 ms
    // (50 %), one that moves after 0.25 s at once; LYNX Tune's slow-down now sets in between 0.275 and 0.375 s
    auto run = [] (double holdSec) {
        auto slot = makeEffectSlot (EffectKind::lynxTune);
        slot.params[lynxtune::scale] = 0.0f; // chromatic
        slot.params[lynxtune::retune] = 0.0f;
        slot.params[lynxtune::humanize] = 1.0f;
        auto fx = makeEffect (slot);
        const double sr = 48000.0;
        fx->prepare (sr);
        const int n = (int) (sr * (holdSec + 0.6));
        std::vector<float> l ((size_t) n), r ((size_t) n);
        double ph = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto t = i / sr;
            const auto f = 440.0 * std::exp2 ((t < holdSec ? 0.0 : 0.3) / 12.0);
            ph += 2.0 * 3.141592653589793 * f / sr;
            float v = 0.0f;
            for (int k = 1; k <= 8; ++k)
                v += (float) (std::sin (k * ph) / k);
            l[(size_t) i] = r[(size_t) i] = 0.2f * v;
        }
        for (int i = 0; i < n; i += 256)
            fx->process (&l[(size_t) i], &r[(size_t) i], std::min (256, n - i), slot.params, 120.0);
        // the output pitch 40 ms after the move (latency removed): zero crossings over 10 ms
        const auto lat = fx->latencySamples();
        const auto at = (int) ((holdSec + 0.04) * sr) + lat;
        int zc = 0, first = -1, last = -1;
        for (int i = at; i < at + (int) (0.02 * sr); ++i)
            if (l[(size_t) i - 1] <= 0.0f && l[(size_t) i] > 0.0f) { ++zc; if (first < 0) first = i; last = i; }
        const auto hz = zc > 1 ? (zc - 1) * sr / (last - first) : 0.0;
        return 1200.0 * std::log2 (hz / 440.0); // cents above A4
    };
    const auto shortHold = run (0.25), longHold = run (0.8);
    CHECK (std::abs (shortHold) < 8.0); // already back on A4
    CHECK (longHold > 15.0);            // still mostly sharp (humanize slows the pull-back)
}

TEST (ember_classic_styles_fitted_round138)
{
    using namespace lynx;
    // the E / N / T / P styles carry their own fitted constants (no longer the struct's defaults), each with its measured
    // colour FIR; style A is unchanged
    const auto& a = emberclassic::styleModel (0);
    CHECK (std::abs (a.cPos - 0.539481) < 1e-6 && std::abs (a.gain0Db + 0.255358) < 1e-6);
    for (int k = 1; k < emberclassic::styleCount; ++k)
    {
        const auto& m = emberclassic::styleModel (k);
        CHECK (m.fir[0] != 1.0 && m.fir[1] != 0.0);
        CHECK (m.cPos != 1.0 && m.pPos != 2.0 && m.bias < 0.0);
        CHECK (std::abs (m.gainPerUnitDb - a.gainPerUnitDb) < 1e-9);
    }
}
