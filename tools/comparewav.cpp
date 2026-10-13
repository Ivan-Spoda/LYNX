// comparewav: compares two WAVs (e.g. FL render vs LYNX render of the same project).
// Usage: comparewav reference.wav test.wav
// Shows the level (RMS and peak), the average spectrum in octave bands (difference in dB), the brightness
// (spectral centroid) and the correlation of the volume envelopes (10 ms) after aligning the start.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "WavReader.h"

namespace
{
using Signal = std::vector<float>;

Signal mono (const lynx::SampleData& s)
{
    Signal out (s.frames());
    const auto* l = s.leftData();
    const auto* r = s.rightData();
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = 0.5f * (l[i] + r[i]);
    return out;
}

double rmsDb (const Signal& x)
{
    double e = 0.0;
    for (auto v : x)
        e += (double) v * v;
    return 10.0 * std::log10 (std::max (1e-20, e / std::max<std::size_t> (1, x.size())));
}

double peakDb (const Signal& x)
{
    float p = 0.0f;
    for (auto v : x)
        p = std::max (p, std::abs (v));
    return 20.0 * std::log10 (std::max (1e-9f, p));
}

void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -2.0 * 3.141592653589793 / (double) len);
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> wn (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                wn *= w;
            }
        }
    }
}

// average power spectrum (4096-sample Hann windows)
std::vector<double> spectrum (const Signal& x)
{
    constexpr std::size_t n = 4096;
    std::vector<double> power (n / 2, 0.0);
    int frames = 0;

    for (std::size_t start = 0; start + n <= x.size(); start += n / 2)
    {
        std::vector<std::complex<double>> a (n);
        for (std::size_t i = 0; i < n; ++i)
            a[i] = x[start + i] * (0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) i / (double) n));
        fft (a);
        for (std::size_t k = 0; k < n / 2; ++k)
            power[k] += std::norm (a[k]);
        ++frames;
    }

    for (auto& p : power)
        p /= std::max (1, frames);
    return power;
}

std::vector<double> envelope (const Signal& x, double rate, std::size_t offset)
{
    const auto hop = (std::size_t) (rate * 0.01);
    std::vector<double> env;
    for (std::size_t start = offset; start + hop <= x.size(); start += hop)
    {
        double e = 0.0;
        for (std::size_t i = start; i < start + hop; ++i)
            e += (double) x[i] * x[i];
        env.push_back (std::sqrt (e / (double) hop));
    }
    return env;
}

std::size_t onset (const Signal& x)
{
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > 0.001f)
            return i;
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: comparewav reference.wav test.wav\n");
        return 2;
    }

    const auto a = lynx::tools::readWav (argv[1]);
    const auto b = lynx::tools::readWav (argv[2]);

    if (a == nullptr || b == nullptr)
    {
        std::fprintf (stderr, "could not read both WAVs (PCM 16/24/32 or float)\n");
        return 1;
    }

    const auto x = mono (*a), y = mono (*b);
    std::printf ("RMS level: %.1f dB vs %.1f dB (difference %+.1f)\n", rmsDb (x), rmsDb (y), rmsDb (y) - rmsDb (x));
    std::printf ("peak: %.1f dB vs %.1f dB\n", peakDb (x), peakDb (y));

    // octave bands starting at 31 Hz
    const auto sx = spectrum (x), sy = spectrum (y);
    const auto binHz = a->sampleRate / 4096.0;
    double centroidX = 0.0, centroidY = 0.0, sumX = 0.0, sumY = 0.0;
    std::printf ("octaves (dB test − reference):");

    for (double lo = 31.25; lo < 16000.0; lo *= 2.0)
    {
        double ex = 0.0, ey = 0.0;
        for (std::size_t k = (std::size_t) (lo / binHz); k < std::min (sx.size(), (std::size_t) (2.0 * lo / binHz)); ++k)
        {
            ex += sx[k];
            ey += k < sy.size() ? sy[k] : 0.0;
        }
        std::printf (" %g:%+.1f", lo * 1.414, 10.0 * std::log10 (std::max (1e-20, ey) / std::max (1e-20, ex)));
    }

    for (std::size_t k = 0; k < std::min (sx.size(), sy.size()); ++k)
    {
        centroidX += sx[k] * (double) k * binHz;
        centroidY += sy[k] * (double) k * binHz;
        sumX += sx[k];
        sumY += sy[k];
    }

    std::printf ("\nbrightness (centroid): %.0f Hz vs %.0f Hz\n", centroidX / std::max (1e-20, sumX), centroidY / std::max (1e-20, sumY));

    // envelopes aligned by the first sound
    const auto ex = envelope (x, a->sampleRate, onset (x)), ey = envelope (y, b->sampleRate, onset (y));
    const auto n = std::min (ex.size(), ey.size());
    double mx = 0.0, my = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        mx += ex[i];
        my += ey[i];
    }
    mx /= std::max<std::size_t> (1, n);
    my /= std::max<std::size_t> (1, n);
    double cov = 0.0, vx = 0.0, vy = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        cov += (ex[i] - mx) * (ey[i] - my);
        vx += (ex[i] - mx) * (ex[i] - mx);
        vy += (ey[i] - my) * (ey[i] - my);
    }
    std::printf ("envelope: correlation %.2f over %.1f s; onset %.1f ms vs %.1f ms\n", cov / std::sqrt (std::max (1e-20, vx * vy)),
                 (double) n * 0.01, onset (x) * 1000.0 / a->sampleRate, onset (y) * 1000.0 / b->sampleRate);
    return 0;
}
