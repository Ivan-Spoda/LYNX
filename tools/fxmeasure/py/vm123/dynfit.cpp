// dynfit: runs a parametric dynamics model on jobs and prints per-block normalized errors (for LM) or writes outputs.
// usage: dynfit <model> <jobs.txt> <params...>   (env OUT=dir writes model outputs)
// jobs line: in.wav ref.wav lat weight k1=v1 k2=v2 ...
#include "wav.h"
#include <cmath>
#include <map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <functional>
#include <omp.h>
using P = std::vector<double>;
using Knobs = std::map<std::string, double>;
static double kn (const Knobs& k, const char* n, double d) { auto it = k.find (n); return it == k.end() ? d : it->second; }
inline double coefMs (double ms, double sr = 48000.0) { return ms <= 0 ? 1.0 : 1.0 - std::exp (-1000.0 / (ms * sr)); }
inline double db (double g) { return 20.0 * std::log10 (std::max (g, 1e-12)); }
inline double undb (double d) { return std::pow (10.0, d / 20.0); }
#include "models.inc"
struct Job { std::string in, ref; int lat; double w; Knobs k; };
int main (int argc, char** argv)
{
    std::string model = argv[1]; std::ifstream jf (argv[2]); P p;
    for (int i = 3; i < argc; ++i) p.push_back (std::atof (argv[i]));
    const char* out = std::getenv ("OUT"); const int block = 480;
    std::string line; std::vector<Job> jobs;
    std::map<std::string, Wav> cache;
    while (std::getline (jf, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls (line); Job j; ls >> j.in >> j.ref >> j.lat >> j.w; std::string kv;
        while (ls >> kv) { auto e = kv.find ('='); j.k[kv.substr (0, e)] = std::atof (kv.substr (e + 1).c_str()); }
        if (! cache.count (j.in)) cache[j.in] = readWav (j.in);
        if (! cache.count (j.ref)) cache[j.ref] = readWav (j.ref);
        jobs.push_back (j);
    }
    std::vector<std::string> res (jobs.size()); std::vector<double> nulls (jobs.size());
    #pragma omp parallel for schedule(dynamic)
    for (int ji = 0; ji < (int) jobs.size(); ++ji)
    {
        const Job& j = jobs[(size_t) ji];
        const Wav& x = cache.at (j.in); const Wav& y = cache.at (j.ref);
        std::vector<float> l = x.l, r = x.r;
        runModel (model, l, r, j.k, p);
        if (out) writeWav (std::string (out) + "/m" + std::to_string (ji) + ".wav", l, r);
        const size_t n = std::min (l.size(), y.l.size() - (size_t) j.lat);
        double eref = 0; for (size_t i = 0; i < n; ++i) eref += (double) y.l[i + j.lat] * y.l[i + j.lat] + (double) y.r[i + j.lat] * y.r[i + j.lat];
        double etot = 0; std::string s = "J" + std::to_string (ji); char buf[32];
        if (std::getenv ("ERRGAIN"))
        {
            const int gb = 96; double ein = 0; for (size_t i = 0; i < n; ++i) ein = std::max (ein, (double) std::abs (x.l[i]));
            for (size_t b0 = 0; b0 + gb <= n; b0 += gb)
            {
                double ex = 0, er = 0, em = 0;
                for (size_t i = b0; i < b0 + gb; ++i) { ex += (double) x.l[i] * x.l[i]; er += (double) y.l[i + j.lat] * y.l[i + j.lat]; em += (double) l[i] * l[i]; }
                if (ex < gb * 1e-6 * ein * ein) continue;
                const double d = 10 * std::log10 ((er + 1e-30) / (em + 1e-30));
                etot += d * d; std::snprintf (buf, sizeof buf, " %.6g", d * std::sqrt (j.w) * 0.01); s += buf;
            }
            res[(size_t) ji] = s; nulls[(size_t) ji] = etot; continue;
        }
        for (size_t b0 = 0; b0 + block <= n; b0 += block)
        {
            double e = 0; for (size_t i = b0; i < b0 + block; ++i) { double a = l[i] - y.l[i + j.lat], c = r[i] - y.r[i + j.lat]; e += a * a + c * c; }
            etot += e; std::snprintf (buf, sizeof buf, " %.6g", std::sqrt (j.w * e / eref)); s += buf;
        }
        res[(size_t) ji] = s; nulls[(size_t) ji] = etot / eref;
    }
    double tot = 0, totw = 0;
    for (size_t ji = 0; ji < jobs.size(); ++ji)
    {
        std::printf ("%s\n", res[ji].c_str()); std::fprintf (stderr, "job %zu null %.2f dB\n", ji, 10 * std::log10 (nulls[ji] + 1e-30));
        tot += jobs[ji].w * nulls[ji]; totw += jobs[ji].w;
    }
    std::fprintf (stderr, "TOTAL %.3f dB\n", 10 * std::log10 (tot / totw + 1e-30));
}
