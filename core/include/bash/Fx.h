#pragma once
// BASH! core — effects. One FxSlot can be any effect type; every type has two
// knobs (A, B), a Gnarl macro that pushes it from tasteful to wrecked, and Mix.

#include <array>
#include <cstdint>
#include <vector>

namespace bash
{
    enum class FxType : int
    {
        None = 0,
        // distortion
        Fuzz, HardClip, Wavefolder, Bitcrush, Rectifier,
        // saturation
        Tape, Tube, Transformer,
        // reverb
        Plate, Hall, Spring, Shimmer,
        // delay
        TapeDelay, DigitalDelay, PingPong, GrainDelay,
        // modulation
        Chorus, Flanger, Phaser, RingMod, FreqShift,
        count
    };
    const char* fxName (FxType);
    const char* fxKnobAName (FxType);
    const char* fxKnobBName (FxType);

    struct FxParams
    {
        FxType type  = FxType::None;
        float  a     = 0.5f;   // 0..1, meaning depends on type
        float  b     = 0.5f;   // 0..1
        float  gnarl = 0.0f;   // 0..1: tasteful -> chaos
        float  mix   = 1.0f;   // dry/wet
    };

    /** Delay time choices for the delays (in quarter notes), picked by knob A. */
    const std::vector<std::pair<const char*, double>>& delayDivisions();

    class FxSlot
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset();
        /** In-place stereo processing. bpm is used by the tempo-synced delays. */
        void process (float* l, float* r, int n, const FxParams&, double bpm);

    private:
        struct Line
        {
            std::vector<float> buf;
            int w = 0;
            void init (int size)            { buf.assign ((size_t) std::max (4, size), 0.0f); w = 0; }
            void clear()                    { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
            void push (float x)             { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
            float tap (double delay) const;  // fractional samples behind the last write
            int size() const                { return (int) buf.size(); }
        };
        struct OnePole { float z = 0.0f; float lp (float x, float c) { z += (x - z) * c; return z; } };
        struct Allpass1 { float z = 0.0f; float process (float x, float a) { const float y = -a * x + z; z = x + a * y; return y; } };
        struct Allpass2   // 2nd-order section for the Hilbert transformer: y = a2 (x + y[n-2]) - x[n-2]
        {
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            float process (float x, float a2) { const float y = a2 * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
        };

        void distortion (float* l, float* r, int n, const FxParams&);
        void saturation (float* l, float* r, int n, const FxParams&);
        void reverb (float* l, float* r, int n, const FxParams&);
        void delay (float* l, float* r, int n, const FxParams&, double bpm);
        void modulation (float* l, float* r, int n, const FxParams&);

        double sr = 44100.0;
        FxType current = FxType::None;
        std::vector<float> dryL, dryR;

        // shared state
        OnePole toneL, toneR, lowL, lowR;
        float dcL = 0.0f, dcR = 0.0f, dcxL = 0.0f, dcxR = 0.0f;
        float holdL = 0.0f, holdR = 0.0f;
        double holdCount = 0.0;
        double lfo = 0.0, lfo2 = 0.0;
        uint32_t rng = 0x2545F491u;
        float nextRandom() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng >> 8) / 16777216.0f; }

        // delays / modulation lines
        Line dlL, dlR, shortL, shortR;
        OnePole fbLpL, fbLpR, fbHpL, fbHpR;
        // grain delay
        struct Grain { double pos = 0, inc = 1; int age = 0, len = 1; bool on = false; };
        std::array<Grain, 4> grains {};
        int grainTimer = 0;

        // reverb: 8-line FDN + input diffusers + spring dispersion + shimmer
        std::array<Line, 8> fdn;
        std::array<OnePole, 8> fdnDamp;
        std::array<Line, 4> diffuse;
        std::array<Allpass1, 12> springAp;
        Line preDelay, shimmerBuf;
        double shimmerPhase = 0.0;
        float shimmerFb = 0.0f;

        // phaser / freq shifter
        std::array<Allpass1, 6> phL, phR;
        float phFbL = 0.0f, phFbR = 0.0f;
        std::array<Allpass2, 4> hilA_L, hilB_L, hilA_R, hilB_R;
        float hilDelayL = 0.0f, hilDelayR = 0.0f;
        float shiftFbL = 0.0f, shiftFbR = 0.0f;
    };

    /** Brick-wall-ish safety limiter for the master bus (ceiling -0.3 dBFS). */
    class Limiter
    {
    public:
        void prepare (double sampleRate);
        void process (float* l, float* r, int n);
        float reduction() const noexcept { return 1.0f - gain; }
    private:
        double sr = 44100.0;
        float gain = 1.0f, attack = 0.0f, release = 0.0f;
    };
}
