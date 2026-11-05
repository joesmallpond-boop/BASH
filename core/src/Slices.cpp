#include "bash/Slices.h"
#include "bash/Rng.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    namespace slicing
    {
        void normalise (std::vector<uint32_t>& s, uint32_t numFrames, uint32_t minGap)
        {
            s.erase (std::remove_if (s.begin(), s.end(), [numFrames] (uint32_t f) { return f >= numFrames; }), s.end());
            s.push_back (0);
            std::sort (s.begin(), s.end());
            std::vector<uint32_t> out;
            out.reserve (s.size());
            for (auto f : s)
                if (out.empty() || f - out.back() >= minGap)
                    out.push_back (f);
            if (out.size() > (size_t) maxSlices)
                out.resize ((size_t) maxSlices);
            if (numFrames == 0) out.clear();
            s.swap (out);
        }

        std::vector<uint32_t> grid (uint32_t numFrames, int divisions)
        {
            std::vector<uint32_t> s;
            divisions = std::clamp (divisions, 1, maxSlices);
            for (int i = 0; i < divisions; ++i)
                s.push_back ((uint32_t) ((uint64_t) numFrames * (uint64_t) i / (uint64_t) divisions));
            normalise (s, numFrames, 1);
            return s;
        }

        std::vector<uint32_t> random (uint32_t numFrames, int count, uint64_t seed, double sr)
        {
            std::vector<uint32_t> s;
            count = std::clamp (count, 1, maxSlices);
            const uint32_t minGap = (uint32_t) std::max (32.0, sr * 0.01);
            // draw extra candidates so spacing rules still leave ~count slices
            for (int i = 1; (int) s.size() < count * 2 && i < count * 8; ++i)
                s.push_back ((uint32_t) (unitFloat (seed, (uint64_t) i, 0x511CEull) * (float) numFrames));
            normalise (s, numFrames, minGap);
            if ((int) s.size() > count) s.resize ((size_t) count);
            return s;
        }

        std::vector<uint32_t> transients (const SampleData& d, float sensitivity, int maxCount)
        {
            const uint32_t n = d.numFrames();
            const int hop = 256, win = 512;
            if (n < (uint32_t) win * 2) return { 0 };

            // Onset strength: rise in log energy of the first difference (favours attacks).
            const int frames = (int) ((n - (uint32_t) win) / (uint32_t) hop);
            std::vector<float> logE ((size_t) frames), odf ((size_t) frames, 0.0f);
            for (int k = 0; k < frames; ++k)
            {
                double e = 0.0;
                const uint32_t base = (uint32_t) (k * hop);
                for (int j = 1; j < win; ++j)
                {
                    const float dx = d.mono (base + (uint32_t) j) - d.mono (base + (uint32_t) j - 1);
                    e += (double) dx * dx;
                }
                logE[(size_t) k] = (float) std::log (e + 1.0e-9);
            }
            for (int k = 1; k < frames; ++k)
                odf[(size_t) k] = std::max (0.0f, logE[(size_t) k] - logE[(size_t) k - 1]);

            const float sens  = std::clamp (sensitivity, 0.0f, 1.0f);
            const float delta = 0.3f + (1.0f - sens) * 2.7f;
            const int   minSpacing = std::max (1, (int) (d.sampleRate * (0.03 + 0.12 * (1.0 - sens)) / hop));

            std::vector<uint32_t> s { 0 };
            int last = -minSpacing;
            for (int k = 1; k < frames - 1; ++k)
            {
                float mean = 0.0f; int cnt = 0;
                for (int j = std::max (0, k - 8); j <= std::min (frames - 1, k + 8); ++j) { mean += odf[(size_t) j]; ++cnt; }
                mean /= (float) cnt;
                const float v = odf[(size_t) k];
                if (v > mean + delta && v >= odf[(size_t) k - 1] && v >= odf[(size_t) k + 1] && k - last >= minSpacing)
                {
                    s.push_back ((uint32_t) std::max (0, k * hop + win / 2 - hop));
                    last = k;
                }
            }
            normalise (s, n);
            if ((int) s.size() > maxCount) s.resize ((size_t) maxCount);
            return s;
        }

        int sliceAt (const std::vector<uint32_t>& s, uint32_t frame)
        {
            if (s.empty()) return -1;
            auto it = std::upper_bound (s.begin(), s.end(), frame);
            return (int) (it - s.begin()) - 1;
        }
    }

    // -------------------------------------------------------------------------
    void SliceVoice::start (const SampleData* sample, uint32_t a, uint32_t b, double rate, float gain, const Params& p)
    {
        if (sample == nullptr || b <= a + 1) return;
        if (cur.active) { old = cur; old.releasing = true; old.fast = true; }

        cur = {};
        cur.sample  = sample;
        cur.lo      = (double) a;
        cur.hi      = (double) b - 1.0;
        cur.reverse = p.reverse;
        cur.pos     = p.reverse ? cur.hi : cur.lo;
        cur.inc     = std::max (0.01, rate);
        cur.gain    = gain;
        cur.env     = 0.0f;
        cur.active  = true;
    }

    void SliceVoice::stopIfUsing (const SampleData* s)
    {
        if (cur.sample == s) cur = {};
        if (old.sample == s) old = {};
    }

    void SliceVoice::render (float* l, float* r, int start, int n, const Params& p)
    {
        if (old.active) renderOne (old, l, r, start, n, p);
        if (cur.active) renderOne (cur, l, r, start, n, p);
    }

    void SliceVoice::renderOne (Play& v, float* l, float* r, int start, int n, const Params& p)
    {
        const SampleData& d = *v.sample;
        const float* L = d.left.data();
        const float* R = d.right.data();
        const int last = (int) d.numFrames() - 1;
        const float attackStep = 1.0f / (float) std::max (1.0, p.attack * sr);
        const float relCoef    = std::exp (-1.0f / (float) std::max (1.0, (v.fast ? 0.004 : std::max (0.002f, p.release)) * sr));
        const double edgeFrames = std::max (16.0, 0.002 * d.sampleRate);   // anti-click fade at slice ends

        auto tap = [last] (const float* x, int i) { return x[std::clamp (i, 0, last)]; };

        for (int i = start; i < start + n; ++i)
        {
            if (v.releasing) { v.env *= relCoef; if (v.env < 1.0e-4f) { v.active = false; return; } }
            else             v.env = std::min (1.0f, v.env + attackStep);

            const double remaining = v.reverse ? v.pos - v.lo : v.hi - v.pos;
            if (remaining <= 0.0) { v.active = false; return; }
            const float edge = (float) std::min (1.0, remaining / edgeFrames);

            // cubic hermite interpolation
            const int    i0 = (int) std::floor (v.pos);
            const float  t  = (float) (v.pos - i0);
            auto hermite = [&] (const float* x)
            {
                const float xm1 = tap (x, i0 - 1), x0 = tap (x, i0), x1 = tap (x, i0 + 1), x2 = tap (x, i0 + 2);
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * t + c2) * t + c1) * t + x0;
            };

            const float g = v.gain * v.env * edge;
            l[i] += hermite (L) * g;
            r[i] += hermite (R) * g;
            v.pos += v.reverse ? -v.inc : v.inc;
        }
    }
}
