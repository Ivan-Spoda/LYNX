// --- FL's "Use fixed size buffers" for hosted plugins (core/FixedBlock.h) - REA round 118 Part B ---

TEST (fixed_block_calls_the_plugin_with_whole_blocks_only)
{
    lynx::FixedBlock fixed (64, 2);
    std::vector<int> sizes;
    std::vector<float> l (1000), r (1000);
    for (int i = 0; i < 1000; ++i)
        l[(std::size_t) i] = (float) (i + 1), r[(std::size_t) i] = -(float) (i + 1);

    // the host's chunks: variable, some of one sample (LYNX splits at every event)
    const int chunks[] = { 1, 7, 64, 3, 200, 1, 1, 90, 633 };
    int at = 0;
    for (const auto n : chunks)
    {
        float* io[2] = { l.data() + at, r.data() + at };
        fixed.run (io, n, true, [&] (float* const* ch, int count) {
            sizes.push_back (count);
            for (int i = 0; i < count; ++i)
                ch[0][i] *= 2.0f, ch[1][i] *= 2.0f; // a "plugin": +6 dB
        });
        at += n;
    }
    CHECK (at == 1000);
    CHECK (sizes.size() == 1000 / 64);
    CHECK (std::all_of (sizes.begin(), sizes.end(), [] (int s) { return s == 64; }));
    CHECK (fixed.latency() == 64);

    // output = processed input delayed by exactly one block; silence before it
    auto exact = true;
    for (int i = 0; i < 1000; ++i)
    {
        const auto want = i < 64 ? 0.0f : 2.0f * (float) (i - 64 + 1);
        exact = exact && l[(std::size_t) i] == want && r[(std::size_t) i] == -want;
    }
    CHECK (exact);
    CHECK (fixed.position() == 1000 % 64);
}

TEST (fixed_block_instrument_mode_and_positions)
{
    lynx::FixedBlock fixed (32, 2);
    std::vector<float> l (100, 5.0f), r (100, 5.0f);
    int calls = 0;
    std::vector<int> positions;
    for (int at = 0; at < 100; at += 10)
    {
        positions.push_back (fixed.position()); // where a MIDI event arriving now lands in the block
        float* io[2] = { l.data() + at, r.data() + at };
        fixed.run (io, 10, false, [&] (float* const* ch, int count) {
            ++calls;
            auto silent = true;
            for (int i = 0; i < count; ++i)
                silent = silent && ch[0][i] == 0.0f && ch[1][i] == 0.0f; // no input for instruments
            CHECK (silent);
            ch[0][0] = 1.0f; // an "instrument": one click at the start of each block
        });
    }
    CHECK (calls == 3);
    CHECK ((positions == std::vector<int> { 0, 10, 20, 30 % 32, 40 % 32, 50 % 32, 60 % 32, 70 % 32, 80 % 32, 90 % 32 }));
    // clicks come out one block later: samples 32, 64, 96
    CHECK (l[31] == 0.0f && l[32] == 1.0f && l[33] == 0.0f && l[64] == 1.0f && l[96] == 1.0f && l[0] == 0.0f);
    CHECK (r[32] == 0.0f);

    fixed.clear();
    CHECK (fixed.position() == 0);
}

TEST (underrun_count_prefers_the_driver)
{
    CHECK (lynx::RtStats::underrunCount (3, 2, -1) == 5); // no driver count: LYNX's own estimate
    CHECK (lynx::RtStats::underrunCount (3, 2, 0) == 0);  // the driver says none
    CHECK (lynx::RtStats::underrunCount (0, 0, 7) == 7);
}
