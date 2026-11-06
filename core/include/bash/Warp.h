#pragma once
// BASH! core — warping audio to the session tempo, like Ableton's warp:
// detect a file's tempo, then time-stretch it (pitch unchanged) so it plays in time.

#include "bash/Slices.h"

#include <memory>

namespace bash
{
    namespace warp
    {
        struct TempoEstimate
        {
            double bpm = 120.0;
            float  confidence = 0.0f;   // 0..1: how clearly the beat stood out
            int    beats = 0;           // > 0 when the file is a whole number of beats long at this tempo (a loop)
        };

        /** Estimates the tempo of a sample (70-180 bpm range; loops snap to their exact length). */
        TempoEstimate detectTempo (const SampleData&);

        /** Time-stretch without changing pitch (WSOLA). ratio = output length / input length,
            e.g. sourceBpm / targetBpm. The result keeps the source's sample rate and path. */
        std::shared_ptr<SampleData> stretch (const SampleData&, double ratio);

        /** Ratio that makes a sample recorded at sourceBpm play in time at targetBpm. */
        inline double ratioFor (double sourceBpm, double targetBpm)
        {
            return sourceBpm > 0.0 && targetBpm > 0.0 ? sourceBpm / targetBpm : 1.0;
        }
    }
}
