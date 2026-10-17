// Round 104 (docs/analise/REA_rodada104.md): Playlist track groups, the Channel Rack's filter groups.

TEST (flui104_track_group_rules)
{
    lynx::Song song;
    song.playlistTracks.resize (8);
    // 1 = header of 2, 3; 5 = header of 6; 0, 4, 7 alone
    lynx::noteedit::groupWithAbove (song, 2);
    lynx::noteedit::groupWithAbove (song, 3);
    lynx::noteedit::groupWithAbove (song, 6);
    lynx::noteedit::groupWithAbove (song, 0); // track 0 can't join a group
    CHECK (! song.playlistTracks[0].grouped);
    CHECK (lynx::noteedit::isGroupHeader (song, 1) && lynx::noteedit::isGroupHeader (song, 5));
    CHECK (! lynx::noteedit::isGroupHeader (song, 0) && ! lynx::noteedit::isGroupHeader (song, 2) && ! lynx::noteedit::isGroupHeader (song, 4));
    CHECK (lynx::noteedit::groupHeaderOf (song, 3) == 1 && lynx::noteedit::groupHeaderOf (song, 1) == 1);
    CHECK (lynx::noteedit::groupHeaderOf (song, 4) == -1 && lynx::noteedit::groupHeaderOf (song, 6) == 5);
    CHECK ((lynx::noteedit::groupMembers (song, 1) == std::vector<int> { 2, 3 }));
    CHECK (lynx::noteedit::groupMembers (song, 2).empty()); // a member is no header

    // folding hides the members only
    lynx::noteedit::setGroupCollapsed (song, 3, true); // any track of the group folds it
    CHECK (song.playlistTracks[1].collapsed);
    CHECK (lynx::noteedit::isFoldedAway (song, 2) && lynx::noteedit::isFoldedAway (song, 3));
    CHECK (! lynx::noteedit::isFoldedAway (song, 1) && ! lynx::noteedit::isFoldedAway (song, 6));

    // "Auto color group" gives the header's colour to the members
    song.playlistTracks[5].colour = 0xFF123456u;
    lynx::noteedit::colourGroup (song, 6);
    CHECK (song.playlistTracks[6].colour == 0xFF123456u && song.playlistTracks[4].colour == 0);

    // a member leaving its group: the members below it now hang from it (the flag model FL stores)
    lynx::noteedit::ungroupTrack (song, 2);
    CHECK (! song.playlistTracks[2].grouped && lynx::noteedit::groupHeaderOf (song, 3) == 2);
    CHECK (! lynx::noteedit::isGroupHeader (song, 1));
    lynx::noteedit::ungroupTrack (song, 1); // a header stays as it is
    CHECK (! song.playlistTracks[1].grouped);
}

TEST (flui104_flp_track_groups_and_filter_groups)
{
    if (realFlpDir.empty())
        return;

    const auto path = std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs" / "Ookay - Thief.flp";
    if (! std::filesystem::exists (path))
        return;

    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
    const auto project = flp::parseProject (file);
    const auto song = lynx::importFlp (project);

    // FL's groups in Thief: DRUMS (1..14), MELODICS (15..22), BASSES (23..26), VOCALS (27..35), FX (36..39); TEMPO alone
    CHECK (song.playlistTracks.size() >= 40);
    if (song.playlistTracks.size() >= 40)
    {
        CHECK (lynx::noteedit::isGroupHeader (song, 0) && lynx::noteedit::groupMembers (song, 0).size() == 13);
        CHECK (lynx::noteedit::isGroupHeader (song, 22) && lynx::noteedit::groupMembers (song, 22).size() == 3);
        CHECK (song.playlistTracks[23].name == "Synth Bass" && lynx::noteedit::groupHeaderOf (song, 23) == 22);
        CHECK (lynx::noteedit::isGroupHeader (song, 26) && lynx::noteedit::isGroupHeader (song, 35));
        CHECK (lynx::noteedit::groupHeaderOf (song, 39) == -1 && ! lynx::noteedit::isGroupHeader (song, 39)); // TEMPO
        CHECK (! song.playlistTracks[22].collapsed); // the demo has every group open
    }

    // the Channel Rack's filter groups: Audio, Automation, Unsorted; Synth Bass is unsorted, the kick an audio channel
    CHECK ((project.filterGroups == std::vector<std::string> { "Audio", "Automation", "Unsorted" }));
    CHECK ((song.channelGroups == std::vector<std::string> { "Audio", "Automation", "Unsorted" }));
    auto groupOf = [&song] (const char* name) {
        for (const auto& c : song.channels)
            if (c.name == name)
                return c.group;
        return -2;
    };
    CHECK (groupOf ("Synth Bass") == 2 && groupOf ("Piano Dark") == 2);
    CHECK (groupOf ("Grv Kick 27") == 0 && groupOf ("Main Vox") == 0);
}
