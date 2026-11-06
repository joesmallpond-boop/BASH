#include "bash/Micro.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    namespace
    {
        constexpr double pi = 3.141592653589793;
        float softClip (float x) { return std::tanh (x); }
    }

    const char* microAlgoName (MicroAlgo a)
    {
        static const char* n[] = { "Mosaic", "Sequencer", "Glide", "Haze", "Tunnel", "Strum", "Blocks", "Interrupt", "Arp", "Pattern", "Warp" };
        return n[std::clamp ((int) a, 0, (int) MicroAlgo::count - 1)];
    }

    const char* microFamilyName (MicroAlgo a)
    {
        switch (a)
        {
            case MicroAlgo::Mosaic: case MicroAlgo::Sequencer: case MicroAlgo::Glide: return "Micro Loop";
            case MicroAlgo::Haze: case MicroAlgo::Tunnel: case MicroAlgo::Strum:      return "Granules";
            case MicroAlgo::Blocks: case MicroAlgo::Interrupt: case MicroAlgo::Arp:   return "Glitch";
            default:                                                                  return "Multi Delay";
        }
    }

    const char* microVariationText (MicroAlgo a, int v)
    {
        static const char* t[(int) MicroAlgo::count][4] = {
            { "loops at x1 and x2", "loops at half speed and x1", "x1, x2 and x4: rising octaves", "half, x1 and x2, the slow one reversed" },
            { "slices swapped in pairs", "slices doubled up", "slices played backwards in order", "a new random order for each phrase" },
            { "glides up an octave", "glides down an octave", "rises and falls back", "glides to a random note in key" },
            { "short diffused grains", "grains at x1 and half speed", "grains at x1 and an octave up", "x1, half and octave up, some reversed" },
            { "a drone cycling every division", "drone plus an octave up", "drone plus an octave down", "a new drone on every note you play" },
            { "chains repeat at pitch", "chains climb in octaves", "chains fall in octaves", "chains scatter across the key" },
            { "bursts repeat at pitch", "bursts climb the scale", "bursts jump up in octaves", "bursts fall down the scale" },
            { "bursts an octave up", "bursts an octave down", "reversed chunks", "micro-montage of the last 2 s" },
            { "arpeggio up", "arpeggio down", "up and down", "random notes in the chord" },
            { "taps on 1, 2, 3", "taps on 1, 1.5, 3", "dotted taps 0.75, 1.5, 2.25", "wide taps on 1, 3, 5" },
            { "taps an octave up", "taps an octave down and up", "taps a fifth and an octave up", "taps -5, +7 and +19 semitones" } };
        return t[std::clamp ((int) a, 0, (int) MicroAlgo::count - 1)][std::clamp (v, 0, 3)];
    }

    const std::vector<std::pair<const char*, double>>& microTimes()
    {
        static const std::vector<std::pair<const char*, double>> t {
            { "x1/4", 4.0 }, { "x1/2", 2.0 }, { "x1", 1.0 }, { "x2", 0.5 }, { "x4", 0.25 }, { "x8", 0.125 } };
        return t;
    }

    const std::vector<const char*>& microReverbNames()
    {
        static const std::vector<const char*> n { "Room", "Plate", "Hall", "Cloud" };
        return n;
    }

    const std::vector<std::pair<const char*, double>>& microLoopSpeeds()
    {
        static const std::vector<std::pair<const char*, double>> s { { "x1/4", 0.25 }, { "x1/2", 0.5 }, { "x1", 1.0 }, { "x2", 2.0 }, { "x4", 4.0 } };
        return s;
    }

    // ------------------------------------------------------------------------------------
    void MicroEngine::prepare (double sampleRate, int block)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        maxBlock = std::max (1, block);
        histLen = (size_t) (historySeconds * sr);
        histL.assign (histLen, 0.0f); histR.assign (histLen, 0.0f);
        dlyLen = (size_t) (sr * 12.0);
        dlyL.assign (dlyLen, 0.0f); dlyR.assign (dlyLen, 0.0f);
        for (auto* v : { &wetL, &wetR, &inL, &inR, &dryBufL, &dryBufR }) v->assign ((size_t) maxBlock, 0.0f);
        const size_t loopCap = (size_t) (maxLoopSeconds * sr);
        loopL.assign (loopCap, 0.0f); loopR.assign (loopCap, 0.0f);
        undoL.assign (loopCap, 0.0f); undoR.assign (loopCap, 0.0f);
        reverb.prepare (sr, maxBlock);
        reset();
    }

    void MicroEngine::reset()
    {
        writeAbs = 0;
        for (auto& v : voices) v.active = false;
        for (auto& b : bins) b.store (0.0f);
        for (auto& b : loopBins) b.store (0.0f);
        for (auto& v : voiceUi) v.store (-1.0f);
        lastTick = -1; freeClock = 0; hazeAcc = strumAcc = 0;
        seqBase = arpSource = -1.0; interruptLeft = 0.0; onsetCount = 0; strumLeft = 0;
        f1L = f2L = f1R = f2R = 0.0f;
        dlyW = 0; tapLpL.fill (0.0f); tapLpR.fill (0.0f); warpPhase.fill (0.0);
        std::fill (dlyL.begin(), dlyL.end(), 0.0f); std::fill (dlyR.begin(), dlyR.end(), 0.0f);
        state.store ((int) LoopState::Empty); loopLen.store (0); recLen.store (0); loopPos = 0.0; undoValid.store (false);
        duck = 1.0f;
    }

    float MicroEngine::inputOverview (int k) const noexcept
    {
        return bins[(size_t) ((newestBin.load() + 1 + k) % viewBins)].load();
    }

    float MicroEngine::readHistory (double absPos, int ch) const noexcept
    {
        if (absPos < 0.0 || absPos > (double) writeAbs || (double) writeAbs - absPos >= (double) histLen - 2.0) return 0.0f;
        const auto& h = ch == 0 ? histL : histR;
        const long long i0 = (long long) absPos;
        const float fr = (float) (absPos - (double) i0);
        const float a = h[(size_t) (i0 % (long long) histLen)], b = h[(size_t) ((i0 + 1) % (long long) histLen)];
        return a + (b - a) * fr;
    }

    float MicroEngine::envelope (double ph, float shape) const noexcept
    {
        // Shape morphs: soft (hann) -> flat -> percussive -> swell
        ph = std::clamp (ph, 0.0, 1.0);
        const double hann = std::sin (pi * ph) * std::sin (pi * ph);
        const double e = 0.03;
        const double flat = ph < e ? ph / e : ph > 1.0 - e ? (1.0 - ph) / e : 1.0;
        const double perc = ph < 0.01 ? ph / 0.01 : std::pow (1.0 - (ph - 0.01) / 0.99, 2.2);
        const double swell = ph > 0.98 ? (1.0 - ph) / 0.02 : std::pow (ph / 0.98, 2.2);
        const double sh = std::clamp ((double) shape, 0.0, 1.0) * 3.0;
        if (sh < 1.0) return (float) (hann + (flat - hann) * sh);
        if (sh < 2.0) return (float) (flat + (perc - flat) * (sh - 1.0));
        return (float) (perc + (swell - perc) * (sh - 2.0));
    }

    double MicroEngine::scaleRate (const Scale& sc, int semis) const
    {
        const int base = 60 + sc.root();
        return std::pow (2.0, (sc.quantize (base + semis) - base) / 12.0);
    }

    bool MicroEngine::nextReverse (const MicroParams& p) noexcept
    {
        if (p.reverse == MicroReverse::Reverse) return true;
        if (p.reverse == MicroReverse::Alternate) { altFlag = ! altFlag; return altFlag; }
        return false;
    }

    void MicroEngine::spawn (Voice v)
    {
        Voice* slot = nullptr;
        double oldest = -1.0;
        for (auto& c : voices)
        {
            if (! c.active) { slot = &c; break; }
            if (c.age / std::max (1.0, c.life) > oldest) { oldest = c.age / std::max (1.0, c.life); slot = &c; }   // steal the most finished
        }
        v.active = true;
        *slot = v;
    }

    void MicroEngine::spawnGrain (double start, double len, double rate, float gain, float pan, double delay)
    {
        Voice v;
        v.start = start; v.loopLen = 0.0; v.rate = rate; v.life = std::max (32.0, len / std::abs (rate)); v.delay = delay;
        v.phase = rate < 0.0 ? len : 0.0;
        const float a = (float) ((std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25 * pi);
        v.gl = std::cos (a) * 1.41421356f * gain; v.gr = std::sin (a) * 1.41421356f * gain;
        spawn (v);
    }

    void MicroEngine::spawnLoop (double start, double len, double rate, double life, float gain, float pan, double glide, int glideMode)
    {
        Voice v;
        v.start = start; v.loopLen = std::max (64.0, len); v.rate = rate; v.life = std::max (64.0, life);
        v.phase = rate < 0.0 ? v.loopLen : 0.0; v.glide = glide; v.glideMode = glideMode;
        const float a = (float) ((std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25 * pi);
        v.gl = std::cos (a) * 1.41421356f * gain; v.gr = std::sin (a) * 1.41421356f * gain;
        spawn (v);
    }

    // ---- what each algorithm does on a division tick -------------------------------------
    void MicroEngine::onTick (const MicroParams& p, const Scale& sc, long long tick, double D)
    {
        const float act = p.activity, rep = p.repeats;
        const int var = std::clamp (p.variation, 0, 3);
        const double now = (double) writeAbs;
        auto rev = [&] (double rate) { return nextReverse (p) ? -rate : rate; };
        for (auto& v : voices) if (v.active) v.shape = p.shape;

        switch (p.algo)
        {
            case MicroAlgo::Mosaic:
            {
                static const std::vector<double> rates[4] = { { 1.0, 2.0 }, { 0.5, 1.0 }, { 1.0, 2.0, 4.0 }, { 0.5, 1.0, 2.0 } };
                const auto& rs = rates[var];
                const int k = 1 + (int) std::lround (act * (double) (rs.size() - 1 + (act > 0.8f ? 1 : 0)));
                for (int i = 0; i < k; ++i)
                {
                    double r = rs[(size_t) ((tick + i) % (long long) rs.size())];
                    if (var == 3 && r == 0.5) r = -0.5;
                    spawnLoop (now - D, D, rev (r), D * (1.0 + rep * 7.0), 0.55f / std::sqrt ((float) k), (i % 2 ? 0.45f : -0.45f) * (float) (i > 0));
                }
                break;
            }
            case MicroAlgo::Sequencer:
            {
                if (seqBase < 0.0 || (tick % 8 == 0 && unit() > rep * 0.85f))
                {
                    seqBase = now - 8.0 * D;   // the last eight divisions become the material
                    seqOrder = { 0, 1, 2, 3, 4, 5, 6, 7 };
                    for (int i = 7; i > 0; --i) std::swap (seqOrder[(size_t) i], seqOrder[(size_t) (rnd() % (uint32_t) (i + 1))]);
                }
                static const int orders[3][8] = { { 1, 0, 3, 2, 5, 4, 7, 6 }, { 0, 0, 2, 2, 4, 4, 6, 6 }, { 7, 6, 5, 4, 3, 2, 1, 0 } };
                const int step = (int) (tick % 8);
                const int slice = var < 3 ? orders[var][step] : seqOrder[(size_t) step];
                double start = seqBase + slice * D;
                if (start < 0.0) start = std::max (0.0, now - D * (double) (1 + (7 - slice) % std::max (1, (int) (now / D))));   // not 8 divisions of audio yet
                const bool ratchet = unit() < act * 0.5f;
                const double octave = unit() < act * 0.25f ? 2.0 : 1.0;
                if (ratchet)
                {
                    spawnGrain (start, D * 0.5, rev (octave), 0.6f, -0.2f);
                    spawnGrain (start, D * 0.5, rev (octave), 0.5f, 0.2f, D * 0.5);
                }
                else spawnGrain (start, D, rev (octave), 0.65f, (step % 2) ? 0.25f : -0.25f);
                break;
            }
            case MicroAlgo::Glide:
            {
                const double depth = 7.0 + act * 17.0;
                double glide = var == 0 ? 12.0 : var == 1 ? -12.0 : var == 2 ? 12.0 : 0.0;
                if (var == 3) glide = std::log2 (scaleRate (sc, (int) std::lround ((unit() * 2.0f - 1.0f) * depth))) * 12.0;
                else glide *= depth / 12.0;
                const double len = D * (0.25 + 0.25 * act);
                spawnLoop (now - len, len, rev (1.0), D * (2.0 + rep * 6.0), 0.5f, unit() * 1.2f - 0.6f, glide, var == 2 ? 1 : 0);
                break;
            }
            case MicroAlgo::Tunnel:
            {
                if (var == 3) break;   // D: envelope-triggered (see onOnset)
                int tunnels = 0;
                for (auto& v : voices) if (v.active && v.loopLen > 0) ++tunnels;
                if (tunnels > 6) break;
                const double len = sr * (0.02 + 0.2 * (1.0 - act));
                const double life = rep > 0.97f ? sr * 60.0 : D * (1.0 + rep * 15.0);
                spawnLoop (now - len, len, rev (1.0), life, 0.5f, unit() * 0.8f - 0.4f);
                if (var == 1) spawnLoop (now - len, len, rev (2.0), life, 0.3f, unit() * 0.8f - 0.4f);
                if (var == 2) spawnLoop (now - len, len, rev (0.5), life, 0.35f, unit() * 0.8f - 0.4f);
                break;
            }
            case MicroAlgo::Blocks:
                if (act > 0.7f && unit() < (act - 0.7f) * 2.0f) onOnset (p, sc, D);   // busy: blocks on the grid too
                break;
            case MicroAlgo::Interrupt:
                if (interruptLeft <= 0.0 && unit() < act * 0.8f)
                {
                    interruptLeft = D * (1.0 + rep * 3.0);
                    interruptKind = var;
                }
                break;
            case MicroAlgo::Arp:
            {
                if (arpSource < 0.0 || (tick % 16 == 0 && unit() > rep)) arpSource = onsetCount > 0 ? onsets[(size_t) ((onsetWrite + 7) % 8)] : now - D;
                const int octaves = 1 + (int) std::lround (act * 2.0);
                static const int chord[] = { 0, 2, 4 };   // scale degrees of the triad
                const int notes = 3 * octaves;
                int idx;
                switch (var)
                {
                    case 0:  idx = arpStep % notes; break;
                    case 1:  idx = notes - 1 - arpStep % notes; break;
                    case 2:  { const int cyc = std::max (1, 2 * notes - 2); const int k = arpStep % cyc; idx = k < notes ? k : cyc - k; break; }
                    default: idx = (int) (rnd() % (uint32_t) notes); break;
                }
                ++arpStep;
                const int degree = chord[idx % 3] + 7 * (idx / 3);
                const int semis = sc.degreeToSemitone (degree);
                spawnGrain (arpSource, D * 0.9, rev (std::pow (2.0, semis / 12.0)), 0.6f, (arpStep % 2) ? 0.3f : -0.3f);
                break;
            }
            default: break;
        }
    }

    void MicroEngine::onOnset (const MicroParams& p, const Scale& sc, double D)
    {
        const float act = p.activity, rep = p.repeats;
        const int var = std::clamp (p.variation, 0, 3);
        const double now = (double) writeAbs;
        switch (p.algo)
        {
            case MicroAlgo::Blocks:
            {
                const double block = D * 0.25;
                const int n = 2 + (int) std::lround (act * 6.0);
                const float decay = 0.45f + 0.5f * rep;
                float g = 0.65f;
                for (int q = 0; q < n; ++q)
                {
                    int semis = 0;
                    if (var == 1) semis = sc.degreeToSemitone (q) ;
                    if (var == 2) semis = 12 * std::min (q, 2);
                    if (var == 3) semis = sc.degreeToSemitone (-q);
                    const double rate = std::pow (2.0, semis / 12.0);
                    spawnGrain (now - block, block, nextReverse (p) ? -rate : rate, g, (q % 2) ? 0.35f : -0.35f, q * block);
                    g *= decay;
                }
                break;
            }
            case MicroAlgo::Tunnel:
                if (var == 3)
                {
                    const double len = sr * (0.03 + 0.15 * (1.0 - act));
                    spawnLoop (now - len, len, 1.0, rep > 0.97f ? sr * 60.0 : D * (2.0 + rep * 14.0), 0.5f, unit() * 0.8f - 0.4f);
                }
                break;
            case MicroAlgo::Strum:
                strumLeft = 1 + (int) std::lround (rep * 12.0);
                strumIndex = 0;
                break;
            case MicroAlgo::Arp:
                if (unit() > rep) arpSource = now - sr * 0.01;
                break;
            default: break;
        }
    }

    // ---- looper commands (audio thread) -------------------------------------------------
    void MicroEngine::handleCommand (int cmd, double bpm)
    {
        auto st = (LoopState) state.load();
        auto closeLoop = [&]
        {
            size_t len = recLen.load();
            if (len < 64) { state.store ((int) LoopState::Empty); recLen.store (0); return; }
            if (quantizeLoop)
            {
                const double beat = 60.0 / std::max (20.0, bpm) * sr;
                const double beats = std::max (1.0, std::round ((double) len / beat));
                const size_t q = std::min (loopL.size(), (size_t) std::llround (beats * beat));
                for (size_t i = len; i < q; ++i) { loopL[i] = 0.0f; loopR[i] = 0.0f; }
                len = q;
            }
            loopLen.store (len);
            loopPos = 0.0; loopGain = 1.0f;
            state.store ((int) LoopState::Playing);
            rebuildLoopBins();
        };
        switch (cmd)
        {
            case cmdPress:
                if (st == LoopState::Empty) { recLen.store (0); undoValid.store (false); state.store ((int) LoopState::Recording); }
                else if (st == LoopState::Recording) closeLoop();
                else if (st == LoopState::Playing) { dubWritten = 0; undoValid.store (false); state.store ((int) LoopState::Overdubbing); }
                else if (st == LoopState::Overdubbing) { state.store ((int) LoopState::Playing); rebuildLoopBins(); }
                else if (st == LoopState::Stopped) { loopPos = 0.0; loopGain = 1.0f; state.store ((int) LoopState::Playing); }
                break;
            case cmdStop:
                if (st == LoopState::Recording) closeLoop();
                if (loopLen.load() > 0) state.store ((int) LoopState::Stopped);
                break;
            case cmdErase:
                state.store ((int) LoopState::Empty); loopLen.store (0); recLen.store (0); undoValid.store (false);
                for (auto& b : loopBins) b.store (0.0f);
                break;
            case cmdUndo:
                if (undoValid.load() && (st == LoopState::Playing || st == LoopState::Stopped))
                {
                    const size_t len = loopLen.load();
                    std::copy (undoL.begin(), undoL.begin() + (long) len, loopL.begin());
                    std::copy (undoR.begin(), undoR.begin() + (long) len, loopR.begin());
                    undoValid.store (false);
                    rebuildLoopBins();
                }
                break;
            case cmdBurstDown:
                burstRecording = true;
                if (st == LoopState::Empty) { recLen.store (0); state.store ((int) LoopState::Recording); }
                else if (st == LoopState::Playing) { dubWritten = 0; undoValid.store (false); state.store ((int) LoopState::Overdubbing); }
                break;
            case cmdBurstUp:
                if (burstRecording)
                {
                    burstRecording = false;
                    if (st == LoopState::Recording) closeLoop();
                    else if (st == LoopState::Overdubbing) { state.store ((int) LoopState::Playing); rebuildLoopBins(); }
                }
                break;
            default: break;
        }
    }

    void MicroEngine::rebuildLoopBins()
    {
        const size_t len = loopLen.load() > 0 ? loopLen.load() : recLen.load();
        if (len == 0) { for (auto& b : loopBins) b.store (0.0f); return; }
        for (int k = 0; k < viewBins; ++k)
        {
            const size_t a = len * (size_t) k / viewBins, b = std::max (a + 1, len * (size_t) (k + 1) / viewBins);
            float pk = 0.0f;
            for (size_t i = a; i < b && i < len; i += 4) pk = std::max ({ pk, std::abs (loopL[i]), std::abs (loopR[i]) });
            loopBins[(size_t) k].store (std::min (1.0f, pk));
        }
    }

    void MicroEngine::copyLoop (std::vector<float>& l, std::vector<float>& r) const
    {
        const size_t len = loopLen.load();
        l.assign (loopL.begin(), loopL.begin() + (long) len);
        r.assign (loopR.begin(), loopR.begin() + (long) len);
    }

    // ---- processing -----------------------------------------------------------------------
    void MicroEngine::process (float* l, float* r, int n, const MicroParams& p, const Scale& sc, double bpm, double ppq, bool running)
    {
        if (n <= 0 || histLen <= 1) return;
        n = std::min (n, maxBlock);
        bpm = std::max (20.0, bpm);
        quantizeLoop = p.quantize;
        if (const int cmd = command.exchange (cmdNone)) handleCommand (cmd, bpm);

        const auto st = (LoopState) state.load();
        const bool looperOnly = p.loopMode == LoopMode::LooperOnly;
        const double speed = microLoopSpeeds()[(size_t) std::clamp (p.loopSpeed, 0, (int) microLoopSpeeds().size() - 1)].second * (p.loopReverse ? -1.0 : 1.0);
        const size_t len = loopLen.load();
        const bool loopPlaying = (st == LoopState::Playing || st == LoopState::Overdubbing) && len > 0;

        // the loop, read once for this block (pre-FX mode feeds it into the effect)
        std::vector<float>& loopOutL = inL;   // reuse: holds loop playback for this block
        std::vector<float>& loopOutR = inR;
        double lp = loopPos;
        for (int i = 0; i < n; ++i)
        {
            float a = 0.0f, b = 0.0f;
            if (loopPlaying)
            {
                while (lp < 0.0) lp += (double) len;
                while (lp >= (double) len) { lp -= (double) len; loopGain *= 1.0f - p.loopFade * 0.6f; }
                const size_t i0 = (size_t) lp, i1 = (i0 + 1) % len;
                const float fr = (float) (lp - std::floor (lp));
                a = (loopL[i0] + (loopL[i1] - loopL[i0]) * fr) * loopGain * p.loopLevel;
                b = (loopR[i0] + (loopR[i1] - loopR[i0]) * fr) * loopGain * p.loopLevel;
                lp += speed;
            }
            loopOutL[(size_t) i] = a; loopOutR[(size_t) i] = b;
        }

        // input to the effect: the tracks (+ the loop in pre-FX mode)
        auto& dryL = dryBufL; auto& dryR = dryBufR;
        std::copy (l, l + n, dryL.begin()); std::copy (r, r + n, dryR.begin());
        if (p.loopMode == LoopMode::PreFx)
            for (int i = 0; i < n; ++i) { l[i] += loopOutL[(size_t) i]; r[i] += loopOutR[(size_t) i]; }

        // 1. history + onset detection
        const double D = microTimes()[(size_t) std::clamp (p.time, 0, (int) microTimes().size() - 1)].second * 60.0 / bpm * sr;
        const float fastC = (float) (1.0 - std::exp (-1.0 / (0.002 * sr))), fastR = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
        const float slowC = (float) (1.0 - std::exp (-1.0 / (0.25 * sr)));
        const size_t binLen = std::max<size_t> (1, histLen / viewBins);
        bool onset = false;
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (l[i]), std::abs (r[i]));
            envFast += (x - envFast) * (x > envFast ? fastC : fastR);
            envSlow += (x - envSlow) * slowC;
            sinceOnset += 1.0;
            if (! p.hold && envFast > 0.01f && envFast > envSlow * 1.8f && sinceOnset > sr * 0.06)
            {
                sinceOnset = 0.0; onset = true;
                onsets[(size_t) onsetWrite] = (double) writeAbs; onsetWrite = (onsetWrite + 1) % 8; onsetCount = std::min (8, onsetCount + 1);
            }
            if (! p.hold)
            {
                const size_t w = (size_t) (writeAbs % (long long) histLen);
                histL[w] = l[i]; histR[w] = r[i];
                ++writeAbs;
                binPeak = std::max (binPeak, x);
                if (writeAbs % (long long) binLen == 0)
                {
                    const int b = (int) ((writeAbs / (long long) binLen - 1) % viewBins);
                    bins[(size_t) b].store (std::min (1.0f, binPeak));
                    newestBin.store (b);
                    binPeak = 0.0f;
                }
            }
        }

        // 2. scheduling: division ticks, onsets, continuous algorithms
        const double beatsNow = running ? ppq : freeClock;
        freeClock = (running ? ppq : freeClock) + n * bpm / 60.0 / sr;
        const double divBeats = microTimes()[(size_t) std::clamp (p.time, 0, (int) microTimes().size() - 1)].second;
        const long long tick = (long long) std::floor (beatsNow / divBeats + 1.0e-9);
        const bool voiceAlgo = p.algo != MicroAlgo::Pattern && p.algo != MicroAlgo::Warp;
        if (! looperOnly && voiceAlgo && ! (p.hold && (p.algo == MicroAlgo::Haze)))
        {
            if (tick != lastTick) { lastTick = tick; onTick (p, sc, tick, D); }
            if (onset) onOnset (p, sc, D);
        }
        if (! looperOnly && p.algo == MicroAlgo::Haze)
        {
            const double perSec = 4.0 + p.activity * 90.0;
            hazeAcc += perSec * n / sr;
            const int var = std::clamp (p.variation, 0, 3);
            while (hazeAcc >= 1.0)
            {
                hazeAcc -= 1.0;
                const double window = sr * (0.1 + p.repeats * 2.9);
                const double len = sr * (var == 0 ? 0.02 + 0.06 * unit() : 0.05 + 0.25 * unit());
                double rate = 1.0;
                const float pick = unit();
                if (var == 1 && pick < 0.5f) rate = 0.5;
                if (var == 2 && pick < 0.4f) rate = 2.0;
                if (var == 3) { rate = pick < 0.33f ? 0.5 : pick < 0.66f ? 2.0 : 1.0; if (unit() < 0.3f) rate = -rate; }
                if (nextReverse (p)) rate = -std::abs (rate);
                const double start = (double) writeAbs - len * std::abs (rate) - unit() * window;
                spawnGrain (start, len, rate, (float) (0.9 / std::sqrt (1.0 + perSec * len / sr)), unit() * 1.6f - 0.8f);
            }
        }
        if (! looperOnly && p.algo == MicroAlgo::Strum && strumLeft > 0 && onsetCount > 0)
        {
            const double every = std::max (sr * 0.03, D / (1.0 + std::lround (p.activity * 3.0)));
            strumAcc += n;
            while (strumAcc >= every && strumLeft > 0)
            {
                strumAcc -= every;
                const int var = std::clamp (p.variation, 0, 3);
                const double src = onsets[(size_t) ((onsetWrite + 7 - strumIndex % onsetCount) % 8)];
                int semis = 0;
                if (var == 1) semis = 12 * (strumIndex % 3);
                if (var == 2) semis = -12 * (strumIndex % 3);
                if (var == 3) semis = (int) std::lround ((unit() * 2.0f - 1.0f) * 12.0f);
                const double rate = var == 3 ? scaleRate (sc, semis) : std::pow (2.0, semis / 12.0);
                spawnGrain (src, std::min (sr * 0.25, D), nextReverse (p) ? -rate : rate, 0.6f, (strumIndex % 2) ? 0.5f : -0.5f);
                ++strumIndex; --strumLeft;
            }
        }
        if (! looperOnly && p.algo == MicroAlgo::Interrupt && interruptLeft > 0.0)
        {
            // a stream of short grains near "now", pitched / reversed / montaged
            const int kind = interruptKind;
            const double gl = kind == 3 ? D / 8.0 : sr * 0.06;
            strumAcc += n;
            while (strumAcc >= gl * 0.5)
            {
                strumAcc -= gl * 0.5;
                const double rate = kind == 0 ? 2.0 : kind == 1 ? 0.5 : kind == 2 ? -1.0 : 1.0;
                const double start = kind == 3 ? (double) writeAbs - unit() * sr * 2.0 : (double) writeAbs - gl * std::abs (rate) - sr * 0.005;
                spawnGrain (start, gl, rate, 0.55f, unit() * 1.0f - 0.5f);
            }
            interruptLeft -= n;
        }
        const float duckTarget = (p.algo == MicroAlgo::Interrupt && interruptLeft > 0.0 && ! looperOnly) ? 1.0f - 0.9f * p.mix : 1.0f;

        // 3. render the voices
        std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
        std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
        int count = 0, ui = 0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            ++count;
            for (int i = 0; i < n; ++i)
            {
                if (v.delay > 0.0) { v.delay -= 1.0; continue; }
                if (v.age >= v.life) { v.active = false; break; }
                const double lifePh = v.age / v.life;
                double rate = v.rate;
                if (v.glide != 0.0)
                {
                    const double g = v.glideMode == 1 ? std::sin (pi * lifePh) : lifePh;
                    rate *= std::pow (2.0, v.glide * g / 12.0);
                }
                double pos;
                float w;
                if (v.loopLen > 0.0)
                {
                    double ph = std::fmod (v.phase, v.loopLen);
                    if (ph < 0.0) ph += v.loopLen;
                    pos = v.start + ph;
                    // short fades at the loop seam, then the life envelope (soft for long loops)
                    const double seam = std::min (ph, v.loopLen - ph) / std::min (v.loopLen * 0.5, sr * 0.004);
                    w = (float) std::min (1.0, seam) * envelope (lifePh, std::min (v.shape, 0.33f));
                }
                else
                {
                    pos = v.start + v.phase;
                    w = envelope (lifePh, v.shape);
                }
                const float a = readHistory (pos, 0), b = readHistory (pos, 1);
                wetL[(size_t) i] += a * w * v.gl;
                wetR[(size_t) i] += b * w * v.gr;
                v.phase += rate;
                v.age += 1.0;
            }
            if (v.active && ui < (int) voiceUi.size())
            {
                const double pos = v.loopLen > 0.0 ? v.start + std::fmod (std::max (0.0, v.phase), v.loopLen) : v.start + v.phase;
                const double ago = (double) writeAbs - pos;
                voiceUi[(size_t) ui++].store ((float) std::clamp (1.0 - ago / (double) histLen, 0.0, 1.0));
            }
        }
        for (; ui < (int) voiceUi.size(); ++ui) voiceUi[(size_t) ui].store (-1.0f);
        voiceCount.store (count);

        // 4. multi delay algorithms
        if (! looperOnly && ! voiceAlgo)
        {
            const int var = std::clamp (p.variation, 0, 3);
            static const double patterns[4][3] = { { 1, 2, 3 }, { 1, 1.5, 3 }, { 0.75, 1.5, 2.25 }, { 1, 3, 5 } };
            static const double warps[4][3] = { { 12, 12, 12 }, { -12, 12, -12 }, { 7, 12, 7 }, { -5, 7, 19 } };
            const int taps = 1 + (int) std::lround (p.activity * 2.0);
            const float fb = p.hold ? 1.0f : std::min (0.95f, p.repeats * 0.95f);
            const double maxDelay = (double) dlyLen - sr * 0.2;
            for (int i = 0; i < n; ++i)
            {
                float ol = 0.0f, orr = 0.0f, last = 0.0f;
                for (int t = 0; t < taps; ++t)
                {
                    const double d = std::min (maxDelay, std::max (64.0, patterns[var][t] * D));
                    float tl, tr;
                    if (p.algo == MicroAlgo::Warp)
                    {
                        // two-grain pitch shifter reading the delay line around this tap
                        const double ratio = std::pow (2.0, warps[var][t] / 12.0);
                        const double win = sr * 0.07;
                        warpPhase[(size_t) t] += (1.0 - ratio) / win;
                        warpPhase[(size_t) t] -= std::floor (warpPhase[(size_t) t]);
                        tl = tr = 0.0f;
                        for (int g = 0; g < 2; ++g)
                        {
                            double ph = warpPhase[(size_t) t] + 0.5 * g; ph -= std::floor (ph);
                            const double rd = d + ph * win;
                            const double rp = (double) dlyW - rd + (double) dlyLen * 2.0;
                            const size_t i0 = (size_t) rp % dlyLen, i1 = (i0 + 1) % dlyLen;
                            const float fr = (float) (rp - std::floor (rp));
                            const float wg = (float) std::sin (pi * ph);
                            tl += (dlyL[i0] + (dlyL[i1] - dlyL[i0]) * fr) * wg * wg;
                            tr += (dlyR[i0] + (dlyR[i1] - dlyR[i0]) * fr) * wg * wg;
                        }
                    }
                    else
                    {
                        const size_t rp = (dlyW + dlyLen - (size_t) d) % dlyLen;
                        tl = dlyL[rp]; tr = dlyR[rp];
                    }
                    // each tap darker than the last
                    const float damp = 0.6f - 0.15f * (float) t;
                    tapLpL[(size_t) t] += (tl - tapLpL[(size_t) t]) * damp;
                    tapLpR[(size_t) t] += (tr - tapLpR[(size_t) t]) * damp;
                    const float tg = 0.75f / (1.0f + 0.3f * (float) t);
                    const bool left = t % 2 == 0;
                    ol  += tapLpL[(size_t) t] * tg * (left ? 1.0f : 0.5f);
                    orr += tapLpR[(size_t) t] * tg * (left ? 0.5f : 1.0f);
                    last = 0.5f * (tapLpL[(size_t) t] + tapLpR[(size_t) t]);
                }
                const float input = p.hold ? 0.0f : 0.5f * (l[i] + r[i]);
                dlyL[dlyW] = softClip (input + last * fb);
                dlyR[dlyW] = softClip (input + last * fb);
                dlyW = (dlyW + 1) % dlyLen;
                wetL[(size_t) i] += ol;
                wetR[(size_t) i] += orr;
            }
        }

        // level match the algorithms (so switching effects doesn't jump in volume)
        {
            static const float trim[(int) MicroAlgo::count] = { 1.0f, 1.9f, 2.6f, 1.4f, 2.2f, 1.5f, 1.1f, 1.6f, 2.4f, 1.3f, 1.5f };
            const float g = trim[(int) p.algo] * ((p.algo == MicroAlgo::Tunnel && p.variation == 3) ? 0.45f : 1.0f);
            for (int i = 0; i < n; ++i) { wetL[(size_t) i] *= g; wetR[(size_t) i] *= g; }
        }

        // 5. filter (resonant low-pass with an LFO on the cutoff)
        if (p.filter < 0.995f || p.modDepth > 0.001f || p.reso > 0.15f)
        {
            const double rateHz = 0.03 * std::pow (400.0, std::clamp ((double) p.modRate, 0.0, 1.0));
            const double k = 2.0 - 1.9 * std::clamp ((double) p.reso, 0.0, 1.0);
            for (int i = 0; i < n; ++i)
            {
                lfoPhase += rateHz / sr; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
                double fc = 60.0 * std::pow (300.0, std::clamp ((double) p.filter, 0.0, 1.0));
                fc *= std::pow (2.0, p.modDepth * 3.0 * std::sin (2.0 * pi * lfoPhase));
                const double g = std::tan (pi * std::clamp (fc, 20.0, sr * 0.45) / sr);
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                auto run = [&] (float x, float& ic1, float& ic2)
                {
                    const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = (float) (2.0 * v1 - ic1); ic2 = (float) (2.0 * v2 - ic2);
                    return (float) v2;
                };
                wetL[(size_t) i] = run (wetL[(size_t) i], f1L, f2L);
                wetR[(size_t) i] = run (wetR[(size_t) i], f1R, f2R);
            }
        }

        // 6. space: reverb on the effect
        if (p.space > 0.001f)
        {
            FxParams rv;
            static constexpr FxType types[] = { FxType::Plate, FxType::Plate, FxType::Hall, FxType::Shimmer };
            const int t = std::clamp (p.reverbType, 0, 3);
            rv.type = types[t];
            rv.a = t == 0 ? 0.12f : t == 1 ? 0.45f : t == 2 ? 0.75f : 0.8f;
            rv.b = 0.45f;
            rv.gnarl = std::clamp (p.decay * (t == 3 ? 0.9f : 0.75f), 0.0f, 0.9f);
            rv.mix = std::clamp (p.space, 0.0f, 1.0f);
            reverb.process (wetL.data(), wetR.data(), n, rv, bpm);
        }

        // 7. mix, looper, level
        float peak = 0.0f;
        const float mix = looperOnly ? 0.0f : p.mix;
        for (int i = 0; i < n; ++i)
        {
            duck += (duckTarget - duck) * 0.01f;
            float ol = dryL[(size_t) i] * (1.0f - mix) * duck + wetL[(size_t) i] * mix;
            float orr = dryR[(size_t) i] * (1.0f - mix) * duck + wetR[(size_t) i] * mix;
            if (p.loopMode != LoopMode::PreFx) { ol += loopOutL[(size_t) i]; orr += loopOutR[(size_t) i]; }
            peak = std::max (peak, std::max (std::abs (wetL[(size_t) i]), std::abs (wetR[(size_t) i])));
            l[i] = ol * p.level;
            r[i] = orr * p.level;
        }
        wetMeter.store (peak);

        // 8. looper: recording takes the section's output (or the dry input in pre-FX / looper-only)
        const size_t cap = loopL.size();
        if (st == LoopState::Recording)
        {
            size_t w = recLen.load();
            for (int i = 0; i < n && w < cap; ++i, ++w)
            {
                const bool dry = p.loopMode != LoopMode::Post;
                loopL[w] = dry ? dryL[(size_t) i] : l[i] / std::max (0.001f, p.level);
                loopR[w] = dry ? dryR[(size_t) i] : r[i] / std::max (0.001f, p.level);
            }
            recLen.store (w);
            if (w % (size_t) (sr * 0.25) < (size_t) n) { loopLen.store (0); rebuildLoopBins(); }
            if (w >= cap) handleCommand (cmdPress, bpm);   // full: close it
        }
        else if (st == LoopState::Overdubbing && len > 0)
        {
            double wp = loopPos;
            for (int i = 0; i < n; ++i)
            {
                while (wp < 0.0) wp += (double) len;
                while (wp >= (double) len) wp -= (double) len;
                const size_t w = (size_t) wp;
                if (dubWritten < len) { undoL[w] = loopL[w]; undoR[w] = loopR[w]; ++dubWritten; if (dubWritten >= len) undoValid.store (true); }
                loopL[w] += dryL[(size_t) i];
                loopR[w] += dryR[(size_t) i];
                wp += speed;
            }
            if (dubWritten > 0) undoValid.store (true);
        }
        loopPos = lp;
        loopPosUi.store (len > 0 ? (float) (std::fmod (std::max (0.0, loopPos), (double) len) / (double) len) : 0.0f);
    }
}
