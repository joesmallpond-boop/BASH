// BASH! core unit tests — no framework, just run the binary.
#include "bash/Engine.h"
#include "bash/Scale.h"
#include "bash/Sequencer.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

using namespace bash;

static void testDeterminism()
{
    Pattern a (42), b (42);
    SequencerParams p; p.type = 0.5f;
    Scale s (0, 0);
    for (uint32_t i = 0; i < 1000; ++i)
    {
        auto x = a.step (i, p, s), y = b.step (i, p, s);
        CHECK (x.active == y.active && x.note == y.note && x.accent == y.accent && x.slide == y.slide);
    }
}

static void testDensityOnlyAdds()
{
    Pattern pat (7);
    Scale s (9, 0);
    for (float type : { 0.0f, 0.4f, 1.0f })
    {
        SequencerParams lo, hi; lo.type = hi.type = type; lo.density = 0.3f; hi.density = 0.8f;
        int loCount = 0, hiCount = 0;
        for (uint32_t i = 0; i < 512; ++i)
        {
            auto a = pat.step (i, lo, s), b = pat.step (i, hi, s);
            if (a.active) { CHECK (b.active); ++loCount; }
            if (b.active) ++hiCount;
            CHECK (a.note == b.note);   // motif unchanged
        }
        CHECK (hiCount > loCount);
    }
    SequencerParams none; none.density = 0.0f;
    SequencerParams all;  all.density  = 1.0f;
    for (uint32_t i = 0; i < 256; ++i) { CHECK (! pat.step (i, none, s).active); CHECK (pat.step (i, all, s).active); }
}

static void testLocksSurviveRegenerate()
{
    Pattern pat (99);
    SequencerParams p; p.density = 1.0f;
    Scale s (0, 0);
    const auto before3 = pat.step (3, p, s);
    std::vector<int> notesBefore;
    for (uint32_t i = 0; i < 64; ++i) notesBefore.push_back (pat.step (i, p, s).note);

    pat.lock (3, lockbit::all, before3);
    pat.regenerate();

    const auto after3 = pat.step (3, p, s);
    CHECK (after3.note == before3.note && after3.accent == before3.accent && after3.slide == before3.slide && after3.active == before3.active);

    int changed = 0;
    for (uint32_t i = 0; i < 64; ++i) if (pat.step (i, p, s).note != notesBefore[i]) ++changed;
    CHECK (changed > 10);

    // partial lock: pitch only
    Pattern q (5);
    StepData forced; forced.note = 48; forced.active = false;
    q.lock (10, lockbit::pitch, forced);
    CHECK (q.step (10, p, s).note == 48);
    CHECK (q.step (10, p, s).active);   // active not locked, density 1 → plays
    q.unlock (10, lockbit::pitch);
    CHECK (! q.isLocked (10));
}

static void testDirections()
{
    Pattern pat (1);
    SequencerParams p; p.length = 4;
    auto seq = [&] (int count) { std::string r; for (int n = 0; n < count; ++n) r += char ('0' + pat.indexForPosition (n, p)); return r; };

    p.direction = Direction::Forward;  CHECK (seq (8)  == "01230123");
    p.direction = Direction::Reverse;  CHECK (seq (8)  == "32103210");
    p.direction = Direction::PingPong; CHECK (seq (10) == "0123210123");
    p.direction = Direction::Forward; p.offset = 2; CHECK (seq (6) == "230123");
    p.offset = 0;
    CHECK (pat.indexForPosition (-1, p) == 3);   // pre-roll wraps correctly

    p.direction = Direction::Random;
    std::set<uint32_t> seen;
    for (int n = 0; n < 200; ++n) { auto i = pat.indexForPosition (n, p); CHECK (i < 4); seen.insert (i); }
    CHECK (seen.size() == 4);

    // Very long cycles
    SequencerParams huge; huge.length = 0xFFFFFFFFu;
    CHECK (pat.indexForPosition (4000000000ll, huge) == 4000000000u);
    CHECK (pat.indexForPosition (0xFFFFFFFFll, huge) == 0u);
    Scale s (0, 0);
    auto far = pat.step (4000000000u, huge, s);
    CHECK (far.note >= 0 && far.note <= 127);
}

static void testScales()
{
    Scale cMinor (0, 0);
    CHECK (cMinor.contains (60) && cMinor.contains (63) && ! cMinor.contains (64));
    CHECK (cMinor.quantize (64) == 63);
    CHECK (cMinor.degreeToSemitone (7) == 12 && cMinor.degreeToSemitone (-1) == -2);

    Scale aPhryg (9, 3);
    Pattern pat (1234);
    SequencerParams p; p.baseNote = 33; p.type = 1.0f; p.density = 1.0f; p.scaleLock = true;
    for (uint32_t i = 0; i < 5000; ++i) CHECK (aPhryg.contains (pat.step (i, p, aPhryg).note));

    // Type 0 stays in key even without scale lock
    p.type = 0.0f; p.scaleLock = false;
    for (uint32_t i = 0; i < 5000; ++i) CHECK (aPhryg.contains (pat.step (i, p, aPhryg).note));

    // Type 1 without scale lock does go chromatic sometimes
    p.type = 1.0f;
    int outside = 0;
    for (uint32_t i = 0; i < 5000; ++i) if (! aPhryg.contains (pat.step (i, p, aPhryg).note)) ++outside;
    CHECK (outside > 100);
}

static void testSerialise()
{
    Pattern a (123456789);
    StepData v; v.note = 41; v.accent = true; v.gate = 0.75f; v.active = true; v.slide = true;
    a.lock (7, lockbit::pitch | lockbit::accent, v);
    a.lock (300000, lockbit::all, v);
    Pattern b;
    CHECK (b.deserialise (a.serialise()));
    CHECK (b.getSeed() == a.getSeed());
    CHECK (b.lockMask (7) == (lockbit::pitch | lockbit::accent));
    CHECK (b.lockMask (300000) == lockbit::all);
    CHECK (! b.deserialise ("garbage"));
}

// Plays a pattern through TrackPlayer with a given block size and returns
// events stamped with absolute sample positions.
static std::vector<std::pair<long, NoteEvent>> playThrough (int blockSize, const SequencerParams& p, uint64_t seed)
{
    Pattern pat (seed);
    Scale s (0, 0);
    TrackPlayer player;
    const double stepsPerSample = 1.0 / 1000.0;   // 1000 samples per step
    const long total = 64 * 1000;
    std::vector<std::pair<long, NoteEvent>> all;
    std::vector<NoteEvent> ev;
    for (long start = 0; start < total; start += blockSize)
    {
        const int n = (int) std::min<long> (blockSize, total - start);
        ev.clear();
        player.process (start * stepsPerSample, stepsPerSample, n, pat, p, s, ev);
        for (auto& e : ev) all.push_back ({ start + e.sampleOffset, e });
    }
    return all;
}

static void testPlayer()
{
    SequencerParams p; p.density = 0.7f; p.type = 0.3f; p.gate = 0.5f;
    auto a = playThrough (64, p, 77);
    auto b = playThrough (333, p, 77);

    // Block size must not change the music.
    CHECK (a.size() == b.size());
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        { bool ok = a[i].first == b[i].first && a[i].second.noteOn == b[i].second.noteOn && a[i].second.note == b[i].second.note; if (! ok && failures < 3) std::printf ("  diff %zu: %ld %d %d vs %ld %d %d\n", i, a[i].first, a[i].second.noteOn, a[i].second.note, b[i].first, b[i].second.noteOn, b[i].second.note); CHECK (ok); }

    // Time-ordered, every note-on eventually released, mono (≤ 2 notes only during a slide handover).
    long last = -1; int held = 0, legatoCount = 0, ons = 0;
    for (auto& [t, e] : a)
    {
        CHECK (t >= last); last = t;
        if (e.noteOn) { ++held; ++ons; if (e.legato) ++legatoCount; }
        else --held;
        CHECK (held >= 0 && held <= 2);
    }
    CHECK (ons > 20);
    CHECK (legatoCount > 0);   // some slides happened

    // Note-on count matches active steps (minus slide ties onto the same pitch).
    Pattern pat (77); Scale s (0, 0);
    int active = 0;
    for (uint32_t i = 0; i < 64; ++i) if (pat.step (pat.indexForPosition (i, p), p, s).active) ++active;
    CHECK (ons <= active && ons >= active - 16);

    // Transport jump: player releases and resyncs without hanging notes.
    Pattern pat2 (3); TrackPlayer pl; std::vector<NoteEvent> ev;
    SequencerParams full; full.density = 1.0f; full.gate = 2.0f;
    pl.process (0.0, 0.001, 500, pat2, full, s, ev);
    ev.clear();
    pl.process (40.0, 0.001, 500, pat2, full, s, ev);   // jumped
    CHECK (! ev.empty() && ! ev[0].noteOn);
}

static void testEngine()
{
    Engine eng;
    eng.prepare (48000.0, 256);
    eng.global().hostSync = true;
    eng.track (0).seq.density = 0.8f;
    eng.track (1).enabled = true;
    eng.track (1).rateIndex = 0;

    Transport tp; tp.hostAvailable = true; tp.hostPlaying = true; tp.hostBpm = 130.0;
    std::vector<float> l (480), r (480);
    std::vector<MidiOut> midi;
    double peak = 0.0; bool finite = true;
    for (int block = 0; block < 400; ++block)   // ~4 seconds
    {
        eng.process (l.data(), r.data(), 480, tp, midi);
        tp.hostPpq += 480.0 * 130.0 / 60.0 / 48000.0;
        for (int i = 0; i < 480; ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max (peak, (double) std::abs (l[i]));
        }
    }
    CHECK (finite);
    CHECK (peak > 0.01 && peak < 2.0);
    bool ch1 = false, ch2 = false;
    for (auto& m : midi) { if (m.channel == 1) ch1 = true; if (m.channel == 2) ch2 = true; }
    CHECK (ch1 && ch2);

    // Stopping releases everything: silence soon after.
    tp.hostPlaying = false;
    midi.clear();
    for (int block = 0; block < 50; ++block) eng.process (l.data(), r.data(), 480, tp, midi);
    double tail = 0.0;
    for (int i = 0; i < 480; ++i) tail = std::max (tail, (double) std::abs (l[i]));
    CHECK (tail < 1.0e-3);

    // Internal clock runs without a host.
    Transport none;
    eng.global().hostSync = false; eng.global().playing = true;
    midi.clear();
    for (int block = 0; block < 100; ++block) eng.process (l.data(), r.data(), 480, none, midi);
    CHECK (! midi.empty());
}

int main()
{
    testDeterminism();
    testDensityOnlyAdds();
    testLocksSurviveRegenerate();
    testDirections();
    testScales();
    testSerialise();
    testPlayer();
    testEngine();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
