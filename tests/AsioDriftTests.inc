// --- LYNX ASIO full-duplex input: drift-compensated FIFO (asio/InputDrift.h) - round 106 ----------------------------

namespace asiodrift
{
// Simulates a capture device whose clock runs `ppm` faster than the playback clock: packets of `packet` frames
// (with timing jitter) feed the FIFO; the playback device asks every `period` frames for enough host blocks of
// `block` frames. Returns the output (stereo interleaved), the input being a 1 kHz sine.
struct DuplexRun
{
    std::vector<float> out;
    std::int64_t underflows = 0, overflows = 0, underflowsLate = 0;
    double ratio = 1.0, fill = 0.0, maxFillError = 0.0;
    int target = 0;
};

inline DuplexRun simulate (double ppm, int packet, int period, int block, int target, double seconds, double stallAt = -1.0)
{
    constexpr double rate = 48000.0, freq = 1000.0, pi = 3.14159265358979323846;
    lynxasio::InputDrift fifo;
    fifo.reset (2, target, rate);
    DuplexRun run;
    const double captureRate = rate * (1.0 + ppm * 1.0e-6);
    std::int64_t captured = 0, produced = 0, renderEvents = 0, packets = 0;
    std::uint32_t seed = 12345;
    auto jitter = [&seed] { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) / 16777216.0 - 0.5) * 0.002; }; // ±1 ms
    std::vector<float> pkt ((size_t) packet * 2), blk ((size_t) block * 2);
    double nextCapture = packet / captureRate + jitter(), nextRender = period / rate;
    while (nextRender < seconds)
    {
        if (nextCapture <= nextRender)
        {
            const bool stalled = stallAt >= 0.0 && nextCapture >= stallAt && nextCapture < stallAt + 0.1;
            if (! stalled)
            {
                for (int i = 0; i < packet; ++i)
                {
                    const float v = 0.5f * (float) std::sin (2.0 * pi * freq * (double) (captured + i) / captureRate);
                    pkt[(size_t) (2 * i)] = v;
                    pkt[(size_t) (2 * i + 1)] = -v;
                }
                fifo.push (pkt.data(), packet, nextCapture);
            }
            captured += packet; // a stalled device loses those frames
            ++packets;
            nextCapture = (double) (packets + 1) * packet / captureRate + jitter();
        }
        else
        {
            ++renderEvents;
            while (produced < renderEvents * period)
            {
                fifo.pull (blk.data(), block, nextRender);
                run.out.insert (run.out.end(), blk.begin(), blk.end());
                produced += block;
                if (produced > (std::int64_t) (rate * 20.0)) // settled (after 20 s)
                    run.maxFillError = std::max (run.maxFillError, std::abs (fifo.averageFill() - fifo.target()));
                if (produced == (std::int64_t) (rate * 20.0) / block * block)
                    run.underflowsLate = -fifo.underflows;
            }
            nextRender = (double) (renderEvents + 1) * period / rate;
        }
    }
    run.underflows = fifo.underflows;
    run.underflowsLate += fifo.underflows;
    run.target = fifo.target();
    run.overflows = fifo.overflows;
    run.ratio = fifo.ratio();
    run.fill = fifo.fill();
    return run;
}

// Largest deviation from a pure sine: a sine obeys y[n+1] = 2 cos(w) y[n] - y[n-1]; a dropped or repeated frame
// breaks that by ~amplitude x w. Checked from `from` (frames) on, left channel.
inline double sineBreak (const std::vector<float>& out, std::size_t from, double w)
{
    double worst = 0.0;
    const double c = 2.0 * std::cos (w);
    for (std::size_t n = from + 1; n + 1 < out.size() / 2; ++n)
        worst = std::max (worst, std::abs (out[2 * (n + 1)] - c * out[2 * n] + out[2 * (n - 1)]));
    return worst;
}
} // namespace asiodrift

TEST (asio_input_drift_tracks_clock_difference)
{
    constexpr double pi = 3.14159265358979323846;
    // shared-mode like: 10 ms capture packets and playback periods, 128-frame host blocks, ±300 ppm clocks
    for (const double ppm : { 300.0, -300.0, 0.0 })
    {
        const auto run = asiodrift::simulate (ppm, 480, 480, 128, 928, 90.0);
        CHECK (run.underflows == 0);
        CHECK (run.overflows == 0);
        CHECK (run.target == 928);
        // the controller found the clock ratio (input frames read per output frame) within 30 ppm
        CHECK (std::abs ((run.ratio - 1.0) * 1.0e6 - ppm) < 30.0);
        CHECK (run.maxFillError < 400.0);
        // after 20 s the input sine reaches the host without a dropped or repeated frame
        const double w = 2.0 * pi * 1000.0 / 48000.0 * (1.0 + ppm * 1.0e-6) / run.ratio;
        CHECK (asiodrift::sineBreak (run.out, 48000 * 20, w) < 2.0e-3);
    }
    // exclusive-mode like: 3 ms packets and periods, 144-frame blocks, a small target
    const auto fast = asiodrift::simulate (120.0, 144, 144, 144, 332, 60.0);
    CHECK (fast.underflows == 0 && fast.overflows == 0);
    CHECK (std::abs ((fast.ratio - 1.0) * 1.0e6 - 120.0) < 30.0);
}

TEST (asio_input_drift_unity_is_transparent)
{
    // same clock: the interpolator must not colour the sound (16-tap windowed sinc, flat below 20 kHz)
    constexpr double pi = 3.14159265358979323846;
    lynxasio::InputDrift fifo;
    fifo.reset (1, 400, 48000.0);
    std::vector<float> in (256), out (256);
    double peak = 0.0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < 256; ++i)
            in[(size_t) i] = (float) std::sin (2.0 * pi * 15000.0 * (b * 256 + i) / 48000.0);
        fifo.push (in.data(), 256);
        fifo.pull (out.data(), 256);
        if (b > 100)
            for (float v : out)
                peak = std::max (peak, (double) std::abs (v));
    }
    CHECK (fifo.underflows == 0 && fifo.overflows == 0);
    CHECK (std::abs (fifo.ratio() - 1.0) < 1.0e-6);
    CHECK (peak > 0.97 && peak < 1.03); // 15 kHz within ±0.3 dB
}

TEST (asio_input_drift_recovers_from_a_stall)
{
    // the capture device stops for 100 ms (frames lost), then continues: one resynchronisation, then clean again
    const auto run = asiodrift::simulate (0.0, 480, 480, 128, 928, 40.0, 15.0);
    CHECK (run.underflows >= 1 && run.underflows <= 8); // one per block that found the FIFO empty during the 100 ms
    CHECK (run.underflowsLate == 0);
    CHECK (run.target > 928 && run.target <= 2 * 928); // the target was raised, within its limit
    CHECK (run.overflows == 0);
    CHECK (asiodrift::sineBreak (run.out, 48000 * 25, 2.0 * 3.14159265358979323846 * 1000.0 / 48000.0 / run.ratio) < 2.0e-3);
}
