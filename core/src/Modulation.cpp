#include "bash/Modulation.h"
#include "bash/Rng.h"

#include <algorithm>
#include <cmath>

namespace bash
{
    const char* modSourceName (ModSource s)
    {
        switch (s)
        {
            case ModSource::Lfo1:       return "LFO 1";
            case ModSource::Lfo2:       return "LFO 2";
            case ModSource::Life:       return "Life";
            case ModSource::Velocity:   return "Vel";
            case ModSource::Aftertouch: return "AT";
            case ModSource::ModWheel:   return "Wheel";
            case ModSource::PitchBend:  return "Bend";
            default:                    return "?";
        }
    }

    const char* modDestName (ModDest d)
    {
        switch (d)
        {
            case ModDest::Type:        return "Type";
            case ModDest::Density:     return "Density";
            case ModDest::Steps:       return "Steps";
            case ModDest::Offset:      return "Offset";
            case ModDest::Accent:      return "Accent";
            case ModDest::Gate:        return "Gate";
            case ModDest::Cutoff:      return "Cutoff";
            case ModDest::Resonance:   return "Resonance";
            case ModDest::Character:   return "Character";
            case ModDest::Timbre:      return "Timbre";
            case ModDest::EnvMod:      return "Env Mod";
            case ModDest::Decay:       return "Decay";
            case ModDest::Drive:       return "Drive";
            case ModDest::Tune:        return "Tune";
            case ModDest::WindowStart: return "Window";
            case ModDest::Level:       return "Level";
            case ModDest::Pan:         return "Pan";
            case ModDest::SendA:       return "Send A";
            case ModDest::SendB:       return "Send B";
            case ModDest::Fx1Gnarl:    return "FX1 Gnarl";
            case ModDest::Fx2Gnarl:    return "FX2 Gnarl";
            default:                   return "?";
        }
    }

    const char* lfoShapeName (LfoShape s)
    {
        switch (s)
        {
            case LfoShape::Sine:       return "Sine";
            case LfoShape::Triangle:   return "Triangle";
            case LfoShape::Saw:        return "Saw";
            case LfoShape::Square:     return "Square";
            case LfoShape::SampleHold: return "S&H";
            case LfoShape::Smooth:     return "Smooth";
            default:                   return "?";
        }
    }

    const std::array<std::pair<const char*, double>, 10>& lfoDivisions()
    {
        static const std::array<std::pair<const char*, double>, 10> d { {
            { "1/16", 0.25 }, { "1/8", 0.5 }, { "1/4", 1.0 }, { "1/2", 2.0 }, { "1 bar", 4.0 },
            { "2 bars", 8.0 }, { "4 bars", 16.0 }, { "8 bars", 32.0 }, { "16 bars", 64.0 }, { "32 bars", 128.0 } } };
        return d;
    }

    float Lfo::nextRandom()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng >> 8) / 8388608.0f - 1.0f;
    }

    float Lfo::shapeAt (LfoShape s, double p, float h, float prevH)
    {
        constexpr double twoPi = 6.283185307179586;
        switch (s)
        {
            case LfoShape::Sine:       return (float) std::sin (twoPi * p);
            case LfoShape::Triangle:   return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case LfoShape::Saw:        return (float) (2.0 * p - 1.0);
            case LfoShape::Square:     return p < 0.5 ? 1.0f : -1.0f;
            case LfoShape::SampleHold: return h;
            case LfoShape::Smooth:
            {
                const float x = (float) (0.5 - 0.5 * std::cos (p * 3.141592653589793));
                return prevH + (h - prevH) * x;
            }
            default: return 0.0f;
        }
    }

    float Lfo::advance (const LfoParams& p, double seconds, double ppq, bool synced)
    {
        const double prev = ph;
        if (p.sync && synced)
        {
            const auto& divs = lfoDivisions();
            const double beats = divs[(size_t) std::clamp (p.division, 0, (int) divs.size() - 1)].second;
            ph = ppq / beats;
            ph -= std::floor (ph);
        }
        else
        {
            ph += seconds * std::max (0.0f, p.rateHz);
            ph -= std::floor (ph);
        }
        if (ph < prev) { prevHeld = held; held = nextRandom(); }   // new cycle: new random value
        out = shapeAt (p.shape, ph, held, prevHeld);
        return out;
    }

    float Life::advance (double seconds, float rateHz)
    {
        t += seconds * std::max (0.0f, rateHz);
        const double k = std::floor (t);
        const float f = (float) (t - k);
        const auto knot = [this] (double i) { return unitFloat (seed, (uint64_t) (int64_t) i, 0x11FEull) * 2.0f - 1.0f; };
        // Catmull-Rom through four random knots: smooth, wandering
        const float p0 = knot (k - 1), p1 = knot (k), p2 = knot (k + 1), p3 = knot (k + 2);
        const float f2 = f * f, f3 = f2 * f;
        out = 0.5f * ((2.0f * p1) + (-p0 + p2) * f + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * f2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * f3);
        out = std::clamp (out, -1.0f, 1.0f);
        return out;
    }
}
