#include "bash/StepSeq.h"
#include "bash/Rng.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace bash
{
    namespace
    {
        enum : uint64_t { sEvolve = 0x57E9001, sEvolveVel, sProb, sGen, sGenVel, sGenRatchet, sGenExtra, sRoll, sRollPick };

        float rnd (uint64_t seed, uint64_t i, uint64_t salt) { return unitFloat (seed, i, salt); }
        uint8_t velBetween (uint64_t seed, uint64_t i, int lo, int hi)
        {
            return (uint8_t) std::clamp (lo + (int) (rnd (seed, i, sGenVel) * (float) (hi - lo + 1)), 1, 127);
        }
        void clearSteps (Lane& l) { for (auto& s : l.steps) s = Step{}; }
        Step hit (uint8_t vel, uint8_t ratchet = 1, RatchetShape shape = RatchetShape::Flat, TrigCond c = TrigCond::Always)
        {
            Step s; s.on = true; s.vel = vel; s.ratchet = ratchet; s.shape = shape; s.cond = c; return s;
        }
        // positions written for 16 steps, mapped onto any lane length
        int at16 (int s16, int len) { return std::clamp ((int) std::lround ((double) s16 * len / 16.0), 0, len - 1); }
    }

    const char* trigCondName (TrigCond c)
    {
        switch (c)
        {
            case TrigCond::Always:   return "Always";
            case TrigCond::Fill:     return "Fill";
            case TrigCond::NotFill:  return "Not fill";
            case TrigCond::First:    return "First";
            case TrigCond::NotFirst: return "Not first";
            case TrigCond::R1of2:    return "1:2";
            case TrigCond::R2of2:    return "2:2";
            case TrigCond::R1of3:    return "1:3";
            case TrigCond::R2of3:    return "2:3";
            case TrigCond::R3of3:    return "3:3";
            case TrigCond::R1of4:    return "1:4";
            case TrigCond::R2of4:    return "2:4";
            case TrigCond::R3of4:    return "3:4";
            case TrigCond::R4of4:    return "4:4";
            default:                 return "?";
        }
    }

    const char* ratchetShapeName (RatchetShape s)
    {
        switch (s)
        {
            case RatchetShape::Up:   return "Up";
            case RatchetShape::Down: return "Down";
            default:                 return "Flat";
        }
    }

    const char* laneAlgoName (LaneAlgo a)
    {
        switch (a)
        {
            case LaneAlgo::Euclid:   return "Euclid";
            case LaneAlgo::Random:   return "Random";
            case LaneAlgo::Groove:   return "Groove";
            case LaneAlgo::Rolls:    return "Rolls";
            case LaneAlgo::Mutate:   return "Mutate";
            case LaneAlgo::Humanize: return "Humanize";
            default:                 return "?";
        }
    }

    const std::vector<std::pair<const char*, double>>& laneSpeeds()
    {
        static const std::vector<std::pair<const char*, double>> s {
            { "x1/4", 0.25 }, { "x1/2", 0.5 }, { "x3/4", 0.75 }, { "x1", 1.0 }, { "x3/2", 1.5 }, { "x2", 2.0 }, { "x4", 4.0 }
        };
        return s;
    }

    LaneRole drumLaneRole (int lane)
    {
        // factory kits: 1 kick, 2 snare, 3 clap, 4 closed hat, 5 open hat, 6-8 perc, 9-12 toms/perc, 13-16 fx/cymbals
        static constexpr LaneRole roles[stepLanes] = {
            LaneRole::Kick, LaneRole::Snare, LaneRole::Clap, LaneRole::ClosedHat, LaneRole::OpenHat,
            LaneRole::Perc, LaneRole::Perc, LaneRole::Perc, LaneRole::Tom, LaneRole::Tom, LaneRole::Perc, LaneRole::Perc,
            LaneRole::Cymbal, LaneRole::Perc, LaneRole::Kick, LaneRole::Cymbal };
        return roles[std::clamp (lane, 0, stepLanes - 1)];
    }

    // ---- pattern ----------------------------------------------------------------
    void StepPattern::clear()
    {
        for (auto& l : lanes) clearSteps (l);
    }

    int StepPattern::activeLanes() const
    {
        int n = 0;
        for (const auto& l : lanes)
            for (int i = 0; i < l.length; ++i)
                if (l.steps[(size_t) i].on) { ++n; break; }
        return n;
    }

    std::string StepPattern::serialise() const
    {
        std::ostringstream o;
        o << "bashstep1\n";
        for (const auto& l : lanes)
        {
            o << (int) l.length << ' ' << (int) l.speed << ' ' << (l.mute ? 1 : 0) << ' ' << (int) l.algo << ' '
              << (int) l.pulses << ' ' << (int) l.rotate << ' ' << l.density << ' ' << l.evolve << ' ' << l.seed << " |";
            for (int i = 0; i < maxSteps; ++i)
            {
                const auto& s = l.steps[(size_t) i];
                if (s == Step{}) continue;
                o << ' ' << i << ':' << (s.on ? 1 : 0) << ',' << (int) s.vel << ',' << (int) s.prob << ',' << (int) s.ratchet << ','
                  << (int) s.shape << ',' << (int) s.cond << ',' << (int) s.nudge;
            }
            o << '\n';
        }
        return o.str();
    }

    bool StepPattern::deserialise (const std::string& text)
    {
        std::istringstream in (text);
        std::string header;
        if (! std::getline (in, header) || header != "bashstep1") return false;
        StepPattern p;
        for (auto& l : p.lanes)
        {
            std::string line;
            if (! std::getline (in, line)) return false;
            const auto bar = line.find ('|');
            if (bar == std::string::npos) return false;
            std::istringstream head (line.substr (0, bar));
            int len, speed, mute, algo, pulses, rotate;
            float density, evolve;
            uint64_t seed;
            if (! (head >> len >> speed >> mute >> algo >> pulses >> rotate >> density >> evolve >> seed)) return false;
            l.length  = (uint8_t) std::clamp (len, 1, maxSteps);
            l.speed   = (uint8_t) std::clamp (speed, 0, (int) laneSpeeds().size() - 1);
            l.mute    = mute != 0;
            l.algo    = (LaneAlgo) std::clamp (algo, 0, (int) LaneAlgo::count - 1);
            l.pulses  = (uint8_t) std::clamp (pulses, 0, maxSteps);
            l.rotate  = (uint8_t) std::clamp (rotate, 0, maxSteps - 1);
            l.density = std::clamp (density, 0.0f, 1.0f);
            l.evolve  = std::clamp (evolve, 0.0f, 1.0f);
            l.seed    = seed;

            std::istringstream steps (line.substr (bar + 1));
            std::string tok;
            while (steps >> tok)
            {
                int i, on, vel, prob, ratchet, shape, cond, nudge;
                char c0, c1, c2, c3, c4, c5, c6;
                std::istringstream t (tok);
                if (! (t >> i >> c0 >> on >> c1 >> vel >> c2 >> prob >> c3 >> ratchet >> c4 >> shape >> c5 >> cond >> c6 >> nudge)) return false;
                if (i < 0 || i >= maxSteps) continue;
                auto& s = l.steps[(size_t) i];
                s.on = on != 0;
                s.vel = (uint8_t) std::clamp (vel, 1, 127);
                s.prob = (uint8_t) std::clamp (prob, 0, 100);
                s.ratchet = (uint8_t) std::clamp (ratchet, 1, 8);
                s.shape = (RatchetShape) std::clamp (shape, 0, (int) RatchetShape::count - 1);
                s.cond = (TrigCond) std::clamp (cond, 0, (int) TrigCond::count - 1);
                s.nudge = (int8_t) std::clamp (nudge, -50, 50);
            }
        }
        *this = p;
        return true;
    }

    // ---- generators --------------------------------------------------------------
    namespace stepgen
    {
        std::vector<bool> euclid (int pulses, int length, int rotate)
        {
            length = std::clamp (length, 1, maxSteps);
            pulses = std::clamp (pulses, 0, length);
            std::vector<bool> on ((size_t) length, false);
            for (int i = 0; i < length; ++i)
                if ((i * pulses) % length < pulses)
                    on[(size_t) (((i + rotate) % length + length) % length)] = true;
            return on;
        }

        void generate (Lane& l, LaneRole role, uint64_t roll)
        {
            const int len = std::clamp ((int) l.length, 1, maxSteps);
            const uint64_t seed = mix64 (l.seed ^ mix64 (roll + 0x6E5Eull));
            const float d = std::clamp (l.density, 0.0f, 1.0f);

            switch (l.algo)
            {
                case LaneAlgo::Euclid:
                {
                    clearSteps (l);
                    const auto e = euclid (l.pulses, len, l.rotate);
                    bool first = true;
                    for (int i = 0; i < len; ++i)
                        if (e[(size_t) i]) { l.steps[(size_t) i] = hit (first ? 118 : velBetween (seed, (uint64_t) i, 85, 105)); first = false; }
                    return;
                }

                case LaneAlgo::Random:
                {
                    clearSteps (l);
                    for (int i = 0; i < len; ++i)
                        if (rnd (seed, (uint64_t) i, sGen) < d)
                            l.steps[(size_t) i] = hit (velBetween (seed, (uint64_t) i, 70, 127));
                    return;
                }

                case LaneAlgo::Groove:
                {
                    clearSteps (l);
                    auto put = [&] (int s16, uint8_t vel) { l.steps[(size_t) at16 (s16, len)] = hit (vel); };
                    auto chance = [&] (uint64_t i, float p) { return rnd (seed, i, sGenExtra) < p; };
                    switch (role)
                    {
                        case LaneRole::Kick:
                            for (int s = 0; s < 16; s += 4) put (s, s == 0 ? 120 : 110);
                            for (int s : { 3, 7, 10, 11, 14 }) if (chance ((uint64_t) s, d * 0.35f)) put (s, 90);
                            break;
                        case LaneRole::Snare:
                        case LaneRole::Clap:
                            put (4, 112); put (12, 116);
                            for (int s : { 2, 7, 9, 10, 15 }) if (chance ((uint64_t) s, d * 0.3f)) put (s, role == LaneRole::Snare ? 45 : 70);
                            if (d > 0.6f) l.steps[(size_t) at16 (15, len)] = hit (80, 2, RatchetShape::Up, TrigCond::Fill);
                            break;
                        case LaneRole::ClosedHat:
                        {
                            const int stride = d > 0.5f ? 1 : 2;
                            for (int s = 0; s < 16; s += stride) put (s, (s % 4 == 2) ? 105 : (s % 2 == 0 ? 85 : 62));
                            for (int s = 1; s < 16; s += 2)
                                if (chance ((uint64_t) (s + 32), d * 0.18f))
                                    l.steps[(size_t) at16 (s, len)] = hit (70, (uint8_t) (2 + uniformInt (seed, (uint64_t) s, sGenRatchet, 2)), RatchetShape::Up);
                            break;
                        }
                        case LaneRole::OpenHat:
                            for (int s : { 2, 6, 10, 14 }) if (s % 8 == 6 || chance ((uint64_t) s, 0.35f + d * 0.5f)) put (s, 92);
                            break;
                        case LaneRole::Tom:
                        {
                            const auto e = euclid (3, 16, (int) uniformInt (seed, 1, sGenExtra, 16));
                            for (int s = 0; s < 16; ++s) if (e[(size_t) s] && chance ((uint64_t) s, 0.4f + d * 0.6f)) put (s, velBetween (seed, (uint64_t) s, 75, 110));
                            break;
                        }
                        case LaneRole::Cymbal:
                            l.steps[0] = hit (110, 1, RatchetShape::Flat, TrigCond::R1of4);
                            if (d > 0.4f) l.steps[(size_t) at16 (14, len)] = hit (90, 1, RatchetShape::Flat, TrigCond::Fill);
                            break;
                        case LaneRole::Perc:
                        case LaneRole::Melodic:
                        default:
                        {
                            const int pulses = 3 + (int) uniformInt (seed, 2, sGenExtra, 4);
                            const auto e = euclid (pulses, 16, (int) uniformInt (seed, 3, sGenExtra, 16));
                            for (int s = 0; s < 16; ++s) if (e[(size_t) s] && chance ((uint64_t) s, 0.5f + d * 0.5f)) put (s, velBetween (seed, (uint64_t) s, 70, 115));
                            break;
                        }
                    }
                    return;
                }

                case LaneAlgo::Rolls:
                {
                    // trap-style: a steady pulse with ratchet rolls, and a big roll at the end on fills
                    clearSteps (l);
                    const int stride = d > 0.7f ? 1 : 2;
                    for (int i = 0; i < len; i += stride)
                    {
                        Step s = hit ((i % 4 == 0) ? 105 : 80);
                        if (rnd (seed, (uint64_t) i, sGenRatchet) < 0.15f + d * 0.4f)
                        {
                            static constexpr uint8_t r[] = { 2, 3, 4, 6 };
                            s.ratchet = r[uniformInt (seed, (uint64_t) i, sGenExtra, 4)];
                            s.shape = rnd (seed, (uint64_t) i, sGenVel) < 0.5f ? RatchetShape::Up : RatchetShape::Down;
                        }
                        l.steps[(size_t) i] = s;
                    }
                    if (len >= 4)
                        for (int i = len - std::max (1, len / 8); i < len; ++i)
                            l.steps[(size_t) i] = hit (100, 4, RatchetShape::Up, TrigCond::Fill);
                    return;
                }

                case LaneAlgo::Mutate:
                {
                    const float amount = 0.1f + d * 0.4f;
                    for (int i = 0; i < len; ++i)
                    {
                        auto& s = l.steps[(size_t) i];
                        if (rnd (seed, (uint64_t) i, sGen) >= amount) continue;
                        if (s.on && rnd (seed, (uint64_t) i, sGenExtra) < 0.5f)
                        {
                            // move the hit to a neighbour
                            const int j = std::clamp (i + (rnd (seed, (uint64_t) i, sGenRatchet) < 0.5f ? -1 : 1), 0, len - 1);
                            if (! l.steps[(size_t) j].on) { l.steps[(size_t) j] = s; s = Step{}; }
                        }
                        else if (s.on) s = Step{};
                        else s = hit (velBetween (seed, (uint64_t) i, 60, 110));
                    }
                    return;
                }

                case LaneAlgo::Humanize:
                default:
                {
                    for (int i = 0; i < len; ++i)
                    {
                        auto& s = l.steps[(size_t) i];
                        if (! s.on) continue;
                        const int dv = (int) ((rnd (seed, (uint64_t) i, sGenVel) * 2.0f - 1.0f) * 25.0f * (0.3f + d));
                        s.vel = (uint8_t) std::clamp ((int) s.vel + dv, 20, 127);
                        s.nudge = (int8_t) std::clamp ((int) ((rnd (seed, (uint64_t) i, sGenExtra) * 2.0f - 1.0f) * 18.0f * d), -50, 50);
                    }
                    return;
                }
            }
        }

        void rollLane (Lane& l, LaneRole role, uint64_t roll)
        {
            const uint64_t s = mix64 (roll ^ 0xD1CEull);
            const int len = std::clamp ((int) l.length, 1, maxSteps);
            switch (l.algo)
            {
                case LaneAlgo::Euclid:
                {
                    const int maxPulses = std::max (1, role == LaneRole::ClosedHat ? len * 3 / 4 : len / 2);
                    l.pulses = (uint8_t) (1 + uniformInt (s, 1, sRollPick, (uint32_t) maxPulses));
                    l.rotate = (uint8_t) uniformInt (s, 2, sRollPick, (uint32_t) len);
                    break;
                }
                case LaneAlgo::Random:   l.density = 0.15f + unitFloat (s, 3, sRollPick) * 0.5f; break;
                default:                 l.density = 0.2f + unitFloat (s, 4, sRollPick) * 0.7f; break;
            }
            l.seed = mix64 (s + 77);
            generate (l, role, roll);
        }
    }

    // ---- playback ----------------------------------------------------------------
    bool StepPlayer::stepPlays (const Lane& l, int laneIndex, int step, int64_t loop, bool fillActive, Step* effective)
    {
        Step s = l.steps[(size_t) step];
        const uint64_t idx = (uint64_t) loop * (uint64_t) maxSteps + (uint64_t) step;

        if (l.evolve > 0.0f && loop > 0)
        {
            // Evolve: each loop a few steps come and go (empty steps half as often), always the same for a given loop
            const float r = unitFloat (l.seed ^ (uint64_t) laneIndex, idx, sEvolve);
            if (s.on && r < l.evolve * 0.25f) s.on = false;
            else if (! s.on && r < l.evolve * 0.12f)
            {
                s = Step{}; s.on = true;
                s.vel = (uint8_t) (60 + uniformInt (l.seed, idx, sEvolveVel, 50));
            }
        }
        if (! s.on) return false;

        bool ok = true;
        switch (s.cond)
        {
            case TrigCond::Always:   break;
            case TrigCond::Fill:     ok = fillActive; break;
            case TrigCond::NotFill:  ok = ! fillActive; break;
            case TrigCond::First:    ok = loop == 0; break;
            case TrigCond::NotFirst: ok = loop > 0; break;
            case TrigCond::R1of2: ok = loop % 2 == 0; break;
            case TrigCond::R2of2: ok = loop % 2 == 1; break;
            case TrigCond::R1of3: ok = loop % 3 == 0; break;
            case TrigCond::R2of3: ok = loop % 3 == 1; break;
            case TrigCond::R3of3: ok = loop % 3 == 2; break;
            case TrigCond::R1of4: ok = loop % 4 == 0; break;
            case TrigCond::R2of4: ok = loop % 4 == 1; break;
            case TrigCond::R3of4: ok = loop % 4 == 2; break;
            case TrigCond::R4of4: ok = loop % 4 == 3; break;
            default: break;
        }
        if (! ok) return false;
        if (s.prob < 100 && unitFloat (l.seed ^ (uint64_t) laneIndex, idx, sProb) * 100.0f >= (float) s.prob) return false;
        if (effective) *effective = s;
        return true;
    }

    namespace
    {
        bool fillAt (double trackPos, const StepContext& c)
        {
            if (c.fill) return true;
            if (c.autoFillBars <= 0) return false;
            const int64_t bar = (int64_t) std::floor (trackPos / std::max (1.0e-9, c.stepsPerQuarter) / 4.0 + 1.0e-9);
            return bar >= 0 && bar % c.autoFillBars == c.autoFillBars - 1;
        }

        double swingDelay (int64_t k, float swing)
        {
            return (swing > 0.0f && (k & 1) != 0) ? std::clamp (swing, 0.0f, 0.75f) * 0.5 : 0.0;
        }

        uint8_t ratchetVel (const Step& s, int r)
        {
            const int n = std::max (1, (int) s.ratchet);
            float g = 1.0f;
            if (s.shape == RatchetShape::Up)   g = 0.35f + 0.65f * (float) (r + 1) / (float) n;
            if (s.shape == RatchetShape::Down) g = 1.0f - 0.65f * (float) r / (float) n;
            return (uint8_t) std::clamp ((int) std::lround (s.vel * g), 1, 127);
        }

        /** Calls emit(trackTime, lane, effectiveStep, ratchetIndex, hitLengthInTrackSteps) for every hit
            whose time falls in [from, to). */
        template <typename Fn>
        void forEachHit (const StepPattern& p, const StepContext& c, double from, double to, Fn&& emit)
        {
            for (int li = 0; li < stepLanes; ++li)
            {
                const auto& l = p.lanes[(size_t) li];
                if (l.mute) continue;
                const int len = std::clamp ((int) l.length, 1, maxSteps);
                const double mult = laneSpeeds()[(size_t) std::clamp ((int) l.speed, 0, (int) laneSpeeds().size() - 1)].second;
                const int64_t kFrom = std::max<int64_t> (0, (int64_t) std::floor (from * mult) - 1);
                const int64_t kTo   = (int64_t) std::floor (to * mult) + 1;
                for (int64_t k = kFrom; k <= kTo; ++k)
                {
                    const int step = (int) (k % len);
                    const int64_t loop = k / len;
                    const double base = (double) k + swingDelay (k, c.swing);
                    Step s;
                    if (! StepPlayer::stepPlays (l, li, step, loop, fillAt (base / mult, c), &s)) continue;
                    const double start = base + s.nudge / 100.0;
                    const int n = std::max (1, (int) s.ratchet);
                    for (int r = 0; r < n; ++r)
                    {
                        const double t = (start + (double) r / n) / mult;
                        if (t < from - 1.0e-9 || t >= to - 1.0e-9) continue;
                        emit (t, li, s, r, std::max (0.02, (double) c.gate / n / mult));
                    }
                }
            }
        }
    }

    void StepPlayer::reset (std::vector<NoteEvent>* out, int sampleOffset)
    {
        if (out != nullptr)
            for (const auto& p : pendingOffs)
            {
                NoteEvent e; e.sampleOffset = sampleOffset; e.noteOn = false; e.note = p.note;
                out->push_back (e);
            }
        pendingOffs.clear();
        started = false;
        lastStep.fill (-1);
    }

    void StepPlayer::process (double posStart, double stepsPerSample, int numSamples,
                              const StepPattern& p, const StepContext& c, std::vector<NoteEvent>& out)
    {
        if (numSamples <= 0) return;
        const size_t first = out.size();
        const double posEnd = posStart + stepsPerSample * numSamples;
        auto offsetAt = [&] (double pos)
        {
            return std::clamp ((int) std::floor ((pos - posStart) / std::max (1.0e-12, stepsPerSample) + 1.0e-6), 0, numSamples - 1);
        };

        if (! started || std::abs (posStart - expectedPos) > std::max (1.0e-6, stepsPerSample * 2.0))
        {
            reset (&out, 0);   // transport jumped: release anything held
            started = true;
        }

        // note-offs that come due in this block
        for (size_t i = 0; i < pendingOffs.size();)
        {
            if (pendingOffs[i].offPos < posEnd)
            {
                NoteEvent e; e.sampleOffset = offsetAt (pendingOffs[i].offPos); e.noteOn = false; e.note = pendingOffs[i].note;
                out.push_back (e);
                pendingOffs.erase (pendingOffs.begin() + (long) i);
            }
            else ++i;
        }

        forEachHit (p, c, posStart, posEnd, [&] (double t, int lane, const Step& s, int r, double length)
        {
            const int note = c.laneNote[(size_t) lane];
            const int off = offsetAt (t);
            if (! c.oneShot)
            {
                // retrigger: close the same note first
                for (size_t i = 0; i < pendingOffs.size(); ++i)
                    if (pendingOffs[i].note == note)
                    {
                        NoteEvent e; e.sampleOffset = off; e.noteOn = false; e.note = note;
                        out.push_back (e);
                        pendingOffs.erase (pendingOffs.begin() + (long) i);
                        break;
                    }
            }
            NoteEvent e;
            e.sampleOffset = off; e.noteOn = true; e.note = note; e.velocity = ratchetVel (s, r);
            e.accent = e.velocity >= 118; e.slice = c.laneSlice[(size_t) lane];
            out.push_back (e);
            if (! c.oneShot)
            {
                const double offPos = t + length;
                if (offPos < posEnd)
                {
                    NoteEvent o; o.sampleOffset = std::max (off, offsetAt (offPos)); o.noteOn = false; o.note = note;
                    out.push_back (o);
                }
                else pendingOffs.push_back ({ offPos, note });
            }
        });

        std::stable_sort (out.begin() + (long) first, out.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.sampleOffset < b.sampleOffset; });

        for (int li = 0; li < stepLanes; ++li)
        {
            const auto& l = p.lanes[(size_t) li];
            const double mult = laneSpeeds()[(size_t) std::clamp ((int) l.speed, 0, (int) laneSpeeds().size() - 1)].second;
            const int64_t k = (int64_t) std::floor (posEnd * mult - 1.0e-9);
            lastStep[(size_t) li] = k < 0 ? -1 : (int) (k % std::clamp ((int) l.length, 1, maxSteps));
        }
        expectedPos = posEnd;
    }

    std::vector<ExportNote> renderStepNotes (const StepPattern& p, const StepContext& c, int64_t numSteps)
    {
        std::vector<ExportNote> notes;
        forEachHit (p, c, 0.0, (double) numSteps, [&] (double t, int lane, const Step& s, int r, double length)
        {
            const double len = c.oneShot ? std::min (0.25, length) : length;
            notes.push_back ({ t, std::min ((double) numSteps, t + len), c.laneNote[(size_t) lane], ratchetVel (s, r), 0 });
        });
        std::sort (notes.begin(), notes.end(), [] (const ExportNote& a, const ExportNote& b) { return a.start < b.start; });
        return notes;
    }
}
