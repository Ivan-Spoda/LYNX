// Spectral waveform analysis (feature/spectral-wave): band mix, peaks/RMS, mip levels, disk-cache format.
// Included at the end of tests.cpp.

#include "core/WaveAnalysis.h"

namespace
{
std::vector<float> waveSine (double hz, float amplitude, std::size_t frames, double rate = 48000.0)
{
    std::vector<float> v (frames);
    for (std::size_t i = 0; i < frames; ++i)
        v[i] = amplitude * (float) std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / rate);
    return v;
}

std::vector<float> waveNoise (float amplitude, std::size_t frames)
{
    std::vector<float> v (frames);
    std::uint32_t x = 0x12345678u;
    for (auto& s : v)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = amplitude * ((float) (x & 0xffffff) / (float) 0x800000 - 1.0f);
    }
    return v;
}

lynx::wave::BandMix waveMixOf (const std::vector<float>& v)
{
    const auto s = lynx::wave::analyse (v.data(), nullptr, v.size(), 48000.0);
    // skip the first 0.1 s (filter settling)
    const auto p = s.query (4800.0, (double) v.size(), -1);
    return lynx::wave::bandMix (p.low, p.mid, p.high);
}
} // namespace

TEST (wave_band_mix_follows_the_spectrum)
{
    const auto frames = (std::size_t) 48000;
    const auto low = waveMixOf (waveSine (60.0, 0.8f, frames));
    const auto mid = waveMixOf (waveSine (1000.0, 0.8f, frames));
    const auto high = waveMixOf (waveSine (9000.0, 0.8f, frames));
    const auto noise = waveMixOf (waveNoise (0.8f, frames));

    CHECK (low.low > 0.95f && low.mid < 0.05f && low.high < 0.01f);
    CHECK (mid.mid > 0.95f && mid.low < 0.05f && mid.high < 0.05f);
    CHECK (high.high > 0.9f && high.low < 0.01f);
    // white noise: energy grows with bandwidth, so mostly "high", some "mid", little "low"
    CHECK (noise.high > noise.mid && noise.mid > noise.low && noise.mid > 0.05f && noise.low < 0.1f);
    CHECK (std::abs (noise.low + noise.mid + noise.high - 1.0f) < 1.0e-4f);

    // bass + hats mixed at musical levels: both colours present
    auto mix = waveSine (55.0, 0.6f, frames);
    const auto hats = waveNoise (0.08f, frames);
    for (std::size_t i = 0; i < frames; ++i)
        mix[i] += hats[i];
    const auto both = waveMixOf (mix);
    CHECK (both.low > 0.6f && both.high > 0.1f && both.high < 0.3f); // measured: 0.84 / 0.01 / 0.15

    // silence is neutral, never NaN
    const auto silent = lynx::wave::bandMix (0.0f, 0.0f, 0.0f);
    CHECK (silent.mid == 1.0f && silent.low == 0.0f && silent.high == 0.0f);
}

TEST (wave_spectral_colours_are_warm_to_cool)
{
    using namespace lynx::wave;
    const auto warm = spectralColour (BandMix { 1.0f, 0.0f, 0.0f });
    const auto neutral = spectralColour (BandMix { 0.0f, 1.0f, 0.0f });
    const auto cool = spectralColour (BandMix { 0.0f, 0.0f, 1.0f });
    CHECK (warm.r > 0.9f && warm.r > warm.g && warm.g > warm.b);
    CHECK (neutral.g > neutral.r && neutral.g > neutral.b);
    CHECK (cool.b > 0.9f && cool.b > cool.r);
    // a blend stays bright and between its anchors
    const auto half = spectralColour (BandMix { 0.5f, 0.0f, 0.5f });
    CHECK (std::max ({ half.r, half.g, half.b }) >= 0.89f);
    CHECK (half.r < warm.r + 1e-6f && half.b > warm.b && half.b < cool.b + 1e-6f);
    for (float l = 0.0f; l <= 1.0f; l += 0.1f)
        for (float h = 0.0f; l + h <= 1.0f; h += 0.1f)
        {
            const auto c = spectralColour (BandMix { l, 1.0f - l - h, h });
            CHECK (c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f && c.b >= 0.0f && c.b <= 1.0f);
        }
}

TEST (wave_peaks_rms_and_levels)
{
    using namespace lynx::wave;
    const auto frames = (std::size_t) 200000;
    const auto v = waveSine (440.0, 0.5f, frames);
    const auto s = analyse (v.data(), nullptr, frames, 48000.0);

    CHECK (s.channels == 1 && s.frames == frames && s.bucket == minBucket && s.scale == 1.0f);
    CHECK (std::abs (s.peak - 0.5f) < 1e-3f);
    CHECK (s.levels.size() > 5 && s.levels.back().size() == 1);
    CHECK (s.levels[0].size() == (frames + minBucket - 1) / minBucket);
    for (std::size_t l = 1; l < s.levels.size(); ++l)
        CHECK (s.levels[l].size() == (s.levels[l - 1].size() + levelFactor - 1) / levelFactor);

    // any zoom: min/max/RMS of a sine (RMS = A / sqrt 2)
    for (const double span : { 1000.0, 5000.0, 60000.0, 190000.0 })
    {
        const auto p = s.query (1000.0, 1000.0 + span, 0);
        CHECK (p.valid);
        CHECK (std::abs (p.hi - 0.5f) < 0.01f && std::abs (p.lo + 0.5f) < 0.01f);
        CHECK (std::abs (p.rms - 0.5f / std::sqrt (2.0f)) < 0.01f);
    }

    // closer than one column: interpolated, still within the sine's range and continuous
    float previous = -1.0f;
    for (double f = 50000.0; f < 50000.0 + 4.0 * minBucket; f += 4.0)
    {
        const auto p = s.query (f, f + 4.0, 0);
        CHECK (p.valid && p.hi <= 0.51f && p.lo >= -0.51f && p.rms > 0.3f);
        if (previous >= 0.0f)
            CHECK (std::abs (p.rms - previous) < 0.02f);
        previous = p.rms;
    }

    // past the end: invalid; a range overlapping the end is clamped
    CHECK (! s.query ((double) frames, (double) frames + 100.0, 0).valid);
    CHECK (s.query ((double) frames - 10.0, (double) frames + 1000.0, 0).valid);
    CHECK (! analyse (nullptr, nullptr, 0, 48000.0).query (0.0, 10.0, -1).valid);
    CHECK (s.memoryBytes() < frames / minBucket * sizeof (Column) * 3 / 2 + 4096);
}

TEST (wave_stereo_and_loud_float_files)
{
    using namespace lynx::wave;
    const auto frames = (std::size_t) 20000;
    const std::vector<float> silent (frames, 0.0f);
    auto loud = waveSine (100.0, 2.0f, frames); // float file over 0 dBFS
    const auto s = analyse (silent.data(), loud.data(), frames, 44100.0);

    CHECK (s.channels == 2 && std::abs (s.scale - 2.0f) < 0.01f);
    const auto l = s.query (0.0, (double) frames, 0), r = s.query (0.0, (double) frames, 1), both = s.query (0.0, (double) frames, -1);
    CHECK (l.rms < 1e-3f && l.hi < 1e-3f);
    CHECK (std::abs (r.hi - 2.0f) < 0.02f && std::abs (r.lo + 2.0f) < 0.02f);
    CHECK (std::abs (both.hi - 2.0f) < 0.02f && both.rms > l.rms && both.rms < r.rms);

    // the same pointer twice is mono
    CHECK (analyse (loud.data(), loud.data(), frames, 44100.0).channels == 1);

    // cancelled: empty
    std::atomic<bool> cancel { true };
    CHECK (analyse (loud.data(), nullptr, frames, 44100.0, &cancel).levels.empty());
}

TEST (wave_disk_cache_round_trip)
{
    using namespace lynx::wave;
    const auto v = waveNoise (0.7f, 77777);
    const auto s = analyse (v.data(), nullptr, v.size(), 44100.0);
    const auto bytes = serialise (s);

    Summary back;
    CHECK (deserialise (bytes.data(), bytes.size(), back));
    CHECK (back.frames == s.frames && back.channels == s.channels && back.bucket == s.bucket && back.sampleRate == s.sampleRate);
    CHECK (back.levels.size() == s.levels.size());
    bool same = true;
    for (std::size_t l = 0; l < s.levels.size(); ++l)
        same = same && std::memcmp (back.levels[l].data(), s.levels[l].data(), s.levels[l].size() * sizeof (Column)) == 0;
    CHECK (same);
    const auto a = s.query (100.0, 30000.0, 0), b = back.query (100.0, 30000.0, 0);
    CHECK (a.hi == b.hi && a.rms == b.rms && a.high == b.high);

    // damaged files are rejected (truncated, extra bytes, other version, wrong magic)
    Summary junk;
    CHECK (! deserialise (bytes.data(), bytes.size() - 1, junk));
    auto longer = bytes;
    longer.push_back (0);
    CHECK (! deserialise (longer.data(), longer.size(), junk));
    auto other = bytes;
    other[4] ^= 0xff;
    CHECK (! deserialise (other.data(), other.size(), junk));
    other = bytes;
    other[0] ^= 0xff;
    CHECK (! deserialise (other.data(), other.size(), junk));
    CHECK (! deserialise (nullptr, 0, junk));
}
