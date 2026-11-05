#include "bash/AcidVoice.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        inline float polyBlep (double t, double dt)
        {
            if (t < dt)       { t /= dt;             return (float) (t + t - t * t - 1.0); }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt;  return (float) (t * t + t + t + 1.0); }
            return 0.0f;
        }

        inline float midiToHz (int n) { return 440.0f * std::pow (2.0f, (float) (n - 69) / 12.0f); }
    }

    void AcidVoice::prepare (double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        reset();
    }

    void AcidVoice::reset()
    {
        note = -1; gate = false;
        amp = filterEnv = accentEnv = 0.0f;
        ic1a = ic2a = ic1b = ic2b = 0.0f;
        phase = 0.0;
        coeffCountdown = 0;
    }

    void AcidVoice::noteOn (int n, int velocity, bool accent, bool legato)
    {
        targetFreq = midiToHz (n);
        const bool glideFromHeld = legato && gate;
        if (! glideFromHeld)
        {
            freq = targetFreq;
            filterEnv = 1.0f;            // retrigger the filter sweep
            if (accent) accentEnv = 1.0f;
            isAccent = accent;
        }
        velocityGain = 0.55f + 0.45f * (float) std::clamp (velocity, 1, 127) / 127.0f;
        note = n;
        gate = true;
    }

    void AcidVoice::noteOff (int n)
    {
        if (n == note)
            gate = false;
    }

    void AcidVoice::render (float* out, int start, int n, const Params& p)
    {
        if (isSilent())
            return;

        const float invSr     = (float) (1.0 / sr);
        const float ampAtt    = 1.0f - std::exp (-invSr / 0.0015f);
        const float ampRel    = 1.0f - std::exp (-invSr / 0.008f);
        const float decayTime = std::max (0.01f, p.decay * (isAccent ? 0.5f : 1.0f));
        const float envCoef   = std::exp (-invSr / decayTime);
        const float accCoef   = std::exp (-invSr / 0.18f);
        const float glideCoef = 1.0f - std::exp (-invSr / std::max (0.001f, p.glide));
        const float kA        = 2.0f - 1.96f * std::clamp (p.resonance, 0.0f, 1.0f); // resonant stage
        const float kB        = 1.414f;                                                   // flat stage
        const float driveAmt  = 1.0f + 4.0f * std::clamp (p.drive, 0.0f, 1.0f);
        const float driveNorm = 1.0f / std::tanh (driveAmt);
        const float squareMix = std::clamp (p.wave, 0.0f, 1.0f);

        for (int i = start; i < start + n; ++i)
        {
            // pitch (glide)
            freq += (targetFreq - freq) * glideCoef;
            const double dt = std::min (0.45, (double) freq / sr);

            // oscillator
            phase += dt;
            if (phase >= 1.0) phase -= 1.0;
            float saw = (float) (2.0 * phase - 1.0) - polyBlep (phase, dt);
            double p2 = phase + 0.5; if (p2 >= 1.0) p2 -= 1.0;
            float sq = (phase < 0.5 ? 1.0f : -1.0f) + polyBlep (phase, dt) - polyBlep (p2, dt);
            const float osc = saw + (sq - saw) * squareMix;

            // envelopes
            amp += ((gate ? 1.0f : 0.0f) - amp) * (gate ? ampAtt : ampRel);
            filterEnv *= envCoef;
            accentEnv *= accCoef;

            // filter coefficients every 8 samples
            if (--coeffCountdown <= 0)
            {
                coeffCountdown = 8;
                const float octaves = p.envMod * filterEnv * 5.0f
                                    + (isAccent ? p.accent * accentEnv * 2.5f : 0.0f);
                float fc = p.cutoff * std::pow (2.0f, octaves);
                fc = std::clamp (fc, 20.0f, (float) sr * 0.45f);
                const float g = (float) std::tan (pi * fc / sr);
                a1a = 1.0f / (1.0f + g * (g + kA)); a2a = g * a1a; a3a = g * a2a;
                a1b = 1.0f / (1.0f + g * (g + kB)); a2b = g * a1b; a3b = g * a2b;
            }

            // stage A (resonant)
            float v3 = osc - ic2a;
            float v1 = a1a * ic1a + a2a * v3;
            float v2 = ic2a + a2a * ic1a + a3a * v3;
            ic1a = 2.0f * v1 - ic1a; ic2a = 2.0f * v2 - ic2a;
            const float lowA = std::tanh (v2);   // gentle saturation keeps resonance musical

            // stage B
            v3 = lowA - ic2b;
            v1 = a1b * ic1b + a2b * v3;
            v2 = ic2b + a2b * ic1b + a3b * v3;
            ic1b = 2.0f * v1 - ic1b; ic2b = 2.0f * v2 - ic2b;

            const float accentGain = isAccent ? 1.0f + 0.5f * p.accent * accentEnv : 1.0f;
            const float y = std::tanh (v2 * driveAmt) * driveNorm;
            out[i] += y * amp * velocityGain * accentGain * 0.5f;
        }
    }
}
