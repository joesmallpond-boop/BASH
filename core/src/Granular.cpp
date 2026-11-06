#include "bash/Granular.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        float softClip (float x) { return std::tanh (x); }
        double wrap01 (double x) { x -= std::floor (x); return x; }
        uint32_t hash32 (uint64_t x)
        {
            x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33;
            return (uint32_t) x;
        }
        float hashUnit (uint64_t x, uint64_t salt) { return (float) (hash32 (x * 0x9E3779B97F4A7C15ull + salt) >> 8) / 16777216.0f; }
    }

    const char* grainWindowName (GrainWindow w)
    {
        switch (w)
        {
            case GrainWindow::Flat:  return "Flat";
            case GrainWindow::Perc:  return "Perc";
            case GrainWindow::Swell: return "Swell";
            default:                 return "Smooth";
        }
    }

    const std::vector<std::pair<const char*, std::array<int, 4>>>& resonatorChords()
    {
        static const std::vector<std::pair<const char*, std::array<int, 4>>> c {
            { "Root",    { 0, 12, 19, 24 } }, { "Fifths", { 0, 7, 12, 19 } }, { "Minor", { 0, 3, 7, 12 } }, { "Major", { 0, 4, 7, 12 } },
            { "Sus",     { 0, 5, 7, 14 } },   { "Minor 7", { 0, 3, 7, 10 } }, { "Cluster", { 0, 2, 3, 7 } } };
        return c;
    }

    const std::vector<std::pair<const char*, double>>& granularDivisions()
    {
        static const std::vector<std::pair<const char*, double>> d {
            { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 },
            { "1/8D", 0.75 }, { "1/4", 1.0 }, { "1/2", 2.0 } };
        return d;
    }

    // ---------------------------------------------------------------------------------
    float GranularEngine::Comb::process (float x, double delay, float g, float damp)
    {
        const size_t len = buf.size();
        double rp = (double) w - delay;
        while (rp < 0) rp += (double) len;
        const size_t i0 = (size_t) rp % len, i1 = (i0 + 1) % len;
        const float fr = (float) (rp - std::floor (rp));
        const float read = buf[i0] + (buf[i1] - buf[i0]) * fr;
        lp += (read - lp) * damp;
        const float y = softClip (x + g * lp);
        buf[w] = y;
        w = (w + 1) % len;
        return y;
    }

    void GranularEngine::prepare (double sampleRate, int block)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        maxBlock = std::max (1, block);
        ringLen = (size_t) (bufferSeconds * sr);
        ringL.assign (ringLen, 0.0f); ringR.assign (ringLen, 0.0f);
        writePos = 0;
        for (auto* v : { &wetL, &wetR, &dryL, &dryR }) v->assign ((size_t) maxBlock, 0.0f);
        for (auto& c : combs) { c.buf.assign ((size_t) (sr / 15.0), 0.0f); c.w = 0; c.lp = 0.0f; }
        dlyL.assign ((size_t) (sr * 4.5), 0.0f); dlyR.assign (dlyL.size(), 0.0f); dlyW = 0;
        reverb.prepare (sr, maxBlock);
        histLen = (size_t) (sr * 4.0);
        histL.assign (histLen, 0.0f); histR.assign (histLen, 0.0f); histW = 0;
        takeL.assign ((size_t) (maxTakeSeconds * sr), 0.0f); takeR.assign (takeL.size(), 0.0f);
        takeLen.store (0);
        reset();
    }

    void GranularEngine::reset()
    {
        for (auto& g : grains) g.active = false;
        spawnPhase.fill (0.0); scanOffset.fill (0.0);
        driftPos.fill (0.0f); driftPitch.fill (0.0f); driftTarget.fill (0.0f); driftPitchTarget.fill (0.0f);
        f1L = f2L = f1R = f2R = 0.0f;
        dlyLpL = dlyLpR = 0.0f;
        glitchMode = -1; glitchStep = -1; glitchFade = 0.0f;
        for (auto& b : bins) b.store (0.0f);
        for (auto& h : headPos) h.store (0.0f);
        loopPos = 0;
    }

    float GranularEngine::overview (int k) const noexcept
    {
        const int newest = writeBin.load();
        return bins[(size_t) ((newest + 1 + k) % overviewBins)].load();
    }

    float GranularEngine::readRing (double pos, int ch) const noexcept
    {
        const auto& ring = ch == 0 ? ringL : ringR;
        const size_t i0 = (size_t) pos % ringLen, i1 = (i0 + 1) % ringLen;
        const float fr = (float) (pos - std::floor (pos));
        return ring[i0] + (ring[i1] - ring[i0]) * fr;
    }

    float GranularEngine::window (GrainWindow w, double ph) const noexcept
    {
        switch (w)
        {
            case GrainWindow::Flat:
            {
                const double e = 0.2;
                if (ph < e)       { const double s = std::sin (0.5 * pi * ph / e); return (float) (s * s); }
                if (ph > 1.0 - e) { const double s = std::sin (0.5 * pi * (1.0 - ph) / e); return (float) (s * s); }
                return 1.0f;
            }
            case GrainWindow::Perc:  return ph < 0.02 ? (float) (ph / 0.02) : (float) std::pow (1.0 - (ph - 0.02) / 0.98, 2.5);
            case GrainWindow::Swell: return ph > 0.98 ? (float) ((1.0 - ph) / 0.02) : (float) std::pow (ph / 0.98, 2.5);
            default: { const double s = std::sin (pi * ph); return (float) (s * s); }
        }
    }

    void GranularEngine::spawn (int h, const GrainHeadParams& hp, const GranularParams& p, const Scale& scale, int offset)
    {
        Grain* g = nullptr;
        for (auto& c : grains) if (! c.active) { g = &c; break; }
        if (g == nullptr) return;   // all busy: skip (density is capped by the pool)

        const double lenSec = 0.001 * std::pow (2000.0, std::clamp ((double) hp.size, 0.0, 1.0));
        const int length = std::max (16, (int) (lenSec * sr));

        double semis = hp.pitch + driftPitch[(size_t) h] * hp.drift * 3.0 + hp.pitchSpray * (unit() * 2.0f - 1.0f);
        if (p.keyLock)
        {
            const int base = 60 + scale.root();
            semis = (double) (scale.quantize (base + (int) std::lround (semis)) - base);
        }
        double rate = std::pow (2.0, std::clamp (semis, -36.0, 36.0) / 12.0);
        if (unit() < hp.reverse) rate = -rate;

        const double headP = headPos[(size_t) h].load();
        const double pos = std::clamp (headP + hp.posSpray * (unit() * 2.0f - 1.0f) * 0.5, 0.0, 1.0);
        double ago = (1.0 - pos) * (double) ringLen;
        if (! p.freeze)
        {
            // keep the grain inside written audio: it mustn't catch the write head or be overwritten
            const double lo = rate > 1.0 ? length * (rate - 1.0) + 64.0 : 64.0;
            const double hi = (double) ringLen - 64.0 - length * std::max (0.0, 1.0 - rate);
            ago = std::clamp (ago, lo, std::max (lo, hi));
        }
        const double writeAtSpawn = (double) ((writePos + ringLen - (size_t) (std::max (0, maxBlockWritten - offset))) % ringLen);
        double start = writeAtSpawn - ago;
        while (start < 0) start += (double) ringLen;

        const float pan = std::clamp (hp.pan + p.spread * (unit() * 2.0f - 1.0f), -1.0f, 1.0f);
        const float a = (float) ((pan + 1.0) * 0.25 * pi);
        // many overlapping grains add up: scale by the expected overlap
        const double rateHz = 0.5 * std::pow (300.0, std::clamp ((double) hp.density, 0.0, 1.0));
        const float norm = (float) (1.0 / std::sqrt (std::max (1.0, rateHz * lenSec)));
        g->active = true;
        g->pos = start;
        g->rate = rate;
        g->age = -offset;   // starts `offset` samples into this block
        g->length = length;
        g->window = p.window;
        g->gl = std::cos (a) * 1.41421356f * hp.level * norm;
        g->gr = std::sin (a) * 1.41421356f * hp.level * norm;
    }

    void GranularEngine::copyBufferFrom (const GranularEngine& o)
    {
        if (o.ringLen != ringLen) return;
        ringL = o.ringL; ringR = o.ringR; writePos = o.writePos;
        for (int k = 0; k < overviewBins; ++k) bins[(size_t) k].store (o.bins[(size_t) k].load());
        writeBin.store (o.writeBin.load());
    }

    void GranularEngine::setRecording (bool on) noexcept
    {
        if (on && ! rec.load()) takeLen.store (0);
        rec.store (on);
    }

    void GranularEngine::copyTake (std::vector<float>& l, std::vector<float>& r) const
    {
        const size_t n = takeLen.load();
        l.assign (takeL.begin(), takeL.begin() + (long) n);
        r.assign (takeR.begin(), takeR.begin() + (long) n);
    }

    // ---------------------------------------------------------------------------------
    void GranularEngine::process (float* l, float* r, int n, const GranularParams& p, const Scale& scale, double bpm, double ppq, bool running)
    {
        if (n <= 0 || ringLen <= 1) return;
        if ((size_t) n > wetL.size()) n = (int) wetL.size();
        bpm = std::max (20.0, bpm);

        // a recorded take fed back into the grains
        if (takeToGrains.exchange (false))
        {
            const size_t tl = takeLen.load(), len = std::min (tl, ringLen);
            for (size_t i = 0; i < len; ++i)
            {
                const size_t dst = (writePos + ringLen - len + i) % ringLen;
                ringL[dst] = takeL[tl - len + i]; ringR[dst] = takeR[tl - len + i];
            }
            const size_t binSize = std::max<size_t> (1, ringLen / overviewBins);
            for (int k = 0; k < overviewBins; ++k)
            {
                float pk = 0.0f;
                for (size_t i = (size_t) k * binSize; i < (size_t) (k + 1) * binSize && i < ringLen; i += 8)
                    pk = std::max ({ pk, std::abs (ringL[i]), std::abs (ringR[i]) });
                bins[(size_t) k].store (std::min (1.0f, pk));
            }
        }

        // 1. capture the master
        std::copy (l, l + n, dryL.begin());
        std::copy (r, r + n, dryR.begin());
        const size_t binSize = std::max<size_t> (1, ringLen / overviewBins);
        if (! p.freeze)
        {
            for (int i = 0; i < n; ++i)
            {
                ringL[writePos] = l[i]; ringR[writePos] = r[i];
                binPeak = std::max ({ binPeak, std::abs (l[i]), std::abs (r[i]) });
                writePos = (writePos + 1) % ringLen;
                if (writePos % binSize == 0)
                {
                    const int b = (int) ((writePos / binSize + overviewBins - 1) % overviewBins);
                    bins[(size_t) b].store (std::min (1.0f, binPeak));
                    writeBin.store (b);
                    binPeak = 0.0f;
                }
            }
        }
        maxBlockWritten = p.freeze ? 0 : n;

        // 2. playheads: scan, drift, spawn
        const double blockSec = n / sr;
        for (int h = 0; h < grainHeads; ++h)
        {
            const auto& hp = p.heads[(size_t) h];
            if (unit() < blockSec * 0.7) { driftTarget[(size_t) h] = unit() * 2.0f - 1.0f; driftPitchTarget[(size_t) h] = unit() * 2.0f - 1.0f; }
            const float k = (float) std::min (1.0, blockSec * 0.9);
            driftPos[(size_t) h]   += (driftTarget[(size_t) h] - driftPos[(size_t) h]) * k;
            driftPitch[(size_t) h] += (driftPitchTarget[(size_t) h] - driftPitch[(size_t) h]) * k;
            scanOffset[(size_t) h] = wrap01 (scanOffset[(size_t) h] + hp.scan * (double) n / (double) ringLen);
            const double pos = wrap01 (hp.position + scanOffset[(size_t) h] + hp.drift * driftPos[(size_t) h] * 0.25);
            headPos[(size_t) h].store ((float) (hp.scan == 0.0f && hp.drift == 0.0f ? hp.position : pos));
            if (! hp.on) continue;

            const double rateHz = 0.5 * std::pow (300.0, std::clamp ((double) hp.density, 0.0, 1.0));
            for (int i = 0; i < n; ++i)
            {
                spawnPhase[(size_t) h] += rateHz / sr;
                if (spawnPhase[(size_t) h] >= 1.0)
                {
                    spawnPhase[(size_t) h] -= 1.0;
                    spawn (h, hp, p, scale, i);
                }
            }
        }

        // 3. render grains
        std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
        std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
        int count = 0;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            ++count;
            for (int i = 0; i < n; ++i)
            {
                if (g.age < 0) { ++g.age; continue; }
                if (g.age >= g.length) { g.active = false; break; }
                const float w = window (g.window, (double) g.age / g.length);
                wetL[(size_t) i] += readRing (g.pos, 0) * w * g.gl;
                wetR[(size_t) i] += readRing (g.pos, 1) * w * g.gr;
                g.pos += g.rate;
                if (g.pos >= (double) ringLen) g.pos -= (double) ringLen;
                if (g.pos < 0.0) g.pos += (double) ringLen;
                ++g.age;
            }
        }
        grainCount.store (count);

        // 4. filter
        if (p.cutoff < 0.999f || p.filterType != 0 || p.reso > 0.2f)
        {
            const double fc = 40.0 * std::pow (450.0, std::clamp ((double) p.cutoff, 0.0, 1.0));
            const double g = std::tan (pi * std::min (fc, sr * 0.45) / sr);
            const double k = 2.0 - 1.92 * std::clamp ((double) p.reso, 0.0, 1.0);
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            auto run = [&] (float x, float& ic1, float& ic2)
            {
                const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = (float) (2.0 * v1 - ic1); ic2 = (float) (2.0 * v2 - ic2);
                const double hpOut = x - k * v1 - v2;
                return (float) (p.filterType == 1 ? v1 * k : p.filterType == 2 ? hpOut : v2);
            };
            for (int i = 0; i < n; ++i) { wetL[(size_t) i] = run (wetL[(size_t) i], f1L, f2L); wetR[(size_t) i] = run (wetR[(size_t) i], f1R, f2R); }
        }

        // 5. resonator: four combs per side tuned to the key, decay up to infinite
        if (p.resMix > 0.001f)
        {
            const auto& chord = resonatorChords()[(size_t) std::clamp (p.resChord, 0, (int) resonatorChords().size() - 1)].second;
            const double t60 = p.resDecay >= 0.99f ? 1.0e9 : 0.08 * std::pow (250.0, (double) p.resDecay);
            const bool infinite = p.resDecay >= 0.99f;
            // infinite = a lossless loop (no damping, unity feedback; the soft clip keeps it bounded)
            const float damp = infinite ? 1.0f : 0.08f + 0.88f * std::clamp (p.resTone, 0.0f, 1.0f);
            std::array<double, 8> delay {};
            std::array<float, 8> fb {};
            for (int c = 0; c < 8; ++c)
            {
                const int note = 12 * (std::clamp (p.resOctave, 1, 6) + 1) + scale.root() + chord[(size_t) (c % 4)];
                const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0) * (c >= 4 ? 1.0025 : 1.0);
                delay[(size_t) c] = std::min ((double) combs[(size_t) c].buf.size() - 2.0, sr / f);
                fb[(size_t) c] = infinite ? 1.0f : (float) std::pow (10.0, -3.0 * delay[(size_t) c] / (sr * t60));
            }
            for (int i = 0; i < n; ++i)
            {
                float ol = 0.0f, orr = 0.0f;
                for (int c = 0; c < 4; ++c)     ol  += combs[(size_t) c].process (wetL[(size_t) i] * 0.4f, delay[(size_t) c], fb[(size_t) c], damp);
                for (int c = 4; c < 8; ++c)     orr += combs[(size_t) c].process (wetR[(size_t) i] * 0.4f, delay[(size_t) c], fb[(size_t) c], damp);
                wetL[(size_t) i] = wetL[(size_t) i] * (1.0f - p.resMix) + ol * 0.5f * p.resMix;
                wetR[(size_t) i] = wetR[(size_t) i] * (1.0f - p.resMix) + orr * 0.5f * p.resMix;
            }
        }

        // 6. ping-pong delay, tempo synced
        if (p.dlyMix > 0.001f)
        {
            const double q = granularDivisions()[(size_t) std::clamp (p.dlyDiv, 0, (int) granularDivisions().size() - 1)].second;
            const double d = std::clamp (q * 60.0 / bpm * sr, 16.0, (double) dlyL.size() - 2.0);
            const float fbk = std::clamp (p.dlyFb, 0.0f, 0.97f);
            const size_t len = dlyL.size();
            for (int i = 0; i < n; ++i)
            {
                const size_t rp = (dlyW + len - (size_t) d) % len;
                const float dl = dlyL[rp], dr = dlyR[rp];
                dlyLpL += (dr - dlyLpL) * 0.4f; dlyLpR += (dl - dlyLpR) * 0.4f;   // darker each repeat
                // ping-pong: the input enters on the left, repeats bounce between the sides
                dlyL[dlyW] = softClip ((wetL[(size_t) i] + wetR[(size_t) i]) * 0.5f + dlyLpL * fbk);
                dlyR[dlyW] = softClip (dlyLpR * fbk);
                dlyW = (dlyW + 1) % len;
                wetL[(size_t) i] += dl * p.dlyMix;
                wetR[(size_t) i] += dr * p.dlyMix;
            }
        }

        // 7. reverb: hall or shimmer, freeze = infinite
        if (p.revMix > 0.001f || p.revFreeze)
        {
            FxParams rv;
            rv.type = p.shimmer > 0.05f ? FxType::Shimmer : FxType::Hall;
            rv.a = p.revSize;
            rv.b = 0.45f;
            rv.gnarl = p.revFreeze ? 1.0f : std::clamp (p.shimmer * 0.6f, 0.0f, 0.9f);
            rv.mix = std::max (p.revMix, p.revFreeze ? 0.5f : 0.0f);
            reverb.process (wetL.data(), wetR.data(), n, rv, bpm);
        }

        // 8. dry / wet
        for (int i = 0; i < n; ++i)
        {
            l[i] = dryL[(size_t) i] * (1.0f - p.mix) + wetL[(size_t) i] * p.mix;
            r[i] = dryR[(size_t) i] * (1.0f - p.mix) + wetR[(size_t) i] * p.mix;
        }

        // 9. glitch: tempo-synced events on the whole section output
        const double divQ = granularDivisions()[(size_t) std::clamp (p.glitchDiv, 0, (int) granularDivisions().size() - 1)].second;
        const double divSamples = divQ * 60.0 / bpm * sr;
        const double qPerSample = bpm / 60.0 / sr;
        const double startQ = running ? ppq : freeClock;
        freeClock = running ? ppq + n * qPerSample : freeClock + n * qPerSample;
        const bool anyMode = p.gRepeat || p.gReverse || p.gTapeStop || p.gCrush || p.gChop;
        for (int i = 0; i < n; ++i)
        {
            histL[histW] = l[i]; histR[histW] = r[i];
            const double qNow = startQ + i * qPerSample;
            const long long step = (long long) std::floor (qNow / divQ + 1.0e-9);
            if (step != glitchStep)
            {
                glitchStep = step;
                glitchMode = -1;
                if (p.stutter) glitchMode = 0;
                else if (anyMode && p.glitch > 0.0f && hashUnit ((uint64_t) step, 0x611C) < p.glitch)
                {
                    int modes[5]; int count = 0;
                    if (p.gRepeat) modes[count++] = 0;
                    if (p.gReverse) modes[count++] = 1;
                    if (p.gTapeStop) modes[count++] = 2;
                    if (p.gCrush) modes[count++] = 3;
                    if (p.gChop) modes[count++] = 4;
                    glitchMode = modes[(int) (hashUnit ((uint64_t) step, 0x7A11) * count) % count];
                }
                if (glitchMode >= 0)
                {
                    static const char* names[] = { "Repeat", "Reverse", "Tape stop", "Crush", "Chop" };
                    glitchName.store (names[glitchMode]);
                    glitchStart = (double) histW;
                    glitchAge = 0.0;
                    glitchLen = divSamples;
                    const int pow2 = p.stutter ? 0 : 1 + (int) (hashUnit ((uint64_t) step, 0x5EED) * 3.0f);   // 2, 4 or 8 repeats
                    sliceLen = std::max (32.0, divSamples / (double) (1 << pow2));
                    tapePos = 0.0;
                    crushCount = 0;
                }
            }

            float gl = l[i], gr = r[i];
            const bool active = glitchMode >= 0 && glitchAge < glitchLen;
            if (active)
            {
                auto hist = [&] (double pos, int ch)
                {
                    while (pos < 0) pos += (double) histLen;
                    const size_t i0 = (size_t) pos % histLen, i1 = (i0 + 1) % histLen;
                    const float fr = (float) (pos - std::floor (pos));
                    const auto& hb = ch == 0 ? histL : histR;
                    return hb[i0] + (hb[i1] - hb[i0]) * fr;
                };
                switch (glitchMode)
                {
                    case 0: { const double ps = glitchStart - sliceLen + std::fmod (glitchAge, sliceLen); gl = hist (ps, 0); gr = hist (ps, 1); break; }
                    case 1: { const double ps = glitchStart - glitchAge; gl = hist (ps, 0); gr = hist (ps, 1); break; }
                    case 2: { tapePos += std::max (0.0, 1.0 - glitchAge / glitchLen); const double ps = glitchStart + tapePos; gl = hist (ps, 0); gr = hist (ps, 1); break; }
                    case 3:
                        if (crushCount++ % 7 == 0) { crushHoldL = std::round (l[i] * 12.0f) / 12.0f; crushHoldR = std::round (r[i] * 12.0f) / 12.0f; }
                        gl = crushHoldL; gr = crushHoldR; break;
                    case 4: { const double sub = std::max (32.0, divSamples / 8.0); const bool open = std::fmod (glitchAge, sub) < sub * 0.5; gl = open ? l[i] : 0.0f; gr = open ? r[i] : 0.0f; break; }
                    default: break;
                }
                glitchAge += 1.0;
            }
            // short crossfades in and out of every event
            const float target = active ? 1.0f : 0.0f;
            glitchFade += (target - glitchFade) * (float) std::min (1.0, 1.0 / (0.003 * sr));
            l[i] = l[i] + (gl - l[i]) * glitchFade;
            r[i] = r[i] + (gr - r[i]) * glitchFade;
            histW = (histW + 1) % histLen;
        }

        // 10. looper and recorder, then the output level
        const size_t tl = takeLen.load();
        const bool recording = rec.load();
        if (p.loopPlay && ! recording && tl > 0)
            for (int i = 0; i < n; ++i)
            {
                loopPos %= tl;
                l[i] += takeL[loopPos] * p.loopLevel;
                r[i] += takeR[loopPos] * p.loopLevel;
                ++loopPos;
            }
        for (int i = 0; i < n; ++i) { l[i] *= p.out; r[i] *= p.out; }
        if (recording)
        {
            size_t w = tl;
            for (int i = 0; i < n && w < takeL.size(); ++i, ++w) { takeL[w] = l[i]; takeR[w] = r[i]; }
            takeLen.store (w);
            if (w >= takeL.size()) rec.store (false);   // full
        }
    }
}
