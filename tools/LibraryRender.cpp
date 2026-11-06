// Renders the whole factory library to a folder and prints measurements for checking:
// length, peak, RMS, spectral centroid (brightness), low-end share, L/R balance.
#include "bash/Library.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace bash;

static void writeWav (const fs::path& p, const SampleData& d)
{
    fs::create_directories (p.parent_path());
    std::ofstream f (p, std::ios::binary);
    const uint32_t n = d.numFrames(), sr = (uint32_t) d.sampleRate, bytes = n * 2 * 2;
    auto u32 = [&f] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&f] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + bytes); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
    f.write ("data", 4); u32 (bytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { d.left[i], d.right[i] })
        {
            const int16_t s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f);
            f.write ((const char*) &s, 2);
        }
}

static void measure (const char* kind, const std::string& name, const SampleData& d, double ms)
{
    double peak = 0, sum = 0, l2 = 0, r2 = 0, low = 0, num = 0, den = 0;
    float z1 = 0, z2 = 0; const float c = 1.0f - std::exp (-2.0f * 3.14159f * 150.0f / (float) d.sampleRate);
    float prev = 0;
    for (uint32_t i = 0; i < d.numFrames(); ++i)
    {
        const float m = d.mono (i);
        peak = std::max (peak, (double) std::max (std::abs (d.left[i]), std::abs (d.right[i])));
        sum += m * m; l2 += d.left[i] * d.left[i]; r2 += d.right[i] * d.right[i];
        z1 += (m - z1) * c; z2 += (z1 - z2) * c; low += z2 * z2;
        const float dm = m - prev; prev = m;
        num += dm * dm; den += m * m;   // slope energy vs energy ~ brightness
    }
    const double n = std::max (1u, d.numFrames());
    const double rmsDb = 10 * std::log10 (sum / n + 1e-20), lowShare = low / (sum + 1e-20);
    const double centroid = std::sqrt (num / (den + 1e-20)) * d.sampleRate / (2 * 3.14159);
    const double bal = 10 * std::log10 ((l2 + 1e-20) / (r2 + 1e-20));
    std::printf ("%-5s %-34s %6.2fs pk %5.1f rms %6.1f  bright %6.0fHz  low %3.0f%%  L/R %+5.1f  %5.0fms\n",
                 kind, name.c_str(), n / d.sampleRate, 20 * std::log10 (peak + 1e-20), rmsDb, centroid, lowShare * 100, bal, ms);
}

int main (int argc, char** argv)
{
    const fs::path out = argc > 1 ? argv[1] : "bash-library";
    auto t0 = std::chrono::steady_clock::now();
    int bad = 0;
    for (const auto& s : library::oneShots())
    {
        auto a = std::chrono::steady_clock::now();
        auto d = s.render();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - a).count();
        measure ("shot", s.category + "/" + s.name, *d, ms);
        for (uint32_t i = 0; i < d->numFrames(); ++i) if (! std::isfinite (d->left[i]) || ! std::isfinite (d->right[i])) { ++bad; break; }
        writeWav (out / (library::oneShotPath (s) + ".wav"), *d);
    }
    for (const auto& l : library::loops())
    {
        auto a = std::chrono::steady_clock::now();
        auto d = l.render();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - a).count();
        measure ("loop", l.category + "/" + l.name, *d, ms);
        const double expected = l.bars * 4 * 60.0 / l.bpm * d->sampleRate;
        if (std::abs ((double) d->numFrames() - expected) > 1.0) { std::printf ("  LENGTH WRONG\n"); ++bad; }
        writeWav (out / (library::loopPath (l) + ".wav"), *d);
    }
    for (const auto& k : library::kits())
        for (const auto& p : k.pads)
            if (! p.sound.empty() && library::findOneShot (p.sound) == nullptr) { std::printf ("kit %s: missing %s\n", k.name.c_str(), p.sound.c_str()); ++bad; }
    std::printf ("%zu one-shots, %zu loops, %zu kits, %zu grooves in %.1f s, %d problems\n",
                 library::oneShots().size(), library::loops().size(), library::kits().size(), grooves::all().size(),
                 std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), bad);
    return bad;
}
