// LYNX ASIO — the input side of a full-duplex stream (REA round 106).
//
// The capture device delivers packets on its own clock; the host's blocks are paced by the playback device. Two
// clocks never run at exactly the same speed (two devices, or Windows' own rate conversion), so a plain FIFO between
// them slowly fills up or runs dry: every few seconds or minutes the input would click when it is trimmed or padded.
//
// InputDrift is that FIFO plus a small adaptive resampler: a PI controller watches the average fill level and
// reads the FIFO slightly faster or slower (at most ±0.5 %, normally a few hundred ppm — far below audibility) so
// the level stays at its target. The fractional reader is a 16-tap Kaiser-windowed sinc (flat to ~20 kHz at
// 48 kHz). Only when the stream is really interrupted (a capture stall, a host that stopped calling) does it
// resynchronise, which is counted.
//
// Plain C++ without Windows headers, so tests/tests.cpp checks it on Linux. Written from textbook DSP (windowed-sinc
// interpolation, PI control); nothing here comes from another driver.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lynxasio
{
class InputDrift
{
public:
    static constexpr int taps = 16;             // interpolation filter length (frames)
    static constexpr int half = taps / 2;       // frames needed on each side of the read position
    static constexpr int phases = 256;          // filter table resolution between two frames
    static constexpr double maxCorrection = 0.005;

    // `target`: the fill level (frames) the controller keeps; `rate`: frames per second (time constants).
    void reset (int numChannels, int target, double rate)
    {
        channels = std::max (1, numChannels);
        targetFill = initialTarget = std::max (half + 1, target);
        targetRaises = 0;
        sampleRate = rate > 0.0 ? rate : 48000.0;
        capacity = 1;
        while (capacity < 4 * targetFill + 16384)
            capacity <<= 1;
        ring.assign ((size_t) capacity * (size_t) channels, 0.0f);
        writeIndex = readIndex = 0;
        frac = 0.0;
        lastPushTime = -1.0;
        lastPacket = 0;
        integral = 0.0;
        ratioNow = 1.0;
        underflows = overflows = 0;
        minRatio = maxRatio = 1.0;
        buildTable();
        // the history before the first frame is silence, and the FIFO starts at its target level
        writeIndex = half + targetFill;
        readIndex = half;
        smoothedFill = targetFill;
    }

    int numChannels() const { return channels; }
    int target() const { return targetFill; }
    int raises() const { return targetRaises; } // how often an underflow raised the target (the latency changed)
    double fill() const { return (double) (writeIndex - readIndex) - frac; }
    double ratio() const { return ratioNow; }
    double averageFill() const { return smoothedFill; }

    // Captured frames (interleaved), `now` = when they arrived (seconds, any steady clock; < 0 = unknown).
    // On overflow the oldest frames are dropped (counted once per event).
    void push (const float* interleaved, int frames, double now = -1.0)
    {
        if (frames <= 0)
            return;
        if (now >= 0.0)
        {
            lastPushTime = now;
            lastPacket = frames;
        }
        // keep `half` frames of history behind the read position for the filter
        if (writeIndex + frames - (readIndex - half) > capacity)
        {
            ++overflows;
            resyncTo (std::max (0, targetFill - frames));
        }
        for (int i = 0; i < frames; ++i)
        {
            float* dst = &ring[(size_t) ((writeIndex + i) & (capacity - 1)) * (size_t) channels];
            if (interleaved != nullptr)
                std::copy (interleaved + (size_t) i * (size_t) channels, interleaved + (size_t) (i + 1) * (size_t) channels, dst);
            else
                std::fill (dst, dst + channels, 0.0f);
        }
        writeIndex += frames;
        if (fill() > (double) (4 * targetFill + 4096)) // the host stopped reading: come back to the target
        {
            ++overflows;
            resyncTo (targetFill);
        }
    }

    void pushSilence (int frames) { push (nullptr, frames, -1.0); }

    // `target` is the level left in the FIFO right after a host block, averaged over time.
    // One host block: `frames` interleaved frames at the playback clock. Returns false when the FIFO ran dry
    // (the block is still complete: the stream is resynchronised with silence first).
    bool pull (float* out, int frames, double now = -1.0)
    {
        bool ok = true;
        const double needed = frames * ratioNow + half + 2;
        if (fill() < needed)
        {
            ++underflows;
            ok = false;
            // The packets came later than the target allows for: keep a larger level from now on (at most twice
            // the initial target), so a device with irregular timing clicks once instead of again and again.
            const int raised = std::min (2 * initialTarget, targetFill + (int) std::ceil (needed - fill()) + 32);
            if (raised > targetFill)
            {
                targetFill = raised;
                ++targetRaises;
            }
            // pad with silence so that the target level is left after this block; the level jump is not drift
            pushSilence ((int) std::ceil (needed - fill()) + targetFill);
            smoothedFill = targetFill;
        }

        for (int i = 0; i < frames; ++i)
        {
            const double p = frac * phases;
            const int ph = std::min (phases - 1, (int) p);
            const float mix = (float) (p - ph);
            const float* h0 = &table[(size_t) ph * taps];
            const float* h1 = &table[(size_t) (ph + 1) * taps];
            float* dst = out + (size_t) i * (size_t) channels;
            std::fill (dst, dst + channels, 0.0f);
            for (int k = 0; k < taps; ++k)
            {
                const float h = h0[k] + mix * (h1[k] - h0[k]);
                const float* x = &ring[(size_t) ((readIndex - half + 1 + k) & (capacity - 1)) * (size_t) channels];
                for (int c = 0; c < channels; ++c)
                    dst[c] += h * x[c];
            }
            frac += ratioNow;
            const auto whole = (std::int64_t) frac;
            readIndex += whole;
            frac -= (double) whole;
        }
        updateRatio (frames, now); // measured on what is left after the block, for the next one
        return ok;
    }

    // Moves the read position so that the level is the target now (once, when the stream has settled: devices
    // often deliver a burst of frames, or nothing, during their first periods). Call it between two blocks.
    void recenter (double now)
    {
        double level = fill();
        if (now >= 0.0 && lastPushTime >= 0.0)
            level += std::clamp ((now - lastPushTime) * sampleRate, 0.0, 4.0 * lastPacket);
        const auto shift = (std::int64_t) std::llround (level - targetFill);
        if (shift > 0)
            readIndex += std::min<std::int64_t> (shift, writeIndex - readIndex - half - 1);
        else if (shift < 0)
            pushSilence ((int) -shift); // rare: the device started late
        smoothedFill = targetFill;
        integral = 0.0;
        ratioNow = 1.0;
    }

    // statistics
    std::int64_t underflows = 0, overflows = 0;
    double minRatio = 1.0, maxRatio = 1.0;

private:
    void resyncTo (int level)
    {
        // drop the oldest frames (or step back) so that exactly `level` frames remain unread
        readIndex = writeIndex - level;
        frac = 0.0;
        smoothedFill = level;
        integral = 0.0;
    }

    void updateRatio (int frames, double now)
    {
        // The level seen by a block depends on when the last capture packet came: just after one it is a packet
        // higher than just before the next. The device has been recording since that packet, so those frames are
        // counted too ("virtual fill"); otherwise the slow phase walk between the two clocks would look like a
        // level change. Averaged over ~0.5 s, then PI control: 1000 ppm of correction per 500 frames of error; the
        // integral term absorbs a constant clock difference.
        double level = fill();
        if (now >= 0.0 && lastPushTime >= 0.0)
            level += std::clamp ((now - lastPushTime) * sampleRate, 0.0, 4.0 * lastPacket);
        const double a = std::min (1.0, frames / (0.5 * sampleRate));
        smoothedFill += a * (level - smoothedFill);
        const double err = smoothedFill - targetFill;
        integral = std::clamp (integral + 2.0e-12 * err * frames, -maxCorrection, maxCorrection);
        ratioNow = 1.0 + std::clamp (2.0e-6 * err + integral, -maxCorrection, maxCorrection);
        minRatio = std::min (minRatio, ratioNow);
        maxRatio = std::max (maxRatio, ratioNow);
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    void buildTable()
    {
        // h(t) for t = k - half + 1 - phase/phases (distance from the read position), cutoff 0.92 x Nyquist,
        // Kaiser window (beta 8); each phase normalised to unity gain at DC. Row `phases` = the next frame's row 0.
        constexpr double pi = 3.14159265358979323846, cutoff = 0.92, beta = 8.0;
        table.assign ((size_t) (phases + 1) * taps, 0.0f);
        for (int ph = 0; ph <= phases; ++ph)
        {
            const double f = (double) ph / phases;
            double sum = 0.0;
            std::vector<double> row (taps);
            for (int k = 0; k < taps; ++k)
            {
                const double t = (double) (k - half + 1) - f;
                const double x = pi * cutoff * t;
                const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (x) / x;
                const double w = t / half;
                const double win = std::abs (w) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - w * w)) / besselI0 (beta);
                row[(size_t) k] = cutoff * sinc * win;
                sum += row[(size_t) k];
            }
            for (int k = 0; k < taps; ++k)
                table[(size_t) ph * taps + (size_t) k] = (float) (row[(size_t) k] / sum);
        }
    }

    int channels = 1, targetFill = 256, initialTarget = 256, targetRaises = 0, capacity = 1;
    double sampleRate = 48000.0;
    std::vector<float> ring, table;
    std::int64_t writeIndex = 0, readIndex = 0; // absolute frame counters
    double frac = 0.0, smoothedFill = 0.0, integral = 0.0, ratioNow = 1.0;
    double lastPushTime = -1.0;
    int lastPacket = 0;
};
} // namespace lynxasio
