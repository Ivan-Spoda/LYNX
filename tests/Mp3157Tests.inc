// Round 157b: MP3 decoding (core/Mp3Decoder, minimp3) — bit-exact against minimp3's own decoder, FL Studio's timing,
// gapless lengths, tag/junk robustness, an .flp audio clip of an MP3 rendered through the rack, and the insert a new
// channel goes to.
#include "../tools/WavReader.h"
#include "core/Mp3Decoder.h"
#include "lame.h"

namespace mp3test
{
// deterministic test signal: tones, noise bursts and an impulse at `clickAt`
std::vector<float> signal (int rate, int frames, int channel, int clickAt)
{
    std::vector<float> x ((std::size_t) frames);
    std::uint32_t seed = 12345u + (std::uint32_t) channel * 777u;
    for (int i = 0; i < frames; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const auto noise = ((float) (seed >> 8) / 16777216.0f - 0.5f);
        const auto t = (double) i / rate;
        const auto burst = ((i / (rate / 8)) % 3 == 1) ? 0.3f : 0.0f;
        x[(std::size_t) i] = (float) (0.15 * std::sin (2.0 * 3.14159265358979 * (220.0 + 110.0 * channel) * t)
                                      + 0.05 * std::sin (2.0 * 3.14159265358979 * 1760.0 * t))
                             + burst * noise;
    }
    if (clickAt >= 0 && clickAt < frames)
        x[(std::size_t) clickAt] = 0.9f;
    return x;
}

enum class Mode
{
    cbr,
    vbr,
    freeFormat
};

// LAME 3.100 (third_party/lame) with or without its Info/Xing + LAME header
std::vector<std::uint8_t> encode (const std::vector<float>& l, const std::vector<float>& r, int rate, int outRate, int channels,
                                  int kbps, Mode mode, bool header)
{
    auto* gf = lame_init();
    lame_set_in_samplerate (gf, rate);
    lame_set_out_samplerate (gf, outRate);
    lame_set_num_channels (gf, channels);
    lame_set_mode (gf, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_bWriteVbrTag (gf, header ? 1 : 0);
    lame_set_write_id3tag_automatic (gf, 0);
    if (mode == Mode::vbr)
    {
        lame_set_VBR (gf, vbr_default);
        lame_set_VBR_quality (gf, 2.0f);
    }
    else
    {
        lame_set_VBR (gf, vbr_off);
        lame_set_brate (gf, kbps);
        if (mode == Mode::freeFormat)
            lame_set_free_format (gf, 1);
    }
    std::vector<std::uint8_t> out;
    if (lame_init_params (gf) < 0)
    {
        lame_close (gf);
        return out;
    }
    std::vector<std::uint8_t> buf (1 << 20);
    std::vector<float> L (l.size()), R (r.empty() ? l.size() : r.size());
    for (std::size_t i = 0; i < l.size(); ++i)
        L[i] = l[i] * 32768.0f, R[i] = (r.empty() ? l[i] : r[i]) * 32768.0f;
    for (std::size_t p = 0; p < L.size(); p += 4096)
    {
        const auto n = (int) std::min<std::size_t> (4096, L.size() - p);
        const auto w = lame_encode_buffer_float (gf, L.data() + p, R.data() + p, n, buf.data(), (int) buf.size());
        out.insert (out.end(), buf.begin(), buf.begin() + std::max (0, w));
    }
    const auto w = lame_encode_flush (gf, buf.data(), (int) buf.size());
    out.insert (out.end(), buf.begin(), buf.begin() + std::max (0, w));
    if (header)
    {
        const auto t = lame_get_lametag_frame (gf, buf.data(), buf.size());
        if (t > 0 && t <= out.size())
            std::copy (buf.begin(), buf.begin() + (std::ptrdiff_t) t, out.begin());
    }
    lame_close (gf);
    return out;
}

std::vector<std::uint8_t> syncsafe (std::size_t n)
{
    return { (std::uint8_t) ((n >> 21) & 127), (std::uint8_t) ((n >> 14) & 127), (std::uint8_t) ((n >> 7) & 127), (std::uint8_t) (n & 127) };
}

// an ID3v2.4 tag with an APIC frame of `pictureBytes` full of fake frame syncs (FF FB / FF F3 / FF E2)
std::vector<std::uint8_t> id3v2 (std::size_t pictureBytes, bool footer)
{
    std::vector<std::uint8_t> body = { 'A', 'P', 'I', 'C' };
    std::vector<std::uint8_t> pic = { 0, 'i', 'm', 'a', 'g', 'e', '/', 'j', 'p', 'e', 'g', 0, 3, 0, 0xFF, 0xD8 };
    for (std::size_t i = 0; i < pictureBytes; ++i)
        pic.push_back ((std::uint8_t) (i % 4 == 0 ? 0xFF : i % 4 == 1 ? (i % 3 == 0 ? 0xFB : i % 3 == 1 ? 0xF3 : 0xE2) : (i * 37) & 0xFF));
    const auto size = syncsafe (pic.size());
    body.insert (body.end(), size.begin(), size.end());
    body.push_back (0);
    body.push_back (0);
    body.insert (body.end(), pic.begin(), pic.end());
    std::vector<std::uint8_t> tag = { 'I', 'D', '3', 4, 0, (std::uint8_t) (footer ? 0x10 : 0) };
    const auto total = syncsafe (body.size());
    tag.insert (tag.end(), total.begin(), total.end());
    tag.insert (tag.end(), body.begin(), body.end());
    if (footer)
    {
        tag.insert (tag.end(), { '3', 'D', 'I', 4, 0, 0x10 });
        tag.insert (tag.end(), total.begin(), total.end());
    }
    return tag;
}

bool sameSamples (const lynx::mp3::Decoded& a, const lynx::mp3::Decoded& b, std::size_t offsetInA = 0)
{
    if (a.left.size() < offsetInA || a.left.size() - offsetInA < b.left.size() || a.right.empty() != b.right.empty())
        return false;
    return std::memcmp (a.left.data() + offsetInA, b.left.data(), b.left.size() * sizeof (float)) == 0
           && (b.right.empty() || std::memcmp (a.right.data() + offsetInA, b.right.data(), b.right.size() * sizeof (float)) == 0);
}

// lag (−range…range) where the decoded audio best matches the source
int bestLag (const std::vector<float>& decoded, const std::vector<float>& source, int range)
{
    int best = 0;
    double bestDot = -1e300;
    for (int lag = -range; lag <= range; ++lag)
    {
        double dot = 0.0;
        for (std::size_t i = 4096; i + 4096 < source.size(); ++i)
        {
            const auto j = (std::int64_t) i + lag;
            if (j >= 0 && j < (std::int64_t) decoded.size())
                dot += (double) decoded[(std::size_t) j] * source[i];
        }
        if (dot > bestDot)
            bestDot = dot, best = lag;
    }
    return best;
}
} // namespace mp3test

TEST (mp3_decoder_is_minimp3_exact_with_fl_timing_and_gapless)
{
    using namespace lynx::mp3;
    constexpr int frames = 3 * 44100 + 321;
    const auto l = mp3test::signal (44100, frames, 0, 22050), r = mp3test::signal (44100, frames, 1, 22050);

    struct Case
    {
        const char* name;
        int outRate, channels, kbps;
        mp3test::Mode mode;
        bool header;
        int headerSamples; // the Xing/Info frame FL plays as silence
    };
    const Case cases[] = {
        { "cbr128", 44100, 2, 128, mp3test::Mode::cbr, true, 1152 },  { "cbr320", 44100, 2, 320, mp3test::Mode::cbr, true, 1152 },
        { "vbr", 44100, 2, 0, mp3test::Mode::vbr, true, 1152 },        { "mono", 44100, 1, 96, mp3test::Mode::cbr, true, 1152 },
        { "notag", 44100, 2, 192, mp3test::Mode::cbr, false, 0 },      { "vbr-notag", 44100, 2, 0, mp3test::Mode::vbr, false, 0 },
        { "mpeg2-22k", 22050, 2, 64, mp3test::Mode::cbr, true, 576 },  { "mpeg25-11k", 11025, 2, 32, mp3test::Mode::cbr, true, 576 },
        { "48k", 48000, 2, 128, mp3test::Mode::cbr, true, 1152 },
    };
    for (const auto& c : cases)
    {
        const auto bytes = mp3test::encode (l, r, 44100, c.outRate, c.channels, c.kbps, c.mode, c.header);
        CHECK (bytes.size() > 1000);
        CHECK (looksLikeMp3 (bytes.data(), bytes.size()));

        Decoded fl, gapless, reference;
        CHECK (decode (bytes.data(), bytes.size(), fl, Timing::flStudio));
        CHECK (decode (bytes.data(), bytes.size(), gapless, Timing::gapless));
        CHECK (decodeReference (bytes.data(), bytes.size(), reference));
        CHECK (fl.info.sampleRate == c.outRate && fl.info.channels == c.channels && (int) fl.right.empty() == (c.channels == 1));

        // gapless: minimp3's own decoder, bit for bit (it trims the LAME header's delay/padding too)
        CHECK (gapless.left.size() == reference.left.size() && mp3test::sameSamples (gapless, reference));
        CHECK (fl.info.xingHeader == c.header && fl.info.lameHeader == c.header);
        if (c.header)
        {
            // the original's length exactly (resampled for the 22/11/48 kHz cases), starting where the source does
            const auto expected = (std::size_t) std::llround ((double) frames * c.outRate / 44100.0);
            CHECK (std::llabs ((long long) gapless.left.size() - (long long) expected) <= (c.outRate == 44100 ? 0 : 2));
            if (c.outRate == 44100)
                CHECK (mp3test::bestLag (gapless.left, l, 8) == 0);
            // FL: the header frame as silence, then the raw stream (encoder + decoder delay kept)
            CHECK (fl.info.leadingSilence == c.headerSamples && fl.info.encoderDelay == 576 + 529);
            const auto lead = (std::size_t) (c.headerSamples + fl.info.encoderDelay);
            CHECK (std::all_of (fl.left.begin(), fl.left.begin() + c.headerSamples, [] (float v) { return v == 0.0f; }));
            CHECK (mp3test::sameSamples (fl, gapless, lead));
            CHECK (fl.left.size() == gapless.left.size() + lead + (std::size_t) fl.info.encoderPadding);
        }
        else
        {
            CHECK (mp3test::sameSamples (fl, gapless) && fl.left.size() == gapless.left.size() && fl.info.leadingSilence == 0);
            if (c.outRate == 44100)
                CHECK (mp3test::bestLag (fl.left, l, 1200) == 576 + 529); // FL: the raw stream, delay included
        }
    }

    // tags and junk around the same stream decode to the same samples
    const auto base = mp3test::encode (l, r, 44100, 44100, 2, 128, mp3test::Mode::cbr, true);
    Decoded plain;
    CHECK (decode (base.data(), base.size(), plain));
    std::vector<std::uint8_t> wrapped = mp3test::id3v2 (300000, false);
    const auto second = mp3test::id3v2 (2000, true); // two tags in a row (JUCE decoded the second one as audio)
    wrapped.insert (wrapped.end(), second.begin(), second.end());
    for (int i = 0; i < 700; ++i)
        wrapped.push_back ((std::uint8_t) (i % 2 ? 0x00 : 0xFF)); // padding/junk before the first frame
    wrapped.insert (wrapped.end(), base.begin(), base.end());
    const std::uint8_t ape[] = { 'A', 'P', 'E', 'T', 'A', 'G', 'E', 'X', 0xD0, 0x07, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0, 0, 0, 0, 0, 0, 0, 0 };
    wrapped.insert (wrapped.end(), std::begin (ape), std::end (ape));
    std::vector<std::uint8_t> v1 (128, 0);
    v1[0] = 'T', v1[1] = 'A', v1[2] = 'G';
    wrapped.insert (wrapped.end(), v1.begin(), v1.end());
    Decoded tagged;
    CHECK (looksLikeMp3 (wrapped.data(), 4096));
    CHECK (decode (wrapped.data(), wrapped.size(), tagged) && tagged.left.size() == plain.left.size() && mp3test::sameSamples (tagged, plain));

    // free format (JUCE refused it): decodes, with the source's timing
    const auto free = mp3test::encode (l, r, 44100, 44100, 2, 400, mp3test::Mode::freeFormat, false);
    Decoded freeDecoded;
    CHECK (decode (free.data(), free.size(), freeDecoded) && freeDecoded.left.size() >= (std::size_t) frames);
    CHECK (mp3test::bestLag (freeDecoded.left, l, 1200) == 576 + 529);

    // a cut file keeps what it has; garbage, empty input and other formats are not MP3
    Decoded cut;
    CHECK (decode (base.data(), base.size() - 300, cut) && cut.left.size() < plain.left.size() && cut.left.size() > plain.left.size() - 3000);
    std::vector<std::uint8_t> junk (50000);
    std::uint32_t seed = 7;
    for (auto& b : junk)
        b = (std::uint8_t) ((seed = seed * 1103515245u + 12345u) >> 24);
    Decoded none;
    CHECK (! decode (junk.data(), junk.size(), none) && none.left.empty());
    CHECK (! decode (nullptr, 0, none));
    const std::uint8_t wav[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 0xFF, 0xFB, 0x90, 0x44 };
    CHECK (! looksLikeMp3 (wav, sizeof wav));
}

TEST (mp3_audio_clip_from_flp_renders_with_fl_timing)
{
    // an .flp audio clip of a LAME MP3 (Info header), at the start of the song, played at 48 kHz: the impulse at
    // source frame 22050 sounds where FL plays it (frame 22050 + 1152 + 1105 of the 44.1 kHz file)
    constexpr int frames = 44100 + 4000;
    const auto l = mp3test::signal (44100, frames, 0, 22050), r = mp3test::signal (44100, frames, 1, 22050);
    auto quiet = l, quietR = r;
    for (auto* v : { &quiet, &quietR })
        for (auto& x : *v)
            x *= 0.05f;
    quiet[22050] = quietR[22050] = 0.9f;
    const auto bytes = mp3test::encode (quiet, quietR, 44100, 44100, 2, 320, mp3test::Mode::cbr, true);
    const auto path = (std::filesystem::temp_directory_path() / ("lynx_mp3clip_" + processTempTag() + ".mp3")).string();
    {
        std::ofstream out (path, std::ios::binary);
        out.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    }

    flp::Project p;
    p.header.ppq = 96;
    p.tempo = 120.0;
    p.channels = { { 0, flp::ChannelType::audioClip, "Beat", "", path, 0 } };
    flp::Arrangement arr;
    flp::PlaylistItem clip;
    clip.kind = flp::PlaylistItem::Kind::channelClip;
    clip.index = 0;
    clip.position = 0;
    clip.length = 96 * 4; // 2 s
    arr.items = { clip };
    p.arrangements.push_back (arr);
    lynx::FlpImportOptions options;
    options.fileExists = [] (const std::string& f) { return std::filesystem::exists (f); };
    const auto song = lynx::importFlp (p, options);
    CHECK (song.channels.size() == 1 && song.channels[0].instrument == lynx::InstrumentKind::audioClip && song.channels[0].samplePath == path);

    const auto sample = lynx::tools::readWav (path);
    CHECK (sample != nullptr && sample->sampleRate == 44100.0 && sample->isStereo());
    if (sample == nullptr)
        return;
    CHECK (sample->frames() > (std::size_t) frames + 1152 + 1105); // (+ the encoder padding at the end)
    int peakAt = -1;
    float peak = 0.0f;
    for (std::size_t i = 0; i < sample->frames(); ++i)
        if (std::abs (sample->left[i]) > peak)
            peak = std::abs (sample->left[i]), peakAt = (int) i;
    CHECK (std::abs (peakAt - (22050 + 2257)) <= 1);

    const double sr = 48000.0;
    auto state = lynx::RackState::build (song, nullptr, sr, [&] (const std::string&) { return sample; });
    lynx::RackRenderer renderer;
    renderer.prepare (sr);
    std::vector<float> L (96000), R (96000);
    double tick = 0.0;
    for (int pos = 0; pos < 96000; pos += 480)
    {
        renderer.process (state.get(), true, tick, 480, L.data() + pos, R.data() + pos);
        tick += 480 * lynx::ticksPerSample (song.bpm, sr);
    }
    int outAt = -1;
    float outPeak = 0.0f;
    for (int i = 0; i < 96000; ++i)
        if (std::abs (L[(std::size_t) i]) > outPeak)
            outPeak = std::abs (L[(std::size_t) i]), outAt = i;
    CHECK (outPeak > 0.3f);
    const auto expected = (22050.0 + 2257.0) * 48000.0 / 44100.0;
    CHECK (std::abs (outAt - expected) <= 2.0);
    std::error_code ec;
    std::filesystem::remove (path, ec);
}

TEST (new_channel_insert_skips_effect_buses)
{
    auto s = lynx::Song::makeDefault(); // channels on inserts 1..5
    CHECK (s.nextFreeInsert() == 6);
    s.mixer[6].effects.push_back ({}); // an effect bus with no channel (her vocal reverb/delay return)
    s.mixer[1].sends.push_back ({ 7, 1.0f }); // fed by a send
    s.mixer[8].name = "Vox FX";               // named by the user
    s.mixer[9].toMaster = false;              // routed elsewhere
    s.mixer[9].sends.push_back ({ 2, 1.0f });
    s.playlistTracks.resize (3);
    s.playlistTracks[2].mixerTrack = 10; // linked to a Playlist track
    CHECK (s.nextFreeInsert() == 11);
    for (std::size_t t = 11; t < s.mixer.size(); ++t)
        s.mixer[t].effects.push_back ({});
    CHECK (s.nextFreeInsert() == 0); // nothing free: Master
}
