// Round 90 (docs/analise/UI_STUDY_round90.md): Channel Rack note preview rule, FLP playlist track colours.

TEST (uilook90_rack_note_preview_rule)
{
    lynx::Pattern p;
    p.toggleStep (0, 0);
    p.toggleStep (0, 4);
    CHECK (! lynx::showsNotePreview (p, 0)); // plain steps keep the step buttons
    CHECK (! lynx::showsNotePreview (p, 1)); // no notes at all

    auto keyed = p;
    keyed.notes[1].key = 63;
    CHECK (lynx::showsNotePreview (keyed, 0)); // another key
    CHECK (lynx::channelKeyRange (keyed, 0) == std::make_pair (60, 63));

    auto longer = p;
    longer.notes[0].length = 2 * lynx::ticksPerStep;
    CHECK (lynx::showsNotePreview (longer, 0));

    auto offGrid = p;
    offGrid.notes[0].start += 1;
    CHECK (lynx::showsNotePreview (offGrid, 0));

    auto slide = p;
    slide.notes[0].slide = true;
    CHECK (lynx::showsNotePreview (slide, 0));
    CHECK (! lynx::showsNotePreview (slide, 2));
    CHECK (lynx::channelKeyRange (slide, 2) == std::make_pair (60, 60));
}

TEST (uilook90_flp_track_colours)
{
    if (realFlpDir.empty())
        return;

    const auto path = std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs" / "Ookay - Thief.flp";
    if (! std::filesystem::exists (path))
        return;

    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
    const auto song = lynx::importFlp (flp::parseProject (file));
    // track 1 "DRUMS" is red (0x00BBGGRR 0x0b0e77), track 23 "BASSES" blue, track 41 has FL's default grey
    CHECK (song.playlistTracks.size() >= 40);
    if (song.playlistTracks.size() >= 40)
    {
        CHECK (song.playlistTracks[0].name == "DRUMS" && song.playlistTracks[0].colour == 0xFF770E0Bu);
        CHECK (song.playlistTracks[22].name == "BASSES" && song.playlistTracks[22].colour == 0xFF16296Cu);
    }
    CHECK (song.playlistTracks.size() <= 40 || song.playlistTracks[40].colour == 0);
}
