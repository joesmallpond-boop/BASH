#include "bash/Drums.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr double pi    = 3.141592653589793;
        constexpr double burstGap = 0.011;   // s between clap bursts
    }

    // =========================================================================
    void DrumVoice::start (const DrumCell& c, int cellIndex, float velocityGain, uint64_t ord)
    {
        params = c;
        cell = cellIndex;
        vel = velocityGain;
        startOrder = ord;
        active = c.kind != DrumCell::Kind::Empty;
        choking = false;
        chokeGain = 1.0f;
        t = 0.0;
        carPhase = modPhase = 0.0;
        nIc1 = nIc2 = 0.0f;
        lpState[0] = lpState[1] = 0.0f;

        if (c.kind == DrumCell::Kind::Sample)
        {
            if (c.sample == nullptr || c.sample->numFrames() < 4) { active = false; return; }
            const double n = (double) c.sample->numFrames() - 2.0;
            const float a = std::clamp (std::min (c.start, c.end), 0.0f, 1.0f);
            const float b = std::clamp (std::max (c.start, c.end), 0.0f, 1.0f);
            lo = a * n; hi = b * n;
            if (hi - lo < 8.0) { active = false; return; }
            inc = c.sample->sampleRate / sr * std::pow (2.0, c.tune / 12.0);
            pos = c.reverse ? hi : lo;
            const double fc = std::clamp ((double) c.cutoff, 20.0, sr * 0.45);
            lpCoef = (float) (1.0 - std::exp (-twoPi * fc / sr));
        }
    }

    float DrumVoice::renderSample()
    {
        return 0.0f;   // stereo path handled in render()
    }

    float DrumVoice::renderFm()
    {
        const auto& p = params.fm;
        const double tt = t;
        const double f = std::max (0.0, (double) p.pitch * std::pow (2.0, p.pitchAmt * std::exp (-tt / std::max (0.001f, p.pitchTime)) / 12.0));
        const double dt = std::min (0.45, f / sr);

        const float modEnv = p.fmAmt * (float) std::exp (-tt / std::max (0.001f, p.fmDecay));
        const float mod = (float) std::sin (twoPi * modPhase) * modEnv;
        const float body = (float) std::sin (twoPi * carPhase + mod) * (float) std::exp (-tt / std::max (0.0005f, p.decay));
        carPhase += dt;              if (carPhase >= 1.0) carPhase -= 1.0;
        modPhase += dt * p.ratio;    if (modPhase >= 1.0) modPhase -= std::floor (modPhase);

        float noiseOut = 0.0f;
        if (p.noise > 0.0f)
        {
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            const float white = (float) (int32_t) noise / 2147483648.0f;
            const double fc = std::min (sr * 0.45, 400.0 * std::pow (2.0, p.tone * 5.5));
            const float g = (float) std::tan (pi * fc / sr), k = 1.2f;
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = white - nIc2;
            const float v1 = a1 * nIc1 + a2 * v3;
            const float v2 = nIc2 + a2 * nIc1 + a3 * v3;
            nIc1 = 2.0f * v1 - nIc1; nIc2 = 2.0f * v2 - nIc2;
            const float hp = white - k * v1 - v2;
            const float filtered = p.tone > 0.6f ? hp : v1 * 1.5f;

            const double burstEnd = (std::max (1, p.bursts) - 1) * burstGap;
            float env;
            if (tt < burstEnd) env = (float) std::exp (-std::fmod (tt, burstGap) / 0.0035);
            else               env = (float) std::exp (-(tt - burstEnd) / std::max (0.001f, p.noiseDecay));
            noiseOut = filtered * env * p.noise;
        }

        const float clickOut = p.click * (float) (std::sin (twoPi * 2800.0 * tt) * std::exp (-tt / 0.0012));

        const float drv = 1.0f + 4.0f * std::clamp (p.drive, 0.0f, 1.0f);
        return std::tanh ((body + noiseOut + clickOut) * drv) / std::tanh (drv);
    }

    void DrumVoice::render (float* l, float* r, int start, int n)
    {
        if (! active) return;
        const float pan = std::clamp (params.pan, -1.0f, 1.0f);
        const float gl = std::cos ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        const float gr = std::sin ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        const float chokeCoef = std::exp (-1.0f / (float) (0.004 * sr));
        const double invSr = 1.0 / sr;

        if (params.kind == DrumCell::Kind::Fm)
        {
            const auto& p = params.fm;
            // ends at about -50 dB of the slowest envelope
            const double life = std::max ({ (double) p.decay, (double) p.noiseDecay * (p.noise > 0 ? 1.0 : 0.0), 0.01 }) * 5.75
                              + (std::max (1, p.bursts) - 1) * burstGap;
            for (int i = start; i < start + n; ++i)
            {
                if (choking) { chokeGain *= chokeCoef; if (chokeGain < 1.0e-4f) { active = false; return; } }
                const float y = renderFm() * params.gain * vel * chokeGain * 0.6f;
                l[i] += y * gl;
                r[i] += y * gr;
                t += invSr;
                if (t > life) { active = false; return; }
            }
            return;
        }

        // sample cell
        const SampleData& d = *params.sample;
        const float* L = d.left.data();
        const float* R = d.right.data();
        const double fadeOut = std::max (0.001f, params.fadeOut);
        for (int i = start; i < start + n; ++i)
        {
            if (choking) { chokeGain *= chokeCoef; if (chokeGain < 1.0e-4f) { active = false; return; } }
            const double remaining = params.reverse ? pos - lo : hi - pos;
            if (remaining <= 0.0) { active = false; return; }

            const int i0 = (int) pos;
            const float fr = (float) (pos - i0);
            float sl = L[i0] + (L[i0 + 1] - L[i0]) * fr;
            float sr2 = R[i0] + (R[i0 + 1] - R[i0]) * fr;

            // envelope: fades + transient shaper
            float g = 1.0f;
            if (params.fadeIn > 0.0f && t < params.fadeIn) g *= (float) (t / params.fadeIn);
            const double remainingSec = remaining / inc / sr;
            if (remainingSec < fadeOut) g *= (float) (remainingSec / fadeOut);
            const float atk = 1.0f + params.attack * 1.5f * (float) std::exp (-t / 0.008);
            float sus = 1.0f;
            if (params.sustain > 0.0f) sus = 1.0f + params.sustain * 1.5f * (1.0f - (float) std::exp (-t / 0.04));
            else if (params.sustain < 0.0f) sus = (float) std::exp (params.sustain * 12.0 * std::max (0.0, t - 0.015));
            g *= std::max (0.0f, atk) * sus;

            // 12 dB low-pass
            lpState[0] += (sl - lpState[0]) * lpCoef;
            lpState[1] += (sr2 - lpState[1]) * lpCoef;
            sl = lpState[0]; sr2 = lpState[1];

            const float gain = params.gain * vel * chokeGain * g;
            l[i] += sl * gain * gl;
            r[i] += sr2 * gain * gr;

            pos += params.reverse ? -inc : inc;
            t += invSr;
        }
    }

    // =========================================================================
    void DrumPlayer::trigger (const DrumCell& c, int cellIndex, int velocity, bool accent)
    {
        if (c.kind == DrumCell::Kind::Empty) return;
        if (c.choke > 0)
            for (auto& v : voices)
                if (v.isActive() && v.chokeGroup() == c.choke) v.choke();

        DrumVoice* target = nullptr;
        for (auto& v : voices) if (! v.isActive()) { target = &v; break; }
        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices) if (v.order() < target->order()) target = &v;
        }
        const float vg = (0.35f + 0.65f * (float) std::clamp (velocity, 1, 127) / 127.0f) * (accent ? 1.2f : 1.0f);
        target->start (c, cellIndex, vg, ++counter);
    }

    void DrumPlayer::stopVoicesUsing (const SampleData* s)
    {
        for (auto& v : voices) if (v.isActive() && v.sample() == s) v.stop();
    }

    // =========================================================================
    namespace
    {
        FmDrumParams fm (float pitch, float pitchAmt, float pitchTime, float ratio, float fmAmt, float fmDecay,
                         float noiseLvl, float tone, float noiseDecay, int bursts, float decay, float click, float drive)
        {
            FmDrumParams p;
            p.pitch = pitch; p.pitchAmt = pitchAmt; p.pitchTime = pitchTime; p.ratio = ratio; p.fmAmt = fmAmt;
            p.fmDecay = fmDecay; p.noise = noiseLvl; p.tone = tone; p.noiseDecay = noiseDecay; p.bursts = bursts;
            p.decay = decay; p.click = click; p.drive = drive;
            return p;
        }
    }

    const std::vector<FmPreset>& fmPresets()
    {
        //                                 pitch  pAmt  pTime  ratio fm   fmDec  noise tone  nDec  bur decay click drive   gain choke
        static const std::vector<FmPreset> presets {
            { "Kick Deep",     fm (48,   30, 0.045f, 1.0f, 0.0f, 0.01f,  0.0f, 0.5f, 0.05f, 1, 0.55f, 0.35f, 0.25f), 1.0f, 0 },  // 0
            { "Kick Punch",    fm (55,   36, 0.030f, 1.0f, 0.8f, 0.01f,  0.0f, 0.5f, 0.05f, 1, 0.35f, 0.60f, 0.50f), 1.0f, 0 },  // 1
            { "Kick Boom",     fm (42,   24, 0.080f, 1.0f, 0.0f, 0.01f,  0.0f, 0.5f, 0.05f, 1, 1.20f, 0.15f, 0.30f), 1.0f, 0 },  // 2
            { "Kick Hard",     fm (60,   48, 0.020f, 1.5f, 2.0f, 0.008f, 0.0f, 0.5f, 0.05f, 1, 0.30f, 0.80f, 0.90f), 0.9f, 0 },  // 3
            { "Snare Tight",   fm (190,  12, 0.020f, 1.6f, 1.5f, 0.03f,  0.7f, 0.7f, 0.12f, 1, 0.12f, 0.40f, 0.30f), 0.9f, 0 },  // 4
            { "Snare Fat",     fm (160,  10, 0.030f, 1.0f, 0.5f, 0.02f,  0.8f, 0.5f, 0.20f, 1, 0.18f, 0.30f, 0.40f), 0.9f, 0 },  // 5
            { "Rim",           fm (420,   6, 0.005f, 2.4f, 2.0f, 0.01f,  0.2f, 0.9f, 0.02f, 1, 0.04f, 0.90f, 0.20f), 0.7f, 0 },  // 6
            { "Clap",          fm (0,     0, 0.010f, 1.0f, 0.0f, 0.01f,  1.0f, 0.6f, 0.15f, 4, 0.001f, 0.0f, 0.30f), 0.9f, 0 },  // 7
            { "Hat Closed",    fm (3200,  0, 0.010f, 3.41f, 4.0f, 0.03f, 0.9f, 0.95f, 0.04f, 1, 0.03f, 0.10f, 0.10f), 0.55f, 1 },// 8
            { "Hat Open",      fm (3200,  0, 0.010f, 3.41f, 4.0f, 0.20f, 0.9f, 0.95f, 0.35f, 1, 0.30f, 0.10f, 0.10f), 0.5f, 1 }, // 9
            { "Hat Pedal",     fm (3000,  0, 0.010f, 3.41f, 3.0f, 0.03f, 0.8f, 0.9f, 0.08f, 1, 0.05f, 0.05f, 0.10f), 0.45f, 1 },// 10
            { "Ride",          fm (2400,  0, 0.010f, 2.73f, 3.0f, 0.50f, 0.4f, 0.9f, 0.60f, 1, 0.90f, 0.20f, 0.10f), 0.45f, 0 },// 11
            { "Crash",         fm (3000,  0, 0.010f, 3.1f, 3.0f, 0.30f,  1.0f, 0.85f, 1.20f, 1, 0.80f, 0.10f, 0.20f), 0.5f, 0 }, // 12
            { "Tom Low",       fm (80,   10, 0.100f, 1.0f, 0.0f, 0.01f,  0.1f, 0.4f, 0.05f, 1, 0.40f, 0.20f, 0.20f), 0.9f, 0 },  // 13
            { "Tom Mid",       fm (120,  10, 0.080f, 1.0f, 0.0f, 0.01f,  0.1f, 0.4f, 0.05f, 1, 0.35f, 0.20f, 0.20f), 0.9f, 0 },  // 14
            { "Tom High",      fm (170,   9, 0.070f, 1.0f, 0.0f, 0.01f,  0.1f, 0.4f, 0.05f, 1, 0.30f, 0.20f, 0.20f), 0.9f, 0 },  // 15
            { "Cowbell",       fm (560,   0, 0.010f, 1.48f, 1.2f, 0.20f, 0.0f, 0.5f, 0.05f, 1, 0.25f, 0.30f, 0.30f), 0.6f, 0 },  // 16
            { "Clave",         fm (2500,  0, 0.010f, 1.0f, 0.0f, 0.01f,  0.0f, 0.5f, 0.05f, 1, 0.035f, 0.5f, 0.10f), 0.6f, 0 }, // 17
            { "Zap",           fm (200,  48, 0.080f, 0.5f, 3.0f, 0.10f,  0.0f, 0.5f, 0.05f, 1, 0.20f, 0.10f, 0.40f), 0.7f, 0 },  // 18
            { "Blip",          fm (880,  12, 0.010f, 1.0f, 0.5f, 0.02f,  0.0f, 0.5f, 0.05f, 1, 0.08f, 0.10f, 0.20f), 0.6f, 0 },  // 19
            { "Shaker",        fm (0,     0, 0.010f, 1.0f, 0.0f, 0.01f,  1.0f, 0.9f, 0.06f, 2, 0.001f, 0.0f, 0.10f), 0.5f, 0 },  // 20
            { "Metal",         fm (330,   0, 0.010f, 3.53f, 6.0f, 0.40f, 0.2f, 0.8f, 0.10f, 1, 0.50f, 0.20f, 0.40f), 0.6f, 0 },  // 21
            { "Sub Drop",      fm (35,   12, 0.500f, 1.0f, 0.0f, 0.01f,  0.0f, 0.5f, 0.05f, 1, 1.50f, 0.05f, 0.30f), 1.0f, 0 },  // 22
            { "Noise Hit",     fm (0,     0, 0.010f, 1.0f, 0.0f, 0.01f,  1.0f, 0.3f, 0.25f, 1, 0.001f, 0.2f, 0.80f), 0.7f, 0 },  // 23
            { "Kick Distorted",fm (52,   40, 0.035f, 2.0f, 1.5f, 0.02f,  0.1f, 0.3f, 0.03f, 1, 0.45f, 0.70f, 1.00f), 0.8f, 0 },  // 24
            { "Snare Noise",   fm (220,   8, 0.010f, 1.0f, 0.0f, 0.01f,  1.0f, 0.8f, 0.25f, 1, 0.06f, 0.30f, 0.50f), 0.8f, 0 },  // 25
            { "Cymbal",        fm (2800,  0, 0.010f, 3.27f, 5.0f, 0.60f, 0.8f, 0.92f, 0.90f, 1, 0.90f, 0.15f, 0.15f), 0.5f, 0 },// 26
            { "Kick Init",     fm (50,   30, 0.040f, 1.0f, 0.0f, 0.01f,  0.0f, 0.5f, 0.05f, 1, 0.45f, 0.30f, 0.20f), 1.0f, 0 },  // 27
            { "Snare Init",    fm (180,  10, 0.020f, 1.0f, 0.5f, 0.02f,  0.7f, 0.65f, 0.15f, 1, 0.14f, 0.30f, 0.20f), 0.9f, 0 },// 28
            { "Clap Init",     fm (0,     0, 0.010f, 1.0f, 0.0f, 0.01f,  1.0f, 0.62f, 0.18f, 3, 0.001f, 0.0f, 0.20f), 0.9f, 0 }, // 29
        };
        return presets;
    }

    const std::vector<FactoryKit>& factoryKits()
    {
        // cells 0-3 = kick, snare/clap, closed hat, open hat (the Groove roles), then extras
        static const std::vector<FactoryKit> kits {
            { "Acid House",   { 0, 4, 8, 9,   7, 16, 13, 14,  6, 11, 12, 15,  19, 20, 22, 21 } },
            { "Electro",      { 1, 6, 10, 9,  7, 17, 19, 18,  13, 14, 15, 16, 20, 21, 11, 23 } },
            { "Industrial",   { 24, 25, 8, 12, 23, 21, 18, 3, 6, 7, 13, 22,  16, 20, 9, 19 } },
            { "Minimal",      { 2, 6, 10, 9,  17, 20, 19, 16, 13, 15, 7, 11,  22, 21, 4, 8 } },
            { "Big Room",     { 3, 5, 8, 9,   7, 12, 22, 18, 13, 14, 15, 11, 25, 20, 6, 0 } },
        };
        return kits;
    }

    const std::vector<FmStartPoint>& fmStartPoints()
    {
        static const std::vector<FmStartPoint> points {
            { "Kick", 27 }, { "Snare", 28 }, { "Clap", 29 }, { "Cymbal", 26 }, { "Hat", 8 }, { "Tom", 14 }
        };
        return points;
    }

    DrumCell makeFmCell (const FmPreset& preset)
    {
        DrumCell c;
        c.kind = DrumCell::Kind::Fm;
        c.fm = preset.p;
        c.gain = preset.gain;
        c.choke = preset.choke;
        return c;
    }
}
