// BASH! factory library: slicer loops and drum kits.
#include "LibraryDsp.h"

#include <cstdio>
#include <map>
#include <mutex>

namespace bash::library
{
    using namespace dsp;

    namespace
    {
        double midiHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

        /** One-shots rendered once and reused by the loops. */
        std::shared_ptr<SampleData> shot (const std::string& name)
        {
            static std::mutex m;
            static std::map<std::string, std::shared_ptr<SampleData>> cache;
            {
                std::lock_guard<std::mutex> g (m);
                auto it = cache.find (name);
                if (it != cache.end()) return it->second;
            }
            const auto* s = findOneShot (name);
            auto d = s != nullptr ? s->render() : std::make_shared<SampleData>();
            std::lock_guard<std::mutex> g (m);
            cache[name] = d;
            return d;
        }

        struct LoopCtx
        {
            double bpm; int bars; float swing = 0.0f;
            double seconds() const { return bars * 4.0 * 60.0 / bpm; }
            double stepSec() const { return 60.0 / bpm / 4.0; }
            size_t frames() const { return (size_t) std::llround (seconds() * SR); }
        };

        /** Place a one-shot by a step string (same letters as the grooves), repeated across the loop. */
        void hits (Buf& out, const LoopCtx& c, const std::string& sound, const std::string& steps, float gain = 1.0f,
                   float pan = 0.0f, float pitchSemis = 0.0f, uint64_t seed = 1)
        {
            auto s = shot (sound);
            if (s == nullptr || s->left.empty() || steps.empty()) return;
            Buf one; one.l = s->left; one.r = s->right;
            if (std::abs (pitchSemis) > 0.01f)
            {
                // simple resample for tuning
                const double rate = std::pow (2.0, pitchSemis / 12.0);
                Buf t ((double) one.n() / rate / SR);
                for (size_t i = 0; i < t.n(); ++i)
                {
                    const double p = i * rate; const size_t i0 = (size_t) p; const float fr = (float) (p - i0);
                    if (i0 + 1 >= one.n()) break;
                    t.l[i] = one.l[i0] + (one.l[i0 + 1] - one.l[i0]) * fr;
                    t.r[i] = one.r[i0] + (one.r[i0 + 1] - one.r[i0]) * fr;
                }
                one = t;
            }
            Rng r (seed);
            const int total = c.bars * 16;
            for (int k = 0; k < total; ++k)
            {
                const char ch = steps[(size_t) k % steps.size()];
                if (ch == '.') continue;
                float g = 1.0f; int ratchet = 1; double nudge = 0.0;
                switch (ch)
                {
                    case 'X': g = 1.25f; break;
                    case 'g': g = 0.32f; break;
                    case 'o': if (r.uni() < 0.5f) continue; g = 0.7f; break;
                    case 'r': ratchet = 2; break;
                    case 't': ratchet = 3; break;
                    case 'R': ratchet = 4; break;
                    case '<': nudge = -0.18; break;
                    case '>': nudge = 0.22; break;
                    default: break;
                }
                const double swing = (k & 1) ? c.swing * 0.5 : 0.0;
                for (int q = 0; q < ratchet; ++q)
                {
                    const double step = k + swing + nudge + (double) q / ratchet;
                    const size_t at = (size_t) std::max (0.0, step * c.stepSec() * SR);
                    const float rg = ch == 'R' ? 0.4f + 0.6f * (q + 1) / ratchet : 1.0f;
                    out.mix (one, at, gain * g * rg, pan);
                }
            }
        }

        /** Wrap anything past the loop end back to the start (seamless loops), then cut to length. */
        void wrap (Buf& b, const LoopCtx& c)
        {
            const size_t len = c.frames();
            b.ensure (len);
            for (size_t i = len; i < b.n(); ++i) { b.l[i % len] += b.l[i]; b.r[i % len] += b.r[i]; }
            b.l.resize (len); b.r.resize (len);
        }

        // ---- melodic voices --------------------------------------------------------
        struct Note { double step, len, midi; float vel = 1.0f; };

        /** Continuous mono synth line: notes with glide, through a filter function. */
        template <typename Osc, typename Cut>
        Mono line (const LoopCtx& c, const std::vector<Note>& notes, Osc&& osc, Cut&& cutoff, float q, double glide, double attack, double release)
        {
            auto x = mono (c.seconds() + release + 0.5);
            Svf f;
            double freq = notes.empty() ? 55.0 : midiHz (notes[0].midi);
            float amp = 0.0f, vel = 1.0f;
            const float aCoef = (float) (1.0 - std::exp (-1.0 / (attack * SR))), rCoef = (float) (1.0 - std::exp (-1.0 / (release * SR)));
            const double gCoef = 1.0 - std::exp (-1.0 / (std::max (0.0005, glide) * SR));
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = tOf (i), step = t / c.stepSec();
                const Note* on = nullptr;
                for (const auto& n : notes) if (step >= n.step && step < n.step + n.len) { on = &n; break; }
                if (on) { freq += (midiHz (on->midi) - freq) * gCoef; vel = on->vel; }
                amp += ((on ? vel : 0.0f) - amp) * (on ? aCoef : rCoef);
                const float s = osc (freq, t);
                f.run (s, cutoff (t, step), q);
                x[i] = f.lp * amp;
            }
            return x;
        }

        Mono organChord (const std::vector<double>& midi, double seconds, double decay)
        {
            auto x = mono (seconds + 0.05);
            for (double m : midi)
            {
                const double f = midiHz (m);
                for (size_t i = 0; i < x.size(); ++i)
                {
                    const double t = tOf (i);
                    const float e = ramp (t, 0.002) * (t < seconds ? env (t, decay) : env (t, decay) * env (t - seconds, 0.01));
                    x[i] += (float) (std::sin (TWO_PI * f * t) * 0.6 + std::sin (TWO_PI * 2 * f * t) * 0.35 + std::sin (TWO_PI * 3 * f * t) * 0.18
                                     + std::sin (TWO_PI * 4 * f * t) * 0.08) * e / (float) midi.size();
                }
            }
            return x;
        }

        Mono epChord (const std::vector<double>& midi, double seconds)
        {
            auto x = mono (seconds + 0.6);
            for (double m : midi)
                addInto (x, fmHit (seconds + 0.6, midiHz (m), 1.0, 1.4, 0.35, 0.9, 0.003), 1.0f / (float) midi.size());
            return x;
        }

        Mono padChord (const std::vector<double>& midi, double seconds, double cutoff)
        {
            auto x = mono (seconds + 1.0);
            for (double m : midi)
                addInto (x, sawStack (seconds + 1.0, midiHz (m), { -0.12, 0.0, 0.11 }, 0.35, 1.0e6), 1.0f / (float) midi.size());
            for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); if (t > seconds) x[i] *= env (t - seconds, 0.35); }
            filter (x, F::LP, cutoff, 0.8f);
            return x;
        }

        /** Formant "voice": a buzzy source through vowel formants. */
        Mono voice (double seconds, double midi, const char* vowel, double attack, double release, uint64_t seed)
        {
            struct V { char c; double f1, f2, f3; };
            static constexpr V vowels[] = { { 'a', 800, 1150, 2900 }, { 'o', 450, 800, 2830 }, { 'e', 400, 2000, 2550 },
                                            { 'i', 300, 2300, 3000 }, { 'u', 325, 700, 2530 } };
            auto x = mono (seconds + release + 0.1);
            Saw s; Rng r (seed);
            Svf f1, f2, f3;
            const size_t nv = std::max<size_t> (1, std::char_traits<char>::length (vowel));
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = tOf (i);
                const double vib = 1.0 + 0.008 * std::sin (TWO_PI * 5.2 * t) * std::min (1.0, t / 0.3);
                const float src = s.next (midiHz (midi) * vib) + r.bi() * 0.05f;
                // move through the vowel string over the note
                const double pos = std::min (0.999, t / std::max (0.05, seconds)) * (double) (nv - 1);
                const size_t a = (size_t) pos, b = std::min (nv - 1, a + 1);
                const double fr = pos - a;
                auto find = [] (char c) { for (const auto& v : vowels) if (v.c == c) return v; return vowels[0]; };
                const V va = find (vowel[a]), vb = find (vowel[b]);
                f1.run (src, va.f1 + (vb.f1 - va.f1) * fr, 6.0f);
                f2.run (src, va.f2 + (vb.f2 - va.f2) * fr, 8.0f);
                f3.run (src, va.f3 + (vb.f3 - va.f3) * fr, 10.0f);
                const float e = ramp (t, attack) * (t < seconds ? 1.0f : env (t - seconds, release));
                x[i] = (f1.bp + 0.6f * f2.bp + 0.25f * f3.bp) * e;
            }
            return x;
        }

        void place (Buf& out, const LoopCtx& c, const Mono& m, double step, float gain = 1.0f, float pan = 0.0f)
        {
            const double swing = ((int) std::floor (step) & 1) ? c.swing * 0.5 : 0.0;
            out.mixMono (m, (size_t) ((step + swing) * c.stepSec() * SR), gain, pan);
        }

        // ---- the loops -------------------------------------------------------------
        std::vector<Loop> build()
        {
            std::vector<Loop> v;
            auto add = [&v] (const char* cat, const std::string& name, double bpm, int bars, float swing, std::function<void (Buf&, const LoopCtx&)> make, float peakDb = -1.5f)
            {
                char t[16]; std::snprintf (t, sizeof t, " %g", bpm);
                v.push_back ({ cat, name + t, bpm, bars, [=]
                {
                    LoopCtx c { bpm, bars, swing };
                    Buf b (c.seconds());
                    make (b, c);
                    wrap (b, c);
                    return toSample (b, peakDb, false);
                } });
            };

            // ===== Breaks =========================================================
            add ("Breaks", "Jungle Break", 170, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Dusty Kick 1",  "x.x.......x.....x.........x..x..", 1.0f);
                hits (b, c, "Snares/Roller Snare 2", "....X..g.g..X..g....X..g.g..X.gX", 0.9f);
                hits (b, c, "Cymbals/Ride Ping",   "x.x.x.x.x.x.x.x.", 0.22f, 0.25f);
                hits (b, c, "Hats/Shuffle Hat",    ".g.g.g.g.g.g.g.g", 0.6f, -0.2f);
                fx (b, FxType::Tape, 0.5f, 0.3f, 0.0f, 1.0f);
                fx (b, FxType::Bitcrush, 0.35f, 0.2f, 0.0f, 0.35f);
                fx (b, FxType::Plate, 0.2f, 0.5f, 0.0f, 0.12f, 0.5);
            });
            add ("Breaks", "Jungle Break Ruff", 172, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Halfstep Kick 3", "x.x.......xx....x.....x...x.....", 1.0f);
                hits (b, c, "Snares/Industrial Snare 1", "....X..g.g..X.g.....X..g..g.X..R", 0.85f);
                hits (b, c, "Hats/Crunch Hat", "xgxgxgxgxgxgxgxg", 0.5f, 0.2f);
                fx (b, FxType::Bitcrush, 0.55f, 0.35f, 0.0f, 0.6f);
                fx (b, FxType::Fuzz, 0.3f, 0.4f, 0.0f, 0.3f);
                fx (b, FxType::Spring, 0.3f, 0.5f, 0.0f, 0.12f, 0.5);
            });
            add ("Breaks", "Roller Break", 174, 2, 0.08f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Roller Kick 2", "x.........x.....x.........x.....", 1.0f);
                hits (b, c, "Snares/Roller Snare 1", "....X.......X.......X..g....X...", 0.9f);
                hits (b, c, "Snares/Ghost Snare 1", "..g....g...g..g.", 0.8f);
                hits (b, c, "Shakers/Shaker 1", "xgxgxgxgxgxgxgxg", 0.7f, 0.15f);
                hits (b, c, "Cymbals/Ride Ping", "..x...x...x...x.", 0.2f, -0.3f);
                fx (b, FxType::Tape, 0.35f, 0.2f, 0.0f, 1.0f);
            });
            add ("Breaks", "Halfstep Break", 140, 2, 0.05f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Halfstep Kick 1", "x..........x....x.........x.....", 1.0f);
                hits (b, c, "Snares/Halfstep Snare 2", "........X...............X.......", 0.9f);
                hits (b, c, "Hats/Hat Closed 2", "..x...x...x...x...x...x...x.x.x.", 0.55f, 0.2f);
                hits (b, c, "Hats/Hat Open 1", "..............x.................", 0.4f, -0.2f);
                hits (b, c, "Snares/Rim Dub", "...........g.................g..", 0.6f);
            });
            add ("Breaks", "2-Step Break", 132, 2, 0.3f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Garage Kick 1", "x.........x.....x......x..x.....", 1.0f);
                hits (b, c, "Snares/Garage Snare 1", "....x.......x.......x.......x...", 0.8f);
                hits (b, c, "Claps/Garage Clap", "....x.......x.......x.......x...", 0.5f);
                hits (b, c, "Hats/Garage Hat", "..x...x...x...x...x...x..xx...x.", 0.6f, 0.2f);
                hits (b, c, "Snares/Rim Garage", "...g..g......g.g", 0.5f, -0.3f);
                hits (b, c, "Shakers/Shaker 2", "x.x.x.x.x.x.x.x.", 0.35f, 0.3f);
            });
            add ("Breaks", "UK Funky Break", 130, 2, 0.15f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Garage Kick 2", "x...x...x...x...", 1.0f);
                hits (b, c, "Claps/Clap Room", "....x.......x...", 0.6f);
                hits (b, c, "Toms/Tom Low", "...x......x.....", 0.6f, -0.2f);
                hits (b, c, "Toms/Tom High", ".....x..x.....x.", 0.5f, 0.3f);
                hits (b, c, "Percussion/Conga Mid", "..x..x...x..x..x", 0.5f, 0.4f);
                hits (b, c, "Shakers/Shaker 1", "xgxgxgxgxgxgxgxg", 0.5f, -0.3f);
            });
            add ("Breaks", "Broken Beat", 125, 2, 0.12f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Broken Kick 1", "x.....x..x......x..x....x.......", 1.0f);
                hits (b, c, "Snares/Garage Snare 2", "....x......x..x.....x.......x..x", 0.8f);
                hits (b, c, "Hats/Hat Closed 1", "..x...x...x...x.", 0.5f, 0.2f);
                hits (b, c, "Percussion/Woodblock Low", ".g..g...g..g....", 0.6f, -0.3f);
            });
            add ("Breaks", "Beat Tape Break", 88, 2, 0.42f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Dusty Kick 2", "x.......x.x.....x......x.x.....", 1.0f);
                hits (b, c, "Snares/Dusty Snare 1", "....x.......x.......x.......x..g", 0.85f);
                hits (b, c, "Hats/Dusty Hat", "x.xgx.xgx.xgx.xg", 0.5f, 0.2f);
                hits (b, c, "Shakers/Shaker Long", "..x...x...x...x.", 0.25f, -0.3f);
                fx (b, FxType::Tape, 0.55f, 0.6f, 0.1f, 1.0f);
                filter (b, F::LP, 9000.0);
                vinyl (b, 900, 1.0f);
            });
            add ("Breaks", "Wonky Break", 92, 2, 0.3f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Dusty Kick 1", "x....>...x.<....x......>..x.....", 1.0f);
                hits (b, c, "Snares/Dusty Snare 2", "....>.......x..<....x.......>...", 0.85f);
                hits (b, c, "Hats/Shuffle Hat", "x.>.x.<.x.>.x.<.", 0.5f, 0.25f);
                hits (b, c, "Claps/Snap", "............x...", 0.4f, -0.3f);
                fx (b, FxType::Tape, 0.5f, 0.8f, 0.2f, 1.0f);
                vinyl (b, 901, 0.7f);
            });
            add ("Breaks", "IDM Break", 160, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Broken Kick 2", "x.x..x.xx..x.x..x...x..x.x..x.x.", 1.0f);
                hits (b, c, "Snares/Roller Snare 3", ".x..x.t..x.xR.x.....x..t.x...R..", 0.7f);
                hits (b, c, "Hats/Tick Hat", "RxrxtxrxrxtxrxRx", 0.45f, 0.3f);
                hits (b, c, "Glitch/Bit Blip", "..x.....o.....x.", 0.4f, -0.4f, 0.0f, 3);
                hits (b, c, "Glitch/Click 1", "o.o.o.o.o.o.o.o.", 0.5f, 0.5f, 0.0f, 4);
                fx (b, FxType::Bitcrush, 0.4f, 0.2f, 0.0f, 0.4f);
            });
            add ("Breaks", "Techno Rumble", 134, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Warehouse Kick 2", "x...x...x...x...", 1.0f);
                hits (b, c, "Percussion/Clank", "x..x..x..x..x..x..x..x..x..x..x.", 0.4f, 0.3f);
                hits (b, c, "Percussion/Metal Hit 1", "..x...x...x..x..", 0.35f, -0.3f);
                hits (b, c, "Hats/Hat Closed 3", "..x...x...x...x.", 0.5f, 0.1f);
            });
            add ("Breaks", "Electro Break", 128, 2, 0.05f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Kicks/Electro Kick 1", "x......x..x.....x..x......x.....", 1.0f);
                hits (b, c, "Claps/Clap Dry", "....x.......x...", 0.7f);
                hits (b, c, "Hats/Hat Closed 1", "xxxxxxxxxxxxxxxx", 0.35f, 0.2f);
                hits (b, c, "Percussion/Zap 1", "......x.......x.", 0.4f, -0.3f);
                hits (b, c, "Toms/Electro Tom High", "....................x.x.........", 0.5f, 0.3f);
            });

            // ===== Bass ===========================================================
            add ("Bass", "Reese", 174, 4, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<Note> notes { { 0, 12, 28 }, { 14, 2, 31 }, { 16, 14, 28 }, { 32, 12, 26 }, { 46, 2, 31 }, { 48, 16, 29 } };
                std::array<Saw, 3> saws;
                auto x = line (c, notes, [&saws] (double f, double t)
                {
                    return 0.45f * (saws[0].next (f * 0.990) + saws[1].next (f * 1.011)) + 0.1f * saws[2].next (f * 2.0) + 0.6f * (float) std::sin (TWO_PI * f * t);
                }, [] (double t, double) { return 380.0 + 260.0 * std::sin (TWO_PI * 0.18 * t); }, 0.9f, 0.04, 0.01, 0.08);
                drive (x, 2.0f);
                b.mixMono (x, 0);
                fx (b, FxType::Chorus, 0.3f, 0.4f, 0.0f, 0.3f);
            });
            add ("Bass", "Reese Dark", 170, 4, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<Note> notes { { 0, 16, 26 }, { 16, 10, 26 }, { 26, 6, 29 }, { 32, 16, 24 }, { 48, 8, 26 }, { 56, 8, 22 } };
                std::array<Saw, 2> saws;
                auto x = line (c, notes, [&saws] (double f, double t)
                {
                    return 0.5f * (saws[0].next (f * 0.986) + saws[1].next (f * 1.014)) + 0.7f * (float) std::sin (TWO_PI * f * t);
                }, [] (double t, double) { return 300.0 + 200.0 * std::sin (TWO_PI * 0.11 * t); }, 1.2f, 0.06, 0.01, 0.1);
                drive (x, 3.5f);
                b.mixMono (x, 0);
                fx (b, FxType::Phaser, 0.3f, 0.5f, 0.0f, 0.4f);
            });
            add ("Bass", "Wobble", 140, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<Note> notes { { 0, 7, 29 }, { 8, 6, 29 }, { 14, 2, 32 }, { 16, 8, 27 }, { 24, 8, 29 } };
                Saw s;
                const double sixteenth = c.stepSec();
                auto x = line (c, notes, [&s] (double f, double t) { return 0.6f * s.next (f) + 0.5f * (float) std::sin (TWO_PI * f * t); },
                               [sixteenth] (double t, double step)
                               {
                                   const double rate = step < 16 ? 2.0 : 4.0;   // 1/8 then 1/16 wobbles
                                   return 150.0 + 2400.0 * std::pow (0.5 + 0.5 * std::sin (TWO_PI * t / (sixteenth * 16.0 / rate) - 1.57), 2.0);
                               }, 3.0f, 0.02, 0.004, 0.03);
                drive (x, 3.0f);
                b.mixMono (x, 0);
            });
            add ("Bass", "Wobble Triplet", 140, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<Note> notes { { 0, 15, 27 }, { 16, 15, 30 } };
                Saw s;
                const double beat = c.stepSec() * 4.0;
                auto x = line (c, notes, [&s] (double f, double t) { return 0.55f * s.next (f) + 0.25f * (float) (std::sin (TWO_PI * f * t) > 0 ? 1 : -1); },
                               [beat] (double t, double) { return 180.0 + 2600.0 * std::pow (0.5 + 0.5 * std::sin (TWO_PI * t / (beat / 3.0) - 1.57), 3.0); }, 4.0f, 0.03, 0.004, 0.03);
                drive (x, 2.5f);
                b.mixMono (x, 0);
            });
            add ("Bass", "Sub Bass", 140, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<Note> notes { { 0, 6, 29 }, { 10, 4, 29 }, { 14, 2, 32 }, { 16, 6, 27 }, { 26, 6, 25 } };
                auto x = line (c, notes, [] (double f, double t) { return (float) std::sin (TWO_PI * f * t); }, [] (double, double) { return 4000.0; }, 0.7f, 0.015, 0.004, 0.05);
                drive (x, 0.8f);
                b.mixMono (x, 0);
            });
            add ("Bass", "Garage Bass", 132, 2, 0.3f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<Note> notes { { 0, 2, 36 }, { 3, 1, 36 }, { 6, 2, 39 }, { 10, 2, 36 }, { 13, 2, 34 }, { 16, 2, 36 }, { 19, 1, 36 }, { 22, 2, 41 }, { 26, 3, 39 } };
                Saw s;
                auto x = line (c, notes, [&s] (double f, double t) { return 0.4f * s.next (f) + 0.7f * (float) std::sin (TWO_PI * f * t); },
                               [] (double, double step) { const double ph = step - std::floor (step); return 300.0 + 1500.0 * std::exp (-ph * 3.0); }, 1.5f, 0.01, 0.003, 0.04);
                drive (x, 1.2f);
                b.mixMono (x, 0);
            });
            add ("Bass", "Bleep Bass", 130, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<Note> notes { { 0, 1, 33 }, { 3, 1, 33 }, { 6, 1, 45 }, { 8, 1, 33 }, { 11, 1, 36 }, { 14, 1, 33 }, { 16, 1, 33 }, { 19, 1, 33 }, { 22, 1, 45 }, { 24, 2, 31 }, { 28, 1, 33 } };
                auto x = line (c, notes, [] (double f, double t) { return (float) (std::sin (TWO_PI * f * t) > 0 ? 0.5 : -0.5) * 0.4f + (float) std::sin (TWO_PI * f * t) * 0.6f; },
                               [] (double, double) { return 1200.0; }, 0.7f, 0.0005, 0.002, 0.015);
                b.mixMono (x, 0);
            });

            // ===== Chords & melodies ==============================================
            add ("Chords", "Garage Organ", 132, 2, 0.3f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<double> cm9 { 60, 63, 67, 70, 74 }, fm9 { 65, 68, 72, 75, 79 }, abmaj { 56, 63, 67, 72 };
                const double st = c.stepSec();
                for (double s : { 0.0, 3.0, 6.0, 10.0 }) place (b, c, organChord (cm9, st * 1.6, 0.4), s, 0.8f);
                for (double s : { 16.0, 19.0, 22.0 }) place (b, c, organChord (fm9, st * 1.6, 0.4), s, 0.8f);
                place (b, c, organChord (abmaj, st * 5.0, 0.6), 26.0, 0.8f);
                fx (b, FxType::Plate, 0.4f, 0.4f, 0.0f, 0.25f, 1.0);
            });
            add ("Chords", "Dub Chords", 140, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<double> gm { 55, 62, 67, 70 }, ebmaj { 51, 58, 63, 67 };
                auto stab = [&c] (const std::vector<double>& ch) { auto x = padChord (ch, c.stepSec() * 0.9, 1800.0); return x; };
                place (b, c, stab (gm), 6, 0.9f);
                place (b, c, stab (gm), 14, 0.7f);
                place (b, c, stab (ebmaj), 22, 0.9f);
                fx (b, FxType::TapeDelay, 0.55f, 0.55f, 0.0f, 0.45f, 0.0, c.bpm);
                fx (b, FxType::Spring, 0.5f, 0.45f, 0.0f, 0.2f);
            });
            add ("Chords", "Rolling Pads", 174, 4, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<std::vector<double>> prog { { 57, 60, 64, 67, 71 }, { 53, 57, 60, 64, 69 }, { 55, 59, 62, 66, 69 }, { 52, 55, 59, 62, 67 } };
                for (int i = 0; i < 4; ++i) place (b, c, padChord (prog[(size_t) i], c.stepSec() * 15.5, 1400.0), i * 16.0, 0.8f);
                fx (b, FxType::Chorus, 0.3f, 0.5f, 0.0f, 0.4f);
                fx (b, FxType::Hall, 0.6f, 0.4f, 0.0f, 0.3f);
            });
            add ("Chords", "Glass Arp", 150, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const double seq[] = { 74, 79, 81, 86, 77 };   // 5-note cycle against 16ths: it never sits still
                for (int k = 0; k < 32; ++k)
                    place (b, c, fmHit (0.8, midiHz (seq[k % 5]), 3.5, 1.8, 0.08, 0.3), k, k % 4 == 0 ? 0.6f : 0.4f, k % 2 ? 0.4f : -0.4f);
                fx (b, FxType::PingPong, 0.3f, 0.45f, 0.0f, 0.3f, 0.0, c.bpm);
                fx (b, FxType::Plate, 0.5f, 0.3f, 0.0f, 0.25f);
            });
            add ("Chords", "Beat Tape Keys", 88, 2, 0.35f, [] (Buf& b, const LoopCtx& c)
            {
                const std::vector<double> dm9 { 50, 57, 60, 64, 65 }, gm7 { 55, 58, 62, 65 }, cmaj9 { 48, 55, 59, 62, 64 };
                place (b, c, epChord (dm9, c.stepSec() * 7), 0, 0.9f);
                place (b, c, epChord (gm7, c.stepSec() * 6), 10, 0.8f);
                place (b, c, epChord (cmaj9, c.stepSec() * 12), 16, 0.9f);
                fx (b, FxType::Tape, 0.4f, 0.9f, 0.3f, 1.0f);
                filter (b, F::LP, 5000.0);
                vinyl (b, 902, 0.6f);
            });
            add ("Chords", "Bleep Melody", 130, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const std::pair<int, double> mel[] = { { 0, 81 }, { 3, 76 }, { 6, 81 }, { 8, 84 }, { 11, 79 }, { 14, 76 }, { 16, 81 }, { 19, 76 }, { 22, 88 }, { 24, 86 }, { 27, 84 }, { 30, 79 } };
                for (auto [s, m] : mel)
                {
                    auto x = squares (0.12, { midiHz (m) }, 0.5, 0.001);
                    for (size_t i = 0; i < x.size(); ++i) if (tOf (i) > 0.07) x[i] *= env (tOf (i) - 0.07, 0.004);
                    filter (x, F::LP, 5000.0);
                    place (b, c, x, s, 0.5f);
                }
                fx (b, FxType::DigitalDelay, 0.4f, 0.35f, 0.0f, 0.25f, 0.0, c.bpm);
            });

            // ===== Vox & textures =================================================
            add ("Vox", "Vox Chops", 140, 2, 0.1f, [] (Buf& b, const LoopCtx& c)
            {
                struct Chop { double step; double len; double midi; const char* v; };
                const Chop chops[] = { { 0, 2, 67, "ao" }, { 3, 1, 70, "e" }, { 6, 3, 72, "aoa" }, { 11, 1, 67, "i" }, { 14, 2, 65, "ou" },
                                       { 16, 2, 67, "ao" }, { 19, 1, 74, "e" }, { 22, 4, 72, "aaoo" }, { 28, 2, 70, "ia" } };
                uint64_t seed = 910;
                for (const auto& ch : chops) place (b, c, voice (c.stepSec() * ch.len, ch.midi, ch.v, 0.006, 0.04, seed++), ch.step, 0.8f);
                fx (b, FxType::Plate, 0.4f, 0.4f, 0.0f, 0.22f);
                fx (b, FxType::PingPong, 0.4f, 0.4f, 0.0f, 0.15f, 0.0, c.bpm);
            });
            add ("Vox", "Vox Pad", 120, 4, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                const double bar = c.stepSec() * 16;
                const std::vector<double> notes { 57, 64, 69, 72 };
                uint64_t seed = 920;
                for (int i = 0; i < 2; ++i)
                    for (double m : notes) place (b, c, voice (bar * 2 - 0.2, m + (i == 1 && m == 72 ? -1 : 0), i == 0 ? "aoou" : "eaao", 0.6, 0.6, seed++), i * 32.0, 0.35f);
                fx (b, FxType::Chorus, 0.4f, 0.5f, 0.0f, 0.4f);
                fx (b, FxType::Hall, 0.7f, 0.4f, 0.0f, 0.35f);
            });
            add ("Textures", "Tape Atmos", 120, 4, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                auto n = noise (c.seconds() + 1, 930, 1.0e6, 0.5);
                sweep (n, F::BP, [] (double t) { return 900.0 + 600.0 * std::sin (TWO_PI * 0.07 * t); }, 0.8f);
                b.mixMono (n, 0, 0.25f);
                place (b, c, padChord ({ 45, 52, 57, 60, 64 }, c.seconds() - 0.5, 900.0), 0, 0.7f);
                fx (b, FxType::Tape, 0.4f, 1.0f, 0.5f, 1.0f);
                fx (b, FxType::Hall, 0.8f, 0.5f, 0.0f, 0.4f);
            }, -3.0f);
            add ("Textures", "Vinyl Atmos", 90, 4, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                vinyl (b, 931, 2.5f);
                place (b, c, epChord ({ 53, 60, 64, 67, 69 }, c.seconds() * 0.45), 0, 0.5f);
                place (b, c, epChord ({ 50, 57, 60, 65 }, c.seconds() * 0.45), 32, 0.5f);
                fx (b, FxType::Tape, 0.4f, 0.8f, 0.2f, 1.0f);
                filter (b, F::LP, 4500.0);
            }, -3.0f);
            add ("Textures", "Granular Drone", 100, 4, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                Rng r (932);
                for (int g = 0; g < 260; ++g)
                {
                    const double step = r.uni() * 64.0;
                    const double m = 57 + (double) (std::array<int, 6> { 0, 3, 7, 10, 12, 15 }[(size_t) (r.uni() * 5.99f)]);
                    auto grain = fmHit (0.35, midiHz (m), 2.0, 0.6, 0.2, 0.12, 0.06);
                    place (b, c, grain, step, 0.12f, r.bi() * 0.8f);
                }
                fx (b, FxType::Shimmer, 0.5f, 0.4f, 0.2f, 0.4f);
            }, -3.0f);
            add ("Textures", "Metal Perc Loop", 130, 2, 0.0f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Percussion/Metal Hit 2", "x..x..x..x..x..x..x..x..x..x..x.", 0.6f, 0.3f);
                hits (b, c, "Percussion/Glass 1", "....x......x....", 0.5f, -0.4f);
                hits (b, c, "Percussion/Tok", "x.xx.x.xx.x.x.xx", 0.5f, 0.0f);
                hits (b, c, "Percussion/Clank", "x....x....x....x....x....x....x.", 0.3f, -0.2f);
            });
            add ("Textures", "Shaker Loop", 132, 2, 0.3f, [] (Buf& b, const LoopCtx& c)
            {
                hits (b, c, "Shakers/Shaker 1", "xgxgXgxgxgxgXgxg", 0.8f, -0.2f);
                hits (b, c, "Shakers/Tambourine", "....x.......x...", 0.4f, 0.3f);
                hits (b, c, "Shakers/Cabasa", "..x...x...x...x.", 0.5f, 0.2f);
            });

            return v;
        }

        // ---- kits ------------------------------------------------------------------
        KitPad P (const char* s, float tune = 0, float db = 0, float pan = 0, int choke = 0) { return { s, tune, db, pan, choke }; }

        std::vector<Kit> buildKits()
        {
            // Lane order (drumLaneRole): 1 kick, 2 snare, 3 clap, 4 closed hat, 5 open hat, 6-8 perc,
            // 9-10 toms, 11-12 perc, 13 cymbal, 14 perc, 15 second kick / sub, 16 cymbal / FX
            return {
                { "Halfstep Pressure", "Dubstep", 140, {
                    P ("Kicks/Halfstep Kick 1"), P ("Snares/Halfstep Snare 2"), P ("Claps/Big Clap", 0, -3), P ("Hats/Hat Closed 2", 0, -4, 0.15f, 1),
                    P ("Hats/Hat Open 2", 0, -5, 0.15f, 1), P ("Snares/Rim Dub", 0, -3, -0.3f), P ("Percussion/Metal Hit 1", 0, -5, 0.3f), P ("Shakers/Shaker 1", 0, -6, -0.2f),
                    P ("Toms/Tom Low"), P ("Toms/Dub Tom"), P ("Percussion/Woodblock Low", 0, -4), P ("FX/Horn Stab", 0, -4),
                    P ("Cymbals/Crash", 0, -6), P ("FX/Laser", 0, -6), P ("Kicks/Sub Kick F"), P ("FX/Sub Drop") } },
                { "2-Step Shuffle", "UK Garage", 132, {
                    P ("Kicks/Garage Kick 1"), P ("Snares/Garage Snare 1"), P ("Claps/Garage Clap", 0, -2), P ("Hats/Garage Hat", 0, -4, 0.2f, 1),
                    P ("Hats/Garage Open Hat", 0, -5, 0.2f, 1), P ("Snares/Rim Garage", 0, -3, -0.25f), P ("Shakers/Shaker 2", 0, -6, 0.3f), P ("Percussion/Woodblock High", 0, -5, -0.3f),
                    P ("Toms/Tom Mid", 0, -2), P ("Toms/Tom High", 0, -2), P ("Percussion/Bongo High", 0, -4, 0.3f), P ("Claps/Snap", 0, -3),
                    P ("Cymbals/Splash", 0, -6), P ("Shakers/Tambourine", 0, -6), P ("Kicks/Garage Kick 3"), P ("FX/Reverse Swell", 0, -4) } },
                { "Rollers", "Drum & Bass", 174, {
                    P ("Kicks/Roller Kick 2"), P ("Snares/Roller Snare 1"), P ("Claps/Clap Room", 0, -3), P ("Hats/Hat Closed 1", 0, -4, 0.2f, 1),
                    P ("Hats/Hat Open 1", 0, -5, 0.2f, 1), P ("Snares/Ghost Snare 1"), P ("Shakers/Shaker 1", 0, -5, -0.2f), P ("Cymbals/Ride Ping", 0, -7, 0.3f),
                    P ("Toms/Tom Low"), P ("Toms/Tom High"), P ("Percussion/Tok", 0, -4), P ("Snares/Rim Crack", 0, -3),
                    P ("Cymbals/Crash", 0, -6), P ("FX/Reese Stab", 0, -3), P ("Kicks/Sub Kick G"), P ("FX/Noise Riser", 0, -5) } },
                { "Jungle Ruff", "Jungle", 170, {
                    P ("Kicks/Dusty Kick 1"), P ("Snares/Roller Snare 2"), P ("Snares/Industrial Snare 1", 0, -3), P ("Hats/Crunch Hat", 0, -4, 0.2f, 1),
                    P ("Hats/Dusty Open Hat", 0, -5, 0.2f, 1), P ("Snares/Ghost Snare 2"), P ("Percussion/Timbale High", 0, -4, 0.3f), P ("Percussion/Timbale Low", 0, -4, -0.3f),
                    P ("Toms/Tom Low"), P ("Toms/Tom Mid"), P ("Cymbals/Ride Ping", 0, -6), P ("FX/Horn Stab", 0, -4),
                    P ("Cymbals/China", 0, -6), P ("FX/Dub Siren", 0, -6), P ("Kicks/Sub Kick A"), P ("FX/Reese Stab", 0, -3) } },
                { "Bleep & Bass", "UK Bass", 130, {
                    P ("Kicks/Garage Kick 2"), P ("Snares/Rim Crack"), P ("Claps/Clap Dry", 0, -2), P ("Hats/Tick Hat", 0, -3, 0.2f, 1),
                    P ("Hats/Hat Open 1", 0, -5, 0.2f, 1), P ("Percussion/Bleep High", 0, -3, 0.3f), P ("Percussion/Bleep Low", 0, -3, -0.3f), P ("Percussion/Clave", 0, -5),
                    P ("Toms/Electro Tom Low"), P ("Toms/Electro Tom High"), P ("Percussion/Cowbell", 0, -5), P ("Percussion/Zap 1", 0, -4),
                    P ("Cymbals/Ride Ping", 0, -7), P ("Glitch/Bit Blip", 0, -5), P ("Kicks/Sub Kick A#"), P ("FX/Bass Hit", 0, -3) } },
                { "Warehouse", "Techno", 134, {
                    P ("Kicks/Warehouse Kick 1"), P ("Snares/Industrial Snare 2"), P ("Claps/Clap Room", 0, -3), P ("Hats/Hat Closed 3", 0, -4, 0.15f, 1),
                    P ("Hats/Trash Hat", 0, -6, 0.15f, 1), P ("Percussion/Clank", 0, -3, 0.3f), P ("Percussion/Metal Hit 2", 0, -4, -0.3f), P ("Shakers/Shaker Long", 0, -6),
                    P ("Toms/Tom Low", -3), P ("Percussion/Log Drum", 0, -3), P ("Percussion/Bucket", 0, -3), P ("Glitch/Radio Static", 0, -8),
                    P ("Cymbals/Ride Wash", 0, -7), P ("Glitch/Ring Snap", 0, -5), P ("Kicks/Warehouse Kick 3"), P ("FX/Impact", 0, -3) } },
                { "Broken Electro", "Electro", 128, {
                    P ("Kicks/Electro Kick 1"), P ("Snares/Garage Snare 2"), P ("Claps/Clap Dry", 0, -2), P ("Hats/Hat Closed 1", 0, -5, 0.2f, 1),
                    P ("Hats/Hat Open 3", 0, -6, 0.2f, 1), P ("Percussion/Zap 2", 0, -4, 0.3f), P ("Percussion/Cowbell", 0, -5, -0.3f), P ("Kicks/Broken Kick 1"),
                    P ("Toms/Electro Tom Low"), P ("Toms/Electro Tom High"), P ("Percussion/Clave", 0, -5), P ("FX/Laser", 0, -5),
                    P ("Cymbals/Crash", 0, -7), P ("Glitch/Data Burst", 0, -6), P ("Kicks/Electro Kick 2"), P ("FX/Bass Hit", 0, -3) } },
                { "Glass Garden", "Percussive / Experimental", 140, {
                    P ("Kicks/Broken Kick 2"), P ("Snares/Rim Wood"), P ("Claps/Snap", 0, -2), P ("Hats/Tick Hat", 0, -3, 0.25f, 1),
                    P ("Hats/Hat Open 1", 0, -6, 0.25f, 1), P ("Percussion/Glass 1", 0, -3, -0.35f), P ("Percussion/Glass 2", 0, -3, 0.35f), P ("Percussion/Metal Hit 1", 0, -4),
                    P ("Percussion/Log Drum"), P ("Percussion/Pitch Drum"), P ("Percussion/Woodblock High", 0, -4, 0.3f), P ("Percussion/Tok", 0, -3, -0.3f),
                    P ("Cymbals/Ride Ping", 0, -6), P ("Glitch/Click 1", 0, -4), P ("Kicks/Sub Kick G"), P ("FX/Whoosh", 0, -5) } },
                { "Beat Tape", "Wonky / Lo-fi", 88, {
                    P ("Kicks/Dusty Kick 2"), P ("Snares/Dusty Snare 1"), P ("Claps/Dusty Clap", 0, -2), P ("Hats/Dusty Hat", 0, -4, 0.2f, 1),
                    P ("Hats/Dusty Open Hat", 0, -5, 0.2f, 1), P ("Snares/Rim Wood", 0, -3), P ("Shakers/Shaker Long", 0, -6), P ("Percussion/Conga Low", 0, -4),
                    P ("Toms/Tom Low", -2), P ("Toms/Tom Mid", -2), P ("Glitch/Vinyl Crackle", 0, -6), P ("Glitch/Tape Flutter Hit", 0, -4),
                    P ("Cymbals/Ride Wash", 0, -8), P ("Glitch/Pop", 0, -4), P ("Kicks/Dusty Kick 1"), P ("FX/Tape Stop", 0, -4) } },
                { "Glitch Lab", "IDM", 160, {
                    P ("Kicks/Broken Kick 1"), P ("Snares/Roller Snare 3"), P ("Claps/Clap Dry", 0, -3), P ("Hats/Micro Hat" /* placeholder replaced below */),
                    P ("Hats/Glitch Hat", 0, -5, 0.2f, 1), P ("Glitch/Bit Blip", 0, -4, 0.3f), P ("Glitch/Click 2", 0, -4, -0.3f), P ("Glitch/Digital Error", 0, -6),
                    P ("Glitch/Granular Crunch", 0, -3), P ("Glitch/Ring Snap", 0, -3), P ("Glitch/Data Burst", 0, -5), P ("Percussion/Zap 2", 0, -4),
                    P ("Cymbals/Splash", 0, -7), P ("Glitch/Click 1", 0, -4), P ("Kicks/Broken Kick 2"), P ("FX/Laser", 0, -5) } },
                { "Sub Weight", "Deep / 808", 140, {
                    P ("Kicks/Sub Kick F"), P ("Snares/Halfstep Snare 1"), P ("Claps/Wide Clap", 0, -2), P ("Hats/Hat Closed 2", 0, -5, 0.15f, 1),
                    P ("Hats/Hat Open 2", 0, -6, 0.15f, 1), P ("Snares/Rim Dub", 0, -3), P ("Percussion/Woodblock Low", 0, -4), P ("Shakers/Shaker 2", 0, -6),
                    P ("Kicks/Sub Kick G"), P ("Kicks/Sub Kick A"), P ("Kicks/Sub Kick A#"), P ("Kicks/Sub Kick F", 12),
                    P ("Cymbals/Crash", 0, -7), P ("FX/Sub Drop"), P ("Kicks/Halfstep Kick 2"), P ("FX/Impact", 0, -3) } },
                { "Funky Percs", "UK Funky", 130, {
                    P ("Kicks/Garage Kick 2"), P ("Snares/Garage Snare 2"), P ("Claps/Clap Room", 0, -2), P ("Hats/Hat Closed 1", 0, -5, 0.2f, 1),
                    P ("Hats/Garage Open Hat", 0, -6, 0.2f, 1), P ("Percussion/Conga High", 0, -3, 0.3f), P ("Percussion/Conga Mid", 0, -3, -0.2f), P ("Shakers/Shaker 1", 0, -6),
                    P ("Toms/Tom Low"), P ("Toms/Tom High"), P ("Percussion/Bongo High", 0, -3, 0.35f), P ("Percussion/Bongo Low", 0, -3, -0.35f),
                    P ("Cymbals/Splash", 0, -7), P ("Percussion/Cowbell", 0, -5), P ("Kicks/Garage Kick 3"), P ("Shakers/Tambourine", 0, -6) } },
            };
        }
    }

    const std::vector<Loop>& loops()
    {
        static const std::vector<Loop> list = build();
        return list;
    }

    const std::vector<Kit>& kits()
    {
        static const std::vector<Kit> list = []
        {
            auto k = buildKits();
            for (auto& kit : k)   // Glitch Lab's closed hat: the micro hat lives in Glitch
                for (auto& p : kit.pads) if (p.sound == "Hats/Micro Hat") p = KitPad { "Glitch/Micro Hat", 0, -3, 0.2f, 1 };
            const std::pair<const char*, const char*> starters[] = {
                { "Halfstep Pressure", "Halfstep Classic" }, { "2-Step Shuffle", "2-Step Classic" }, { "Rollers", "Two-Step Roller" },
                { "Jungle Ruff", "Break Chop" }, { "Bleep & Bass", "Bleep Stepper" }, { "Warehouse", "Warehouse Rumble" },
                { "Broken Electro", "Broken Electro" }, { "Glass Garden", "Tribal Halfstep" }, { "Beat Tape", "Lo-Fi Head Nod" },
                { "Glitch Lab", "Glitch Ratchets" }, { "Sub Weight", "Deep Halfstep" }, { "Funky Percs", "Funky Percussion" } };
            for (auto& kit : k)
                for (auto [kitName, groove] : starters) if (kit.name == kitName) kit.groove = groove;
            return k;
        }();
        return list;
    }
}
