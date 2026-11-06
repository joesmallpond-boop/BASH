#pragma once
// BASH! core — step sequencer. The alternative to the generative sequencer on any
// track: 16 lanes (drum pads, slices or scale notes) of up to 64 steps each.
//
// Per step:  on, velocity, probability, ratchet (with velocity shape), trig
//            condition (fill / not fill / first / A:B loop counts) and micro-timing.
// Per lane:  its own length and speed (polymeter), mute, and an algorithm that can
//            write the lane (Euclid, Random, Groove styles, Rolls, Mutate, Humanize)
//            plus Evolve, which varies the lane a little every time it loops.
// Per track: Fill (latch) and Auto fill every N bars.
//
// Everything random is a pure function of the lane seed and the loop count, so
// the same session always plays back the same way.

#include "bash/Sequencer.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bash
{
    constexpr int stepLanes = 16;
    constexpr int maxSteps  = 64;

    enum class TrigCond : uint8_t
    {
        Always = 0, Fill, NotFill, First, NotFirst,
        R1of2, R2of2, R1of3, R2of3, R3of3, R1of4, R2of4, R3of4, R4of4,
        count
    };
    const char* trigCondName (TrigCond);

    enum class RatchetShape : uint8_t { Flat = 0, Up, Down, count };
    const char* ratchetShapeName (RatchetShape);

    struct Step
    {
        bool     on      = false;
        uint8_t  vel     = 100;   // 1..127
        uint8_t  prob    = 100;   // 0..100 %
        uint8_t  ratchet = 1;     // 1..8 hits inside the step
        RatchetShape shape = RatchetShape::Flat;
        TrigCond cond    = TrigCond::Always;
        int8_t   nudge   = 0;     // -50..+50 % of a step
        bool operator== (const Step& o) const noexcept
        {
            return on == o.on && vel == o.vel && prob == o.prob && ratchet == o.ratchet
                && shape == o.shape && cond == o.cond && nudge == o.nudge;
        }
    };

    /** What a lane's die (and Generate) writes. */
    enum class LaneAlgo : uint8_t { Euclid = 0, Random, Groove, Rolls, Mutate, Humanize, count };
    const char* laneAlgoName (LaneAlgo);

    /** Lane speeds relative to the track rate (polyrhythm). */
    const std::vector<std::pair<const char*, double>>& laneSpeeds();
    constexpr int defaultLaneSpeed = 3;   // x1

    struct Lane
    {
        std::array<Step, maxSteps> steps {};
        uint8_t  length  = 16;            // 1..64
        uint8_t  speed   = defaultLaneSpeed;
        bool     mute    = false;
        LaneAlgo algo    = LaneAlgo::Euclid;
        uint8_t  pulses  = 4;             // Euclid
        uint8_t  rotate  = 0;             // Euclid
        float    density = 0.5f;          // Random / Rolls / Mutate amount
        float    evolve  = 0.0f;          // 0..1: chance-per-loop variations at playback
        uint64_t seed    = 1;
    };

    struct StepPattern
    {
        std::array<Lane, stepLanes> lanes;
        std::string serialise() const;
        bool deserialise (const std::string&);
        void clear();
        int  activeLanes() const;          // lanes with at least one step on
    };

    /** Lane roles for Groove: what kind of part a lane plays. */
    enum class LaneRole : uint8_t { Kick, Snare, Clap, ClosedHat, OpenHat, Perc, Tom, Cymbal, Melodic };
    LaneRole drumLaneRole (int lane);   // the factory kit layout (pads 1-16)

    namespace stepgen
    {
        /** Rewrite a lane with its algorithm. Euclid/Random/Groove/Rolls replace the steps;
            Mutate and Humanize change the existing ones. `roll` varies the result (the die). */
        void generate (Lane&, LaneRole, uint64_t roll);
        /** Pick fresh, musically sensible algorithm settings and generate (the lane die). */
        void rollLane (Lane&, LaneRole, uint64_t roll);
        /** Euclidean rhythm: pulses spread as evenly as possible over length, rotated. */
        std::vector<bool> euclid (int pulses, int length, int rotate);
    }

    /** How a step-pattern event becomes a note for the track's engine. */
    struct StepContext
    {
        double stepsPerQuarter = 4.0;   // track rate
        bool   fill = false;            // Fill button latched
        int    autoFillBars = 0;        // 0 = off; else the last bar of every N bars is a fill
        float  swing = 0.0f;            // 0..0.75 (as the generative sequencer)
        float  gate  = 0.5f;            // note length in steps, for synth/slicer lanes
        bool   oneShot = true;          // drums: no note-offs needed
        std::array<int, stepLanes> laneNote {};   // MIDI note each lane plays
        std::array<int, stepLanes> laneSlice {};  // slice each lane plays (slicer)
    };

    class StepPlayer
    {
    public:
        /** Same timing contract as TrackPlayer::process (positions in track steps).
            Appends events in time order. Caller holds the pattern's lock. */
        void process (double posStart, double stepsPerSample, int numSamples,
                      const StepPattern&, const StepContext&, std::vector<NoteEvent>& out);
        void reset (std::vector<NoteEvent>* out = nullptr, int sampleOffset = 0);
        /** Step index (0..length-1) each lane last started, for the UI playhead; -1 before start. */
        int laneStep (int lane) const noexcept { return lastStep[(size_t) lane]; }

        /** Would this step play on loop `loop` (conditions, probability, evolve)? Exposed for tests/export. */
        static bool stepPlays (const Lane&, int laneIndex, int step, int64_t loop, bool fillActive, Step* effective = nullptr);

    private:
        struct Pending { double offPos; int note; };
        std::vector<Pending> pendingOffs;
        std::array<int, stepLanes> lastStep {};
        bool started = false;
        double expectedPos = 0.0;
    };

    /** Offline: the notes a step pattern plays over numSteps track steps (for MIDI export). */
    std::vector<ExportNote> renderStepNotes (const StepPattern&, const StepContext&, int64_t numSteps);
}
