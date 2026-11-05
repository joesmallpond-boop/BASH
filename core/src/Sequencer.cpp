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

        int64_t floorMod (int64_t a, int64_t b) { int64_t m = a % b; return m < 0 ? m + b : m; }

        bool isV2Family (Algorithm a) { return a == Algorithm::CoreAcidV2 || a == Algorithm::DedicatedBass; }

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

    StepData Pattern::generate (uint32_t i, const SequencerParams& p, const Scale& scale) const noexcept
    {
        const float t = std::clamp (p.type, 0.0f, 1.0f);
        const bool v2 = isV2Family (p.algorithm);
        StepData s;

        // Rhythm: a fixed priority per step, compared against Density. Because
        // the priority never depends on Density, raising Density only ever adds
        // notes and never changes the ones already playing.
        const float jitter     = unitFloat (seed, i, salt::rhythmJitter);
        const float structured = std::clamp (gridPriority[i % 16] + (jitter - 0.5f) * 0.35f, 0.0f, 0.999f);
        const float chaotic    = unitFloat (seed, i, salt::rhythm);
        const float priority   = structured * (1.0f - t) + chaotic * t;
        s.active = priority < std::clamp (p.density, 0.0f, 1.0f);

        // Pitch: weighted structured pick, replaced by wild picks as Type rises.
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

        int note = p.baseNote + semis;
        if (p.scaleLock) note = scale.quantize (note);
        s.note = std::clamp (note, 0, 127);

        // Articulation
        s.accent = unitFloat (seed, i, salt::accent) < 0.22f + 0.2f * t;
        s.slide  = unitFloat (seed, i, salt::slide)  < (v2 ? 0.12f : 0.18f) + 0.15f * t;
        const float gv = (unitFloat (seed, i, salt::gateVar) - 0.5f) * t;
        s.gate = std::clamp (p.gate * (1.0f + gv), 0.05f, 2.0f);
        s.velocity = velocityFor (s.accent, p.accentAmount);
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
            if (l.mask & lockbit::pitch)  s.note   = p.scaleLock ? scale.quantize (l.value.note) : l.value.note;
            if (l.mask & lockbit::accent) s.accent = l.value.accent;
            if (l.mask & lockbit::slide)  s.slide  = l.value.slide;
            if (l.mask & lockbit::gate)   s.gate   = l.value.gate;
            s.velocity = velocityFor (s.accent, p.accentAmount);
        }
        return s;
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
        o << "bashpat1 " << seed << ' ' << locks.size();
        for (auto& [idx, l] : locks)
            o << ' ' << idx << ' ' << (int) l.mask << ' ' << (int) l.value.active << ' ' << l.value.note
              << ' ' << (int) l.value.accent << ' ' << (int) l.value.slide << ' ' << l.value.gate;
        return o.str();
    }

    bool Pattern::deserialise (const std::string& text)
    {
        std::istringstream in (text);
        std::string tag;
        uint64_t s = 0;
        size_t n = 0;
        if (! (in >> tag >> s >> n) || tag != "bashpat1")
            return false;

        std::map<uint32_t, StepLock> loaded;
        for (size_t k = 0; k < n; ++k)
        {
            uint32_t idx; int mask, a, note, acc, sl; float g;
            if (! (in >> idx >> mask >> a >> note >> acc >> sl >> g))
                return false;
            StepLock l;
            l.mask = (uint8_t) mask;
            l.value.active = a != 0; l.value.note = note;
            l.value.accent = acc != 0; l.value.slide = sl != 0; l.value.gate = g;
            loaded[idx] = l;
        }
        seed = s;
        locks = std::move (loaded);
        return true;
    }

    // ---------------------------------------------------------------------
    void TrackPlayer::reset (std::vector<NoteEvent>* out, int sampleOffset)
    {
        if (out != nullptr && heldNote >= 0)
            out->push_back ({ sampleOffset, false, heldNote, 0, false, false });
        heldNote = -1;
        heldSlides = false;
        started = false;
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
            lastBoundary = (int64_t) std::ceil (posStart - 1e-9) - 1;
        }
        expectedPos = posEnd;

        auto offsetAt = [&] (double pos)
        {
            // small epsilon so float rounding never lands a step one sample early
            const int s = (int) std::floor ((pos - posStart) / stepsPerSample + 1.0e-6);
            return std::clamp (s, 0, numSamples - 1);
        };

        for (int64_t b = lastBoundary + 1; (double) b < posEnd - 1e-9; ++b)
        {
            // Release a note whose gate ended before this step.
            if (heldNote >= 0 && heldOffPos <= (double) b)
            {
                out.push_back ({ offsetAt (heldOffPos), false, heldNote, 0, false, false });
                heldNote = -1;
                heldSlides = false;
            }

            const uint32_t idx = pattern.indexForPosition (b, p);
            const StepData s = pattern.step (idx, p, scale);
            lastIndex = idx;
            lastBoundary = b;

            if (! s.active)
                continue;

            const int at = offsetAt ((double) b);
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
                out.push_back ({ at, true, s.note, s.velocity, legato, s.accent });
                if (legato)
                    out.push_back ({ at, false, heldNote, 0, false, false });
            }

            heldNote = s.note;
            const StepData next = pattern.step (pattern.indexForPosition (b + 1, p), p, scale);
            if (s.slide && next.active)
            {
                heldSlides = true;
                heldOffPos = (double) b + 1.05;   // overlap into the next step
            }
            else
            {
                heldSlides = false;
                heldOffPos = (double) b + s.gate;
            }
        }

        if (heldNote >= 0 && heldOffPos < posEnd && heldOffPos <= (double) (lastBoundary + 1))
        {
            out.push_back ({ offsetAt (heldOffPos), false, heldNote, 0, false, false });
            heldNote = -1;
            heldSlides = false;
        }
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
            default:                       return "?";
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
