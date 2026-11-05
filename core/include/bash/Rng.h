#pragma once
// BASH! core — counter-based random numbers.
// Every random value is a pure function of (seed, index, salt), so a pattern
// can be any length (up to 2^32 steps) without storing it, and the same seed
// always reproduces the same music.

#include <cstdint>

namespace bash
{
    inline uint64_t mix64 (uint64_t z) noexcept
    {
        // splitmix64 finaliser
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    inline uint64_t hash3 (uint64_t seed, uint64_t index, uint64_t salt) noexcept
    {
        return mix64 (seed ^ mix64 (index ^ mix64 (salt)));
    }

    /** Uniform float in [0, 1). */
    inline float unitFloat (uint64_t seed, uint64_t index, uint64_t salt) noexcept
    {
        return (float) (hash3 (seed, index, salt) >> 40) * (1.0f / 16777216.0f);
    }

    /** Uniform integer in [0, n). n must be > 0. */
    inline uint32_t uniformInt (uint64_t seed, uint64_t index, uint64_t salt, uint32_t n) noexcept
    {
        return (uint32_t) ((hash3 (seed, index, salt) >> 32) % n);
    }

    // Salts keep the different random "streams" for one step independent, so
    // e.g. changing density never changes which pitch a step plays.
    namespace salt
    {
        enum : uint64_t
        {
            rhythm = 0x1001, rhythmJitter, pitchPick, pitchChaos, pitchChaosRange,
            chromatic, chromaticAmount, octaveUp, octaveDown, accent, slide,
            gateVar, direction, reseed, slicePick, sliceChaos,
            harmony, harmonyChaos, chordRhythm, chordExtra, bassRhythm, bassPick, arpPick, arpChaos
        };
    }
}
