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
