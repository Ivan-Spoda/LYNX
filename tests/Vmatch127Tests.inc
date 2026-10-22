// --- Round 127 (vmatch127): the remaining substitutes of her corpus play her saved settings -----------------------

namespace vm127
{
lynx::PluginIdentity named (const char* name)
{
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst2;
    id.name = name;
    return id;
}
} // namespace vm127

TEST (panman_substitute_moves_the_balance_round127)
{
    using namespace lynx;
    const auto id = vm127::named ("PanMan");
    const auto* e = findPluginSubstitute (id, false);
    CHECK (e != nullptr && e->effect == EffectKind::autoPan);
    if (e == nullptr)
        return;
    const auto slot = makeSubstituteSlot (*e);
    // render a constant 1.0 for two beats at 120 BPM: the balance must swing (round 113's preset was a pass-through)
    auto fx = makeEffect (slot);
    fx->prepare (48000.0);
    fx->setSongPosition (0.0);
    std::vector<float> l (48000, 1.0f), r (48000, 1.0f);
    for (int i = 0; i < 48000; i += 480)
    {
        fx->setSongPosition (i / 48000.0 * 2.0 * 960.0); // 120 BPM: two beats per second, 960 ticks per beat
        fx->process (l.data() + i, r.data() + i, 480, slot.params, 120.0);
    }
    const auto [lmin, lmax] = std::minmax_element (l.begin(), l.end());
    const auto [rmin, rmax] = std::minmax_element (r.begin(), r.end());
    CHECK (*lmax > 0.99f && *rmax > 0.99f);
    CHECK (std::abs (*lmin - 0.5f) < 0.02f && std::abs (*rmin - 0.5f) < 0.02f);
}

namespace vm127
{
// JUCE MemoryBlock::toBase64Encoding ("<size>." + 6 bits per character, least significant bits first)
std::string juceBase64 (const std::string& raw)
{
    static constexpr const char* alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    std::string out = std::to_string (raw.size()) + ".";
    const auto bits = raw.size() * 8;
    for (std::size_t bit = 0; bit < bits; bit += 6)
    {
        unsigned v = 0;
        for (int k = 0; k < 6 && bit + (std::size_t) k < bits; ++k)
            if (((unsigned char) raw[(bit + (std::size_t) k) >> 3] >> ((bit + (std::size_t) k) & 7)) & 1u)
                v |= 1u << k;
        out += alphabet[v];
    }
    return out;
}

// an Auto-Tune Pro component state with a ParamsData block holding the given X<n> values
std::vector<std::uint8_t> autoTuneState (int key, int scale, const std::vector<std::pair<int, double>>& xs)
{
    std::string params = "<Parameters";
    for (const auto& [n, v] : xs)
        params += " X" + std::to_string (n) + "=\"" + std::to_string (v) + "\"";
    params += "/>";
    const auto xml = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?> <SessionChunkData IsDefaultAAXChunk=\"0\"><ChunkForVersion ChunkVersion=\"10.0.0\">")
                     + "<Parameters ParamsData=\"" + juceBase64 (params) + "\" IsUnencrypted=\"1\"/><Custom RAND_ID=\"6334\" ScaleGivenKey=\""
                     + std::to_string (key) + "\" ScaleGivenScale=\"" + std::to_string (scale)
                     + "\" ScaleGivenDetune=\"0\"><GraphModeData/></Custom></ChunkForVersion></SessionChunkData>";
    std::vector<std::uint8_t> c { 'V', 'C', '2', '!' };
    const auto size = (std::uint32_t) xml.size();
    for (int i = 0; i < 4; ++i)
        c.push_back ((std::uint8_t) (size >> (8 * i)));
    c.insert (c.end(), xml.begin(), xml.end());
    return c;
}

std::vector<std::uint8_t> efxState (int key, int scale, const char* notes /* 12 x '0'/'1', C … B */, double retune, double shift, double throat, double mix)
{
    static constexpr const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<AUTOTUNEEFXPLUS>";
    auto param = [&x] (const std::string& id, double v) { x += "<PARAM id=\"" + id + "\" value=\"" + std::to_string (v) + "\"/>"; };
    param ("pDryWetMix", mix);
    param ("pFormant", 1.0);
    param ("pHumanize", 0.0);
    param ("pInputType", 1.0);
    param ("pKey", key);
    for (int k = 0; k < 12; ++k)
        param (std::string ("pNote") + names[k], notes[k] == '1' ? 1.0 : 0.0);
    param ("pPitchShift", shift);
    param ("pRetuneSpeed", retune);
    param ("pScale", scale);
    param ("pThroatLength", throat);
    param ("pTracking", 5.0);
    x += "</AUTOTUNEEFXPLUS>";
    return std::vector<std::uint8_t> (x.begin(), x.end());
}

lynx::EffectSlot tuneSlot (const std::vector<std::uint8_t>& component, int& translated)
{
    auto at = subst113::autoTune (component);
    const auto id = lynx::pluginIdentity (at);
    const auto* e = lynx::findPluginSubstitute (id, false);
    CHECK (e != nullptr);
    return e != nullptr ? lynx::makeSubstituteSlot (*e, &id, &translated) : lynx::EffectSlot {};
}
} // namespace vm127

TEST (auto_tune_pro_extra_controls_translate_round127)
{
    using namespace lynx;
    namespace t = lynxtune;
    // her usual state: Alto-Tenor, detune 0, transpose 0, mix 100, formant on, throat 100, tracking 50 → LYNX defaults
    {
        int n = 0;
        const auto s = vm127::tuneSlot (vm127::autoTuneState (4, 1, { { 4, 7.44 }, { 61, 100 }, { 90, 20 }, { 3, 0 }, { 74, 0 }, { 92, 100 }, { 10, 1 }, { 70, 1 }, { 71, 100 }, { 9, 50 }, { 11, 0 } }), n);
        CHECK (n == 12 && s.kind == EffectKind::lynxTune);
        CHECK (t::keyIndex (s.params[t::key]) == 4 && t::scaleIndex (s.params[t::scale]) == (int) vocal::Scale::minor);
        CHECK (std::abs (t::referenceHz (s.params[t::reference]) - 440.0) < 1e-3 && t::transposeSemitones (s.params[t::transpose]) == 0);
        CHECK (s.params[t::mix] == 1.0f && t::rangeIndex (s.params[t::range]) == 1 && s.params[t::formantPreserve] == 1.0f);
        CHECK (std::abs (t::formantSemitones (s.params[t::formantShift])) < 1e-4 && std::abs (s.params[t::tracking] - 0.4f) < 1e-6f);
        CHECK (std::abs (s.params[t::flexTune] - 0.2f) < 1e-6f && std::abs (s.params[t::humanize] - 1.0f) < 1e-6f);
    }
    // detune +20 c, transpose −5, mix 60 %, Low Male, throat 120 %, tracking 75
    {
        int n = 0;
        const auto s = vm127::tuneSlot (vm127::autoTuneState (2, 0, { { 4, 20 }, { 3, 20 }, { 74, -5 }, { 92, 60 }, { 10, 2 }, { 70, 1 }, { 71, 120 }, { 9, 75 } }), n);
        CHECK (std::abs (t::referenceHz (s.params[t::reference]) - 440.0 * std::exp2 (20.0 / 1200.0)) < 0.01);
        CHECK (t::transposeSemitones (s.params[t::transpose]) == -5 && std::abs (s.params[t::mix] - 0.6f) < 1e-6f);
        CHECK (t::rangeIndex (s.params[t::range]) == 2 && std::abs (t::formantSemitones (s.params[t::formantShift]) + 12.0 * std::log2 (1.2)) < 0.01);
        CHECK (std::abs (s.params[t::tracking] - 0.6f) < 1e-6f);
    }
    // Classic mode: no formant correction, no Flex-Tune, no transpose; Bass Instrument → Instrument
    {
        int n = 0;
        const auto s = vm127::tuneSlot (vm127::autoTuneState (9, 1, { { 4, 6 }, { 90, 20 }, { 11, 1 }, { 70, 1 }, { 74, 3 }, { 10, 4 } }), n);
        CHECK (s.params[t::formantPreserve] == 0.0f && s.params[t::flexTune] == 0.0f && t::transposeSemitones (s.params[t::transpose]) == 0);
        CHECK (t::rangeIndex (s.params[t::range]) == 3);
    }
    // formant correction off
    {
        int n = 0;
        const auto s = vm127::tuneSlot (vm127::autoTuneState (9, 1, { { 70, 0 }, { 71, 150 } }), n);
        CHECK (s.params[t::formantPreserve] == 0.0f && s.params[t::formantShift] == 0.5f);
    }
}

TEST (auto_tune_efx_states_translate_round127)
{
    using namespace lynx;
    namespace t = lynxtune;
    // her B minor state with F added (custom targets), −12 st, throat 50 %
    {
        int n = 0;
        const auto s = vm127::tuneSlot (vm127::efxState (11, 1, "011011110101", 20.0, -12.0, 50.0, 100.0), n);
        CHECK (n == 23 && t::keyIndex (s.params[t::key]) == 11 && t::scaleIndex (s.params[t::scale]) == (int) vocal::Scale::custom);
        const char* notes = "011011110101";
        for (int k = 0; k < 12; ++k)
            CHECK ((s.params[(std::size_t) (t::noteC + k)] >= 0.5f) == (notes[k] == '1'));
        CHECK (t::transposeSemitones (s.params[t::transpose]) == -12 && std::abs (t::formantSemitones (s.params[t::formantShift]) - 7.0) < 1e-4);
        CHECK (std::abs (t::retuneMs (s.params[t::retune]) - (1.36 * 20.0 - 3.0)) < 0.01);
    }
    // plain B minor, mix 95 %
    {
        int n = 0;
        const auto s = vm127::tuneSlot (vm127::efxState (11, 1, "011010110101", 20.0, 0.0, 100.0, 95.0), n);
        CHECK (t::scaleIndex (s.params[t::scale]) == (int) vocal::Scale::minor && std::abs (s.params[t::mix] - 0.95f) < 1e-6f);
        CHECK (std::abs (t::formantSemitones (s.params[t::formantShift])) < 1e-4);
        const auto sc = pitchCorrectorScale (vm127::efxState (11, 1, "011010110101", 20.0, 0.0, 100.0, 95.0));
        CHECK (sc && sc->key == 11 && sc->scale == (int) vocal::Scale::minor);
    }
    // chromatic, 221.6 ms
    {
        int n = 0;
        const auto s = vm127::tuneSlot (vm127::efxState (0, 2, "111111111111", 221.648, 0.0, 100.0, 100.0), n);
        CHECK (t::scaleIndex (s.params[t::scale]) == (int) vocal::Scale::chromatic && std::abs (t::retuneMs (s.params[t::retune]) - (1.36 * 221.648 - 3.0)) < 0.1);
    }
}

TEST (waves_cla76_translates_onto_crest_round127)
{
    using namespace lynx;
    namespace c = dyn::crest;
    // her one state, the factory "Start Me Up": Input -8.7, Output -16.9, attack 4.3, release 4.31, all buttons, mix 41.3
    std::string values;
    for (int i = 0; i < 106; ++i)
        values += i == 0 ? "8.7 " : i == 1 ? "16.9 " : i == 2 ? "4.3 " : i == 3 ? "4.31 " : i == 59 ? "4 " : i == 103 ? "41.3 " : "0 ";
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 16.0_x64";
    id.state = substtest::wavesComponent ("CLA-76", values);
    const auto* e = findPluginSubstitute (id, false);
    CHECK (e != nullptr && e->effect == EffectKind::crestCompressor);
    if (e == nullptr)
        return;
    int n = 0;
    const auto s = makeSubstituteSlot (*e, &id, &n);
    CHECK (n == 11 && c::styleOf (s.params[c::style]) == c::Style::bus);
    CHECK (std::abs (c::thresholdDb (s.params[c::threshold]) + 43.7) < 0.05 && std::abs (c::ratioOf (s.params[c::ratio]) - 7.1) < 0.1);
    CHECK (std::abs (c::rangeDb (s.params[c::range]) - 31.9) < 0.05 && std::abs (c::trimDb (s.params[c::output]) - 31.6) < 0.05);
    CHECK (std::abs (c::attackMs (s.params[c::attack]) - 5.91) < 0.1 && std::abs (c::releaseMs (s.params[c::release]) - 97.0) < 1.0);
    CHECK (std::abs (s.mix - 0.413f) < 1e-5f && s.params[c::mix] == 1.0f && s.params[c::autoGain] == 0.0f);
    // its static curve (wet path) at her setting, measured on the official trial (1 kHz sine, gain dB): +31.45 below
    // the knee, about -9.7 dBFS out on the plateau, the 32 dB ceiling at full scale
    CHECK (std::abs (c::staticGainDb (s.params, -60.0)) < 1e-6);
    CHECK (c::staticGainDb (s.params, 0.0) >= -31.95 && c::staticGainDb (s.params, 0.0) <= -31.8);
}
