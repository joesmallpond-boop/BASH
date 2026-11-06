#include "bash/Warp.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bash
{
    namespace warp
    {
        namespace
        {
            constexpr double minBpm = 70.0, maxBpm = 180.0;

            // Onset strength per hop: rise in level, mostly of the low end (kicks carry the
            // beat), plus the slope of the full band (snares, hats) so beats without a kick still count.
            std::vector<float> onsetStrength (const SampleData& d, int hop, uint32_t maxFrames)
            {
                const uint32_t n = std::min (d.numFrames(), maxFrames);
                const int win = hop * 4;
                if (n < (uint32_t) (win * 4)) return {};
                const double sr = d.sampleRate > 0 ? d.sampleRate : 44100.0;
                const float lpC = (float) (1.0 - std::exp (-2.0 * 3.141592653589793 * 150.0 / sr));
                std::vector<float> low (n);
                float z1 = 0.0f, z2 = 0.0f;
                for (uint32_t i = 0; i < n; ++i) { z1 += (d.mono (i) - z1) * lpC; z2 += (z1 - z2) * lpC; low[i] = z2; }

                const int frames = (int) ((n - (uint32_t) win) / (uint32_t) hop);
                std::vector<float> logFull ((size_t) frames), logLow ((size_t) frames), odf ((size_t) frames, 0.0f);
                for (int k = 0; k < frames; ++k)
                {
                    double e = 0.0, el = 0.0;
                    const uint32_t base = (uint32_t) k * (uint32_t) hop;
                    for (int j = 1; j < win; j += 2)
                    {
                        const float dx = d.mono (base + (uint32_t) j) - d.mono (base + (uint32_t) j - 1);
                        e += (double) dx * dx;
                        el += (double) low[base + (uint32_t) j] * low[base + (uint32_t) j];
                    }
                    // amplitude (not log): a loud kick should count for more than a quiet hat
                    logFull[(size_t) k] = (float) std::sqrt (e);
                    logLow[(size_t) k]  = (float) std::sqrt (el);
                }
                const auto norm = [] (const std::vector<float>& x)
                {
                    float peak = 1.0e-9f; for (float v : x) peak = std::max (peak, v); return 1.0f / peak;
                };
                const float nl = norm (logLow), nf = norm (logFull);
                for (int k = 1; k < frames; ++k)
                    odf[(size_t) k] = 0.65f * std::max (0.0f, logLow[(size_t) k] - logLow[(size_t) k - 1]) * nl
                                    + 0.35f * std::max (0.0f, logFull[(size_t) k] - logFull[(size_t) k - 1]) * nf;
                // remove the slow average so the autocorrelation sees the pulse, not the level
                const float mean = std::accumulate (odf.begin(), odf.end(), 0.0f) / (float) frames;
                for (auto& v : odf) v -= mean;
                return odf;
            }
        }

        TempoEstimate detectTempo (const SampleData& d)
        {
            TempoEstimate out;
            const double sr = d.sampleRate > 0 ? d.sampleRate : 44100.0;
            const int hop = std::max (64, (int) std::lround (sr / 200.0));   // ~5 ms
            const auto odf = onsetStrength (d, hop, (uint32_t) (sr * 90.0));
            const double hopSec = (double) hop / sr;
            const double duration = d.numFrames() / sr;

            double acBpm = 0.0;
            if (odf.size() > 64)
            {
                const int lagMin = std::max (2, (int) std::floor (60.0 / maxBpm / hopSec));
                const int lagMax = std::min ((int) odf.size() / 2, (int) std::ceil (60.0 / minBpm / hopSec) * 4);
                auto ac = [&] (int lag)
                {
                    if (lag <= 0 || lag >= (int) odf.size()) return 0.0;
                    double s = 0.0;
                    for (size_t k = 0; k + (size_t) lag < odf.size(); ++k) s += (double) odf[k] * odf[k + (size_t) lag];
                    return s / (double) (odf.size() - (size_t) lag);
                };
                std::vector<double> r ((size_t) lagMax + 1, 0.0);
                for (int lag = lagMin / 2; lag <= lagMax; ++lag) r[(size_t) lag] = ac (lag);
                const double r0 = std::max (1.0e-12, ac (0) > 0 ? ac (0) : 1.0e-12);

                double best = -1.0e30; int bestLag = 0;
                const int beatLagMax = (int) std::ceil (60.0 / minBpm / hopSec);
                for (int lag = lagMin; lag <= std::min (beatLagMax, lagMax); ++lag)
                {
                    // the beat and its multiples (bar structure) should all line up
                    double score = r[(size_t) lag];
                    if (2 * lag <= lagMax) score += 0.5 * r[(size_t) (2 * lag)];
                    if (4 * lag <= lagMax) score += 0.25 * r[(size_t) (4 * lag)];
                    const double bpm = 60.0 / (lag * hopSec);
                    const double oct = std::log2 (bpm / 120.0);
                    score *= std::exp (-0.5 * oct * oct / (0.6 * 0.6));   // gentle preference for ~120
                    if (score > best) { best = score; bestLag = lag; }
                }
                if (bestLag > 0)
                {
                    // half-time vs double-time: if the pulse twice as fast is nearly as strong, it's the beat
                    const int half = bestLag / 2;
                    if (half >= lagMin && 60.0 / (half * hopSec) <= maxBpm)
                    {
                        double rh = -1.0e30; int hl = half;
                        for (int l = half - 1; l <= half + 1; ++l) if (l > 0 && r[(size_t) l] > rh) { rh = r[(size_t) l]; hl = l; }
                        if (rh > 0.85 * r[(size_t) bestLag]) bestLag = hl;
                    }
                    // parabolic refinement of the peak
                    double lag = bestLag;
                    if (bestLag > 1 && bestLag + 1 < (int) r.size())
                    {
                        const double a = r[(size_t) bestLag - 1], b = r[(size_t) bestLag], c = r[(size_t) bestLag + 1];
                        const double den = a - 2.0 * b + c;
                        if (std::abs (den) > 1.0e-12) lag += std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
                    }
                    acBpm = 60.0 / (lag * hopSec);
                    out.confidence = (float) std::clamp (r[(size_t) bestLag] / r0, 0.0, 1.0);
                }
            }

            // Loops: if the file is a whole number of beats at (about) that tempo, use the exact length.
            if (duration > 0.5)
            {
                int bestN = 0; double bestErr = 1.0e9;
                for (int n = 1; n <= 256; ++n)
                {
                    const double bpm = 60.0 * n / duration;
                    if (bpm < minBpm || bpm > maxBpm) continue;
                    const double err = acBpm > 0.0 ? std::abs (std::log (bpm / acBpm)) : (n % 4 == 0 ? 0.0 : 1.0);
                    const double tol = (n % 4 == 0) ? 0.015 : 0.006;   // bar-length loops snap more readily
                    if (err < tol && err < bestErr) { bestErr = err; bestN = n; }
                }
                if (bestN > 0)
                {
                    out.bpm = 60.0 * bestN / duration;
                    out.beats = bestN;
                    if (acBpm <= 0.0) out.confidence = 0.3f;
                    return out;
                }
            }

            if (acBpm <= 0.0) return out;   // nothing to go on: 120
            while (acBpm < minBpm) acBpm *= 2.0;
            while (acBpm > maxBpm) acBpm *= 0.5;
            out.bpm = std::round (acBpm * 100.0) / 100.0;
            return out;
        }

        std::shared_ptr<SampleData> stretch (const SampleData& in, double ratio)
        {
            auto out = std::make_shared<SampleData>();
            out->sampleRate = in.sampleRate;
            out->path = in.path;
            const int nIn = (int) in.numFrames();
            ratio = std::clamp (ratio, 0.25, 4.0);
            const double sr = in.sampleRate > 0 ? in.sampleRate : 44100.0;

            int frame = (int) std::lround (sr * 0.040);
            frame += frame & 1;
            const int hs = frame / 2;                         // output hop (50% overlap)
            if (std::abs (ratio - 1.0) < 1.0e-4 || nIn < frame * 2)
            {
                out->left = in.left; out->right = in.right;
                return out;
            }

            const int nOut = (int) std::lround (nIn * ratio);
            const int tol = (int) std::lround (sr * 0.012);   // how far a frame may move to line up
            const int pad = hs;

            // padded mono copy for matching, padded stereo for output
            auto padded = [&] (const std::vector<float>& x)
            {
                std::vector<float> p ((size_t) (nIn + 2 * pad + frame + tol), 0.0f);
                std::copy (x.begin(), x.end(), p.begin() + pad);
                return p;
            };
            const auto pl = padded (in.left), pr = padded (in.right);
            std::vector<float> m (pl.size());
            for (size_t i = 0; i < m.size(); ++i) m[i] = pl[i] + pr[i];
            const int nPadIn = nIn + 2 * pad;

            std::vector<float> win ((size_t) frame);
            for (int i = 0; i < frame; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * 3.141592653589793 * i / frame);

            std::vector<float> ol ((size_t) (nOut + 2 * pad + frame), 0.0f), orr (ol.size(), 0.0f);
            int prevPos = 0;
            const int maxPos = std::max (0, nPadIn - frame);
            for (int k = 0; ; ++k)
            {
                const int outStart = k * hs;
                if (outStart >= nOut + pad) break;
                // output sample (outStart - pad) corresponds to input sample (outStart - pad) / ratio
                const double nominal = ((double) outStart - pad) / ratio + pad;
                int pos = std::clamp ((int) std::lround (nominal), 0, maxPos);
                if (k > 0)
                {
                    // WSOLA: pick the frame start near `nominal` whose start best continues the previous frame
                    const int target = std::min (prevPos + hs, maxPos);
                    double best = -1.0e30; int bestPos = pos;
                    const int from = std::max (0, pos - tol), to = std::min (maxPos, pos + tol);
                    for (int c = from; c <= to; c += 2)
                    {
                        double xy = 0.0, yy = 1.0e-9;
                        for (int j = 0; j < hs; j += 4)
                        {
                            const float y = m[(size_t) (c + j)];
                            xy += (double) y * m[(size_t) (target + j)];
                            yy += (double) y * y;
                        }
                        const double score = xy / std::sqrt (yy);
                        if (score > best) { best = score; bestPos = c; }
                    }
                    pos = bestPos;
                }
                for (int i = 0; i < frame; ++i)
                {
                    const size_t o = (size_t) (outStart + i);
                    if (o >= ol.size()) break;
                    ol[o]  += pl[(size_t) (pos + i)] * win[(size_t) i];
                    orr[o] += pr[(size_t) (pos + i)] * win[(size_t) i];
                }
                prevPos = pos;
            }

            out->left.assign (ol.begin() + pad, ol.begin() + pad + nOut);
            out->right.assign (orr.begin() + pad, orr.begin() + pad + nOut);
            return out;
        }
    }
}
