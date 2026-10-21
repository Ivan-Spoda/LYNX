// --- Round 120: Waves Tune Real-Time's saved scale; LYNX Tune's retune timing ---------------------------------------

namespace tune120
{
// a Waves Tune Real-Time component (Waves preset chunk) whose setups carry a <Scale> element: root and the twelve
// note statuses above it ("0" = a target, "x" = excluded), as the plugin saves them
std::vector<std::uint8_t> wavesTuneComponent (int rootA, const std::string& notesA, int rootB, const std::string& notesB,
                                              const std::string& active = "SETUP_A")
{
    auto scaleXml = [] (int root, const std::string& notes) {
        std::string s = "<PluginSpecific TagName=\"Scale\" MenuItemString=\"Scale\" TagID=\"0\" DataType=\"XMLObject\">\n<Data><![CDATA[<Scale Name=\"Natural Minor\">\n <NoteSegmentation>\n";
        for (int k = 0; k < 12; ++k)
            s += "  <Note NoteCents=\"" + std::to_string (100 * k) + "\" StatusIsForAllOctaves=\"true\">\n   <NoteStatus>"
                 + std::string (1, notes[(std::size_t) k]) + "</NoteStatus>\n  </Note>\n";
        s += " </NoteSegmentation>\n <Root>" + std::to_string (root) + "</Root>\n <ScaleHasChanged>false</ScaleHasChanged>\n</Scale>\n]]></Data>\n</PluginSpecific>\n";
        return s;
    };
    const std::string xml = "<PresetChunkXMLTree version=\"3\">\n <Preset Name=\"\" GenericType=\"TNLV\">\n  <PresetHeader>\n   <PluginName>Waves Tune Real-Time</PluginName>\n   <ActiveSetup>"
                            + active + "</ActiveSetup>\n  </PresetHeader>\n  <PresetData Setup=\"SETUP_A\">\n   <Parameters Type=\"RealWorld\">\n80 900 6 0 * 3 * 4 0 0\n</Parameters>\n"
                            + scaleXml (rootA, notesA) + "  </PresetData>\n  <PresetData Setup=\"SETUP_B\">\n   <Parameters Type=\"RealWorld\">\n80 900 6 0 * 1 * 7 0 0\n</Parameters>\n"
                            + scaleXml (rootB, notesB) + "  </PresetData>\n </Preset>\n</PresetChunkXMLTree>\n";
    std::vector<std::uint8_t> c { 'L', 'V', 'L', 'M', 's', 'e', 't', 'A', 'X', 'P', 's', 't' };
    c.insert (c.end(), xml.begin(), xml.end());
    return c;
}
const std::string naturalMinor = "0x00x0x00x0x"; // [DATA] the statuses of her "Natural Minor" states
const std::string major = "0x0x00x0x0x0";

lynx::ForeignPlugin wavesTune (const std::vector<std::uint8_t>& component)
{
    lynx::ForeignPlugin wt;
    wt.format = lynx::ForeignPlugin::Format::vst3;
    wt.name = "WaveShell1-VST3 16.0_x64";
    wt.state = substtest::flVst3State (component);
    return wt;
}
} // namespace tune120

TEST (waves_tune_scale_is_read_round120)
{
    using namespace lynx;
    const auto eMinor = wavesTuneScale (tune120::wavesTuneComponent (4, tune120::naturalMinor, 4, tune120::naturalMinor));
    CHECK (eMinor && eMinor->key == 4 && eMinor->scale == (int) vocal::Scale::minor);
    CHECK (eMinor && eMinor->mask == vocal::scaleMask (vocal::Scale::minor, 4));
    const auto fMajor = wavesTuneScale (tune120::wavesTuneComponent (5, tune120::major, 4, tune120::naturalMinor));
    CHECK (fMajor && fMajor->key == 5 && fMajor->scale == (int) vocal::Scale::major);
    // the active setup's scale, not the first one
    const auto b = wavesTuneScale (tune120::wavesTuneComponent (5, tune120::major, 8, tune120::naturalMinor, "SETUP_B"));
    CHECK (b && b->key == 8 && b->scale == (int) vocal::Scale::minor);
    // a note set LYNX Tune has no name for: custom, absolute pitch classes (root D: D, D#, A)
    const auto odd = wavesTuneScale (tune120::wavesTuneComponent (2, "00xxxxx0xxxx", 2, tune120::major));
    CHECK (odd && odd->scale == (int) vocal::Scale::custom && odd->mask == ((1u << 2) | (1u << 3) | (1u << 9)));
    // every note: chromatic
    const auto all = wavesTuneScale (tune120::wavesTuneComponent (0, "000000000000", 0, tune120::major));
    CHECK (all && all->scale == (int) vocal::Scale::chromatic);
    // not a Waves Tune state / a broken one
    CHECK (! wavesTuneScale (substtest::wavesComponent ("Waves Tune Real-Time", "80 900 6 0 * 3 * 4")));
    CHECK (! wavesTuneScale (tune120::wavesTuneComponent (12, tune120::major, 0, tune120::major)));
    CHECK (! wavesTuneScale (tune120::wavesTuneComponent (0, "xxxxxxxxxxxx", 0, tune120::major)));
    // the generic reader takes either corrector
    CHECK (pitchCorrectorScale (subst113::autoTuneState (9, 1)) && pitchCorrectorScale (subst113::autoTuneState (9, 1))->key == 9);
}

TEST (waves_tune_substitute_takes_its_own_scale_round120)
{
    using namespace lynx;
    // luisa sonsa's Waves Tune Real-Time (E natural minor) alone in a song: LYNX Tune in E minor, from its own state
    Song song;
    song.mixer = Song::defaultMixer (2);
    const auto wt = tune120::wavesTune (tune120::wavesTuneComponent (4, tune120::naturalMinor, 4, tune120::naturalMinor));
    song.mixer[1].effects.push_back (subst113::pluginSlot (wt));
    const auto context = substituteContextOf (song); // it votes for the project's key too
    CHECK (context.knowsKey() && context.key == 4 && context.scale == (int) vocal::Scale::minor);
    auto slot = song.mixer[1].effects[0];
    CHECK (substituteEffectSlot (slot, pluginIdentity (wt), SubstituteContext {}));
    CHECK (slot.kind == EffectKind::lynxTune);
    CHECK (lynxtune::keyIndex (slot.params[lynxtune::key]) == 4);
    CHECK (vocal::Scale (lynxtune::scaleIndex (slot.params[lynxtune::scale])) == vocal::Scale::minor);
    // its own scale wins over another project key
    SubstituteContext other;
    other.key = 7;
    other.scale = (int) vocal::Scale::major;
    auto again = song.mixer[1].effects[0];
    CHECK (substituteEffectSlot (again, pluginIdentity (wt), other));
    CHECK (lynxtune::keyIndex (again.params[lynxtune::key]) == 4);
    // a custom note set: LYNX Tune's custom scale with the same note switches
    const auto oddWt = tune120::wavesTune (tune120::wavesTuneComponent (2, "00xxxxx0xxxx", 2, tune120::major));
    auto odd = subst113::pluginSlot (oddWt);
    CHECK (substituteEffectSlot (odd, pluginIdentity (oddWt), SubstituteContext {}));
    CHECK (vocal::Scale (lynxtune::scaleIndex (odd.params[lynxtune::scale])) == vocal::Scale::custom);
    for (int k = 0; k < 12; ++k)
        CHECK ((odd.params[(std::size_t) (lynxtune::noteC + k)] >= 0.5f) == (k == 2 || k == 3 || k == 9));
}

TEST (waves_tune_note_transition_becomes_the_retune_round120)
{
    using namespace lynx;
    // RealWorld 20 = Speed 21.4 ms, 21 = Note Transition 54.2 ms (her most common state): LYNX Tune's retune = 54.2 ms
    auto c = tune120::wavesTuneComponent (4, tune120::naturalMinor, 4, tune120::naturalMinor);
    const std::string from = "80 900 6 0 * 3 * 4 0 0\n", to = "80 900 6 0 * 3 * 4 0 0\n* * * * * 1 0 80 1 1\n21.4 54.2 100 0 0 0 600 0 0 30\n";
    const auto at = std::search (c.begin(), c.end(), from.begin(), from.end());
    CHECK (at != c.end());
    c.erase (at, at + (std::ptrdiff_t) from.size());
    c.insert (c.begin() + (at - c.begin()), to.begin(), to.end());
    const auto wt = tune120::wavesTune (c);
    auto slot = subst113::pluginSlot (wt);
    CHECK (substituteEffectSlot (slot, pluginIdentity (wt), SubstituteContext {}));
    CHECK (std::abs (lynxtune::retuneMs (slot.params[lynxtune::retune]) - 54.2) < 0.5);
    CHECK (slot.params[lynxtune::humanize] == 0.0f);
    CHECK (lynxtune::keyIndex (slot.params[lynxtune::key]) == 4);
}

namespace tune120
{
// output pitch minus the sung pitch (cents) around `t` seconds of a LYNX Tune run
double correctionAt (const std::vector<float>& x, const std::vector<float>& y, int latency, double fs, double t)
{
    const auto at = (int) (t * fs) - 512;
    return r95::cents (r95::measureHz (y, at + latency, 1024, fs, 150.0, 500.0), r95::measureHz (x, at, 1024, fs, 150.0, 500.0));
}
} // namespace tune120

TEST (lynx_tune_phrase_starts_without_the_last_correction_round120)
{
    // phrase 1: A3 +45 c (pulled down ~45 c); 0.3 s of silence; phrase 2: A3 −45 c. The second phrase starts at the sung
    // pitch and is pulled UP from there (it used to start with phrase 1's −45 c correction still applied)
    const double fs = 44100.0;
    auto x = r95::vowel (fs, (int) (fs * 1.6), [] (double t) { return 220.0 * std::exp2 ((t < 0.8 ? 45.0 : -45.0) / 1200.0); });
    for (int i = (int) (0.6 * fs); i < (int) (0.9 * fs); ++i)
        x[(std::size_t) i] = 0.0f;
    auto p = r95::tuneDefaults();
    p[lynx::lynxtune::scale] = 0.0f;
    p[lynx::lynxtune::retune] = (float) std::sqrt (120.0 / 400.0);
    int latency = 0;
    const auto y = r95::runTune (x, fs, p, latency);
    CHECK (tune120::correctionAt (x, y, latency, fs, 0.55) < -35.0); // phrase 1 corrected
    const auto early = tune120::correctionAt (x, y, latency, fs, 0.95);
    CHECK (early > -5.0 && early < 25.0); // not −45…−90 c
    CHECK (tune120::correctionAt (x, y, latency, fs, 1.5) > 38.0);
}

TEST (lynx_tune_retune_curve_matches_waves_tune_round120)
{
    // [EXEC] Waves Tune Real-Time at Note Transition 54.2 ms on a held A3 +30 c from silence: 50 % of the correction at
    // ~50 ms, 90 % at ~144 ms. LYNX Tune at retune 54.2 ms: the same first-order curve (onset at the sung pitch)
    const double fs = 44100.0;
    auto x = r95::vowel (fs, (int) (fs * 1.0), [] (double) { return 220.0 * std::exp2 (30.0 / 1200.0); });
    for (int i = 0; i < (int) (0.2 * fs); ++i)
        x[(std::size_t) i] = 0.0f;
    auto p = r95::tuneDefaults();
    p[lynx::lynxtune::scale] = 0.0f;
    p[lynx::lynxtune::retune] = (float) std::sqrt (54.2 / 400.0);
    int latency = 0;
    const auto y = r95::runTune (x, fs, p, latency);
    const auto at60 = -tune120::correctionAt (x, y, latency, fs, 0.2 + 0.06);
    const auto at200 = -tune120::correctionAt (x, y, latency, fs, 0.2 + 0.2);
    const auto late = -tune120::correctionAt (x, y, latency, fs, 0.9);
    CHECK (at60 > 6.0 && at60 < 24.0);
    CHECK (at200 > 24.0);
    CHECK (late > 29.0 && late < 31.0);
}

TEST (lynx_tune_glide_is_not_pulled_back_round120)
{
    // an octave glide in 2 s at retune 120 ms: the correction averages near zero (Waves Tune RT: −3 c), not ~−15 c
    // against the glide's direction (the 0.1 st target hysteresis did that)
    const double fs = 44100.0;
    const auto x = r95::vowel (fs, (int) (fs * 2.0), [] (double t) { return 196.0 * std::exp2 (t / 2.0); });
    auto p = r95::tuneDefaults();
    p[lynx::lynxtune::scale] = 0.0f;
    p[lynx::lynxtune::retune] = (float) std::sqrt (120.0 / 400.0);
    int latency = 0;
    const auto y = r95::runTune (x, fs, p, latency);
    double sum = 0.0;
    int n = 0;
    for (double t = 0.3; t < 1.8; t += 0.01, ++n)
        sum += tune120::correctionAt (x, y, latency, fs, t);
    CHECK (std::abs (sum / n) < 10.0);
}

// --- Auto-Tune Pro (round 120 §5) ------------------------------------------------------------------------------------

namespace tune120
{
// JUCE MemoryBlock::toBase64Encoding of `raw` ("<size>.<chars>", 6 bits per char, LSB first)
std::string juceBase64 (const std::string& raw)
{
    static constexpr const char* alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    std::string out = std::to_string (raw.size()) + ".";
    const auto bits = raw.size() * 8;
    for (std::size_t b = 0; b < bits; b += 6)
    {
        unsigned v = 0;
        for (int k = 0; k < 6 && b + (std::size_t) k < bits; ++k)
            if (((unsigned char) raw[(b + (std::size_t) k) >> 3] >> ((b + (std::size_t) k) & 7)) & 1u)
                v |= 1u << k;
        out += alphabet[v];
    }
    return out;
}

// an Auto-Tune Pro component state with real ParamsData (as her projects save it)
std::vector<std::uint8_t> autoTuneState (int key, int scale, double retune, double humanize, double flex)
{
    char p[512];
    std::snprintf (p, sizeof p, "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <ActualParams><Parameter X0=\"0.0\"/><Parameter X1=\"%d.0\"/>"
                                "<Parameter X2=\"%d.0\"/><Parameter X4=\"%g\"/><Parameter X31=\"19.99999809265137\"/><Parameter X61=\"%g\"/>"
                                "<Parameter X62=\"0.0\"/><Parameter X90=\"%g\"/><Parameter X92=\"100.0\"/></ActualParams>",
                   scale, key, retune, humanize, flex);
    const std::string body (p);
    std::string params ("VC2!");
    for (int i = 0; i < 4; ++i)
        params += (char) (body.size() >> (8 * i));
    params += body;
    const auto xml = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?> <SessionChunkData><ChunkForVersion ChunkVersion=\"10.0.0\">")
                     + "<Parameters ParamsData=\"" + juceBase64 (params) + "\" IsUnencrypted=\"1\"/><Custom RAND_ID=\"12316\" ScaleGivenKey=\""
                     + std::to_string (key) + "\" ScaleGivenScale=\"" + std::to_string (scale) + "\" ScaleGivenDetune=\"0\"/></ChunkForVersion></SessionChunkData>";
    std::vector<std::uint8_t> c { 'V', 'C', '2', '!' };
    for (int i = 0; i < 4; ++i)
        c.push_back ((std::uint8_t) (xml.size() >> (8 * i)));
    c.insert (c.end(), xml.begin(), xml.end());
    return c;
}
} // namespace tune120

TEST (auto_tune_pro_params_are_read_round120)
{
    using namespace lynx;
    // her luisa sonsa state: E minor, Retune Speed 8.46 ms, Humanize 100, Flex-Tune 20
    const auto state = tune120::autoTuneState (4, 1, 8.46, 100.0, 20.0);
    const auto x = autoTuneProParams (state);
    CHECK (x.size() == 9);
    CHECK (x.count (4) && std::abs (x.at (4) - 8.46) < 1e-4);
    CHECK (x.count (61) && x.at (61) == 100.0);
    CHECK (x.count (90) && x.at (90) == 20.0);
    CHECK (autoTuneProParams (subst113::autoTuneState (9, 1)).empty()); // "4....." is no ParamsData
    CHECK (autoTuneProParams (tune120::wavesTuneComponent (4, tune120::major, 4, tune120::major)).empty());

    const auto at = subst113::autoTune (state);
    auto slot = subst113::pluginSlot (at);
    CHECK (substituteEffectSlot (slot, pluginIdentity (at)));
    CHECK (lynxtune::keyIndex (slot.params[lynxtune::key]) == 4);
    CHECK (vocal::Scale (lynxtune::scaleIndex (slot.params[lynxtune::scale])) == vocal::Scale::minor);
    CHECK (std::abs (lynxtune::retuneMs (slot.params[lynxtune::retune]) - (1.36 * 8.46 - 3.0)) < 0.05);
    CHECK (std::abs (slot.params[lynxtune::humanize] - 1.0f) < 1e-6f);
    CHECK (std::abs (slot.params[lynxtune::flexTune] - 0.2f) < 1e-6f);
    // retune 0 (the hard effect) stays 0
    auto hard = subst113::pluginSlot (subst113::autoTune (tune120::autoTuneState (9, 1, 0.0, 0.0, 0.0)));
    CHECK (substituteEffectSlot (hard, pluginIdentity (hard.plugin.foreign)));
    CHECK (lynxtune::retuneMs (hard.params[lynxtune::retune]) == 0.0);
}

TEST (lynx_tune_flex_tune_round120)
{
    // [EXEC] Auto-Tune Pro, Flex-Tune 50, instant retune, chromatic: a note 10 c off is corrected fully, 30 c off by
    // 20 c (min (d, (50 − d))), 45 c off by 5 c
    const double fs = 44100.0;
    const double offs[3] { 10.0, 30.0, 45.0 }, want[3] { 0.0, 10.0, 40.0 };
    for (int k = 0; k < 3; ++k)
    {
        const auto x = r95::vowel (fs, (int) (fs * 0.8), [&] (double) { return 220.0 * std::exp2 (offs[k] / 1200.0); });
        auto p = r95::tuneDefaults();
        p[lynx::lynxtune::scale] = 0.0f;
        p[lynx::lynxtune::retune] = 0.0f;
        p[lynx::lynxtune::flexTune] = 0.5f;
        int latency = 0;
        const auto y = r95::runTune (x, fs, p, latency);
        const auto out = r95::cents (r95::measureHz (y, (int) (0.5 * fs) + latency, 2048, fs), 220.0);
        CHECK (std::abs (out - want[k]) < 3.0);
    }
    // in a scale the distance counts in the gap to the next target: E minor, F +20 c (80 c below F#, gap 200 c) at
    // Flex-Tune 50: corrected by (50 − 40) / 40 × 80 = 20 c toward F# (Auto-Tune Pro: 23 c)
    const auto x = r95::vowel (fs, (int) (fs * 0.8), [] (double) { return 174.6141 * std::exp2 (20.0 / 1200.0); });
    auto p = r95::tuneDefaults();
    p[lynx::lynxtune::key] = 4.0f / 11.0f;
    p[lynx::lynxtune::scale] = 2.0f / (float) (lynx::vocal::scaleCount - 1);
    p[lynx::lynxtune::retune] = 0.0f;
    p[lynx::lynxtune::flexTune] = 0.5f;
    int latency = 0;
    const auto y = r95::runTune (x, fs, p, latency);
    CHECK (std::abs (r95::cents (r95::measureHz (y, (int) (0.5 * fs) + latency, 2048, fs), 174.6141) - 40.0) < 4.0);
}

TEST (lynx_tune_humanize_slows_only_sustained_notes_round120)
{
    // [EXEC] Auto-Tune Pro, retune 5 ms: a note held steady 0.8 s that then goes 30 c sharp is pulled back slowly with
    // Humanize 100 (50 % at ~104 ms), fast without it (4 ms) and fast when the note was not steady (a ±10 c vibrato)
    const double fs = 44100.0;
    auto run = [fs] (double vib, float human) {
        const auto x = r95::vowel (fs, (int) (fs * 1.4), [vib] (double t) {
            return 220.0 * std::exp2 (((t < 0.8 ? 0.0 : 30.0) + vib * std::sin (2.0 * r95::pi * 5.5 * t)) / 1200.0);
        });
        auto p = r95::tuneDefaults();
        p[lynx::lynxtune::scale] = 0.0f;
        p[lynx::lynxtune::retune] = (float) std::sqrt ((1.36 * 5.0 - 3.0) / 400.0);
        p[lynx::lynxtune::humanize] = human;
        int latency = 0;
        const auto y = r95::runTune (x, fs, p, latency);
        return -tune120::correctionAt (x, y, latency, fs, 0.8 + 0.1); // correction 100 ms after the move (of 30 c)
    };
    CHECK (run (0.0, 0.0f) > 26.0);
    CHECK (run (0.0, 1.0f) < 20.0);
    CHECK (run (10.0, 1.0f) > 22.0);
}
