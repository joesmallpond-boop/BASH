#pragma once
// BASH! core — the GRANULAR section at the end of the chain: an experimental
// sound-design playground fed by the master output.
//
//   master -> [capture buffer] -> 4 grain playheads -> filter -> resonator -> delay -> reverb
//          -> dry/wet -> glitch (repeat / reverse / tape stop / crush / chop) -> looper -> recorder -> out
//
// The capture buffer keeps the last 16 s of the master (Freeze holds it). Each playhead
// sprays grains from its position with its own size, density, pitch and drift. The
// resonator rings at the session key (decay up to infinite). The recorder captures the
// section's output into a take you can loop, drag out, or feed back into the grains.

#include "bash/Fx.h"
#include "bash/Scale.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace bash
{
    constexpr int grainHeads = 4;

    struct GrainHeadParams
    {
        bool  on = false;
        float position   = 0.75f;   // 0 = oldest audio in the buffer, 1 = newest
        float size       = 0.45f;   // 0..1 -> 1 ms .. 2 s (log)
        float density    = 0.45f;   // 0..1 -> 0.5 .. 150 grains per second (log)
        float pitch      = 0.0f;    // semitones, -24..24
        float pitchSpray = 0.0f;    // semitones of random spread, 0..24
        float posSpray   = 0.05f;   // 0..1 of the buffer
        float scan       = 0.0f;    // -2..2: how fast the playhead travels through the buffer
        float reverse    = 0.0f;    // chance a grain plays backwards
        float level      = 0.8f;
        float pan        = 0.0f;
        float drift      = 0.0f;    // 0..1: slow wandering of position and pitch
    };

    enum class GrainWindow : int { Smooth = 0, Flat, Perc, Swell, count };
    const char* grainWindowName (GrainWindow);

    /** Resonator chords (intervals from the key's root). */
    const std::vector<std::pair<const char*, std::array<int, 4>>>& resonatorChords();
    /** Tempo divisions for the glitch and delay: name and length in quarter notes. */
    const std::vector<std::pair<const char*, double>>& granularDivisions();

    struct GranularParams
    {
        bool  on = false;
        float mix = 1.0f;           // dry master vs the grain chain
        bool  freeze = false;       // stop writing: play with what's in the buffer
        std::array<GrainHeadParams, grainHeads> heads {};
        GrainWindow window = GrainWindow::Smooth;
        float spread = 0.5f;        // random stereo spread of grains
        bool  keyLock = true;       // grain pitches snap to the key and scale

        float cutoff = 1.0f, reso = 0.15f;   // 0..1 (40 Hz .. 18 kHz), 0..1
        int   filterType = 0;                // 0 LP, 1 BP, 2 HP

        float resMix = 0.0f, resDecay = 0.6f, resTone = 0.5f;   // decay 1 = infinite
        int   resChord = 0, resOctave = 3;

        float dlyMix = 0.0f, dlyFb = 0.45f;  int dlyDiv = 3;
        float revMix = 0.25f, revSize = 0.6f, shimmer = 0.0f;   bool revFreeze = false;

        float glitch = 0.0f;  int glitchDiv = 2;   // chance per division
        bool  gRepeat = true, gReverse = true, gTapeStop = true, gCrush = true, gChop = true;
        bool  stutter = false;                      // hold: repeat the last division

        bool  loopPlay = false;  float loopLevel = 0.8f;   // play the recorded take as a loop
        float out = 1.0f;
    };

    class GranularEngine
    {
    public:
        static constexpr double bufferSeconds = 16.0;
        static constexpr double maxTakeSeconds = 60.0;
        static constexpr int    maxGrains = 96;
        static constexpr int    overviewBins = 512;

        void prepare (double sampleRate, int maxBlock);
        /** In place: l/r carry the master in and the section's output out. Audio thread. */
        void process (float* l, float* r, int n, const GranularParams&, const Scale&, double bpm, double ppq, bool running);
        void reset();
        /** While the section is off: keep the capture buffer listening (cheap), so there's
            audio to play with the moment it's switched on. */
        void capture (const float* l, const float* r, int n, bool freeze);

        // --- UI (any thread) ---
        /** Buffer overview, oldest (k = 0) to newest; peak 0..1. */
        float overview (int k) const noexcept;
        float headPosition (int h) const noexcept   { return headPos[(size_t) h].load(); }
        int   activeGrains() const noexcept         { return grainCount.load(); }
        const char* lastGlitch() const noexcept     { return glitchName.load(); }

        // --- recorder ---
        void   setRecording (bool on) noexcept;            // starting a recording replaces the take
        bool   recording() const noexcept               { return rec.load(); }
        double takeSeconds() const noexcept             { return (double) takeLen.load() / sr; }
        /** Copy the take (call when not recording). */
        void   copyTake (std::vector<float>& l, std::vector<float>& r) const;
        void   requestTakeToGrains() noexcept           { takeToGrains.store (true); }
        void   clearTake() noexcept                     { if (! rec.load()) takeLen.store (0); }
        double sampleRate() const noexcept              { return sr; }

        /** For offline renders: copy the capture buffer (and its write position). */
        void copyBufferFrom (const GranularEngine&);

    private:
        struct Grain
        {
            bool   active = false;
            double pos = 0.0, rate = 1.0;   // ring position (samples), step per sample
            int    age = 0, length = 1;
            float  gl = 1.0f, gr = 1.0f;
            GrainWindow window = GrainWindow::Smooth;
        };
        struct Comb
        {
            std::vector<float> buf; size_t w = 0; float lp = 0.0f;
            float process (float x, double delay, float g, float damp);
        };

        float readRing (double pos, int ch) const noexcept;
        void  spawn (int head, const GrainHeadParams&, const GranularParams&, const Scale&, int offsetInBlock);
        float window (GrainWindow, double phase) const noexcept;
        uint32_t rnd() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float unit() noexcept   { return (float) (rnd() >> 8) * (1.0f / 16777216.0f); }

        double sr = 48000.0;
        int    maxBlock = 512;
        int    maxBlockWritten = 0;   // samples written to the ring this block (0 when frozen)
        std::vector<float> ringL, ringR;
        size_t ringLen = 1, writePos = 0, filled = 0;   // filled: how much real audio the ring holds
        uint32_t s = 0x2545F491;

        std::array<Grain, maxGrains> grains {};
        std::array<double, grainHeads> spawnPhase {}, scanOffset {};
        std::array<float, grainHeads> driftPos {}, driftPitch {}, driftTarget {}, driftPitchTarget {};
        std::array<std::atomic<float>, grainHeads> headPos {};
        std::atomic<int> grainCount { 0 };

        // overview (peaks per bin of the ring)
        std::array<std::atomic<float>, overviewBins> bins {};
        std::atomic<int> writeBin { 0 };
        float binPeak = 0.0f;

        // wet chain
        std::vector<float> wetL, wetR, dryL, dryR;
        float f1L = 0, f2L = 0, f1R = 0, f2R = 0;           // filter state
        std::array<Comb, 8> combs;                         // 4 per side
        std::vector<float> dlyL, dlyR; size_t dlyW = 0; float dlyLpL = 0, dlyLpR = 0;
        FxSlot reverb;

        // glitch
        std::vector<float> histL, histR; size_t histLen = 1, histW = 0;
        int    glitchMode = -1;           // -1 none, 0 repeat, 1 reverse, 2 tape stop, 3 crush, 4 chop
        long long glitchStep = -1;
        double glitchStart = 0.0;         // hist position where the event started
        double glitchAge = 0.0, glitchLen = 1.0, sliceLen = 1.0, tapePos = 0.0;
        float  crushHoldL = 0, crushHoldR = 0; int crushCount = 0;
        float  glitchFade = 0.0f;
        double freeClock = 0.0;
        std::atomic<const char*> glitchName { "" };

        // recorder / looper
        std::vector<float> takeL, takeR;
        std::atomic<size_t> takeLen { 0 };
        std::atomic<bool> rec { false }, takeToGrains { false };
        size_t loopPos = 0;
    };
}
