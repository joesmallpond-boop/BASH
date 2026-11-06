// BASH! factory library: drum one-shots. Each one is a small synthesis recipe.
#include "LibraryDsp.h"

#include <map>
#include <mutex>

namespace bash::library
{
    using namespace dsp;

    void finish (SampleData& d, float peakDb, bool trimTail)
    {
        if (d.left.empty()) return;
        if (d.right.size() != d.left.size()) d.right = d.left;
        // DC blocker (distortion can leave an offset)
        for (auto* ch : { &d.left, &d.right })
        {
            float x1 = 0.0f, y1 = 0.0f;
            const float R = 1.0f - (float) (TWO_PI * 12.0 / SR);
            for (auto& v : *ch) { const float y = v - x1 + R * y1; x1 = v; y1 = y; v = y; }
        }
        float peak = 1.0e-9f;
        for (size_t i = 0; i < d.left.size(); ++i) peak = std::max ({ peak, std::abs (d.left[i]), std::abs (d.right[i]) });
        const float g = std::pow (10.0f, peakDb / 20.0f) / peak;
        for (auto& v : d.left) v *= g;
        for (auto& v : d.right) v *= g;
        if (trimTail)
        {
            const float floor = std::pow (10.0f, -66.0f / 20.0f);
            size_t last = 0;
            for (size_t i = 0; i < d.left.size(); ++i)
                if (std::abs (d.left[i]) > floor || std::abs (d.right[i]) > floor) last = i;
            const size_t len = std::min (d.left.size(), last + (size_t) (0.01 * SR));
            d.left.resize (std::max<size_t> (len, 64)); d.right.resize (d.left.size());
        }
        const size_t fade = std::min<size_t> (d.left.size() / 4, (size_t) (0.008 * SR));
        for (size_t i = 0; i < fade; ++i)
        {
            const float f = (float) i / (float) fade;
            d.left[d.left.size() - 1 - i] *= f;
            d.right[d.right.size() - 1 - i] *= f;
        }
    }

    namespace
    {
        // ---- building blocks for drums ----------------------------------------------
        struct KickSpec
        {
            double f0 = 160, f1 = 52, pitchTau = 0.03, ampTau = 0.3, length = 0.9;
            float click = 0.4f, drive = 1.5f, sub = 0.0f;   // sub: extra sustained sine
            double subTau = 0.6;
            uint64_t seed = 1;
        };

        Mono kickMono (const KickSpec& k)
        {
            auto x = sweptSine (k.length, k.f0, k.f1, k.pitchTau, k.ampTau, 0.0008);
            if (k.sub > 0.0f) addInto (x, sweptSine (k.length, k.f1 * 1.02, k.f1, 0.05, k.subTau, 0.004), k.sub);
            if (k.click > 0.0f)
            {
                auto c = noise (0.02, k.seed, 0.0025, 0.0001);
                filter (c, F::HP, 1800.0);
                addInto (x, c, k.click * 0.9f);
                addInto (x, sweptSine (0.02, 3200, 900, 0.002, 0.003, 0.0001), k.click * 0.35f);
            }
            drive (x, k.drive);
            return x;
        }

        struct SnareSpec
        {
            double body = 200, bodyDrop = 1.25, bodyTau = 0.07, noiseTau = 0.16, length = 1.2;
            double noiseHp = 1200, noiseBp = 4000;
            float bodyLevel = 0.8f, noiseLevel = 0.75f, drive = 1.2f, snap = 0.3f;
            uint64_t seed = 7;
        };

        Mono snareMono (const SnareSpec& s)
        {
            auto x = sweptSine (s.length, s.body * s.bodyDrop, s.body, 0.012, s.bodyTau, 0.0005);
            addInto (x, sweptSine (s.length, s.body * 1.6 * s.bodyDrop, s.body * 1.6, 0.01, s.bodyTau * 0.6), 0.35f);
            for (auto& v : x) v *= s.bodyLevel;
            auto n = noise (s.length, s.seed, s.noiseTau, 0.0004);
            filter (n, F::HP, s.noiseHp, 0.6f);
            auto bp = n; filter (bp, F::BP, s.noiseBp, 0.7f);
            addInto (x, n, s.noiseLevel * 0.6f);
            addInto (x, bp, s.noiseLevel * 0.7f);
            if (s.snap > 0.0f)
            {
                auto c = noise (0.015, s.seed + 1, 0.002, 0.0001);
                filter (c, F::HP, 3000.0);
                addInto (x, c, s.snap);
            }
            drive (x, s.drive);
            return x;
        }

        Mono clapMono (double bp, double tailTau, int bursts, double spacing, uint64_t seed, double q = 1.4)
        {
            auto x = mono (0.9);
            Rng r (seed);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = tOf (i);
                float e = 0.0f;
                for (int b = 0; b < bursts; ++b)
                {
                    const double tb = t - b * spacing * (0.85 + 0.3 * (b % 2));
                    if (tb >= 0.0) e = std::max (e, env (tb, b == bursts - 1 ? tailTau : 0.0045f) * (b == bursts - 1 ? 1.0f : 0.85f));
                }
                x[i] = r.bi() * e;
            }
            filter (x, F::BP, bp, (float) q);
            filter (x, F::HP, 500.0);
            drive (x, 1.6f);   // denser, punchier clap
            return x;
        }

        Mono hatMono (double base, double tau, double hp, uint64_t seed, float noiseMix = 0.35f, double attack = 0.0003)
        {
            auto m = metal (tau * 7.0 + 0.05, base, tau, hp, seed, attack);
            auto n = noise (tau * 7.0 + 0.05, seed + 99, tau * 0.8, attack);
            filter (n, F::HP, hp * 1.1, 0.7f);
            addInto (m, n, noiseMix);
            return m;
        }

        Mono pitchedPerc (double f, double drop, double tau, double noiseAmt, double noiseBp, uint64_t seed, double len = 0.6)
        {
            auto x = sweptSine (len, f * drop, f, 0.008, tau, 0.0004);
            addInto (x, sweptSine (len, f * 2.7 * drop, f * 2.7, 0.006, tau * 0.35), 0.18f);
            if (noiseAmt > 0)
            {
                auto n = noise (len, seed, 0.012, 0.0002);
                filter (n, F::BP, noiseBp, 1.0f);
                addInto (x, n, (float) noiseAmt);
            }
            return x;
        }

        // ---- the list ----------------------------------------------------------------
        std::vector<OneShot> build()
        {
            std::vector<OneShot> v;
            auto add = [&v] (const char* cat, const std::string& name, std::function<Buf()> make, float peakDb = -1.0f, bool trim = true)
            {
                v.push_back ({ cat, name, [make, peakDb, trim] { auto b = make(); return toSample (b, peakDb, trim); } });
            };

            // ===== Kicks ==========================================================
            const std::pair<const char*, double> subNotes[] = { { "F", 43.65 }, { "G", 49.0 }, { "A", 55.0 }, { "A#", 58.27 } };
            for (auto [note, f] : subNotes)
                add ("Kicks", std::string ("Sub Kick ") + note, [f = f]
                {
                    KickSpec k; k.f0 = f * 3.6; k.f1 = f; k.pitchTau = 0.028; k.ampTau = 0.12; k.length = 2.4;
                    k.sub = 1.0f; k.subTau = 0.85; k.click = 0.25f; k.drive = 1.4f;
                    return Buf (kickMono (k));
                });
            for (int i = 0; i < 3; ++i)
                add ("Kicks", "Halfstep Kick " + std::to_string (i + 1), [i]
                {
                    KickSpec k; k.f0 = 190 - i * 15; k.f1 = 46 + i * 5; k.pitchTau = 0.034; k.ampTau = 0.32 + i * 0.06; k.length = 1.1;
                    k.click = 0.55f; k.drive = 2.2f + i * 0.9f; k.seed = 10 + (uint64_t) i;
                    Buf b (kickMono (k));
                    if (i == 2) fx (b, FxType::Tape, 0.6f, 0.1f, 0.0f, 1.0f);
                    return b;
                });
            for (int i = 0; i < 3; ++i)
                add ("Kicks", "Roller Kick " + std::to_string (i + 1), [i]
                {
                    KickSpec k; k.f0 = 230 + i * 20; k.f1 = 60 + i * 4; k.pitchTau = 0.024; k.ampTau = 0.15 + i * 0.02; k.length = 0.6;
                    k.click = 0.85f; k.drive = 2.0f + i * 0.5f; k.seed = 20 + (uint64_t) i;
                    Buf b (kickMono (k));
                    filter (b, F::HP, 32.0);
                    return b;
                });
            for (int i = 0; i < 3; ++i)
                add ("Kicks", "Garage Kick " + std::to_string (i + 1), [i]
                {
                    KickSpec k; k.f0 = 150 + i * 10; k.f1 = 56 + i * 3; k.pitchTau = 0.03; k.ampTau = 0.2 + i * 0.04; k.length = 0.7;
                    k.click = 0.35f; k.drive = 1.3f; k.seed = 30 + (uint64_t) i;
                    Buf b (kickMono (k));
                    fx (b, FxType::Tube, 0.4f, 0.4f, 0.0f, 0.6f);
                    return b;
                });
            for (int i = 0; i < 3; ++i)
                add ("Kicks", "Warehouse Kick " + std::to_string (i + 1), [i]
                {
                    KickSpec k; k.f0 = 150; k.f1 = 48 + i * 3; k.pitchTau = 0.03; k.ampTau = 0.28; k.length = 1.4;
                    k.click = 0.6f; k.drive = 3.0f; k.seed = 40 + (uint64_t) i;
                    Buf dry (kickMono (k));
                    // rumble: the kick through a dark hall, low-passed and driven, under the dry hit
                    Buf wet = dry;
                    fx (wet, FxType::Hall, 0.45f + i * 0.1f, 0.6f, 0.0f, 1.0f, 1.2);
                    filter (wet, F::LP, 160.0 + i * 30, 0.9f);
                    drive (wet, 3.0f + i);
                    for (size_t j = 0; j < wet.n(); ++j) wet.l[j] = wet.r[j] = 0.5f * (wet.l[j] + wet.r[j]);   // low end stays mono
                    Buf b (1.6);
                    b.mix (dry, 0, 1.0f);
                    b.mix (wet, 0, 0.55f);
                    return b;
                });
            for (int i = 0; i < 2; ++i)
                add ("Kicks", "Dusty Kick " + std::to_string (i + 1), [i]
                {
                    KickSpec k; k.f0 = 115; k.f1 = 50 + i * 6; k.pitchTau = 0.035; k.ampTau = 0.24; k.length = 0.7; k.click = 0.3f; k.drive = 1.2f;
                    Buf b (kickMono (k));
                    fx (b, FxType::Bitcrush, 0.42f + i * 0.1f, 0.22f, 0.0f, 0.8f);
                    fx (b, FxType::Tape, 0.55f, 0.45f, 0.0f, 1.0f);
                    filter (b, F::LP, 5200.0);
                    vinyl (b, 50 + (uint64_t) i, 0.6f);
                    return b;
                });
            for (int i = 0; i < 2; ++i)
                add ("Kicks", "Electro Kick " + std::to_string (i + 1), [i]
                {
                    KickSpec k; k.f0 = 95 + i * 20; k.f1 = 47 + i * 4; k.pitchTau = 0.05; k.ampTau = 0.55 - i * 0.15; k.length = 1.5; k.click = 0.1f; k.drive = 1.0f;
                    return Buf (kickMono (k));
                });
            for (int i = 0; i < 2; ++i)
                add ("Kicks", "Broken Kick " + std::to_string (i + 1), [i]
                {
                    KickSpec k; k.f0 = 320; k.f1 = 70 + i * 10; k.pitchTau = 0.012; k.ampTau = 0.085 + i * 0.03; k.length = 0.35; k.click = 1.0f; k.drive = 2.5f;
                    auto x = kickMono (k);
                    addInto (x, fmHit (0.08, 140 + i * 30, 1.41, 3.0, 0.01, 0.02), 0.25f);
                    return Buf (x);
                });

            // ===== Snares =========================================================
            for (int i = 0; i < 3; ++i)
                add ("Snares", "Halfstep Snare " + std::to_string (i + 1), [i]
                {
                    SnareSpec s; s.body = 185 + i * 15; s.bodyTau = 0.08; s.noiseTau = 0.17 + i * 0.03; s.noiseBp = 3200 + i * 600; s.drive = 1.4f + i * 0.6f; s.seed = 100 + (uint64_t) i;
                    Buf b (snareMono (s));
                    if (i == 2) fx (b, FxType::HardClip, 0.4f, 0.3f, 0.0f, 0.7f);
                    fx (b, FxType::Plate, 0.45f + i * 0.08f, 0.45f, 0.0f, 0.3f, 1.2);
                    return b;
                });
            for (int i = 0; i < 3; ++i)
                add ("Snares", "Roller Snare " + std::to_string (i + 1), [i]
                {
                    SnareSpec s; s.body = 230 + i * 20; s.bodyTau = 0.05; s.noiseTau = 0.11 + i * 0.02; s.noiseHp = 1800; s.noiseBp = 5000;
                    s.noiseLevel = 0.95f; s.snap = 0.6f; s.drive = 2.4f; s.seed = 110 + (uint64_t) i;
                    Buf b (snareMono (s));
                    fx (b, FxType::Plate, 0.15f, 0.5f, 0.0f, 0.16f, 0.5);
                    return b;
                });
            for (int i = 0; i < 2; ++i)
                add ("Snares", "Garage Snare " + std::to_string (i + 1), [i]
                {
                    SnareSpec s; s.body = 255 + i * 25; s.bodyTau = 0.045; s.noiseTau = 0.09; s.noiseHp = 2500; s.noiseBp = 6500; s.snap = 0.5f; s.drive = 1.6f; s.seed = 120 + (uint64_t) i;
                    Buf b (snareMono (s));
                    fx (b, FxType::Spring, 0.3f, 0.4f, 0.0f, 0.12f, 0.5);
                    return b;
                });
            for (int i = 0; i < 4; ++i)
            {
                static const char* rimNames[] = { "Rim Garage", "Rim Wood", "Rim Dub", "Rim Crack" };
                add ("Snares", rimNames[i], [i]
                {
                    auto x = sweptSine (0.25, 1750 + i * 150, 1650 + i * 120, 0.002, 0.011, 0.0001);
                    addInto (x, sweptSine (0.25, 520 - i * 30, 450 - i * 30, 0.004, 0.022), 0.7f);
                    auto c = noise (0.03, 130 + (uint64_t) i, 0.004, 0.0001);
                    filter (c, F::BP, 3500.0, 1.2f);
                    addInto (x, c, 0.6f);
                    drive (x, i == 3 ? 2.5f : 0.8f);
                    Buf b (x);
                    if (i == 2) fx (b, FxType::TapeDelay, 0.35f, 0.45f, 0.0f, 0.35f, 1.6, 140.0);
                    return b;
                });
            }
            for (int i = 0; i < 2; ++i)
                add ("Snares", "Dusty Snare " + std::to_string (i + 1), [i]
                {
                    SnareSpec s; s.body = 205 + i * 30; s.bodyTau = 0.07; s.noiseTau = 0.2; s.noiseHp = 900; s.noiseBp = 2600; s.snap = 0.1f; s.drive = 1.0f; s.seed = 140 + (uint64_t) i;
                    Buf b (snareMono (s));
                    fx (b, FxType::Bitcrush, 0.45f, 0.3f, 0.0f, 0.7f);
                    fx (b, FxType::Tape, 0.5f, 0.5f, 0.0f, 1.0f);
                    filter (b, F::LP, 6500.0);
                    vinyl (b, 150 + (uint64_t) i, 0.5f);
                    return b;
                });
            for (int i = 0; i < 2; ++i)
                add ("Snares", "Ghost Snare " + std::to_string (i + 1), [i]
                {
                    SnareSpec s; s.body = 215 + i * 20; s.bodyTau = 0.035; s.noiseTau = 0.05; s.snap = 0.15f; s.drive = 0.6f; s.seed = 160 + (uint64_t) i;
                    return Buf (snareMono (s));
                }, -7.0f);
            for (int i = 0; i < 2; ++i)
                add ("Snares", "Industrial Snare " + std::to_string (i + 1), [i]
                {
                    SnareSpec s; s.body = 170 + i * 40; s.bodyTau = 0.06; s.noiseTau = 0.14; s.noiseBp = 2200; s.drive = 3.0f; s.seed = 170 + (uint64_t) i;
                    auto x = snareMono (s);
                    addInto (x, metal (0.4, 380 + i * 90, 0.09, 1500, 171 + (uint64_t) i), 0.7f);
                    Buf b (x);
                    fx (b, FxType::Fuzz, 0.5f, 0.4f, 0.1f, 0.6f);
                    fx (b, FxType::Spring, 0.5f, 0.5f, 0.0f, 0.2f, 0.8);
                    return b;
                });

            // ===== Claps ==========================================================
            add ("Claps", "Clap Dry",     [] { return Buf (clapMono (1200, 0.07, 4, 0.010, 200)); });
            add ("Claps", "Clap Room",    [] { Buf b (clapMono (1150, 0.09, 4, 0.011, 201)); fx (b, FxType::Plate, 0.3f, 0.5f, 0.0f, 0.3f, 0.8); return b; });
            add ("Claps", "Garage Clap",  [] { Buf b (clapMono (1500, 0.06, 3, 0.008, 202, 1.8)); fx (b, FxType::Spring, 0.25f, 0.4f, 0.0f, 0.15f, 0.5); return b; });
            add ("Claps", "Wide Clap",    [] { Buf b (clapMono (1300, 0.08, 4, 0.012, 203)); widen (b, 11.0); fx (b, FxType::Chorus, 0.4f, 0.5f, 0.0f, 0.4f); return b; });
            add ("Claps", "Dub Clap",     [] { Buf b (clapMono (1100, 0.08, 4, 0.011, 204)); fx (b, FxType::TapeDelay, 0.45f, 0.55f, 0.0f, 0.35f, 2.0, 140.0); return b; });
            add ("Claps", "Big Clap",     [] { Buf b (clapMono (1000, 0.12, 5, 0.012, 205)); fx (b, FxType::Hall, 0.5f, 0.45f, 0.0f, 0.35f, 2.0); return b; });
            add ("Claps", "Dusty Clap",   [] { Buf b (clapMono (1250, 0.07, 4, 0.010, 206)); fx (b, FxType::Bitcrush, 0.5f, 0.35f, 0.0f, 0.8f); filter (b, F::LP, 6000.0); return b; });
            add ("Claps", "Snap",         [] { Buf b (clapMono (2300, 0.025, 2, 0.006, 207, 2.2)); fx (b, FxType::Plate, 0.2f, 0.4f, 0.0f, 0.18f, 0.5); return b; });

            // ===== Hats (closed) ==================================================
            for (int i = 0; i < 3; ++i)
                add ("Hats", "Hat Closed " + std::to_string (i + 1), [i] { return Buf (hatMono (400 + i * 60, 0.035 + i * 0.008, 7000, 300 + (uint64_t) i)); }, -3.0f);
            for (int i = 0; i < 2; ++i)
                add ("Hats", "Noise Hat " + std::to_string (i + 1), [i]
                {
                    auto n = noise (0.25, 310 + (uint64_t) i, 0.028 + i * 0.012, 0.0003);
                    filter (n, F::HP, 8000.0 - i * 1500, 0.8f);
                    return Buf (n);
                }, -3.0f);
            add ("Hats", "Tick Hat",    [] { return Buf (hatMono (520, 0.009, 9000, 320, 0.5f)); }, -4.0f);
            add ("Hats", "Shuffle Hat", [] { return Buf (hatMono (430, 0.055, 6500, 321, 0.6f, 0.003)); }, -4.0f);
            add ("Hats", "Garage Hat",  [] { return Buf (hatMono (470, 0.04, 7500, 322, 0.5f)); }, -3.0f);
            add ("Hats", "Crunch Hat",  [] { Buf b (hatMono (410, 0.04, 6000, 323)); fx (b, FxType::Bitcrush, 0.6f, 0.4f, 0.0f, 1.0f); return b; }, -4.0f);
            add ("Hats", "Dusty Hat",   [] { Buf b (hatMono (390, 0.045, 5500, 324, 0.7f)); fx (b, FxType::Tape, 0.4f, 0.5f, 0.0f, 1.0f); filter (b, F::LP, 7500.0); return b; }, -4.0f);
            add ("Hats", "Glitch Hat",  [] { Buf b (hatMono (450, 0.03, 7000, 325)); fx (b, FxType::RingMod, 0.7f, 0.5f, 0.0f, 0.7f); return b; }, -4.0f);
            add ("Hats", "Pedal Hat",   [] { return Buf (hatMono (360, 0.05, 4500, 326, 0.2f, 0.004)); }, -4.0f);
            // open
            for (int i = 0; i < 3; ++i)
                add ("Hats", "Hat Open " + std::to_string (i + 1), [i] { return Buf (hatMono (410 + i * 50, 0.24 + i * 0.12, 6500, 330 + (uint64_t) i, 0.45f)); }, -3.0f);
            add ("Hats", "Garage Open Hat", [] { return Buf (hatMono (480, 0.2, 7500, 333, 0.6f)); }, -3.0f);
            add ("Hats", "Dusty Open Hat",  [] { Buf b (hatMono (395, 0.3, 5500, 334, 0.6f)); fx (b, FxType::Tape, 0.5f, 0.5f, 0.0f, 1.0f); filter (b, F::LP, 8000.0); return b; }, -4.0f);
            add ("Hats", "Trash Hat",       [] { Buf b (hatMono (350, 0.28, 4000, 335, 0.9f)); fx (b, FxType::Fuzz, 0.6f, 0.5f, 0.1f, 0.7f); filter (b, F::HP, 3000.0); return b; }, -4.0f);

            // ===== Cymbals ========================================================
            add ("Cymbals", "Ride Ping", []
            {
                auto x = fmHit (3.5, 3300, 1.4142, 1.6, 0.25, 0.7, 0.0003);
                addInto (x, metal (3.5, 520, 0.7, 5000, 340), 0.6f);
                return Buf (x);
            }, -4.0f);
            add ("Cymbals", "Ride Wash", [] { Buf b (metal (2.6, 470, 1.4, 4000, 341)); auto n = noise (2.6, 342, 1.0); filter (n, F::HP, 6000.0); b.mixMono (n, 0, 0.4f); return b; }, -4.0f);
            add ("Cymbals", "Crash", []
            {
                auto x = metal (3.0, 360, 1.5, 3000, 343, 0.002);
                auto n = noise (3.0, 344, 1.2, 0.002); filter (n, F::HP, 3500.0);
                addInto (x, n, 0.7f);
                Buf b (x); widen (b, 7.0);
                fx (b, FxType::Hall, 0.4f, 0.4f, 0.0f, 0.25f, 1.0);
                return b;
            }, -3.0f);
            add ("Cymbals", "Reverse Crash", []
            {
                auto x = metal (2.4, 360, 1.0, 3000, 345, 0.002);
                auto n = noise (2.4, 346, 0.9); filter (n, F::HP, 3500.0); addInto (x, n, 0.7f);
                Buf b (x); fx (b, FxType::Hall, 0.5f, 0.4f, 0.0f, 0.35f, 0.6);
                reverse (b);
                return b;
            }, -3.0f, false);
            add ("Cymbals", "China", [] { Buf b (metal (2.0, 300, 0.9, 2500, 347, 0.001)); fx (b, FxType::Fuzz, 0.5f, 0.4f, 0.0f, 0.6f); return b; }, -4.0f);
            add ("Cymbals", "Splash", [] { auto x = metal (1.2, 430, 0.35, 4500, 348); auto n = noise (1.2, 349, 0.3); filter (n, F::HP, 5000.0); addInto (x, n, 0.6f); return Buf (x); }, -4.0f);

            // ===== Shakers ========================================================
            auto shaker = [] (double bp, double attack, double tau, uint64_t seed)
            {
                auto n = noise (0.4, seed, tau, attack);
                filter (n, F::BP, bp, 0.9f);
                filter (n, F::HP, 3000.0);
                return Buf (n);
            };
            add ("Shakers", "Shaker 1",       [shaker] { return shaker (6500, 0.012, 0.05, 360); }, -5.0f);
            add ("Shakers", "Shaker 2",       [shaker] { return shaker (8000, 0.02, 0.06, 361); }, -5.0f);
            add ("Shakers", "Shaker Long",    [shaker] { return shaker (7000, 0.04, 0.12, 362); }, -5.0f);
            add ("Shakers", "Shaker Accent",  [shaker] { return shaker (5500, 0.006, 0.07, 363); }, -4.0f);
            add ("Shakers", "Cabasa",         [shaker] { auto b = shaker (9500, 0.003, 0.04, 364); filter (b, F::HP, 7000.0); return b; }, -5.0f);
            add ("Shakers", "Tambourine", []
            {
                auto x = metal (0.6, 1300, 0.12, 6000, 365, 0.001);
                auto n = noise (0.6, 366, 0.08); filter (n, F::HP, 7000.0); addInto (x, n, 0.5f);
                return Buf (x);
            }, -4.0f);

            // ===== Percussion =====================================================
            add ("Percussion", "Bongo High",   [] { return Buf (pitchedPerc (410, 1.25, 0.09, 0.3, 2500, 400)); }, -2.0f);
            add ("Percussion", "Bongo Low",    [] { return Buf (pitchedPerc (290, 1.25, 0.12, 0.25, 2000, 401)); }, -2.0f);
            add ("Percussion", "Conga High",   [] { return Buf (pitchedPerc (330, 1.15, 0.16, 0.2, 1800, 402)); }, -2.0f);
            add ("Percussion", "Conga Mid",    [] { return Buf (pitchedPerc (250, 1.15, 0.2, 0.2, 1500, 403)); }, -2.0f);
            add ("Percussion", "Conga Low",    [] { return Buf (pitchedPerc (190, 1.15, 0.25, 0.15, 1200, 404)); }, -2.0f);
            add ("Percussion", "Timbale High", [] { auto x = pitchedPerc (620, 1.1, 0.12, 0.2, 3000, 405); addInto (x, metal (0.6, 900, 0.18, 2500, 406), 0.35f); return Buf (x); }, -2.0f);
            add ("Percussion", "Timbale Low",  [] { auto x = pitchedPerc (450, 1.1, 0.15, 0.2, 2500, 407); addInto (x, metal (0.6, 700, 0.2, 2000, 408), 0.35f); return Buf (x); }, -2.0f);
            add ("Percussion", "Woodblock High", [] { auto x = sweptSine (0.2, 1250, 1180, 0.004, 0.028, 0.0001); auto c = noise (0.02, 409, 0.002); filter (c, F::BP, 2500.0, 2.0f); addInto (x, c, 0.4f); return Buf (x); }, -3.0f);
            add ("Percussion", "Woodblock Low",  [] { auto x = sweptSine (0.2, 830, 790, 0.004, 0.035, 0.0001); auto c = noise (0.02, 410, 0.002); filter (c, F::BP, 1800.0, 2.0f); addInto (x, c, 0.4f); return Buf (x); }, -3.0f);
            add ("Percussion", "Clave",        [] { return Buf (sweptSine (0.2, 2550, 2500, 0.002, 0.04, 0.0001)); }, -4.0f);
            add ("Percussion", "Cowbell",      [] { auto x = squares (0.5, { 540.0, 800.0 }, 0.09, 0.0003); filter (x, F::BP, 900.0, 1.2f); return Buf (x); }, -3.0f);
            add ("Percussion", "Bleep High",   [] { auto x = squares (0.12, { 1000.0 }, 0.6, 0.001); for (size_t i = 0; i < x.size(); ++i) if (tOf (i) > 0.07) x[i] *= env (tOf (i) - 0.07, 0.004); filter (x, F::LP, 6000.0); return Buf (x); }, -6.0f);
            add ("Percussion", "Bleep Low",    [] { auto x = squares (0.16, { 500.0 }, 0.6, 0.001); for (size_t i = 0; i < x.size(); ++i) if (tOf (i) > 0.1) x[i] *= env (tOf (i) - 0.1, 0.005); filter (x, F::LP, 4000.0); return Buf (x); }, -6.0f);
            add ("Percussion", "Zap 1",        [] { return Buf (sweptSine (0.25, 2400, 90, 0.025, 0.08, 0.0001)); }, -2.0f);
            add ("Percussion", "Zap 2",        [] { auto x = sweptSine (0.3, 4000, 160, 0.015, 0.06, 0.0001); drive (x, 3.0f); return Buf (x); }, -3.0f);
            add ("Percussion", "Metal Hit 1",  [] { return Buf (fmHit (0.8, 310, 1.414, 4.0, 0.05, 0.22)); }, -3.0f);
            add ("Percussion", "Metal Hit 2",  [] { auto x = fmHit (0.8, 470, 2.76, 5.0, 0.03, 0.15); drive (x, 1.5f); return Buf (x); }, -3.0f);
            add ("Percussion", "Glass 1",      [] { Buf b (fmHit (3.2, 1760, 3.5, 2.2, 0.15, 0.55)); fx (b, FxType::Plate, 0.5f, 0.3f, 0.0f, 0.3f, 1.2); return b; }, -4.0f);
            add ("Percussion", "Glass 2",      [] { Buf b (fmHit (3.0, 2640, 2.01, 1.6, 0.2, 0.45)); fx (b, FxType::PingPong, 0.3f, 0.4f, 0.0f, 0.25f, 1.2, 140.0); return b; }, -4.0f);
            add ("Percussion", "Tok",          [] { auto x = sweptSine (0.15, 950, 320, 0.006, 0.018, 0.0001); auto c = noise (0.01, 411, 0.0015); filter (c, F::HP, 3000.0); addInto (x, c, 0.5f); return Buf (x); }, -3.0f);
            add ("Percussion", "Clank",        [] { auto x = metal (0.7, 180, 0.12, 1000, 412); addInto (x, fmHit (0.7, 233, 2.37, 6.0, 0.02, 0.1), 0.6f); Buf b (x); fx (b, FxType::Spring, 0.4f, 0.4f, 0.0f, 0.3f, 0.8); return b; }, -3.0f);
            add ("Percussion", "Log Drum",     [] { auto x = sweptSine (0.6, 210, 180, 0.01, 0.13, 0.0003); addInto (x, sweptSine (0.6, 420, 360, 0.01, 0.06), 0.3f); drive (x, 0.8f); return Buf (x); }, -2.0f);
            add ("Percussion", "Pitch Drum",   [] { return Buf (sweptSine (0.5, 210, 300, 0.06, 0.14, 0.0004)); }, -2.0f);
            add ("Percussion", "Bucket",       [] { auto x = sweptSine (0.4, 160, 135, 0.01, 0.09); auto n = noise (0.4, 413, 0.05); filter (n, F::BP, 650.0, 1.5f); addInto (x, n, 0.5f); return Buf (x); }, -2.0f);

            // ===== Toms ===========================================================
            const std::pair<const char*, double> toms[] = { { "Tom Low", 90 }, { "Tom Mid", 130 }, { "Tom High", 190 } };
            for (auto [name, f] : toms)
                add ("Toms", name, [f = f] { auto x = sweptSine (0.9, f * 1.6, f, 0.04, 0.22, 0.0006); auto n = noise (0.9, 420, 0.03); filter (n, F::BP, f * 8, 0.8f); addInto (x, n, 0.15f); drive (x, 0.8f); return Buf (x); }, -2.0f);
            add ("Toms", "Electro Tom Low",  [] { return Buf (sweptSine (1.2, 260, 85, 0.08, 0.38, 0.0005)); }, -2.0f);
            add ("Toms", "Electro Tom High", [] { return Buf (sweptSine (1.0, 420, 150, 0.07, 0.3, 0.0005)); }, -2.0f);
            add ("Toms", "Dub Tom",          [] { Buf b (sweptSine (0.8, 200, 110, 0.05, 0.2, 0.0006)); fx (b, FxType::TapeDelay, 0.5f, 0.5f, 0.0f, 0.4f, 2.5, 140.0); return b; }, -2.0f, true);

            // ===== FX =============================================================
            add ("FX", "Sub Drop", []
            {
                auto x = mono (2.2); double ph = 0;
                for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); ph += (28.0 + 62.0 * std::exp (-t / 0.6)) / SR; x[i] = (float) std::sin (TWO_PI * ph) * ramp (t, 0.01) * env (t, 0.9); }
                drive (x, 0.8f);
                return Buf (x);
            });
            add ("FX", "Impact", []
            {
                KickSpec k; k.f0 = 120; k.f1 = 38; k.pitchTau = 0.08; k.ampTau = 0.6; k.length = 1.6; k.click = 0.6f; k.drive = 2.0f;
                auto x = kickMono (k);
                auto n = noise (1.6, 430, 0.35); filter (n, F::LP, 3000.0); addInto (x, n, 0.4f);
                Buf b (x); fx (b, FxType::Hall, 0.7f, 0.5f, 0.0f, 0.4f, 2.5);
                return b;
            });
            add ("FX", "Laser", [] { auto x = mono (0.4); double ph = 0; for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); ph += (120 + 3000 * std::exp (-t / 0.05)) / SR; x[i] = (ph - std::floor (ph) < 0.5 ? 0.6f : -0.6f) * env (t, 0.1); } filter (x, F::LP, 7000.0); return Buf (x); }, -4.0f);
            add ("FX", "Reverse Swell", []
            {
                auto x = sawStack (1.2, 220, { -0.08, 0.07, 12.0 }, 0.002, 0.25);
                filter (x, F::LP, 2500.0);
                Buf b (x); fx (b, FxType::Hall, 0.7f, 0.4f, 0.0f, 0.7f, 2.0);
                reverse (b);
                return b;
            }, -3.0f, false);
            add ("FX", "Horn Stab", []
            {
                auto x = mono (0.7); Saw s1, s2, s3; Svf f;
                for (size_t i = 0; i < x.size(); ++i)
                {
                    const double t = tOf (i);
                    const double pitch = 466.16 * (1.0 - 0.06 * std::exp (-t / 0.03));
                    float v = (s1.next (pitch) + s2.next (pitch * 1.006) + s3.next (pitch * 0.5)) / 3.0f;
                    f.run (v, 900 + 3500 * env (t, 0.12), 1.2f);
                    x[i] = f.lp * ramp (t, 0.008) * (t < 0.45 ? 1.0f : env (t - 0.45, 0.06));
                }
                drive (x, 1.5f);
                Buf b (x); fx (b, FxType::Plate, 0.4f, 0.4f, 0.0f, 0.25f, 1.0);
                return b;
            });
            add ("FX", "Dub Siren", []
            {
                auto x = mono (1.6); double ph = 0;
                for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); const double f = 700 + 450 * std::sin (TWO_PI * 5.5 * t) * (1.0 - std::exp (-t / 0.3)); ph += f / SR; x[i] = (float) std::sin (TWO_PI * ph) * ramp (t, 0.02) * (t < 1.1 ? 1.0f : env (t - 1.1, 0.1)); }
                Buf b (x); fx (b, FxType::TapeDelay, 0.45f, 0.6f, 0.0f, 0.45f, 2.5, 140.0); fx (b, FxType::Spring, 0.5f, 0.4f, 0.0f, 0.2f, 1.0);
                return b;
            }, -3.0f);
            add ("FX", "Reese Stab", []
            {
                auto x = sawStack (0.6, 55.0, { -0.18, 0.15, 12.02 }, 0.004, 0.25);
                addInto (x, sweptSine (0.6, 55, 55, 1, 0.3), 0.5f);
                filter (x, F::LP, 800.0, 1.0f); drive (x, 2.0f);
                return Buf (x);
            });
            add ("FX", "Wobble Stab", []
            {
                auto x = mono (0.6); Saw s; Svf f;
                for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); const float v = s.next (43.65) + (float) std::sin (TWO_PI * 43.65 * t); f.run (v, 200 + 1800 * (0.5 + 0.5 * std::sin (TWO_PI * 7.0 * t - 1.5)), 3.0f); x[i] = f.lp * ramp (t, 0.003) * (t < 0.45 ? 1.0f : env (t - 0.45, 0.04)); }
                drive (x, 2.5f);
                return Buf (x);
            });
            add ("FX", "Tape Stop", []
            {
                auto x = mono (1.0); double ph = 0;
                for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); const double rate = std::max (0.0, 1.0 - t / 0.9); ph += 110.0 * rate * rate / SR; x[i] = (float) (std::sin (TWO_PI * ph) + 0.5 * std::sin (TWO_PI * ph * 1.5)) * 0.6f; }
                filter (x, F::LP, 1800.0); drive (x, 1.0f);
                return Buf (x);
            }, -2.0f);
            add ("FX", "Noise Riser", []
            {
                auto x = noise (2.0, 440, 100.0, 1.8);
                sweep (x, F::BP, [] (double t) { return 300.0 * std::pow (2.0, t * 3.4); }, 1.4f);
                Buf b (x); widen (b, 9.0);
                return b;
            }, -3.0f, false);
            add ("FX", "Downlifter", []
            {
                auto x = noise (2.0, 441, 0.6, 0.002);
                sweep (x, F::BP, [] (double t) { return 9000.0 * std::pow (2.0, -t * 3.0); }, 1.4f);
                Buf b (x); fx (b, FxType::Hall, 0.5f, 0.4f, 0.0f, 0.3f, 1.0);
                return b;
            }, -3.0f);
            add ("FX", "Whoosh", []
            {
                auto x = noise (1.2, 442, 100.0, 0.0);
                for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); x[i] *= (float) std::sin (3.14159 * std::min (1.0, t / 1.2)); }
                sweep (x, F::BP, [] (double t) { return 600.0 + 5000.0 * std::sin (3.14159 * std::min (1.0, t / 1.2)); }, 2.0f);
                Buf b (x); widen (b, 12.0);
                return b;
            }, -4.0f);
            add ("FX", "Bass Hit", []
            {
                auto x = sawStack (0.8, 49.0, { 0.0, 0.1 }, 0.002, 0.28);
                filter (x, F::LP, 500.0, 1.2f);
                addInto (x, sweptSine (0.8, 120, 49, 0.03, 0.35), 0.9f);
                drive (x, 2.0f);
                return Buf (x);
            });

            // ===== Glitch =========================================================
            add ("Glitch", "Click 1", [] { auto x = mono (0.03); x[0] = 1.0f; x[1] = -0.6f; filter (x, F::BP, 3000.0, 0.9f); return Buf (x); }, -6.0f);
            add ("Glitch", "Click 2", [] { auto x = mono (0.03); x[0] = 1.0f; x[3] = 0.7f; x[7] = -0.8f; filter (x, F::HP, 5000.0); return Buf (x); }, -6.0f);
            add ("Glitch", "Vinyl Crackle", [] { Buf b (2.0); vinyl (b, 450, 3.0f); return b; }, -10.0f, false);
            add ("Glitch", "Bit Blip", [] { Buf b (squares (0.05, { 2000.0, 2990.0 }, 0.03)); fx (b, FxType::Bitcrush, 0.8f, 0.5f, 0.0f, 1.0f); return b; }, -6.0f);
            add ("Glitch", "Digital Error", []
            {
                auto x = mono (0.25); Rng r (451); double ph = 0, f = 800;
                for (size_t i = 0; i < x.size(); ++i) { if (i % 441 == 0) f = 200 + r.uni() * 4000; ph += f / SR; x[i] = (ph - std::floor (ph) < 0.5 ? 0.5f : -0.5f) * env (tOf (i), 0.12); }
                return Buf (x);
            }, -6.0f);
            add ("Glitch", "Radio Static", []
            {
                auto x = noise (0.8, 452, 0.3, 0.002);
                for (size_t i = 0; i < x.size(); ++i) x[i] *= 0.5f + 0.5f * (float) std::sin (TWO_PI * 30.0 * tOf (i));
                filter (x, F::BP, 2200.0, 1.0f);
                return Buf (x);
            }, -6.0f);
            add ("Glitch", "Ring Snap", [] { auto x = noise (0.08, 453, 0.012, 0.0002); for (size_t i = 0; i < x.size(); ++i) x[i] *= (float) std::sin (TWO_PI * 1700 * tOf (i)); return Buf (x); }, -4.0f);
            add ("Glitch", "Granular Crunch", []
            {
                auto x = mono (0.5); Rng r (454);
                for (int g = 0; g < 14; ++g)
                {
                    const size_t at = (size_t) (g * 0.03 * SR + r.uni() * 400);
                    auto grain = noise (0.012, 455 + (uint64_t) g, 0.004, 0.0005);
                    filter (grain, F::BP, 800 + r.uni() * 5000, 2.0f);
                    addInto (x, grain, 1.0f - g / 16.0f, at);
                }
                return Buf (x);
            }, -4.0f);
            add ("Glitch", "Pop",       [] { return Buf (sweptSine (0.06, 600, 280, 0.004, 0.008, 0.0001)); }, -4.0f);
            add ("Glitch", "Micro Hat", [] { auto n = noise (0.02, 456, 0.003, 0.0001); filter (n, F::HP, 9000.0); return Buf (n); }, -6.0f);
            add ("Glitch", "Data Burst", []
            {
                auto x = mono (0.3); Rng r (457); double ph = 0, f = 1000;
                for (size_t i = 0; i < x.size(); ++i) { if (i % 160 == 0) f = (r.uni() < 0.5 ? 1 : 2) * (300 + 100 * std::floor (r.uni() * 20)); ph += f / SR; x[i] = (float) std::sin (TWO_PI * ph) * env (tOf (i), 0.15) * (r.uni() < 0.97f ? 1.0f : 0.0f); }
                Buf b (x); fx (b, FxType::Bitcrush, 0.6f, 0.3f, 0.0f, 1.0f);
                return b;
            }, -6.0f);
            add ("Glitch", "Tape Flutter Hit", [] { Buf b (snareMono (SnareSpec {})); fx (b, FxType::Tape, 0.6f, 1.0f, 0.6f, 1.0f); fx (b, FxType::GrainDelay, 0.4f, 0.5f, 0.3f, 0.5f, 0.6, 140.0); return b; }, -3.0f);

            return v;
        }
    }

    const std::vector<OneShot>& oneShots()
    {
        static const std::vector<OneShot> list = build();
        return list;
    }

    std::string oneShotPath (const OneShot& s) { return "Drums/" + s.category + "/" + s.name; }
    std::string loopPath (const Loop& l)       { return "Loops/" + l.category + "/" + l.name; }

    const OneShot* findOneShot (const std::string& categoryAndName)
    {
        for (const auto& s : oneShots())
            if (s.category + "/" + s.name == categoryAndName) return &s;
        return nullptr;
    }
}
