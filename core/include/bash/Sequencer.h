#pragma once
// BASH! core — the generative sequencer.
//
// A Pattern is a seed plus a sparse list of locks. Every step is computed on
// demand from (seed, step index), so patterns can be up to 2^32 steps long and
// recall exactly. Locks override the generated values for individual steps
// and survive Regenerate.

#include "bash/Rng.h"
#include "bash/Scale.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bash
{
    enum class Algorithm : int
    {
        CoreAcidV1 = 0,   // legacy acid generator
        CoreAcidV2,       // harmonic constraints tuned for low end
        Counterpoint,     // chords (stream A) + complementary bass (stream B)
        Arpeggiator,      // only the notes currently held on MIDI input
        DedicatedBass,    // rhythmically locked, driving low end
        count
    };

    enum class Direction : int { Forward = 0, Reverse, PingPong, Random, count };

    /** How a sampler track chooses slices. */
    enum class SliceMode : int
    {
        Window = 0,   // stay inside a short run of slices (window start + size)
        Bounce,       // jump anywhere across all slices
        Sequential,   // slices in order, following the sequencer's direction
        Groove,       // drums: each cell in the window gets a role (kick, snare, hats, perc), several hits per step
        count
    };

    struct SequencerParams
    {
        Algorithm algorithm   = Algorithm::CoreAcidV1;
        float     type        = 0.0f;   // 0 = quantised 303-style, 1 = chaos
        float     density     = 0.6f;   // 0..1, fraction of steps allowed to fire
        uint32_t  length      = 16;     // loop length in steps, 1..2^32-1
        uint32_t  offset      = 0;      // playhead start shift
        Direction direction   = Direction::Forward;
        float     accentAmount= 0.6f;   // 0..1 velocity prominence of accents
        float     gate        = 0.5f;   // note length in steps (0.05..2)
        int       baseNote    = 36;     // MIDI note for scale degree 0
        bool      scaleLock   = true;   // quantise output to the global scale

        // slicer tracks (sliceCount 0 = not a slicer)
        uint32_t  sliceCount  = 0;
        SliceMode sliceMode   = SliceMode::Window;
        uint32_t  windowStart = 0;
        uint32_t  windowSize  = 4;
        bool      drumTrack   = false;  // slices are drum cells; note = 36 + cell

        // arpeggiator input: notes currently held on MIDI in (ascending)
        std::array<int, 16> held {};
        int       heldCount   = 0;
    };

    constexpr int maxChordNotes = 4;

    struct StepData
    {
        bool    active   = false;
        int     note     = 36;
        int     velocity = 90;
        bool    accent   = false;
        bool    slide    = false;
        float   gate     = 0.5f;    // in steps
        int     slice    = 0;       // slicer tracks: which slice plays

        // Counterpoint: extra chord tones above `note` (stream A) and a bass line (stream B)
        std::array<int, maxChordNotes - 1> chord {};
        int     chordExtra = 0;
        bool    bassActive = false;
        int     bassNote   = 0;
    };

    namespace lockbit
    {
        enum : uint8_t { active = 1, pitch = 2, accent = 4, slide = 8, gate = 16, slice = 32, all = 63 };
    }

    struct StepLock
    {
        uint8_t  mask = 0;
        StepData value;
    };

    /** Tiny spin mutex: UI-thread edits vs. audio-thread reads, both very short. */
    class SpinMutex
    {
    public:
        void lock() noexcept   { while (flag.test_and_set (std::memory_order_acquire)) {} }
        void unlock() noexcept { flag.clear (std::memory_order_release); }
    private:
        std::atomic_flag flag = ATOMIC_FLAG_INIT;
    };

    class Pattern
    {
    public:
        explicit Pattern (uint64_t initialSeed = 1) : seed (initialSeed) {}

        uint64_t getSeed() const noexcept       { return seed; }
        void     setSeed (uint64_t s) noexcept  { seed = s; }

        /** New seed; locked steps are untouched. */
        void regenerate() noexcept;

        /** Raw procedural step, ignoring locks. */
        StepData generate (uint32_t index, const SequencerParams&, const Scale&) const noexcept;

        /** The step as it plays: generated, with any locks applied. */
        StepData step (uint32_t index, const SequencerParams&, const Scale&) const;

        /** Which pattern index plays at absolute step position n (after offset + direction). */
        uint32_t indexForPosition (int64_t n, const SequencerParams&) const noexcept;

        // Locks
        void lock (uint32_t index, uint8_t mask, const StepData& value);
        void unlock (uint32_t index, uint8_t mask = lockbit::all);
        void clearLocks()                                  { locks.clear(); }
        bool isLocked (uint32_t index) const               { return locks.count (index) != 0; }
        uint8_t lockMask (uint32_t index) const;
        const std::map<uint32_t, StepLock>& getLocks() const noexcept { return locks; }

        /** Plain-text state (seed + locks) for saving in the host session. */
        std::string serialise() const;
        bool deserialise (const std::string&);

        SpinMutex& mutex() noexcept { return guard; }

    private:
        uint64_t seed;
        std::map<uint32_t, StepLock> locks;
        mutable SpinMutex guard;
    };

    // ---------------------------------------------------------------------
    /** Turns a pattern into timed note events for one block of audio. */
    struct NoteEvent
    {
        int  sampleOffset = 0;
        bool noteOn       = true;
        int  note         = 0;
        int  velocity     = 0;
        bool legato       = false;  // note-on that should glide from the held note (303 slide)
        bool accent       = false;
        int  slice        = 0;
        int  stream       = 0;      // 0 = main / chords, 1 = Counterpoint bass
    };

    class TrackPlayer
    {
    public:
        /** posStart: block start in steps (e.g. ppq * stepsPerQuarter).
            stepsPerSample: how far the position moves per sample.
            Appends events in time order. Caller must hold pattern.mutex(). */
        void process (double posStart, double stepsPerSample, int numSamples,
                      const Pattern&, const SequencerParams&, const Scale&,
                      std::vector<NoteEvent>& out);

        /** Releases any held note at the given offset and forgets position. */
        void reset (std::vector<NoteEvent>* out = nullptr, int sampleOffset = 0);

        /** Pattern index of the last step that started (for the UI playhead). */
        uint32_t currentIndex() const noexcept { return lastIndex; }

    private:
        struct PolyHeld { int note; double offPos; int stream; };
        void releasePoly (double upTo, double posStart, double stepsPerSample, int numSamples,
                          std::vector<NoteEvent>& out, int forStream = -1, int atSample = -1);
        std::vector<PolyHeld> polyHeld;

        bool     started     = false;
        int64_t  lastBoundary = 0;
        double   expectedPos = 0.0;
        int      heldNote    = -1;
        double   heldOffPos  = 0.0;
        bool     heldSlides  = false;
        uint32_t lastIndex   = 0;
    };

    const char* algorithmName (Algorithm);
    const char* directionName (Direction);
    const char* sliceModeName (SliceMode);
}
