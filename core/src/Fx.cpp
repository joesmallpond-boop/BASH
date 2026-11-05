#include "bash/Fx.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr double pi    = 3.141592653589793;

        // FDN line lengths at 48 kHz (mutually prime-ish), scaled by room size
        constexpr int fdnBase[8]     = { 1031, 1327, 1523, 1871, 2053, 2381, 2713, 3049 };
        constexpr int diffuseBase[4] = { 142, 107, 379, 277 };
        constexpr float maxRoom = 1.8f;

        // Olli Niemitalo's 90-degree phase-difference network
        constexpr float hilA[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        constexpr float hilB[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };

        inline float softClip (float x) { return x / (1.0f + std::abs (x)); }
        inline float onePoleCoef (double hz, double sr) { return (float) (1.0 - std::exp (-twoPi * std::min (hz, sr * 0.45) / sr)); }

        bool isDistortion (FxType t) { return t >= FxType::Fuzz && t <= FxType::Rectifier; }
        bool isSaturation (FxType t) { return t >= FxType::Tape && t <= FxType::Transformer; }
        bool isReverb (FxType t)     { return t >= FxType::Plate && t <= FxType::Shimmer; }
        bool isDelay (FxType t)      { return t >= FxType::TapeDelay && t <= FxType::GrainDelay; }
    }

    const char* fxName (FxType t)
    {
        switch (t)
        {
            case FxType::None:         return "Off";
            case FxType::Fuzz:         return "Fuzz";
            case FxType::HardClip:     return "Hard Clip";
            case FxType::Wavefolder:   return "Wavefolder";
            case FxType::Bitcrush:     return "Bitcrush";
            case FxType::Rectifier:    return "Rectifier";
            case FxType::Tape:         return "Tape Sat";
            case FxType::Tube:         return "Tube";
            case FxType::Transformer:  return "Transformer";
            case FxType::Plate:        return "Plate";
            case FxType::Hall:         return "Hall";
            case FxType::Spring:       return "Spring";
            case FxType::Shimmer:      return "Shimmer";
            case FxType::TapeDelay:    return "Tape Delay";
            case FxType::DigitalDelay: return "Digital Delay";
            case FxType::PingPong:     return "Ping-Pong";
            case FxType::GrainDelay:   return "Grain Delay";
            case FxType::Chorus:       return "Chorus";
            case FxType::Flanger:      return "Flanger";
            case FxType::Phaser:       return "Phaser";
            case FxType::RingMod:      return "Ring Mod";
            case FxType::FreqShift:    return "Freq Shifter";
            default:                   return "?";
        }
    }

    const char* fxKnobAName (FxType t)
    {
        if (t == FxType::Bitcrush)  return "BITS";
        if (t == FxType::Rectifier) return "RECTIFY";
        if (isDistortion (t) || isSaturation (t)) return "DRIVE";
        if (isReverb (t)) return "SIZE";
        if (isDelay (t))  return "TIME";
        if (t == FxType::RingMod)   return "FREQ";
        if (t == FxType::FreqShift) return "SHIFT";
        if (t == FxType::None)      return "A";
        return "RATE";
    }

    const char* fxKnobBName (FxType t)
    {
        if (t == FxType::Bitcrush)  return "RATE";
        if (t == FxType::Rectifier) return "DRIVE";
        if (t == FxType::Tape)      return "WOW";
        if (t == FxType::Tube)      return "BIAS";
        if (t == FxType::Transformer) return "BODY";
        if (isDistortion (t)) return "TONE";
        if (isReverb (t)) return "DAMP";
        if (isDelay (t))  return "FEEDBACK";
        if (t == FxType::RingMod)   return "WOBBLE";
        if (t == FxType::FreqShift || t == FxType::Flanger || t == FxType::Phaser) return "FEEDBACK";
        if (t == FxType::None)      return "B";
        return "DEPTH";
    }

    const std::vector<std::pair<const char*, double>>& delayDivisions()
    {
        static const std::vector<std::pair<const char*, double>> d {
            { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/16D", 0.375 },
            { "1/8", 0.5 }, { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 }, { "1/4", 1.0 }, { "1/2T", 4.0 / 3.0 },
            { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/1", 4.0 }
        };
        return d;
    }

    // -------------------------------------------------------------------------
    float FxSlot::Line::tap (double delay) const
    {
        const int n = (int) buf.size();
        delay = std::clamp (delay, 0.0, (double) n - 3.0);
        double pos = (double) w - 1.0 - delay;
        while (pos < 0.0) pos += n;
        const int i0 = (int) pos;
        const int i1 = i0 + 1 >= n ? 0 : i0 + 1;
        const float f = (float) (pos - i0);
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }

    void FxSlot::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        dryL.assign ((size_t) std::max (1, maxBlock), 0.0f);
        dryR.assign ((size_t) std::max (1, maxBlock), 0.0f);
        const double scale = sr / 48000.0;
        dlL.init ((int) (sr * 2.1) + 8);
        dlR.init ((int) (sr * 2.1) + 8);
        shortL.init ((int) (sr * 0.05) + 8);
        shortR.init ((int) (sr * 0.05) + 8);
        for (int i = 0; i < 8; ++i) fdn[(size_t) i].init ((int) (fdnBase[i] * maxRoom * scale) + 8);
        for (int i = 0; i < 4; ++i) diffuse[(size_t) i].init ((int) (diffuseBase[i] * scale) + 8);
        preDelay.init ((int) (sr * 0.12) + 8);
        shimmerBuf.init (8192);
        reset();
    }

    void FxSlot::reset()
    {
        toneL = toneR = lowL = lowR = {};
        dcL = dcR = dcxL = dcxR = 0.0f;
        holdL = holdR = 0.0f; holdCount = 0.0;
        lfo = 0.0; lfo2 = 0.25;
        dlL.clear(); dlR.clear(); shortL.clear(); shortR.clear();
        fbLpL = fbLpR = fbHpL = fbHpR = {};
        grains = {}; grainTimer = 0;
        for (auto& l : fdn) l.clear();
        for (auto& d : fdnDamp) d = {};
        for (auto& d : diffuse) d.clear();
        for (auto& a : springAp) a = {};
        preDelay.clear(); shimmerBuf.clear();
        shimmerPhase = 0.0; shimmerFb = 0.0f;
        for (auto& a : phL) a = {};
        for (auto& a : phR) a = {};
        phFbL = phFbR = 0.0f;
        for (auto* arr : { &hilA_L, &hilB_L, &hilA_R, &hilB_R }) for (auto& a : *arr) a = {};
        hilDelayL = hilDelayR = 0.0f;
        shiftFbL = shiftFbR = 0.0f;
    }

    void FxSlot::process (float* l, float* r, int n, const FxParams& p, double bpm)
    {
        if (p.type != current) { reset(); current = p.type; }
        if (p.type == FxType::None || n <= 0) return;

        n = std::min (n, (int) dryL.size());
        std::copy (l, l + n, dryL.begin());
        std::copy (r, r + n, dryR.begin());

        if (isDistortion (p.type))      distortion (l, r, n, p);
        else if (isSaturation (p.type)) saturation (l, r, n, p);
        else if (isReverb (p.type))     reverb (l, r, n, p);
        else if (isDelay (p.type))      delay (l, r, n, p, bpm);
        else                            modulation (l, r, n, p);

        const float mix = std::clamp (p.mix, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            l[i] = dryL[(size_t) i] + (l[i] - dryL[(size_t) i]) * mix;
            r[i] = dryR[(size_t) i] + (r[i] - dryR[(size_t) i]) * mix;
            // safety: nothing leaves an effect as NaN or absurdly loud
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) { reset(); l[i] = r[i] = 0.0f; }
            l[i] = std::clamp (l[i], -8.0f, 8.0f);
            r[i] = std::clamp (r[i], -8.0f, 8.0f);
        }
    }

    // ---- distortion ------------------------------------------------------------
    void FxSlot::distortion (float* l, float* r, int n, const FxParams& p)
    {
        const float g = std::clamp (p.gnarl, 0.0f, 1.0f);
        const float drive = 1.0f + p.a * 24.0f * (1.0f + 2.0f * g);
        const float toneC = onePoleCoef (400.0 * std::pow (2.0, p.b * 5.6), sr);

        auto dcBlock = [] (float x, float& xPrev, float& yPrev) { const float y = x - xPrev + 0.995f * yPrev; xPrev = x; yPrev = y; return y; };

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float x = ch == 0 ? l[i] : r[i];
                float y = x;
                switch (p.type)
                {
                    case FxType::Fuzz:
                    {
                        const float bias = 0.15f + 0.35f * g;
                        y = std::tanh (x * drive + bias) - std::tanh (bias);
                        if (g > 0.5f) y = std::tanh (y * (1.0f + 6.0f * (g - 0.5f)));   // stacked gain
                        y = (ch == 0 ? toneL : toneR).lp (y, toneC) * 0.8f;
                        break;
                    }
                    case FxType::HardClip:
                    {
                        const float ceiling = 1.0f - 0.6f * g;
                        y = std::clamp (x * drive, -ceiling, ceiling) / ceiling * 0.7f;
                        y = (ch == 0 ? toneL : toneR).lp (y, toneC);
                        break;
                    }
                    case FxType::Wavefolder:
                    {
                        const float v = x * (1.0f + p.a * 8.0f * (1.0f + 2.0f * g));
                        y = std::sin (v * 1.5707963f);
                        if (g > 0.0f) y = std::sin (y * (1.0f + 3.0f * g) * 1.5707963f);
                        y = (ch == 0 ? toneL : toneR).lp (y, toneC) * 0.7f;
                        break;
                    }
                    case FxType::Bitcrush:
                    {
                        const float bits = std::max (1.0f, 16.0f - p.a * 14.0f - g * 1.5f);
                        const float levels = std::pow (2.0f, bits - 1.0f);
                        float& hold = ch == 0 ? holdL : holdR;
                        if (ch == 0)
                        {
                            holdCount += 1.0;
                            const double factor = 1.0 + p.b * p.b * 48.0 + g * 40.0;
                            if (holdCount >= factor) { holdCount -= factor; holdL = std::round (l[i] * levels) / levels; holdR = std::round (r[i] * levels) / levels; }
                        }
                        y = hold;
                        break;
                    }
                    case FxType::Rectifier:
                    {
                        const float rect = x + (std::abs (x) - x) * p.a;
                        y = std::tanh (rect * (1.0f + p.b * 8.0f + g * 20.0f));
                        if (g > 0.6f) y = std::abs (y) * 2.0f - 1.0f + (y - std::abs (y));   // octave-up fuzz
                        y = ch == 0 ? dcBlock (y, dcxL, dcL) : dcBlock (y, dcxR, dcR);
                        y *= 0.7f;
                        break;
                    }
                    default: break;
                }
                (ch == 0 ? l[i] : r[i]) = y;
            }
        }
    }

    // ---- saturation ------------------------------------------------------------
    void FxSlot::saturation (float* l, float* r, int n, const FxParams& p)
    {
        const float g = std::clamp (p.gnarl, 0.0f, 1.0f);
        auto dcBlock = [] (float x, float& xPrev, float& yPrev) { const float y = x - xPrev + 0.995f * yPrev; xPrev = x; yPrev = y; return y; };

        if (p.type == FxType::Tape)
        {
            const float drive = 1.0f + p.a * 6.0f * (1.0f + 2.0f * g);
            const float norm = 1.0f / softClip (drive);
            const float hf = onePoleCoef (14000.0 * (1.0 - 0.7 * g), sr);
            const double wowDepth = (0.0005 + p.b * 0.0025 + g * 0.006) * sr;
            for (int i = 0; i < n; ++i)
            {
                lfo += 0.6 / sr;   if (lfo >= 1.0) lfo -= 1.0;
                lfo2 += 7.0 / sr;  if (lfo2 >= 1.0) lfo2 -= 1.0;
                const double mod = 0.004 * sr + wowDepth * (0.5 + 0.5 * std::sin (twoPi * lfo)) + 0.0002 * sr * std::sin (twoPi * lfo2) * (p.b + g);
                shortL.push (l[i]); shortR.push (r[i]);
                l[i] = toneL.lp (softClip (shortL.tap (mod) * drive) * norm, hf);
                r[i] = toneR.lp (softClip (shortR.tap (mod) * drive) * norm, hf);
            }
            return;
        }

        if (p.type == FxType::Tube)
        {
            const float drive = 1.0f + p.a * 10.0f * (1.0f + 2.0f * g);
            const float bias = (p.b - 0.5f) * 0.8f;
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x = ch == 0 ? l[i] : r[i];
                    float y = std::tanh (x * drive + bias) - std::tanh (bias);
                    if (y < 0.0f) y *= 0.8f;   // asymmetry = even harmonics
                    y = ch == 0 ? dcBlock (y, dcxL, dcL) : dcBlock (y, dcxR, dcR);
                    (ch == 0 ? l[i] : r[i]) = y * 0.8f;
                }
            }
            return;
        }

        // Transformer: heavy low-end saturation
        const float lowC = onePoleCoef (220.0, sr);
        const float drive = 1.0f + p.a * 10.0f * (1.0f + g);
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = ch == 0 ? l[i] : r[i];
                const float low = (ch == 0 ? lowL : lowR).lp (x, lowC);
                const float sat = std::tanh (low * drive);
                const float y = std::tanh ((x - low) * (1.0f + g * 2.0f) + sat * (0.6f + p.b * 0.8f));
                (ch == 0 ? l[i] : r[i]) = y * 0.85f;
            }
        }
    }

    // ---- reverb ----------------------------------------------------------------
    void FxSlot::reverb (float* l, float* r, int n, const FxParams& p)
    {
        const float g = std::clamp (p.gnarl, 0.0f, 1.0f);
        const bool freeze = g > 0.95f;
        float room = 0.5f + p.a * 0.5f;
        if (p.type == FxType::Hall || p.type == FxType::Shimmer) room = 1.0f + p.a * 0.75f;
        if (p.type == FxType::Spring) room = 0.35f + p.a * 0.3f;
        room = std::min (room, maxRoom);
        const double t60 = 0.35 + (double) p.a * p.a * 7.0 + g * 25.0;
        const float damp = onePoleCoef (18000.0 * std::pow (2.0, -p.b * 5.0), sr);
        const double scale = sr / 48000.0;
        const double pre = (p.type == FxType::Hall ? 0.025 : p.type == FxType::Shimmer ? 0.03 : 0.004) * sr;
        const float shimmerAmt = (p.type == FxType::Shimmer ? 0.35f : 0.0f) + g * 0.3f;
        const float loopDrive = 1.0f + g * 2.0f;

        double len[8]; float fb[8];
        for (int k = 0; k < 8; ++k)
        {
            len[k] = fdnBase[k] * room * scale;
            fb[k] = freeze ? 1.0f : (float) std::pow (10.0, -3.0 * len[k] / (t60 * sr));
        }

        for (int i = 0; i < n; ++i)
        {
            float in = (freeze ? 0.12f : 0.5f) * (l[i] + r[i]);   // frozen: infinite wash that still builds
            preDelay.push (in);
            in = preDelay.tap (pre) + shimmerFb * shimmerAmt;

            if (p.type == FxType::Spring)
                for (auto& a : springAp) in = a.process (in, 0.65f);   // dispersion "boing"

            for (int d = 0; d < 4; ++d)   // input diffusion
            {
                auto& line = diffuse[(size_t) d];
                const double dl = diffuseBase[d] * scale;
                const float delayed = line.tap (dl - 1.0);
                const float w = in + 0.6f * delayed;
                line.push (w);
                in = delayed - 0.6f * w;
            }

            float o[8]; float sum = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                o[k] = fdnDamp[(size_t) k].lp (fdn[(size_t) k].tap (len[k] - 1.0), damp);
                if (g > 0.3f) o[k] = std::tanh (o[k] * loopDrive) / loopDrive;
                sum += o[k];
            }
            const float house = sum * 0.25f;   // Householder: 2/N
            for (int k = 0; k < 8; ++k)
                fdn[(size_t) k].push ((o[k] - house) * fb[k] + in * ((k & 1) ? -0.35f : 0.35f));

            const float outL = (o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]) * 0.3f;
            const float outR = (o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7]) * 0.3f;

            if (shimmerAmt > 0.0f)
            {
                shimmerBuf.push (0.5f * (outL + outR));
                const double N = 4000.0;
                shimmerPhase += (2.0 - 1.0) / N;
                if (shimmerPhase >= 1.0) shimmerPhase -= 1.0;
                double p2 = shimmerPhase + 0.5; if (p2 >= 1.0) p2 -= 1.0;
                shimmerFb = shimmerBuf.tap ((1.0 - shimmerPhase) * N) * (float) std::sin (pi * shimmerPhase)
                          + shimmerBuf.tap ((1.0 - p2) * N) * (float) std::sin (pi * p2);
                shimmerFb = std::tanh (shimmerFb);
            }

            l[i] = outL;
            r[i] = outR;
        }
    }

    // ---- delay -----------------------------------------------------------------
    void FxSlot::delay (float* l, float* r, int n, const FxParams& p, double bpm)
    {
        const float g = std::clamp (p.gnarl, 0.0f, 1.0f);
        const auto& divs = delayDivisions();
        const int di = std::clamp ((int) (p.a * (float) divs.size()), 0, (int) divs.size() - 1);
        const double base = std::min (divs[(size_t) di].second * 60.0 / std::max (20.0, bpm) * sr, (double) dlL.size() - 8.0);
        const float fbAmt = p.b * 1.05f + g * 0.35f;                          // can exceed 1: the soft clip keeps it bounded
        const float lpC = onePoleCoef (12000.0 * (1.0 - 0.85 * g), sr);
        const float hpC = onePoleCoef (60.0 * (1.0 + 10.0 * g), sr);
        const float drive = 1.0f + g * 3.0f;

        auto shapeFb = [&] (float x, OnePole& lp, OnePole& hp)
        {
            x = lp.lp (x, lpC);
            x = x - hp.lp (x, hpC);
            return p.type == FxType::DigitalDelay && g < 0.1f ? std::clamp (x, -1.5f, 1.5f) : std::tanh (x * drive) / drive;
        };

        if (p.type == FxType::GrainDelay)
        {
            const int grainLen = std::max (64, (int) ((0.05 + 0.12 * (1.0 - g)) * sr));
            for (int i = 0; i < n; ++i)
            {
                if (--grainTimer <= 0)
                {
                    grainTimer = grainLen / 2;
                    for (auto& gr : grains)
                        if (! gr.on)
                        {
                            gr.on = true; gr.age = 0; gr.len = grainLen;
                            gr.pos = base * (0.5 + 0.5 * nextRandom()) + grainLen;
                            const float pitchRoll = nextRandom();
                            gr.inc = pitchRoll < g * 0.3f ? 2.0 : pitchRoll < g * 0.6f ? 0.5 : 1.0;
                            break;
                        }
                }
                float outL = 0.0f, outR = 0.0f;
                for (auto& gr : grains)
                {
                    if (! gr.on) continue;
                    const float w = (float) std::sin (pi * (double) gr.age / gr.len);
                    outL += dlL.tap (gr.pos) * w;
                    outR += dlR.tap (gr.pos) * w;
                    gr.pos -= gr.inc - 1.0;   // reading position drifts for pitch shift
                    gr.pos = std::clamp (gr.pos, 2.0, (double) dlL.size() - 4.0);
                    if (++gr.age >= gr.len) gr.on = false;
                }
                dlL.push (l[i] + shapeFb (outL, fbLpL, fbHpL) * fbAmt * 0.7f);
                dlR.push (r[i] + shapeFb (outR, fbLpR, fbHpR) * fbAmt * 0.7f);
                l[i] = outL; r[i] = outR;
            }
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            double t = base;
            if (p.type == FxType::TapeDelay)
            {
                lfo += 0.45 / sr;  if (lfo >= 1.0) lfo -= 1.0;
                lfo2 += 6.5 / sr;  if (lfo2 >= 1.0) lfo2 -= 1.0;
                t += (0.0015 + 0.006 * g) * sr * std::sin (twoPi * lfo) + 0.0002 * sr * std::sin (twoPi * lfo2);
            }
            const float oL = dlL.tap (t), oR = dlR.tap (t);
            if (p.type == FxType::PingPong)
            {
                dlL.push (0.5f * (l[i] + r[i]) + shapeFb (oR, fbLpL, fbHpL) * fbAmt);
                dlR.push (shapeFb (oL, fbLpR, fbHpR) * fbAmt);
            }
            else
            {
                dlL.push (l[i] + shapeFb (oL, fbLpL, fbHpL) * fbAmt);
                dlR.push (r[i] + shapeFb (oR, fbLpR, fbHpR) * fbAmt);
            }
            l[i] = oL; r[i] = oR;
        }
    }

    // ---- modulation --------------------------------------------------------------
    void FxSlot::modulation (float* l, float* r, int n, const FxParams& p)
    {
        const float g = std::clamp (p.gnarl, 0.0f, 1.0f);
        const double rate = 0.05 + (double) p.a * p.a * 6.0 + g * g * 60.0;   // up to audio-ish rates

        switch (p.type)
        {
            case FxType::Chorus:
            case FxType::Flanger:
            {
                const bool flanger = p.type == FxType::Flanger;
                const double baseDelay = (flanger ? 0.0007 : 0.012) * sr;
                const double depth = (flanger ? 0.0005 + p.b * 0.004 : 0.0005 + p.b * 0.008 + g * 0.01) * sr;
                const float fb = flanger ? (0.5f + p.b * 0.3f + g * 0.18f) * (g > 0.5f ? -1.0f : 1.0f) : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    lfo += rate / sr; if (lfo >= 1.0) lfo -= 1.0;
                    const double mL = baseDelay + depth * (0.5 + 0.5 * std::sin (twoPi * lfo));
                    const double mR = baseDelay + depth * (0.5 + 0.5 * std::sin (twoPi * lfo + pi * 0.5));
                    const float tL = shortL.tap (mL), tR = shortR.tap (mR);
                    shortL.push (l[i] + tL * fb);
                    shortR.push (r[i] + tR * fb);
                    l[i] = flanger ? 0.5f * (l[i] + tL) : 0.6f * l[i] + 0.6f * tL;
                    r[i] = flanger ? 0.5f * (r[i] + tR) : 0.6f * r[i] + 0.6f * tR;
                }
                return;
            }

            case FxType::Phaser:
            {
                const float fbAmt = 0.2f + p.b * 0.5f + g * 0.25f;
                for (int i = 0; i < n; ++i)
                {
                    lfo += rate / sr; if (lfo >= 1.0) lfo -= 1.0;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const double ph = lfo + (ch == 1 ? 0.25 : 0.0);
                        const double f = 200.0 * std::pow (2.0, 4.0 * (0.5 + 0.5 * std::sin (twoPi * ph)));
                        const double tn = std::tan (pi * std::min (f, sr * 0.45) / sr);
                        const float a = (float) ((tn - 1.0) / (tn + 1.0));
                        auto& stages = ch == 0 ? phL : phR;
                        float& fb = ch == 0 ? phFbL : phFbR;
                        const float x = ch == 0 ? l[i] : r[i];
                        float y = x + fb * fbAmt;
                        for (auto& s : stages) y = s.process (y, a);
                        fb = std::tanh (y);
                        (ch == 0 ? l[i] : r[i]) = 0.5f * (x + y);
                    }
                }
                return;
            }

            case FxType::RingMod:
            {
                const double baseHz = 20.0 * std::pow (2.0, p.a * 9.0);
                for (int i = 0; i < n; ++i)
                {
                    lfo2 += 0.5 / sr; if (lfo2 >= 1.0) lfo2 -= 1.0;
                    const double hz = baseHz * std::pow (2.0, p.b * 2.0 * std::sin (twoPi * lfo2));
                    lfo += hz / sr; if (lfo >= 1.0) lfo -= 1.0;
                    const float sine = (float) std::sin (twoPi * lfo);
                    const float carrier = sine + ((sine >= 0.0f ? 1.0f : -1.0f) - sine) * g;   // gnarl -> square
                    l[i] *= carrier;
                    r[i] *= carrier;
                }
                return;
            }

            case FxType::FreqShift:
            default:
            {
                const double shiftHz = (p.a * 2.0 - 1.0) * 1500.0 * (1.0 + g * 3.0);
                const float fbAmt = p.b * 0.7f + g * 0.25f;
                for (int i = 0; i < n; ++i)
                {
                    lfo += shiftHz / sr; lfo -= std::floor (lfo);
                    const float c = (float) std::cos (twoPi * lfo), s = (float) std::sin (twoPi * lfo);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto& A = ch == 0 ? hilA_L : hilA_R;
                        auto& B = ch == 0 ? hilB_L : hilB_R;
                        float& dly = ch == 0 ? hilDelayL : hilDelayR;
                        float& fb  = ch == 0 ? shiftFbL : shiftFbR;
                        const float x = (ch == 0 ? l[i] : r[i]) + fb * fbAmt;
                        float a = x, b = x;
                        for (int k = 0; k < 4; ++k) { a = A[(size_t) k].process (a, hilA[k] * hilA[k]); b = B[(size_t) k].process (b, hilB[k] * hilB[k]); }
                        const float I = dly; dly = a;   // path A gets one sample of delay
                        const float y = I * c - b * s;
                        fb = std::tanh (y);
                        (ch == 0 ? l[i] : r[i]) = y;
                    }
                }
                return;
            }
        }
    }

    // ---- limiter -----------------------------------------------------------------
    void Limiter::prepare (double sampleRate)
    {
        sr = sampleRate;
        gain = 1.0f;
        release = (float) (1.0 - std::exp (-1.0 / (0.12 * sr)));
    }

    void Limiter::process (float* l, float* r, int n)
    {
        const float ceiling = 0.966f;   // -0.3 dBFS
        for (int i = 0; i < n; ++i)
        {
            const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
            const float needed = peak * gain > ceiling ? ceiling / std::max (peak, 1.0e-9f) : 1.0f;
            if (needed < gain) gain = needed;
            else               gain += (std::min (1.0f, needed) - gain) * release;
            l[i] = std::clamp (l[i] * gain, -ceiling, ceiling);
            r[i] = std::clamp (r[i] * gain, -ceiling, ceiling);
        }
    }
}
