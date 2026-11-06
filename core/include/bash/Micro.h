#pragma once
// BASH! core — MICRO: a micro-loop / granular / glitch / multi-delay processor with a
// phrase looper, sitting between the tracks (and returns) and the master.
//
// 11 algorithms in four families, each with four variations (A-D):
//   Micro Loop: Mosaic (overlapping loops at different speeds), Sequencer (recent audio
//               cut up and resequenced), Glide (short loops gliding in pitch)
//   Granules:   Haze (a wash of grains), Tunnel (cycling micro-loops -> drones),
//               Strum (rhythmic chains of recent note onsets)
//   Glitch:     Blocks (onsets trigger bursts of repeats), Interrupt (pitched bursts,
//               reversals and montage cut into the signal), Arp (a captured sound played
//               as an arpeggio in the session key)
//   Multi Delay: Pattern (rhythmic tap patterns), Warp (pitch-shifted, filtered taps)
// Shared controls: Activity, Repeats, Shape, Time, Filter + Resonance (with an LFO),
// Space (reverb, four types, decay), Mix, Level, Reverse / Alternate and Hold.
// Phrase looper: 60 s, record / play / overdub / undo / stop / erase, reverse, speed,
// fade, Post / Pre-FX / Looper-only modes, Quantize and Burst recording.

#include "bash/Fx.h"
#include "bash/Scale.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace bash
{
    enum class MicroAlgo : int { Mosaic = 0, Sequencer, Glide, Haze, Tunnel, Strum, Blocks, Interrupt, Arp, Pattern, Warp, count };
    const char* microAlgoName (MicroAlgo);
    const char* microFamilyName (MicroAlgo);
    /** What each variation (A-D) of an algorithm does, for the UI. */
    const char* microVariationText (MicroAlgo, int variation);
    /** Time: the division as a multiple of the beat ("x1/4" = every 4 beats ... "x8" = 32nds). */
    const std::vector<std::pair<const char*, double>>& microTimes();
    const std::vector<const char*>& microReverbNames();
    const std::vector<std::pair<const char*, double>>& microLoopSpeeds();

    enum class MicroReverse : int { Off = 0, Reverse, Alternate };
    enum class LoopMode : int { Post = 0, PreFx, LooperOnly };

    struct MicroParams
    {
        bool  on = false;
        MicroAlgo algo = MicroAlgo::Mosaic;
        int   variation = 0;            // 0..3 = A..D
        float activity = 0.5f, repeats = 0.5f, shape = 0.3f;
        int   time = 2;                 // into microTimes()
        float filter = 1.0f, reso = 0.1f, modRate = 0.3f, modDepth = 0.0f;
        float space = 0.25f;  int reverbType = 1;  float decay = 0.5f;
        float mix = 0.5f, level = 1.0f;
        MicroReverse reverse = MicroReverse::Off;
        bool  hold = false;

        float loopLevel = 0.8f, loopFade = 0.0f;
        int   loopSpeed = 2;            // into microLoopSpeeds() (x1)
        bool  loopReverse = false, quantize = false;
        LoopMode loopMode = LoopMode::Post;
    };

    class MicroEngine
    {
    public:
        static constexpr double historySeconds = 8.0;
        static constexpr double maxLoopSeconds = 60.0;
        static constexpr int    maxVoices = 48;
        static constexpr int    viewBins = 400;

        void prepare (double sampleRate, int maxBlock);
        void reset();
        /** In place: l/r = everything from the tracks in, MICRO's output out. Audio thread. */
        void process (float* l, float* r, int n, const MicroParams&, const Scale&, double bpm, double ppq, bool running);

        // ---- phrase looper (any thread; applied at the next block) ----
        enum class LoopState : int { Empty = 0, Recording, Playing, Overdubbing, Stopped };
        void loopPress() noexcept   { command.store (cmdPress); }   // record -> play -> overdub -> play
        void loopStop() noexcept    { command.store (cmdStop); }
        void loopErase() noexcept   { command.store (cmdErase); }
        void loopUndo() noexcept    { command.store (cmdUndo); }
        void burst (bool down) noexcept { command.store (down ? cmdBurstDown : cmdBurstUp); }
        LoopState loopState() const noexcept { return (LoopState) state.load(); }
        double loopSeconds() const noexcept  { return (double) loopLen.load() / sr; }
        double recordedSeconds() const noexcept { return (double) recLen.load() / sr; }
        float  loopPosition() const noexcept { return loopPosUi.load(); }
        bool   canUndo() const noexcept      { return undoValid.load(); }
        /** Copy the loop (message thread; not while recording). */
        void copyLoop (std::vector<float>& l, std::vector<float>& r) const;
        double sampleRate() const noexcept { return sr; }

        // ---- visuals ----
        /** Input over the last 8 s, oldest (0) to newest; peak 0..1. */
        float inputOverview (int k) const noexcept;
        /** The loop's waveform (0..1), when there is one. */
        float loopOverview (int k) const noexcept { return loopBins[(size_t) k].load(); }
        /** Where each voice is reading, 0..1 across the input view (-1 = idle). */
        float voicePosition (int v) const noexcept { return voiceUi[(size_t) v].load(); }
        int   activeVoices() const noexcept { return voiceCount.load(); }
        float wetLevel() const noexcept     { return wetMeter.load(); }

    private:
        struct Voice
        {
            bool   active = false;
            double start = 0.0;          // absolute history position (samples)
            double loopLen = 0.0;        // > 0: loop [start, start + loopLen)
            double phase = 0.0;          // read offset from start (samples)
            double rate = 1.0;           // read speed (negative = reverse)
            double glide = 0.0;          // semitones swept over the life (0 = none)
            int    glideMode = 0;        // 0 linear, 1 up-and-back
            double age = 0.0, life = 1.0, delay = 0.0;
            float  gl = 1.0f, gr = 1.0f;
            float  shape = 0.3f;
        };
        struct Comb { std::vector<float> l, r; };

        float readHistory (double absPos, int ch) const noexcept;
        void  spawn (Voice v);
        void  spawnGrain (double start, double len, double rate, float gain, float pan, double delay = 0.0);
        void  spawnLoop (double start, double loopLen, double rate, double life, float gain, float pan, double glide = 0.0, int glideMode = 0);
        bool  nextReverse (const MicroParams&) noexcept;
        void  onTick (const MicroParams&, const Scale&, long long tick, double divSamples);
        void  onOnset (const MicroParams&, const Scale&, double divSamples);
        float envelope (double ph, float shape) const noexcept;
        void  handleCommand (int cmd, double bpm);
        double scaleRate (const Scale&, int semis) const;
        uint32_t rnd() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float unit() noexcept   { return (float) (rnd() >> 8) * (1.0f / 16777216.0f); }

        double sr = 48000.0;
        int maxBlock = 512;
        uint32_t s = 0x1234567;

        // live input history
        std::vector<float> histL, histR;
        size_t histLen = 1;
        long long writeAbs = 0;          // samples written so far (absolute clock)
        std::array<std::atomic<float>, viewBins> bins {};
        std::atomic<int> newestBin { 0 };
        float binPeak = 0.0f;

        std::array<Voice, maxVoices> voices {};
        std::array<std::atomic<float>, 24> voiceUi {};
        std::atomic<int> voiceCount { 0 };
        bool altFlag = false;

        // timing
        long long lastTick = -1;
        double freeClock = 0.0, hazeAcc = 0.0, strumAcc = 0.0;
        int strumIndex = 0, strumLeft = 0, arpStep = 0, seqCounter = 0;
        double seqBase = -1.0, arpSource = -1.0, interruptLeft = 0.0;
        int interruptKind = 0;
        std::array<int, 8> seqOrder { 0, 1, 2, 3, 4, 5, 6, 7 };

        // onsets
        float envFast = 0.0f, envSlow = 0.0f;
        double sinceOnset = 1.0e9;
        std::array<double, 8> onsets {};
        int onsetCount = 0, onsetWrite = 0;

        // delay algorithms
        std::vector<float> dlyL, dlyR;
        size_t dlyLen = 1, dlyW = 0;
        std::array<float, 3> tapLpL {}, tapLpR {};
        std::array<double, 3> warpPhase {};

        // wet chain
        std::vector<float> wetL, wetR, inL, inR, dryBufL, dryBufR;
        bool quantizeLoop = false;
        float f1L = 0, f2L = 0, f1R = 0, f2R = 0;
        double lfoPhase = 0.0;
        FxSlot reverb;
        std::atomic<float> wetMeter { 0.0f };
        float duck = 1.0f;

        // looper
        static constexpr int cmdNone = 0, cmdPress = 1, cmdStop = 2, cmdErase = 3, cmdUndo = 4, cmdBurstDown = 5, cmdBurstUp = 6;
        std::atomic<int> command { cmdNone };
        std::atomic<int> state { 0 };
        std::vector<float> loopL, loopR, undoL, undoR;
        std::atomic<size_t> loopLen { 0 }, recLen { 0 };
        double loopPos = 0.0;
        size_t dubWritten = 0;
        float loopGain = 1.0f;
        std::atomic<float> loopPosUi { 0.0f };
        std::atomic<bool> undoValid { false };
        bool burstRecording = false;
        std::array<std::atomic<float>, viewBins> loopBins {};
        void rebuildLoopBins();
    };
}
