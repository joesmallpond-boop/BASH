#pragma once
// BASH! core — modulation: two global LFOs, a per-track "Life" drift generator,
// MIDI expression sources, and a per-track matrix of destinations x sources.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bash
{
    enum class ModSource : int { Lfo1 = 0, Lfo2, Life, Velocity, Aftertouch, ModWheel, PitchBend, count };
    constexpr int numModSources = (int) ModSource::count;
    const char* modSourceName (ModSource);

    enum class ModDest : int
    {
        Type = 0, Density, Steps, Offset, Accent, Gate,
        Cutoff, Resonance, Character, Timbre, EnvMod, Decay, Drive, Tune,
        WindowStart, Level, Pan, SendA, SendB, Fx1Gnarl, Fx2Gnarl,
        count
    };
    constexpr int numModDests = (int) ModDest::count;
    const char* modDestName (ModDest);

    enum class LfoShape : int { Sine = 0, Triangle, Saw, Square, SampleHold, Smooth, count };
    const char* lfoShapeName (LfoShape);

    /** LFO rates when synced, in beats (quarter notes) per cycle. */
    const std::array<std::pair<const char*, double>, 10>& lfoDivisions();

    struct LfoParams
    {
        LfoShape shape = LfoShape::Sine;
        float    rateHz = 0.5f;
        bool     sync = false;
        int      division = 4;   // index into lfoDivisions()
    };

    class Lfo
    {
    public:
        /** Advance by `seconds` (free) or to song position `ppq` (synced). Returns -1..1. */
        float advance (const LfoParams&, double seconds, double ppq, bool synced);
        float value() const noexcept { return out; }
        double phase() const noexcept { return ph; }
        static float shapeAt (LfoShape, double phase, float held, float prevHeld);
    private:
        double ph = 0.0;
        float out = 0.0f, held = 0.0f, prevHeld = 0.0f;
        uint32_t rng = 0x1234567u;
        float nextRandom();
    };

    /** Smooth organic drift (1-D value noise with cubic smoothing), -1..1. */
    class Life
    {
    public:
        void setSeed (uint64_t s) { seed = s; }
        float advance (double seconds, float rateHz);
        float value() const noexcept { return out; }
    private:
        uint64_t seed = 1;
        double t = 0.0;
        float out = 0.0f;
    };

    /** Amounts (-1..1) for every destination x source, per track. Lock-free:
        the UI writes, the audio thread reads. */
    struct ModMatrix
    {
        std::array<std::array<std::atomic<float>, numModSources>, numModDests> amount {};
        ModMatrix() { clear(); }
        void clear() { for (auto& row : amount) for (auto& a : row) a.store (0.0f); }
        float get (ModDest d, ModSource s) const { return amount[(size_t) d][(size_t) s].load (std::memory_order_relaxed); }
        void  set (ModDest d, ModSource s, float v) { amount[(size_t) d][(size_t) s].store (v, std::memory_order_relaxed); }
        bool  any() const { for (auto& row : amount) for (auto& a : row) if (a.load (std::memory_order_relaxed) != 0.0f) return true; return false; }
    };
}
