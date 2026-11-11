#include "bash/Sequencer.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace bash
{
    namespace
    {
        // How strongly each 16th-note position "wants" to play when Type = 0.
        // Lower = fires earlier as Density rises. Downbeats first, then
        // offbeats, then the in-between 16ths — a classic acid feel.
        constexpr float gridPriority[16] = {
            0.00f, 0.80f, 0.45f, 0.70f, 0.20f, 0.85f, 0.40f, 0.65f,
            0.10f, 0.90f, 0.50f, 0.60f, 0.25f, 0.75f, 0.35f, 0.55f
        };

        struct WeightedTarget { int semitones; int weight; };

        // Structured (Type = 0) note pools, as semitones above the root.
        // V1: classic acid spread. V2: root/fifth/octave heavy for clean low end.
        constexpr WeightedTarget poolV1[] = {
            { 0, 6 }, { 12, 3 }, { 7, 2 }, { 3, 1 }, { 10, 1 }, { 5, 1 }, { 2, 1 }, { -2, 1 }
        };
        constexpr WeightedTarget poolV2[] = {
            { 0, 8 }, { 12, 3 }, { 7, 3 }, { -5, 1 }, { 10, 1 }
        };

        template <size_t N>
        int pickWeighted (const WeightedTarget (&pool)[N], float u)
        {
            int total = 0;
            for (auto& t : pool) total += t.weight;
            int x = (int) (u * (float) total);
            for (auto& t : pool)
            {
                if (x < t.weight) return t.semitones;
                x -= t.weight;
            }
            return pool[0].semitones;
        }

        constexpr int drumBaseNoteValue = 36;

        int64_t floorMod (int64_t a, int64_t b) { int64_t m = a % b; return m < 0 ? m + b : m; }


        int velocityFor (bool accent, float accentAmount)
        {
            const float v = accent ? 90.0f + accentAmount * 37.0f
                                   : 90.0f - accentAmount * 20.0f;
            return std::clamp ((int) std::lround (v), 1, 127);
        }
    }

    // ---------------------------------------------------------------------
    void Pattern::regenerate() noexcept
    {
        seed = mix64 (seed + salt::reseed);
    }

    namespace
    {
        // Driving bass: 8ths first, then the 16th pushes.
        constexpr float bassGrid[16] = {
            0.00f, 0.70f, 0.20f, 0.75f, 0.05f, 0.70f, 0.25f, 0.75f,
            0.02f, 0.70f, 0.20f, 0.75f, 0.08f, 0.70f, 0.30f, 0.80f
        };
        // Counterpoint chord stabs: syncopated, sparse.
        constexpr float chordGrid[16] = {
            0.05f, 0.90f, 0.60f, 0.35f, 0.85f, 0.80f, 0.25f, 0.90f,
            0.15f, 0.90f, 0.30f, 0.50f, 0.85f, 0.80f, 0.40f, 0.95f
        };
        // Counterpoint bass: on the beats, filling where chords rest.
        constexpr float cpBassGrid[16] = {
            0.00f, 0.85f, 0.45f, 0.80f, 0.20f, 0.85f, 0.50f, 0.80f,
            0.10f, 0.85f, 0.50f, 0.80f, 0.25f, 0.85f, 0.55f, 0.90f
        };
        constexpr WeightedTarget bassPool[]  = { { 0, 10 }, { 12, 3 }, { 7, 2 }, { 10, 1 } };
        constexpr WeightedTarget bassWild[]  = { { 0, 2 }, { 3, 1 }, { 5, 1 }, { 7, 2 }, { 10, 1 }, { 12, 2 }, { -5, 1 } };
        struct DegreeWeight { int degree; int weight; };
        constexpr DegreeWeight progression[] = { { 0, 4 }, { 5, 2 }, { 3, 2 }, { 4, 2 }, { 1, 1 }, { 2, 1 } };

        float rhythmPriority (const float (&grid)[16], uint64_t seed, uint32_t i, float t, float jitterAmt, uint64_t saltA, uint64_t saltB)
        {
            const float jitter     = unitFloat (seed, i, saltA);
            const float structured = std::clamp (grid[i % 16] + (jitter - 0.5f) * jitterAmt, 0.0f, 0.999f);
            const float chaotic    = unitFloat (seed, i, saltB);
            return structured * (1.0f - t) + chaotic * t;
        }

        int pickDegree (float u)
        {
            int total = 0;
            for (auto& d : progression) total += d.weight;
            int x = (int) (u * (float) total);
            for (auto& d : progression) { if (x < d.weight) return d.degree; x -= d.weight; }
            return 0;
        }
    }

    StepData Pattern::generate (uint32_t i, const SequencerParams& p, const Scale& scale) const noexcept
    {
        const float t       = std::clamp (p.type, 0.0f, 1.0f);
        const float density = std::clamp (p.density, 0.0f, 1.0f);
        StepData s;

        switch (p.algorithm)
        {
            case Algorithm::DedicatedBass:
            {
                // Rhythm stays locked to the grid: chaos only loosens it halfway.
                s.active = rhythmPriority (bassGrid, seed, i, t * 0.5f, 0.15f, salt::rhythmJitter, salt::rhythm) < density;
                int semis = pickWeighted (bassPool, unitFloat (seed, i, salt::pitchPick));
                if (unitFloat (seed, i, salt::pitchChaos) < t * 0.6f)
                    semis = pickWeighted (bassWild, unitFloat (seed, i, salt::pitchChaosRange));
                s.note = scale.quantize (p.baseNote + semis);
                s.accent = unitFloat (seed, i, salt::accent) < (i % 4 == 0 ? 0.5f : 0.1f) + 0.2f * t;
                s.slide  = unitFloat (seed, i, salt::slide)  < 0.05f + 0.1f * t;
                s.gate   = std::clamp (p.gate, 0.05f, 2.0f);
                break;
            }

            case Algorithm::Arpeggiator:
            {
                s.active = p.heldCount > 0
                        && rhythmPriority (gridPriority, seed, i, t, 0.35f, salt::rhythmJitter, salt::rhythm) < density;
                if (p.heldCount > 0)
                {
                    const int n = std::min (p.heldCount, (int) p.held.size());
                    int k = (int) (i % (uint32_t) n);                          // straight up
                    if (t > 0.33f && ((i / (uint32_t) n) % 2) == 1) k = n - 1 - k;   // up-down
                    if (unitFloat (seed, i, salt::arpChaos) < t) k = (int) uniformInt (seed, i, salt::arpPick, (uint32_t) n);
                    s.note = p.held[(size_t) k];
                }
                else
                {
                    s.note = p.baseNote;
                }
                s.accent = unitFloat (seed, i, salt::accent) < 0.22f + 0.2f * t;
                s.slide  = unitFloat (seed, i, salt::slide)  < 0.12f + 0.15f * t;
                s.gate   = std::clamp (p.gate * (1.0f + (unitFloat (seed, i, salt::gateVar) - 0.5f) * t), 0.05f, 2.0f);
                break;   // held notes are used exactly: no scale quantise
            }

            case Algorithm::Counterpoint:
            {
                // Harmony changes per bar (or per half bar as Type rises).
                const uint32_t bar  = i / 16u;
                const uint32_t half = i / 8u;
                const bool fast = unitFloat (seed, half, salt::harmonyChaos) < t * 0.5f;
                const int degree = pickDegree (unitFloat (seed, fast ? half + 0x10000u : bar, salt::harmony));
                const int root = scale.stepFrom (p.baseNote + 12, degree);

                s.note = root;
                s.chord[0] = scale.stepFrom (root, 2);
                s.chord[1] = scale.stepFrom (root, 4);
                s.chordExtra = 2;
                if (unitFloat (seed, bar, salt::chordExtra) < 0.15f + 0.5f * t)
                {
                    s.chord[2] = scale.stepFrom (root, 6);   // 7th
                    s.chordExtra = 3;
                }
                s.active = rhythmPriority (chordGrid, seed, i, t, 0.3f, salt::chordRhythm, salt::rhythm) < density;

                const bool bassBeat = rhythmPriority (cpBassGrid, seed, i, t, 0.3f, salt::bassRhythm, salt::rhythm + 77) < density;
                s.bassActive = bassBeat && (! s.active || unitFloat (seed, i, salt::bassPick + 5) < 0.25f + t * 0.5f);
                int bass = scale.stepFrom (p.baseNote, degree);
                if (bass > p.baseNote + 7) bass -= 12;
                if (unitFloat (seed, i, salt::bassPick) < 0.12f + 0.3f * t) bass = scale.stepFrom (bass, 4);   // fifth
                s.bassNote = bass;

                s.accent = unitFloat (seed, i, salt::accent) < 0.15f + 0.2f * t;
                s.slide  = false;
                s.gate   = std::clamp (p.gate * 1.5f, 0.05f, 2.0f);
                break;
            }

            case Algorithm::Chords:
            {
                // A four-chord progression in the key (one of a few classic shapes, picked by the seed),
                // a chord per bar, half bar or beat as Type rises. Density: long chords -> busy stabs.
                static constexpr int progressions[][4] = {
                    { 0, 5, 3, 4 }, { 0, 4, 5, 3 }, { 1, 4, 0, 0 }, { 5, 3, 0, 4 },
                    { 0, 3, 4, 3 }, { 0, 2, 5, 3 }, { 3, 4, 2, 5 }, { 0, 5, 1, 4 } };
                const float j = std::clamp (p.jazz, 0.0f, 1.0f);
                const uint32_t chordLen = t < 0.34f ? 16u : t < 0.67f ? 8u : 4u;
                const uint32_t ci = i / chordLen, inChord = i % chordLen;
                const auto& prog = progressions[uniformInt (seed, 0, salt::harmony, 8)];
                int degree = prog[ci % 4];
                if (unitFloat (seed, ci, salt::harmonyChaos) < t * 0.35f) degree = pickDegree (unitFloat (seed, ci, salt::harmony + 3));
                if (j > 0.5f && ci % 4 == 3 && unitFloat (seed, ci, salt::harmony + 9) < j - 0.3f) degree = 4;   // a V to turn round

                // rhythm: the chord's first step always; more stabs as Density rises
                const bool first = inChord == 0;
                s.active = first ? density > 0.05f
                                 : rhythmPriority (chordGrid, seed, i, t, 0.3f, salt::chordRhythm, salt::rhythm) < (density - 0.35f) * 1.5f;
                const float sustain = std::clamp (1.0f - density, 0.15f, 1.0f);
                s.gate = std::clamp (p.gate * 2.0f * (float) (chordLen - inChord) * sustain, 0.25f, (float) (chordLen - inChord));

                // the voicing: stacked thirds in the key, richer with Jazz
                const int root = scale.stepFrom (p.baseNote + 12, degree);
                const int center = p.baseNote + 19;
                int degs[7]; int nd = 0;
                degs[nd++] = 0; degs[nd++] = 2; degs[nd++] = 4;
                auto r = [&] (uint64_t k) { return unitFloat (seed, ci * 31u + (uint32_t) k, salt::chordExtra); };
                if (r (1) < j * 1.5f) degs[nd++] = 6;                       // 7th
                if (j > 0.3f && r (2) < j * 1.1f) degs[nd++] = 8;           // 9th
                if (j > 0.55f && r (3) < j) degs[nd++] = r (4) < 0.6f ? 12 : 10;   // 13th / 11th
                if (r (5) < j * 0.35f) degs[1] = r (6) < 0.5f ? 3 : 1;      // sus4 / sus2
                int notes[7]; int nn = 0;
                for (int k = 0; k < nd; ++k) notes[nn++] = scale.stepFrom (root, degs[k]);
                if (j > 0.82f && nn > 3 && r (7) < (j - 0.8f) * 3.0f)       // an altered tone: b9 or #11
                    notes[nn - 1] += r (8) < 0.5f ? -1 : 1;

                // rootless above a bass root (from about halfway), and a drop-2 spread
                const bool rootless = j > 0.45f && nn >= 4 && r (9) < j;
                int upper[7]; int nu = 0;
                for (int k = rootless ? 1 : 0; k < nn; ++k)
                {
                    int x = notes[k];
                    while (x < center - 5) x += 12;
                    while (x >= center + 9) x -= 12;
                    upper[nu++] = x;
                }
                std::sort (upper, upper + nu);
                nu = (int) (std::unique (upper, upper + nu) - upper);
                if (j > 0.35f && nu >= 4 && r (10) < j) { upper[nu - 2] -= 12; std::sort (upper, upper + nu); }

                int bass = root;
                while (bass > p.baseNote + 7) bass -= 12;
                if (rootless || j > 0.2f) { s.note = bass; for (int k = 0; k < nu && k < maxChordNotes - 1; ++k) s.chord[(size_t) k] = upper[k]; s.chordExtra = std::min (nu, maxChordNotes - 1); }
                else { s.note = upper[0]; for (int k = 1; k < nu; ++k) s.chord[(size_t) k - 1] = upper[k]; s.chordExtra = nu - 1; }

                s.accent = first && unitFloat (seed, i, salt::accent) < 0.3f;
                s.slide  = false;
                break;
            }

            case Algorithm::CoreAcidV1:
            case Algorithm::CoreAcidV2:
            default:
            {
                const bool v2 = p.algorithm == Algorithm::CoreAcidV2;
                // Rhythm: a fixed priority per step compared against Density, so
                // raising Density only ever adds notes and never changes the motif.
                s.active = rhythmPriority (gridPriority, seed, i, t, 0.35f, salt::rhythmJitter, salt::rhythm) < density;

                int semis = v2 ? pickWeighted (poolV2, unitFloat (seed, i, salt::pitchPick))
                               : pickWeighted (poolV1, unitFloat (seed, i, salt::pitchPick));
                semis = scale.quantize (p.baseNote + semis) - p.baseNote;

                if (unitFloat (seed, i, salt::pitchChaos) < t)
                    semis = v2 ? -5 + (int) uniformInt (seed, i, salt::pitchChaosRange, 18)    // -5..+12
                               : -12 + (int) uniformInt (seed, i, salt::pitchChaosRange, 37);  // -12..+24

                if (! v2 && unitFloat (seed, i, salt::chromatic) < t * 0.5f)
                {
                    const int amount = 1 + (int) uniformInt (seed, i, salt::chromaticAmount, 6);
                    semis += (uniformInt (seed, i, salt::chromaticAmount + 99, 2) == 0) ? amount : -amount;
                }

                const float upChance = v2 ? 0.06f : 0.12f + 0.2f * t;
                if (unitFloat (seed, i, salt::octaveUp) < upChance) semis += 12;
                if (! v2 && unitFloat (seed, i, salt::octaveDown) < 0.05f + 0.1f * t) semis -= 12;

                s.note   = p.baseNote + semis;
                s.accent = unitFloat (seed, i, salt::accent) < 0.22f + 0.2f * t;
                s.slide  = unitFloat (seed, i, salt::slide)  < (v2 ? 0.12f : 0.18f) + 0.15f * t;
                const float gv = (unitFloat (seed, i, salt::gateVar) - 0.5f) * t;
                s.gate = std::clamp (p.gate * (1.0f + gv), 0.05f, 2.0f);
                break;
            }
        }

        // An imported motif replaces the structured layer; Type still morphs away from it.
        if (! motif.empty() && p.algorithm != Algorithm::Counterpoint && p.algorithm != Algorithm::Arpeggiator && p.algorithm != Algorithm::Chords)
        {
            const auto& m = motif[(size_t) (i % (uint32_t) motif.size())];
            if (unitFloat (seed, i, salt::motifChaos) >= t)
            {
                s.active = (m.active ? 0.02f : 0.97f) < density;
                s.note   = m.note;
                s.accent = m.accent;
                s.gate   = std::clamp (m.gate, 0.05f, 2.0f);
                s.slide  = false;
            }
        }

        if (p.scaleLock && p.algorithm != Algorithm::Arpeggiator && ! p.drumTrack)
        {
            s.note = scale.quantize (s.note);
            s.bassNote = scale.quantize (s.bassNote);
        }
        s.note     = std::clamp (s.note, 0, 127);
        s.bassNote = std::clamp (s.bassNote, 0, 127);
        for (auto& c : s.chord) c = std::clamp (c, 0, 127);
        s.velocity = velocityFor (s.accent, p.accentAmount);

        // Drum groove: one rhythm role per cell in the window, up to 4 hits per step
        if (p.drumTrack && p.sliceMode == SliceMode::Groove && p.sliceCount > 0)
        {
            static constexpr float roles[8][16] = {
                { 0.00f, 0.95f, 0.70f, 0.90f, 0.35f, 0.95f, 0.75f, 0.85f, 0.10f, 0.95f, 0.60f, 0.90f, 0.40f, 0.95f, 0.65f, 0.80f }, // kick
                { 0.95f, 0.98f, 0.95f, 0.90f, 0.05f, 0.95f, 0.90f, 0.75f, 0.95f, 0.98f, 0.92f, 0.90f, 0.05f, 0.90f, 0.85f, 0.70f }, // snare / clap
                { 0.10f, 0.55f, 0.15f, 0.60f, 0.12f, 0.55f, 0.15f, 0.60f, 0.10f, 0.55f, 0.15f, 0.60f, 0.12f, 0.55f, 0.15f, 0.65f }, // closed hat
                { 0.95f, 0.95f, 0.25f, 0.95f, 0.95f, 0.95f, 0.30f, 0.95f, 0.95f, 0.95f, 0.25f, 0.95f, 0.95f, 0.95f, 0.30f, 0.95f }, // open hat
                { 0.90f, 0.80f, 0.85f, 0.45f, 0.90f, 0.70f, 0.85f, 0.60f, 0.90f, 0.75f, 0.50f, 0.85f, 0.90f, 0.65f, 0.85f, 0.55f }, // perc 1
                { 0.85f, 0.90f, 0.60f, 0.90f, 0.80f, 0.90f, 0.85f, 0.50f, 0.85f, 0.90f, 0.70f, 0.90f, 0.80f, 0.90f, 0.85f, 0.60f }, // perc 2
                { 0.95f, 0.85f, 0.90f, 0.80f, 0.95f, 0.85f, 0.90f, 0.70f, 0.95f, 0.85f, 0.90f, 0.80f, 0.95f, 0.85f, 0.90f, 0.60f }, // fill 1
                { 0.90f, 0.95f, 0.85f, 0.95f, 0.90f, 0.95f, 0.85f, 0.95f, 0.90f, 0.95f, 0.85f, 0.95f, 0.90f, 0.95f, 0.75f, 0.95f }, // fill 2
            };
            const uint32_t n  = p.sliceCount;
            const uint32_t ws = std::min (p.windowStart, n - 1);
            const uint32_t wz = std::clamp<uint32_t> (p.windowSize, 1u, std::min (8u, n - ws));
            int hits[4]; int count = 0;   // up to 4 drum hits per step
            for (uint32_t r = 0; r < wz && count < 4; ++r)
            {
                const float jitter = unitFloat (seed, i * 8u + r, salt::grooveJitter);
                const float structured = std::clamp (roles[r][i % 16] + (jitter - 0.5f) * 0.2f, 0.0f, 0.999f);
                const float chaotic = unitFloat (seed, i * 8u + r, salt::grooveChaos);
                const float prio = structured * (1.0f - t) + chaotic * t;
                if (prio < density) hits[count++] = (int) (ws + r);
            }
            s.active = count > 0;
            s.slice = count > 0 ? hits[0] : (int) ws;
            s.note = drumBaseNoteValue + s.slice;
            s.chordExtra = std::max (0, count - 1);
            for (int c = 1; c < count; ++c) s.chord[(size_t) c - 1] = drumBaseNoteValue + hits[c];
            s.slide = false;
            s.bassActive = false;
            return s;
        }

        // Slice choice (sampler and drum tracks)
        if (p.sliceCount > 0)
        {
            const uint32_t n = p.sliceCount;
            const bool wild = unitFloat (seed, i, salt::sliceChaos) < t;
            switch (p.sliceMode)
            {
                case SliceMode::Bounce:
                    s.slice = (int) uniformInt (seed, i, salt::slicePick, n);
                    break;
                case SliceMode::Sequential:
                    s.slice = (int) (wild ? uniformInt (seed, i, salt::slicePick, n) : i % n);
                    break;
                case SliceMode::Window:
                default:
                {
                    const uint32_t ws = std::min (p.windowStart, n - 1);
                    const uint32_t wz = std::clamp<uint32_t> (p.windowSize, 1u, n - ws);
                    s.slice = (int) (ws + (wild ? uniformInt (seed, i, salt::slicePick, wz) : i % wz));
                    break;
                }
            }
            if (p.drumTrack && ! motif.empty())
            {
                // imported drum MIDI: the motif note picks the pad
                s.slice = std::clamp (s.note - drumBaseNoteValue, 0, (int) n - 1);
            }
            if (p.drumTrack)
            {
                s.note = drumBaseNoteValue + s.slice;
                s.chordExtra = 0;
                s.bassActive = false;
            }
        }
        return s;
    }

    StepData Pattern::step (uint32_t i, const SequencerParams& p, const Scale& scale) const
    {
        StepData s = generate (i, p, scale);
        auto it = locks.find (i);
        if (it != locks.end())
        {
            const auto& l = it->second;
            if (l.mask & lockbit::active) s.active = l.value.active;
            if (l.mask & lockbit::pitch)
            {
                s.note = p.scaleLock ? scale.quantize (l.value.note) : l.value.note;
                if (p.algorithm == Algorithm::Counterpoint)
                {
                    s.chord[0] = scale.stepFrom (s.note, 2);
                    s.chord[1] = scale.stepFrom (s.note, 4);
                    s.chord[2] = scale.stepFrom (s.note, 6);
                }
            }
            if (l.mask & lockbit::accent) s.accent = l.value.accent;
            if (l.mask & lockbit::slide)  s.slide  = l.value.slide;
            if (l.mask & lockbit::gate)   s.gate   = l.value.gate;
            if ((l.mask & lockbit::slice) && p.sliceCount > 0)
                s.slice = std::clamp (l.value.slice, 0, (int) p.sliceCount - 1);
            s.velocity = velocityFor (s.accent, p.accentAmount);
        }
        return s;
    }

    double Pattern::stepStart (int64_t n, const SequencerParams& p) const noexcept
    {
        double delay = 0.0;
        if (p.swing > 0.0f && (n & 1) != 0) delay += std::clamp (p.swing, 0.0f, 0.75f) * 0.5;
        if (p.jitter > 0.0f) delay += unitFloat (seed, (uint64_t) n, salt::jitter) * std::clamp (p.jitter, 0.0f, 1.0f) * 0.4;
        return (double) n + delay;
    }

    uint32_t Pattern::indexForPosition (int64_t n, const SequencerParams& p) const noexcept
    {
        const int64_t len = std::max<int64_t> (1, (int64_t) p.length);
        const int64_t m = n + (int64_t) p.offset;
        switch (p.direction)
        {
            case Direction::Reverse:  return (uint32_t) (len - 1 - floorMod (m, len));
            case Direction::PingPong:
            {
                if (len == 1) return 0;
                const int64_t period = 2 * len - 2;
                const int64_t r = floorMod (m, period);
                return (uint32_t) (r < len ? r : period - r);
            }
            case Direction::Random:   return (uint32_t) (hash3 (seed, (uint64_t) m, salt::direction) % (uint64_t) len);
            case Direction::Forward:
            default:                  return (uint32_t) floorMod (m, len);
        }
    }

    void Pattern::lock (uint32_t index, uint8_t mask, const StepData& value)
    {
        auto& l = locks[index];
        // Only overwrite the fields being locked now; keep earlier locked fields.
        if (mask & lockbit::active) l.value.active = value.active;
        if (mask & lockbit::pitch)  l.value.note   = value.note;
        if (mask & lockbit::accent) l.value.accent = value.accent;
        if (mask & lockbit::slide)  l.value.slide  = value.slide;
        if (mask & lockbit::gate)   l.value.gate   = value.gate;
        if (mask & lockbit::slice)  l.value.slice  = value.slice;
        l.mask |= mask;
    }

    void Pattern::unlock (uint32_t index, uint8_t mask)
    {
        auto it = locks.find (index);
        if (it == locks.end()) return;
        it->second.mask &= (uint8_t) ~mask;
        if (it->second.mask == 0) locks.erase (it);
    }

    uint8_t Pattern::lockMask (uint32_t index) const
    {
        auto it = locks.find (index);
        return it == locks.end() ? 0 : it->second.mask;
    }

    std::string Pattern::serialise() const
    {
        std::ostringstream o;
        o << "bashpat3 " << seed << ' ' << locks.size();
        for (auto& [idx, l] : locks)
            o << ' ' << idx << ' ' << (int) l.mask << ' ' << (int) l.value.active << ' ' << l.value.note
              << ' ' << (int) l.value.accent << ' ' << (int) l.value.slide << ' ' << l.value.gate
              << ' ' << l.value.slice;
        o << ' ' << motif.size();
        for (auto& m : motif)
            o << ' ' << (int) m.active << ' ' << m.note << ' ' << (int) m.accent << ' ' << m.gate;
        return o.str();
    }

    bool Pattern::deserialise (const std::string& text)
    {
        std::istringstream in (text);
        std::string tag;
        uint64_t s = 0;
        size_t n = 0;
        if (! (in >> tag >> s >> n) || (tag != "bashpat1" && tag != "bashpat2" && tag != "bashpat3"))
            return false;
        const bool hasSlice = tag != "bashpat1";

        std::map<uint32_t, StepLock> loaded;
        for (size_t k = 0; k < n; ++k)
        {
            uint32_t idx; int mask, a, note, acc, sl; float g;
            int slice = 0;
            if (! (in >> idx >> mask >> a >> note >> acc >> sl >> g))
                return false;
            if (hasSlice && ! (in >> slice))
                return false;
            StepLock l;
            l.mask = (uint8_t) mask;
            l.value.active = a != 0; l.value.note = note;
            l.value.accent = acc != 0; l.value.slide = sl != 0; l.value.gate = g; l.value.slice = slice;
            loaded[idx] = l;
        }
        std::vector<StepData> loadedMotif;
        size_t motifCount = 0;
        if (tag == "bashpat3" && (in >> motifCount))
        {
            for (size_t k = 0; k < motifCount && k < 65536; ++k)
            {
                int a, note, acc; float g;
                if (! (in >> a >> note >> acc >> g)) return false;
                StepData m; m.active = a != 0; m.note = note; m.accent = acc != 0; m.gate = g;
                loadedMotif.push_back (m);
            }
        }
        seed = s;
        locks = std::move (loaded);
        motif = std::move (loadedMotif);
        return true;
    }

    // ---------------------------------------------------------------------
    void TrackPlayer::reset (std::vector<NoteEvent>* out, int sampleOffset)
    {
        if (out != nullptr && heldNote >= 0)
            out->push_back ({ sampleOffset, false, heldNote, 0, false, false });
        if (out != nullptr)
            for (auto& h : polyHeld)
                out->push_back ({ sampleOffset, false, h.note, 0, false, false, 0, h.stream });
        polyHeld.clear();
        heldNote = -1;
        heldSlides = false;
        started = false;
    }

    void TrackPlayer::releasePoly (double upTo, double posStart, double stepsPerSample, int numSamples,
                                   std::vector<NoteEvent>& out, int forStream, int atSample)
    {
        std::sort (polyHeld.begin(), polyHeld.end(), [] (const PolyHeld& a, const PolyHeld& b) { return a.offPos < b.offPos; });
        for (auto it = polyHeld.begin(); it != polyHeld.end();)
        {
            const bool streamMatch = forStream < 0 || it->stream == forStream;
            if (streamMatch && (atSample >= 0 || it->offPos <= upTo))
            {
                int at = atSample;
                if (at < 0)
                    at = std::clamp ((int) std::floor ((it->offPos - posStart) / stepsPerSample + 1.0e-6), 0, numSamples - 1);
                out.push_back ({ at, false, it->note, 0, false, false, 0, it->stream });
                it = polyHeld.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

    void TrackPlayer::process (double posStart, double stepsPerSample, int numSamples,
                               const Pattern& pattern, const SequencerParams& p, const Scale& scale,
                               std::vector<NoteEvent>& out)
    {
        if (numSamples <= 0 || stepsPerSample <= 0.0)
            return;

        const double posEnd = posStart + stepsPerSample * numSamples;

        // Transport jumped (loop, seek): release and resync.
        if (! started || std::abs (posStart - expectedPos) > 0.01)
        {
            if (started) reset (&out, 0);
            started = true;
            // a swung/jittered step can start up to ~0.8 of a step late: look one step back
            lastBoundary = (int64_t) std::ceil (posStart - 1e-9) - 2;
            while (pattern.stepStart (lastBoundary + 1, p) < posStart - 1e-9) ++lastBoundary;
        }
        expectedPos = posEnd;

        auto offsetAt = [&] (double pos)
        {
            // small epsilon so float rounding never lands a step one sample early
            const int s = (int) std::floor ((pos - posStart) / stepsPerSample + 1.0e-6);
            return std::clamp (s, 0, numSamples - 1);
        };

        for (int64_t b = lastBoundary + 1; pattern.stepStart (b, p) < posEnd - 1e-9; ++b)
        {
            const double start = pattern.stepStart (b, p);   // grid position + swing + jitter
            // Release notes whose gate ended before this step.
            releasePoly (start, posStart, stepsPerSample, numSamples, out);
            if (heldNote >= 0 && heldOffPos <= start)
            {
                out.push_back ({ offsetAt (heldOffPos), false, heldNote, 0, false, false });
                heldNote = -1;
                heldSlides = false;
            }

            const uint32_t idx = pattern.indexForPosition (b, p);
            const StepData s = pattern.step (idx, p, scale);
            lastIndex = idx;
            lastBoundary = b;

            if (p.algorithm == Algorithm::Counterpoint || p.algorithm == Algorithm::Chords || (p.drumTrack && p.sliceMode == SliceMode::Groove))
            {
                const int at = offsetAt (start);
                if (heldNote >= 0) { out.push_back ({ at, false, heldNote, 0, false, false }); heldNote = -1; heldSlides = false; }
                if (s.active)
                {
                    releasePoly (0.0, posStart, stepsPerSample, numSamples, out, 0, at);
                    out.push_back ({ at, true, s.note, s.velocity, false, s.accent, s.slice, 0, 0 });
                    polyHeld.push_back ({ s.note, start + s.gate, 0 });
                    const int chordVel = p.algorithm == Algorithm::Chords ? 6 : 12;
                    int strum = 1;
                    for (int c = 0; c < s.chordExtra; ++c)
                    {
                        const int cn = s.chord[(size_t) c];
                        bool dup = cn == s.note;
                        for (int d = 0; d < c; ++d) dup = dup || s.chord[(size_t) d] == cn;
                        if (dup) continue;
                        out.push_back ({ at, true, cn, std::max (1, s.velocity - chordVel), false, s.accent, s.slice, 0, strum++ });
                        polyHeld.push_back ({ cn, start + s.gate, 0 });
                    }
                }
                if (s.bassActive)
                {
                    releasePoly (0.0, posStart, stepsPerSample, numSamples, out, 1, at);
                    out.push_back ({ at, true, s.bassNote, s.velocity, false, s.accent, s.slice, 1 });
                    polyHeld.push_back ({ s.bassNote, start + std::min (s.gate, 0.9f), 1 });
                }
                continue;
            }

            if (! polyHeld.empty())   // switched away from Counterpoint
                releasePoly (0.0, posStart, stepsPerSample, numSamples, out, -1, offsetAt (start));

            if (! s.active)
                continue;

            const int at = offsetAt (start);
            const bool legato = heldNote >= 0 && heldSlides;

            if (heldNote >= 0 && ! legato)
            {
                // Long gate overlapping the next note: cut it.
                out.push_back ({ at, false, heldNote, 0, false, false });
                heldNote = -1;
            }

            if (legato && s.note == heldNote)
            {
                // Slide into the same pitch = a tie: just keep holding.
            }
            else
            {
                out.push_back ({ at, true, s.note, s.velocity, legato, s.accent, s.slice });
                if (legato)
                    out.push_back ({ at, false, heldNote, 0, false, false });
            }

            heldNote = s.note;
            const StepData next = pattern.step (pattern.indexForPosition (b + 1, p), p, scale);
            if (s.slide && next.active)
            {
                heldSlides = true;
                heldOffPos = pattern.stepStart (b + 1, p) + 0.05;   // overlap into the next step
            }
            else
            {
                heldSlides = false;
                heldOffPos = start + s.gate;
            }
        }

        {
            // poly notes ending inside this block (but not past the next step)
            const double limit = std::min (posEnd - 1e-9, pattern.stepStart (lastBoundary + 1, p));
            releasePoly (limit, posStart, stepsPerSample, numSamples, out);
        }

        if (heldNote >= 0 && heldOffPos < posEnd && heldOffPos <= pattern.stepStart (lastBoundary + 1, p))
        {
            out.push_back ({ offsetAt (heldOffPos), false, heldNote, 0, false, false });
            heldNote = -1;
            heldSlides = false;
        }
    }

    std::vector<ExportNote> renderNotes (const Pattern& pattern, const SequencerParams& p, const Scale& scale, int64_t numSteps)
    {
        constexpr int ticksPerStep = 96;
        TrackPlayer player;
        std::vector<NoteEvent> ev;
        std::vector<ExportNote> out;
        std::vector<std::pair<size_t, int>> open;   // index into out, stream

        auto handle = [&] (const NoteEvent& e, double pos)
        {
            if (e.noteOn)
            {
                out.push_back ({ pos, pos, e.note, e.velocity, e.stream, e.slice });
                open.push_back ({ out.size() - 1, e.stream });
            }
            else
            {
                for (auto it = open.begin(); it != open.end(); ++it)
                    if (out[it->first].note == e.note && it->second == e.stream)
                    {
                        out[it->first].end = pos;
                        open.erase (it);
                        break;
                    }
            }
        };

        const int block = ticksPerStep * 4;
        const int64_t total = numSteps * ticksPerStep;
        for (int64_t t0 = 0; t0 < total; t0 += block)
        {
            const int n = (int) std::min<int64_t> (block, total - t0);
            ev.clear();
            player.process ((double) t0 / ticksPerStep, 1.0 / ticksPerStep, n, pattern, p, scale, ev);
            for (auto& e : ev) handle (e, (double) (t0 + e.sampleOffset) / ticksPerStep);
        }
        ev.clear();
        player.reset (&ev, 0);
        for (auto& e : ev) handle (e, (double) numSteps);
        for (auto& o : out) if (o.end <= o.start) o.end = std::min ((double) numSteps, o.start + 0.25);
        return out;
    }

    const char* algorithmName (Algorithm a)
    {
        switch (a)
        {
            case Algorithm::CoreAcidV1:    return "Core Acid V1";
            case Algorithm::CoreAcidV2:    return "Core Acid V2";
            case Algorithm::Counterpoint:  return "Counterpoint";
            case Algorithm::Arpeggiator:   return "Arpeggiator";
            case Algorithm::DedicatedBass: return "Dedicated Bass";
            case Algorithm::Chords:        return "Chords";
            default:                       return "?";
        }
    }

    const char* sliceModeName (SliceMode m)
    {
        switch (m)
        {
            case SliceMode::Window:     return "Window";
            case SliceMode::Bounce:     return "Bounce";
            case SliceMode::Sequential: return "Sequential";
            case SliceMode::Groove:     return "Groove";
            default:                    return "?";
        }
    }

    const char* directionName (Direction d)
    {
        switch (d)
        {
            case Direction::Forward:  return "Forward";
            case Direction::Reverse:  return "Reverse";
            case Direction::PingPong: return "Ping-Pong";
            case Direction::Random:   return "Random";
            default:                  return "?";
        }
    }
}
