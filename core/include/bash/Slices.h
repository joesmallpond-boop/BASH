#pragma once
// BASH! core — samples and slicing.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bash
{
    constexpr int maxSlices = 256;

    /** An immutable decoded audio file. Created on the message thread, read by audio. */
    struct SampleData
    {
        std::vector<float> left, right;   // right == left for mono files
        double sampleRate = 44100.0;
        std::string path;

        uint32_t numFrames() const noexcept { return (uint32_t) left.size(); }
        float mono (uint32_t i) const noexcept { return 0.5f * (left[i] + right[i]); }
    };

    namespace slicing
    {
        /** Sort, de-duplicate, force a slice at 0, drop points past the end,
            keep slices at least minGap frames apart, cap at 256. */
        void normalise (std::vector<uint32_t>& slices, uint32_t numFrames, uint32_t minGap = 32);

        /** Even divisions across the whole file (e.g. 16 on a one-bar loop = 1/16ths). */
        std::vector<uint32_t> grid (uint32_t numFrames, int divisions);

        /** count slices at seeded random positions. */
        std::vector<uint32_t> random (uint32_t numFrames, int count, uint64_t seed, double sampleRate);

        /** Onset detection. sensitivity 0..1 (higher finds more slices). */
        std::vector<uint32_t> transients (const SampleData&, float sensitivity, int maxCount = maxSlices);

        /** Index of the slice containing frame (slices must be normalised). */
        int sliceAt (const std::vector<uint32_t>& slices, uint32_t frame);
    }

    /** Plays slices of a sample: pitch by resampling, reverse, attack/release,
        click-free retriggers (the previous slice fades out underneath). */
    class SliceVoice
    {
    public:
        struct Params
        {
            float attack  = 0.002f;  // s
            float release = 0.03f;   // s
            bool  reverse = false;
        };

        void prepare (double sampleRate) { sr = sampleRate; reset(); }
        void reset() { cur = {}; old = {}; }

        void start (const SampleData* sample, uint32_t startFrame, uint32_t endFrame,
                    double rate, float gain, const Params&);
        void release() { if (cur.active) cur.releasing = true; }
        void stopIfUsing (const SampleData* sample);

        /** Adds stereo output to l/r [start, start+n). */
        void render (float* l, float* r, int start, int n, const Params&);

        bool isSilent() const noexcept { return ! cur.active && ! old.active; }
        bool isPlaying (const SampleData* s) const noexcept { return (cur.active && cur.sample == s) || (old.active && old.sample == s); }

        /** Current play position in frames (for the UI), or -1. */
        double position() const noexcept { return cur.active ? cur.pos : -1.0; }

    private:
        struct Play
        {
            const SampleData* sample = nullptr;
            double pos = 0, inc = 1, lo = 0, hi = 0;
            float  gain = 1, env = 0;
            bool   active = false, releasing = false, reverse = false, fast = false;
        };
        void renderOne (Play&, float* l, float* r, int start, int n, const Params&);

        double sr = 44100.0;
        Play cur, old;
    };
}
