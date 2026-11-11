#include "bash/MachineSynth.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586;
        constexpr double pi    = 3.141592653589793;

        inline float polyBlep (double t, double dt)
        {
            if (t < dt)       { t /= dt;            return (float) (t + t - t * t - 1.0); }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
            return 0.0f;
        }

        inline float sawBl (double ph, double dt)   { return (float) (2.0 * ph - 1.0) - polyBlep (ph, dt); }

        inline float pulseBl (double ph, double dt, double pw)
        {
            double p2 = ph + 1.0 - pw; if (p2 >= 1.0) p2 -= 1.0;
            return (ph < pw ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (p2, dt);
        }

        inline void advance (double& ph, double dt) { ph += dt; if (ph >= 1.0) ph -= std::floor (ph); }

        inline float midiToHz (int n) { return 440.0f * std::pow (2.0f, (float) (n - 69) / 12.0f); }

        // vowel formants (Hz): A E I O U
        constexpr float vowels[5][3] = {
            { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 }, { 450, 800, 2830 }, { 325, 700, 2530 }
        };
        constexpr float formantGains[3] = { 1.0f, 0.6f, 0.35f };

        constexpr float fmRatios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f };
    }

    const char* machineName (Machine m)
    {
        switch (m)
        {
            case Machine::Acid:          return "Acid";
            case Machine::Sid:           return "SID";
            case Machine::FmPlus:        return "FM+";
            case Machine::SuperWave:     return "SuperWave";
            case Machine::Vo:            return "VO";
            case Machine::PulseInterval: return "Pulse-Interval";
            case Machine::Noise:         return "Noise";
            case Machine::PolySaw:       return "Poly Saw";
            case Machine::Pad:           return "Pad";
            case Machine::Keys:          return "Keys";
            case Machine::Organ:         return "Organ";
            default:                     return "?";
        }
    }

    const char* machineCharacterName (Machine m)
    {
        switch (m)
        {
            case Machine::Acid:          return "SAW-SQR";
            case Machine::Sid:           return "WIDTH";
            case Machine::FmPlus:        return "FM AMT";
            case Machine::SuperWave:     return "DETUNE";
            case Machine::Vo:            return "VOWEL";
            case Machine::PulseInterval: return "INTERVAL";
            case Machine::Noise:         return "RESO";
            case Machine::PolySaw:       return "DETUNE";
            case Machine::Pad:           return "MOVEMENT";
            case Machine::Keys:          return "TINE";
            case Machine::Organ:         return "DRAWBARS";
            default:                     return "CHARACTER";
        }
    }

    const char* machineTimbreName (Machine m)
    {
        switch (m)
        {
            case Machine::Acid:          return "SUB";
            case Machine::Sid:           return "CRUSH";
            case Machine::FmPlus:        return "RATIO";
            case Machine::SuperWave:     return "SPREAD";
            case Machine::Vo:            return "SHIFT";
            case Machine::PulseInterval: return "PW";
            case Machine::Noise:         return "COLOUR";
            case Machine::PolySaw:       return "SUB";
            case Machine::Pad:           return "AIR";
            case Machine::Keys:          return "BARK";
            case Machine::Organ:         return "ROTARY";
            default:                     return "TIMBRE";
        }
    }

    const char* filterTypeName (FilterType f)
    {
        switch (f)
        {
            case FilterType::LowPass:  return "Low-pass";
            case FilterType::BandPass: return "Band-pass";
            case FilterType::HighPass: return "High-pass";
            default:                   return "?";
        }
    }

    // ---------------------------------------------------------------------
    void MachineVoice::prepare (double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        reset();
    }

    void MachineVoice::reset()
    {
        note = -1; gate = false; ampStage = Stage::Idle;
        amp = filterEnv = accentEnv = 0.0f;
        ic1a = ic2a = ic1b = ic2b = 0.0f;
        formants = {}; noiseBp = {}; noiseLp = 0.0f;
        formantPos = formantShift = -1.0f;
        for (size_t i = 0; i < phase.size(); ++i) phase[i] = (double) i * 0.137;   // decorrelated SuperWave starts
        modPhase = lfoPhase = 0.0;
        fmFeedback = 0.0f;
        modEnv = keyEnv = 0.0f;
        coeffCountdown = 0;
    }

    void MachineVoice::noteOn (int n, int velocity, bool accent, bool legato)
    {
        targetFreq = midiToHz (n);
        const bool glideFromHeld = legato && gate;
        if (! glideFromHeld)
        {
            freq = targetFreq;
            filterEnv = 1.0f;
            if (accent) accentEnv = 1.0f;
            isAccent = accent;
            ampStage = Stage::Attack;     // retrigger from the current level: no click
        }
        velocityGain = 0.55f + 0.45f * (float) std::clamp (velocity, 1, 127) / 127.0f;
        if (! glideFromHeld)
        {
            modEnv = 0.4f + 0.6f * (float) std::clamp (velocity, 1, 127) / 127.0f;
            keyEnv = 1.0f;
            drift = ((float) ((counter * 2654435761u) % 1000u) / 1000.0f - 0.5f) * 0.004f;   // +-0.2%
        }
        note = n;
        gate = true;
        startedAt = ++counter;
    }

    void MachineVoice::noteOff (int n)
    {
        if (n == note && gate)
        {
            gate = false;
            ampStage = Stage::Release;
        }
    }

    void MachineVoice::updateFilter (const Params& p, float fc)
    {
        fc = std::clamp (fc, 20.0f, (float) sr * 0.45f);
        g  = (float) std::tan (pi * fc / sr);
        kA = 2.0f - 1.96f * std::clamp (p.resonance, 0.0f, 1.0f);
        kB = 1.414f;
    }

    float MachineVoice::oscillator (const Params& p, double dt)
    {
        const float c = std::clamp (p.character, 0.0f, 1.0f);
        const float t = std::clamp (p.timbre, 0.0f, 1.0f);

        switch (p.machine)
        {
            case Machine::Acid:
            default:
            {
                const float saw = sawBl (phase[0], dt);
                const float sq  = pulseBl (phase[0], dt, 0.5);
                // sub oscillator an octave down (square), amount = timbre below 0.5 → none
                double& sub = phase[1];
                const float subAmt = std::max (0.0f, t - 0.5f) * 1.2f;
                const float subOsc = pulseBl (sub, dt * 0.5, 0.5);
                advance (phase[0], dt);
                advance (sub, dt * 0.5);
                return saw + (sq - saw) * c + subOsc * subAmt;
            }

            case Machine::Sid:
            {
                advance (lfoPhase, 0.35 / sr);
                const double pw = std::clamp (0.5 - 0.45 * c + 0.06 * std::sin (twoPi * lfoPhase), 0.03, 0.97);
                float y = pulseBl (phase[0], dt, pw);
                advance (phase[0], dt);
                const float levels = std::pow (2.0f, 8.0f - 6.0f * t);   // 256 .. 4 steps
                y = std::round (y * levels * 0.5f) / (levels * 0.5f);
                return y * 0.8f;
            }

            case Machine::FmPlus:
            {
                const float ratio = fmRatios[std::min (7, (int) (t * 8.0f))];
                const float index = c * 6.0f + p.envMod * filterEnv * 4.0f;
                const float mod = (float) std::sin (twoPi * modPhase + fmFeedback * 1.5);
                fmFeedback = mod * c * 0.6f;
                const float y = (float) std::sin (twoPi * phase[0] + index * mod);
                advance (phase[0], dt);
                advance (modPhase, dt * ratio);
                return y;
            }

            case Machine::SuperWave:
            {
                static constexpr float spread[7] = { -1.0f, -0.62f, -0.3f, 0.0f, 0.3f, 0.62f, 1.0f };
                const float detune = c * 0.6f;   // semitones at the edges
                float y = 0.0f;
                for (int i = 0; i < 7; ++i)
                {
                    const double d = dt * std::pow (2.0, spread[i] * detune / 12.0);
                    const float gainI = i == 3 ? 1.0f - 0.5f * t : 0.25f + 0.75f * t;
                    y += sawBl (phase[(size_t) i], d) * gainI;
                    advance (phase[(size_t) i], d);
                }
                return y * 0.35f;
            }

            case Machine::Vo:
            {
                const float src = sawBl (phase[0], dt) * 0.7f + pulseBl (phase[0], dt, 0.3) * 0.3f;
                advance (phase[0], dt);
                if (std::abs (c - formantPos) > 0.002f || std::abs (t - formantShift) > 0.002f)
                {
                    formantPos = c; formantShift = t;
                    const float pos = c * 4.0f;
                    const int v0 = std::min (3, (int) pos);
                    const float frac = pos - (float) v0;
                    const float shift = 0.7f + 0.7f * t;
                    for (int f = 0; f < 3; ++f)
                    {
                        const float hz = (vowels[v0][f] + (vowels[v0 + 1][f] - vowels[v0][f]) * frac) * shift;
                        auto& b = formants[(size_t) f];
                        const float gg = (float) std::tan (pi * std::min (hz, (float) sr * 0.45f) / sr);
                        b.k = 0.12f;
                        b.a1 = 1.0f / (1.0f + gg * (gg + b.k)); b.a2 = gg * b.a1; b.a3 = gg * b.a2;
                    }
                }
                float y = 0.0f;
                for (int f = 0; f < 3; ++f)
                {
                    auto& b = formants[(size_t) f];
                    const float v3 = src - b.ic2;
                    const float v1 = b.a1 * b.ic1 + b.a2 * v3;
                    const float v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
                    b.ic1 = 2.0f * v1 - b.ic1; b.ic2 = 2.0f * v2 - b.ic2;
                    y += v1 * formantGains[f];
                }
                return y * 0.6f;
            }

            case Machine::PulseInterval:
            {
                const double pw = 0.5 - 0.42 * t;
                const double dt2 = dt * std::pow (2.0, std::round (c * 12.0f) / 12.0);
                const float y = pulseBl (phase[0], dt, pw) + pulseBl (phase[1], dt2, pw);
                advance (phase[0], dt);
                advance (phase[1], dt2);
                return y * 0.55f;
            }

            case Machine::PolySaw:
            {
                const double d = c * 0.012;   // up to ~20 cents apart
                const double dA = dt * (1.0 + d + drift), dB = dt * (1.0 - d + drift);
                const float y = sawBl (phase[0], dA) + sawBl (phase[1], dB);
                const float sub = pulseBl (phase[2], dt * 0.5, 0.5);
                advance (phase[0], dA); advance (phase[1], dB); advance (phase[2], dt * 0.5);
                return y * 0.45f + sub * t * 0.5f;
            }

            case Machine::Pad:
            {
                advance (lfoPhase, (0.25 + 0.5 * c) / sr);
                const double wob = std::sin (twoPi * lfoPhase);
                const double d = 0.004 + 0.01 * c;
                const double d0 = dt * (1.0 + drift + d * wob * 0.3), d1 = dt * (1.0 - d), d2 = dt * (1.0 + d);
                float y = sawBl (phase[0], d0) * 0.4f + sawBl (phase[1], d1) * 0.3f + sawBl (phase[2], d2) * 0.3f;
                const double pw = 0.5 + 0.35 * c * std::sin (twoPi * lfoPhase * 0.7 + 1.0);
                y += pulseBl (phase[3], dt * 0.5, pw) * 0.35f;
                advance (phase[0], d0); advance (phase[1], d1); advance (phase[2], d2); advance (phase[3], dt * 0.5);
                noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
                const float air = (float) (int32_t) noiseState / 2147483648.0f;
                return y * 0.6f + air * t * 0.025f;
            }

            case Machine::Keys:
            {
                // two-operator FM electric piano: a 1:1 "bark" pair and a high, fast-fading tine
                const float invSr = (float) (1.0 / sr);
                modEnv *= std::exp (-invSr / 0.35f);
                keyEnv *= std::exp (-invSr / 2.5f);
                const float bark = (float) std::sin (twoPi * modPhase) * (0.4f + 2.6f * t) * modEnv;
                const float body = (float) std::sin (twoPi * phase[0] + bark);
                const float tineMod = (float) std::sin (twoPi * phase[2]) * 2.0f * modEnv * modEnv;
                const float tine = (float) std::sin (twoPi * phase[1] + tineMod) * c * modEnv * 0.5f;
                advance (phase[0], dt); advance (modPhase, dt); advance (phase[1], std::min (0.45, dt * 4.0)); advance (phase[2], std::min (0.45, dt * 14.0));
                return (body * 0.8f + tine) * (0.35f + 0.65f * keyEnv);
            }

            case Machine::Organ:
            {
                // drawbars 16', 8', 4', 2 2/3', 2' and 1': Character sweeps from mellow to full
                advance (lfoPhase, (5.5 + 1.2 * t) / sr);
                const double vib = 1.0 + 0.004 * t * std::sin (twoPi * lfoPhase);
                static constexpr double harm[6] = { 0.5, 1.0, 2.0, 3.0, 4.0, 8.0 };
                const float bars[6] = { 0.8f, 1.0f, 0.3f + 0.7f * c, 0.6f * c, 0.5f * c, 0.4f * c * c };
                float y = 0.0f;
                for (int h = 0; h < 6; ++h)
                {
                    const double dh = dt * harm[h] * vib;
                    if (dh < 0.45) y += (float) std::sin (twoPi * phase[(size_t) h]) * bars[h];
                    advance (phase[(size_t) h], std::min (0.45, dh));
                }
                const float amTrem = 1.0f - 0.15f * t * (float) (0.5 + 0.5 * std::sin (twoPi * lfoPhase));
                return y * 0.3f * amTrem;
            }

            case Machine::Noise:
            {
                noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
                const float white = (float) (int32_t) noiseState / 2147483648.0f;
                noiseLp += (white - noiseLp) * (0.05f + 0.95f * (1.0f - t));
                const float src = noiseLp;
                auto& b = noiseBp;
                const float gg = (float) std::tan (pi * std::min ((double) freq, sr * 0.45) / sr);
                b.k = 2.0f - 1.98f * c;
                b.a1 = 1.0f / (1.0f + gg * (gg + b.k)); b.a2 = gg * b.a1; b.a3 = gg * b.a2;
                const float v3 = src - b.ic2;
                const float v1 = b.a1 * b.ic1 + b.a2 * v3;
                const float v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
                b.ic1 = 2.0f * v1 - b.ic1; b.ic2 = 2.0f * v2 - b.ic2;
                return (v1 * (0.6f + 2.0f * c) + src * (1.0f - c) * 0.6f);
            }
        }
    }

    void MachineVoice::render (float* out, int start, int n, const Params& p)
    {
        if (isSilent())
            return;

        const float invSr     = (float) (1.0 / sr);
        // chords stack up to 7 voices: the poly machines sit lower so a full chord doesn't clip
        const float voiceGain = isPolyMachine (p.machine) ? 0.24f : 0.5f;
        // the poly voices keep a natural swell and tail even with acid-style envelope settings
        const float attack  = p.machine == Machine::Pad ? std::max (p.attack, 0.08f) : p.attack;
        const float release = p.machine == Machine::Pad ? std::max (p.release, 0.6f)
                            : isPolyMachine (p.machine) ? std::max (p.release, 0.15f) : p.release;
        const float attackInc = invSr / std::max (0.0005f, attack);
        const float decCoef   = std::exp (-invSr / std::max (0.002f, p.ampDecay * 0.35f));
        const float relCoef   = std::exp (-invSr / std::max (0.002f, release * 0.35f));
        const float sustain   = std::clamp (p.sustain, 0.0f, 1.0f);
        const float decayTime = std::max (0.01f, p.decay * (isAccent ? 0.5f : 1.0f));
        const float envCoef   = std::exp (-invSr / decayTime);
        const float accCoef   = std::exp (-invSr / 0.18f);
        const float glideCoef = 1.0f - std::exp (-invSr / std::max (0.001f, p.glide));
        const float driveAmt  = 1.0f + 4.0f * std::clamp (p.drive, 0.0f, 1.0f);
        const float driveNorm = 1.0f / std::tanh (driveAmt);
        const int   ftype     = (int) p.filterType;

        for (int i = start; i < start + n; ++i)
        {
            freq += (targetFreq - freq) * glideCoef;
            const double dt = std::min (0.45, (double) freq / sr);

            // amp envelope
            switch (ampStage)
            {
                case Stage::Attack:  amp += attackInc; if (amp >= 1.0f) { amp = 1.0f; ampStage = Stage::Decay; } break;
                case Stage::Decay:   amp = sustain + (amp - sustain) * decCoef; if (std::abs (amp - sustain) < 1.0e-4f) ampStage = Stage::Sustain; break;
                case Stage::Sustain: amp = sustain; break;
                case Stage::Release: amp *= relCoef; if (amp < 1.0e-4f) { amp = 0.0f; ampStage = Stage::Idle; } break;
                case Stage::Idle:    amp = 0.0f; break;
            }
            filterEnv *= envCoef;
            accentEnv *= accCoef;

            const float osc = oscillator (p, dt);

            if (--coeffCountdown <= 0)
            {
                coeffCountdown = 8;
                const float octaves = p.envMod * filterEnv * 5.0f + (isAccent ? p.accent * accentEnv * 2.5f : 0.0f);
                updateFilter (p, p.cutoff * std::pow (2.0f, octaves));
            }

            const float a1a = 1.0f / (1.0f + g * (g + kA)), a2a = g * a1a, a3a = g * a2a;
            const float a1b = 1.0f / (1.0f + g * (g + kB)), a2b = g * a1b, a3b = g * a2b;

            // stage A (resonant)
            float v3 = osc - ic2a;
            float v1 = a1a * ic1a + a2a * v3;
            float v2 = ic2a + a2a * ic1a + a3a * v3;
            ic1a = 2.0f * v1 - ic1a; ic2a = 2.0f * v2 - ic2a;
            float stageA = ftype == 0 ? v2 : ftype == 1 ? v1 : osc - kA * v1 - v2;
            stageA = std::tanh (stageA);

            // stage B
            v3 = stageA - ic2b;
            v1 = a1b * ic1b + a2b * v3;
            v2 = ic2b + a2b * ic1b + a3b * v3;
            ic1b = 2.0f * v1 - ic1b; ic2b = 2.0f * v2 - ic2b;
            const float filtered = ftype == 0 ? v2 : ftype == 1 ? v1 * 1.6f : stageA - kB * v1 - v2;

            const float accentGain = isAccent ? 1.0f + 0.5f * p.accent * accentEnv : 1.0f;
            const float y = std::tanh (filtered * driveAmt) * driveNorm;
            out[i] += y * amp * velocityGain * accentGain * voiceGain;
        }
    }

    // ---------------------------------------------------------------------
    void MachineSynth::noteOn (int note, int velocity, bool accent, bool legato)
    {
        if (! poly)
        {
            voices[0].noteOn (note, velocity, accent, legato);
            return;
        }

        MachineVoice* target = nullptr;
        for (auto& v : voices) if (v.isGated() && v.currentNote() == note) { target = &v; break; }
        if (target == nullptr) for (auto& v : voices) if (v.isSilent()) { target = &v; break; }
        if (target == nullptr) for (auto& v : voices) if (! v.isGated()) { target = &v; break; }
        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices) if (v.age() < target->age()) target = &v;
        }
        target->noteOn (note, velocity, accent, false);
    }

    void MachineSynth::noteOff (int note)
    {
        for (auto& v : voices) v.noteOff (note);
    }

    bool MachineSynth::isSilent() const noexcept
    {
        for (auto& v : voices) if (! v.isSilent()) return false;
        return true;
    }

    void MachineSynth::render (float* out, int start, int n, const MachineVoice::Params& p)
    {
        for (auto& v : voices) v.render (out, start, n, p);
    }
}
