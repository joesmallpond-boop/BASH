// BASH! core unit tests — no framework, just run the binary.
#include "bash/Engine.h"
#include "bash/Scale.h"
#include "bash/Sequencer.h"
#include "bash/Slices.h"
#include "bash/MachineSynth.h"
#include "bash/Drums.h"
#include "bash/Fx.h"
#include "bash/Modulation.h"
#include "bash/StepSeq.h"
#include "bash/Warp.h"
#include "bash/Library.h"

#include <cmath>
#include <mutex>
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

// ---------------------------------------------------------------------------
// Slicer

// A synthetic drum loop: a decaying noise burst every `spacing` frames.
static SampleData makeClicks (uint32_t frames, uint32_t spacing, double sr = 48000.0)
{
    SampleData d; d.sampleRate = sr;
    d.left.assign (frames, 0.0f);
    uint64_t x = 12345;
    for (uint32_t start = spacing / 2; start < frames; start += spacing)
        for (uint32_t i = 0; i < 2000 && start + i < frames; ++i)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            const float noise = (float) ((x >> 40) & 0xFFFF) / 32768.0f - 1.0f;
            d.left[start + i] = noise * std::exp (-(float) i / 300.0f);
        }
    d.right = d.left;
    return d;
}

static void testSlicePlacement()
{
    auto g = slicing::grid (48000, 16);
    CHECK (g.size() == 16 && g[0] == 0 && g[1] == 3000 && g[15] == 45000);
    CHECK (slicing::grid (1000, 999).size() == 256);   // capped

    auto r = slicing::random (96000, 32, 7, 48000.0);
    CHECK (r.size() >= 20 && r.size() <= 32 && r[0] == 0);
    for (size_t i = 1; i < r.size(); ++i) CHECK (r[i] > r[i - 1] && r[i] - r[i - 1] >= 480);
    CHECK (slicing::random (96000, 32, 7, 48000.0) == r);   // seeded
    // spread over the whole file, not clumped at one end: every eighth holds slices
    for (uint64_t seed = 1; seed < 50; ++seed)
    {
        const auto rs = slicing::random (480000, 32, seed, 48000.0);
        CHECK (rs.size() == 32);
        int perEighth[8] = {};
        for (auto f : rs) ++perEighth[f * 8 / 480000];
        for (int k = 0; k < 8; ++k) CHECK (perEighth[k] >= 3 && perEighth[k] <= 5);
        CHECK (rs.back() > 480000 * 9 / 10);
        CHECK (slicing::random (480000, 32, seed, 48000.0) != slicing::random (480000, 32, seed + 1, 48000.0));
    }

    std::vector<uint32_t> messy { 900, 5, 900, 100000, 30, 400 };
    slicing::normalise (messy, 1000, 32);
    CHECK ((messy == std::vector<uint32_t> { 0, 400, 900 }));

    // 8 hits → ~8 transient slices (+ the slice at 0)
    auto clicks = makeClicks (96000, 12000);
    auto tr = slicing::transients (clicks, 0.5f);
    CHECK (tr.size() >= 8 && tr.size() <= 10);
    int near = 0;
    for (uint32_t hit = 6000; hit < 96000; hit += 12000)
        for (auto p : tr) if (std::abs ((long) p - (long) hit) < 600) { ++near; break; }
    CHECK (near == 8);
    CHECK (slicing::transients (clicks, 1.0f).size() >= tr.size());

    CHECK (slicing::sliceAt (g, 0) == 0 && slicing::sliceAt (g, 3001) == 1 && slicing::sliceAt (g, 47999) == 15);
}

static void testSlicePicking()
{
    Pattern pat (11);
    Scale s (0, 0);
    SequencerParams p; p.density = 1.0f; p.sliceCount = 64;

    p.sliceMode = SliceMode::Window; p.windowStart = 10; p.windowSize = 4; p.type = 0.0f;
    for (uint32_t i = 0; i < 64; ++i) CHECK (pat.step (i, p, s).slice == (int) (10 + i % 4));
    p.type = 1.0f;
    std::set<int> seen;
    for (uint32_t i = 0; i < 400; ++i) { int sl = pat.step (i, p, s).slice; CHECK (sl >= 10 && sl < 14); seen.insert (sl); }
    CHECK (seen.size() == 4);

    p.windowStart = 62; p.windowSize = 8;   // window clipped at the end
    for (uint32_t i = 0; i < 100; ++i) { int sl = pat.step (i, p, s).slice; CHECK (sl >= 62 && sl < 64); }

    p.sliceMode = SliceMode::Bounce; p.type = 0.0f; seen.clear();
    for (uint32_t i = 0; i < 2000; ++i) { int sl = pat.step (i, p, s).slice; CHECK (sl >= 0 && sl < 64); seen.insert (sl); }
    CHECK (seen.size() > 50);

    p.sliceMode = SliceMode::Sequential;
    for (uint32_t i = 0; i < 128; ++i) CHECK (pat.step (i, p, s).slice == (int) (i % 64));

    // slice locks survive regenerate
    p.sliceMode = SliceMode::Bounce;
    StepData v; v.slice = 42;
    pat.lock (5, lockbit::slice, v);
    pat.regenerate();
    CHECK (pat.step (5, p, s).slice == 42);
    p.sliceCount = 10;
    CHECK (pat.step (5, p, s).slice == 9);   // clamped when fewer slices exist

    Pattern back; CHECK (back.deserialise (pat.serialise()));
    CHECK (back.step (5, p, s).slice == 9);
    CHECK (back.deserialise ("bashpat1 5 1 3 2 1 40 0 0 0.5"));   // old format still loads
    CHECK (back.lockMask (3) == 2);
}

static void testSliceVoice()
{
    SampleData d; d.sampleRate = 48000.0;
    d.left.assign (48000, 0.5f); d.right.assign (48000, -0.5f);
    SliceVoice v; v.prepare (48000.0);
    SliceVoice::Params p; p.attack = 0.001f;
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);

    v.start (&d, 1000, 3000, 1.0, 1.0f, p);    // 2000-frame slice
    v.render (l.data(), r.data(), 0, 4800, p);
    CHECK (std::abs (l[1000] - 0.5f) < 0.01f && std::abs (r[1000] + 0.5f) < 0.01f);
    CHECK (std::abs (l[2500]) < 1.0e-6f);       // stopped at the slice end
    CHECK (v.isSilent());

    // double speed plays the slice in half the time
    std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
    v.start (&d, 0, 2000, 2.0, 1.0f, p);
    v.render (l.data(), r.data(), 0, 4800, p);
    CHECK (std::abs (l[900] - 0.5f) < 0.01f && std::abs (l[1100]) < 1.0e-6f);

    // retrigger mid-slice never jumps: no sample-to-sample step bigger than the signal
    SampleData sine; sine.sampleRate = 48000.0;
    for (int i = 0; i < 48000; ++i) sine.left.push_back (std::sin (i * 0.05f));
    sine.right = sine.left;
    std::vector<float> o (9600, 0.0f), o2 (9600, 0.0f);
    v.start (&sine, 0, 48000, 1.0, 1.0f, p);
    v.render (o.data(), o2.data(), 0, 4000, p);
    v.start (&sine, 20000, 48000, 1.0, 1.0f, p);
    v.render (o.data(), o2.data(), 4000, 5600, p);
    float maxJump = 0.0f;
    for (int i = 1; i < 9600; ++i) maxJump = std::max (maxJump, std::abs (o[i] - o[i - 1]));
    CHECK (maxJump < 0.2f);

    v.stopIfUsing (&sine);
    CHECK (v.isSilent());
}

static void testSlicerEngine()
{
    auto loop = makeClicks (96000, 6000);
    Engine eng;
    eng.prepare (48000.0, 256);
    auto& t = eng.track (2);
    t.enabled = true;
    t.engine = EngineType::Slicer;
    t.seq.density = 1.0f;
    t.seq.sliceMode = SliceMode::Bounce;
    eng.track (0).enabled = false;
    eng.setSample (2, &loop);
    eng.setSlices (2, slicing::grid (loop.numFrames(), 16));
    CHECK (eng.sliceCount (2) == 16);

    Transport tp; tp.hostAvailable = true; tp.hostPlaying = true; tp.hostBpm = 120.0;
    std::vector<float> l (480), r (480);
    std::vector<MidiOut> midi;
    double energy = 0.0;
    std::set<int> slicesHeard;
    const uint64_t before = eng.blocksProcessed();
    for (int b = 0; b < 200; ++b)
    {
        eng.process (l.data(), r.data(), 480, tp, midi);
        tp.hostPpq += 480.0 * 2.0 / 48000.0;
        for (int i = 0; i < 480; ++i) energy += (double) l[i] * l[i];
        if (eng.playingSlice (2) >= 0) slicesHeard.insert (eng.playingSlice (2));
    }
    CHECK (energy > 1.0);
    CHECK (slicesHeard.size() > 4);
    CHECK (eng.blocksProcessed() == before + 200);
    bool ch3 = false; for (auto& m : midi) if (m.channel == 3) ch3 = true;
    CHECK (ch3);

    // swapping the sample out: next block no longer reads the old one
    eng.setSample (2, nullptr);
    midi.clear();
    for (int b = 0; b < 20; ++b) { eng.process (l.data(), r.data(), 480, tp, midi); tp.hostPpq += 480.0 * 2.0 / 48000.0; }
    float peak = 0.0f; for (int i = 0; i < 480; ++i) peak = std::max (peak, std::abs (l[i]));
    CHECK (peak < 1.0e-6f);
}

// ---------------------------------------------------------------------------
// Algorithms and machines

static void testScaleSteps()
{
    Scale cMaj (0, 1);
    CHECK (cMaj.stepFrom (60, 2) == 64 && cMaj.stepFrom (60, 4) == 67 && cMaj.stepFrom (60, 7) == 72);
    CHECK (cMaj.stepFrom (62, 2) == 65 && cMaj.stepFrom (60, -1) == 59);
    CHECK (cMaj.stepFrom (61, 0) == 60);   // off-scale input quantises first
}

static void testAlgorithms()
{
    Scale aMin (9, 0);
    Pattern pat (2024);

    // Dedicated bass: low register, in key, beat 1 always plays at moderate density
    SequencerParams bass; bass.algorithm = Algorithm::DedicatedBass; bass.baseNote = 33; bass.density = 0.4f;
    for (float t : { 0.0f, 1.0f })
    {
        bass.type = t;
        for (uint32_t i = 0; i < 512; ++i)
        {
            auto s = pat.step (i, bass, aMin);
            CHECK (s.note >= 33 - 5 && s.note <= 33 + 12 && aMin.contains (s.note));
            if (t == 0.0f && i % 16 == 0) CHECK (s.active);
        }
    }

    // Arpeggiator: only held notes, silent with nothing held
    SequencerParams arp; arp.algorithm = Algorithm::Arpeggiator; arp.density = 1.0f; arp.type = 0.0f;
    for (uint32_t i = 0; i < 32; ++i) CHECK (! pat.step (i, arp, aMin).active);
    arp.held = { 61, 64, 68 }; arp.heldCount = 3;   // C# E G# — not even in A minor: used exactly
    for (float t : { 0.0f, 0.5f, 1.0f })
    {
        arp.type = t;
        std::set<int> seen;
        for (uint32_t i = 0; i < 300; ++i)
        {
            auto s = pat.step (i, arp, aMin);
            CHECK (s.note == 61 || s.note == 64 || s.note == 68);
            seen.insert (s.note);
        }
        CHECK (seen.size() == 3);
    }
    arp.type = 0.0f;
    CHECK (pat.step (0, arp, aMin).note == 61 && pat.step (1, arp, aMin).note == 64 && pat.step (2, arp, aMin).note == 68);

    // Counterpoint: in-key triads, bass below the chord, both streams appear
    SequencerParams cp; cp.algorithm = Algorithm::Counterpoint; cp.baseNote = 33; cp.density = 0.6f;
    int chords = 0, basses = 0;
    for (uint32_t i = 0; i < 256; ++i)
    {
        auto s = pat.step (i, cp, aMin);
        CHECK (aMin.contains (s.note) && aMin.contains (s.chord[0]) && aMin.contains (s.chord[1]) && aMin.contains (s.bassNote));
        CHECK (s.chord[0] > s.note && s.chord[1] > s.chord[0]);
        CHECK (s.bassNote < s.note);
        if (s.active) ++chords;
        if (s.bassActive) ++basses;
        if (i % 16 != 0) CHECK (s.note == pat.step (i - i % 16, cp, aMin).note);   // harmony per bar at Type 0
    }
    CHECK (chords > 20 && basses > 20);

    // Player: chord notes on stream 0, bass on stream 1, everything released
    TrackPlayer pl; std::vector<NoteEvent> ev;
    for (int blk = 0; blk < 64; ++blk) pl.process (blk * 1.0, 0.01, 100, pat, cp, aMin, ev);
    pl.reset (&ev, 0);
    int held0 = 0, held1 = 0, maxHeld0 = 0, ons1 = 0;
    for (auto& e : ev)
    {
        int& h = e.stream == 1 ? held1 : held0;
        h += e.noteOn ? 1 : -1;
        CHECK (h >= 0);
        maxHeld0 = std::max (maxHeld0, held0);
        if (e.noteOn && e.stream == 1) ++ons1;
    }
    CHECK (held0 == 0 && held1 == 0 && maxHeld0 >= 3 && ons1 > 5);
}

static void testMachines()
{
    for (int m = 0; m < (int) Machine::count; ++m)
        for (int f = 0; f < (int) FilterType::count; ++f)
        {
            MachineSynth syn; syn.prepare (48000.0);
            MachineVoice::Params p; p.machine = (Machine) m; p.filterType = (FilterType) f;
            p.cutoff = 1200.0f; p.character = 0.6f; p.timbre = 0.7f; p.resonance = 0.9f;
            std::vector<float> out (24000, 0.0f);
            syn.noteOn (45, 110, true, false);
            syn.render (out.data(), 0, 12000, p);
            syn.noteOff (45);
            syn.render (out.data(), 12000, 12000, p);
            double rms = 0.0; float peak = 0.0f; bool finite = true;
            for (int i = 0; i < 12000; ++i) { rms += out[i] * out[i]; peak = std::max (peak, std::abs (out[i])); finite = finite && std::isfinite (out[i]); }
            rms = std::sqrt (rms / 12000.0);
            CHECK (finite);
            CHECK (rms > 0.005);
            CHECK (peak < 2.0f);
            CHECK (std::abs (out[23999]) < 1.0e-3f);   // released to silence
            CHECK (syn.isSilent());
            if (! (rms > 0.005)) std::printf ("  silent machine %s filter %d\n", machineName ((Machine) m), f);
        }

    // poly: three notes sound together, a fourth/fifth steal politely
    MachineSynth syn; syn.prepare (48000.0); syn.setPolyphonic (true);
    MachineVoice::Params p;
    std::vector<float> out (4800, 0.0f);
    for (int n : { 57, 60, 64, 67, 71 }) syn.noteOn (n, 100, false, false);
    syn.render (out.data(), 0, 4800, p);
    for (int n : { 57, 60, 64, 67, 71 }) syn.noteOff (n);
    std::vector<float> tail (24000, 0.0f);
    syn.render (tail.data(), 0, 24000, p);
    CHECK (syn.isSilent());
}

static void testEngineMidiIn()
{
    Engine eng; eng.prepare (48000.0, 256);
    eng.track (0).seq.algorithm = Algorithm::Arpeggiator;
    eng.track (0).seq.density = 1.0f;
    Transport tp; tp.hostAvailable = true; tp.hostPlaying = true; tp.hostBpm = 120.0;
    std::vector<float> l (480), r (480);
    std::vector<MidiOut> midi;
    for (int b = 0; b < 50; ++b) { eng.process (l.data(), r.data(), 480, tp, midi); tp.hostPpq += 0.02; }
    CHECK (midi.empty());   // arp with nothing held is silent
    eng.midiNoteIn (64, 100); eng.midiNoteIn (60, 100); eng.midiNoteIn (67, 100); eng.midiNoteIn (60, 90);
    CHECK (eng.heldInputCount() == 3);
    for (int b = 0; b < 100; ++b) { eng.process (l.data(), r.data(), 480, tp, midi); tp.hostPpq += 0.02; }
    std::set<int> notes; for (auto& m : midi) if (m.noteOn) notes.insert (m.note);
    CHECK ((notes == std::set<int> { 60, 64, 67 }));
    eng.midiNoteIn (64, 0);
    CHECK (eng.heldInputCount() == 2);

    // Counterpoint bass goes out on channel track + 8
    eng.track (0).seq.algorithm = Algorithm::Counterpoint;
    midi.clear();
    for (int b = 0; b < 200; ++b) { eng.process (l.data(), r.data(), 480, tp, midi); tp.hostPpq += 0.02; }
    bool ch1 = false, ch9 = false;
    for (auto& m : midi) { ch1 |= m.channel == 1; ch9 |= m.channel == 9; }
    CHECK (ch1 && ch9);
}

// ---------------------------------------------------------------------------
// Drums

static void testFmDrums()
{
    for (const auto& preset : fmPresets())
    {
        DrumPlayer pl; pl.prepare (48000.0);
        pl.trigger (makeFmCell (preset), 0, 120, false);
        std::vector<float> l (48000 * 12, 0.0f), r (48000 * 12, 0.0f);
        pl.render (l.data(), r.data(), 0, (int) l.size());
        double e = 0.0; bool finite = true; float peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i) { e += l[i] * l[i]; finite = finite && std::isfinite (l[i]); peak = std::max (peak, std::abs (l[i])); }
        CHECK (finite);
        CHECK (e > 0.5);
        CHECK (peak < 2.0f);
        CHECK (pl.isSilent());   // one-shots end on their own
        if (! (e > 0.5)) std::printf ("  quiet preset %s\n", preset.name);
    }
    CHECK (fmStartPoints().size() >= 4);
    for (auto& sp : fmStartPoints()) CHECK (sp.preset >= 0 && sp.preset < (int) fmPresets().size());
    for (auto& kit : factoryKits())
        for (int c : kit.cells) CHECK (c >= -1 && c < (int) fmPresets().size());
}

static void testDrumSamplesAndChoke()
{
    SampleData d; d.sampleRate = 48000.0;
    for (int i = 0; i < 48000; ++i) d.left.push_back (i < 24000 ? 0.5f : -0.5f);
    d.right = d.left;

    DrumCell c; c.kind = DrumCell::Kind::Sample; c.sample = &d; c.start = 0.5f; c.end = 0.75f; c.fadeOut = 0.001f;
    DrumPlayer pl; pl.prepare (48000.0);
    pl.trigger (c, 3, 127, false);
    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    pl.render (l.data(), r.data(), 0, 48000);
    CHECK (l[1000] < -0.1f);            // second half of the file (negative)
    CHECK (std::abs (l[13000]) < 1.0e-6f); // stopped after a quarter of the file
    CHECK (pl.isSilent());

    // choke: closed hat cuts open hat in the same group
    DrumCell open = makeFmCell (fmPresets()[9]), closed = makeFmCell (fmPresets()[8]);
    CHECK (open.choke == 1 && closed.choke == 1);
    DrumPlayer hp; hp.prepare (48000.0);
    hp.trigger (open, 1, 100, false);
    std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
    hp.render (l.data(), r.data(), 0, 4800);
    hp.trigger (closed, 0, 100, false);
    hp.render (l.data(), r.data(), 4800, 48000 - 4800);
    CHECK (hp.isSilent());
    float lateEnergy = 0.0f;
    for (int i = 24000; i < 48000; ++i) lateEnergy += l[i] * l[i];
    CHECK (lateEnergy < 1.0e-6f);       // the open hat's long tail was cut

    pl.trigger (c, 3, 127, false);
    pl.stopVoicesUsing (&d);
    CHECK (pl.isSilent());
}

static void testDrumGroove()
{
    Pattern pat (909);
    Scale s (0, 0);
    SequencerParams p; p.drumTrack = true; p.sliceCount = numCells; p.sliceMode = SliceMode::Groove;
    p.windowStart = 8; p.windowSize = 4; p.density = 0.5f; p.type = 0.0f;
    int multi = 0, kicksOnOne = 0;
    for (uint32_t i = 0; i < 256; ++i)
    {
        auto st = pat.step (i, p, s);
        if (! st.active) continue;
        CHECK (st.slice >= 8 && st.slice < 12);
        CHECK (st.note == 36 + st.slice);
        for (int c = 0; c < st.chordExtra; ++c) CHECK (st.chord[(size_t) c] >= 36 + 8 && st.chord[(size_t) c] < 36 + 12);
        if (st.chordExtra > 0) ++multi;
        if (i % 16 == 0 && st.slice == 8) ++kicksOnOne;
    }
    CHECK (multi > 20);          // kick + hat together etc.
    CHECK (kicksOnOne == 16);    // the first cell (kick role) always lands on beat 1 at Type 0

    // other modes: note follows the cell
    p.sliceMode = SliceMode::Bounce; p.density = 1.0f;
    for (uint32_t i = 0; i < 64; ++i) { auto st = pat.step (i, p, s); CHECK (st.note == 36 + st.slice && st.chordExtra == 0); }
}

static void testDrumEngine()
{
    Engine eng; eng.prepare (48000.0, 256);
    const auto& kit = factoryKits()[0];
    for (int c = 0; c < numCells; ++c) if (kit.cells[(size_t) c] >= 0) eng.setCell (c, makeFmCell (fmPresets()[(size_t) kit.cells[(size_t) c]]));
    auto& t = eng.track (1);
    t.enabled = true; t.engine = EngineType::Drums;
    t.seq.sliceMode = SliceMode::Groove; t.seq.windowStart = 0; t.seq.windowSize = 4; t.seq.density = 0.6f;
    eng.track (0).enabled = false;

    Transport tp; tp.hostAvailable = true; tp.hostPlaying = true; tp.hostBpm = 125.0;
    std::vector<float> l (480), r (480);
    std::vector<MidiOut> midi;
    double energy = 0.0;
    for (int b = 0; b < 300; ++b)
    {
        eng.process (l.data(), r.data(), 480, tp, midi);
        tp.hostPpq += 480.0 * 125.0 / 60.0 / 48000.0;
        for (int i = 0; i < 480; ++i) energy += (double) l[i] * l[i];
    }
    CHECK (energy > 1.0);
    std::set<int> notes; for (auto& m : midi) if (m.noteOn && m.channel == 2) notes.insert (m.note);
    CHECK (notes.count (36) && notes.count (38) && notes.size() >= 3);

    // replacing a sample cell: voices using the old one stop on the next block
    eng.track (1).enabled = false;
    for (int b = 0; b < 1200; ++b) { eng.process (l.data(), r.data(), 480, tp, midi); tp.hostPpq += 480.0 * 125.0 / 60.0 / 48000.0; }
    SampleData d; d.sampleRate = 48000.0; d.left.assign (480000, 0.3f); d.right = d.left;
    DrumCell sc; sc.kind = DrumCell::Kind::Sample; sc.sample = &d;
    eng.setCell (0, sc);
    eng.track (1).enabled = true;
    eng.track (1).seq.sliceMode = SliceMode::Window; eng.track (1).seq.windowSize = 1; eng.track (1).seq.density = 1.0f;
    for (int b = 0; b < 40; ++b) { eng.process (l.data(), r.data(), 480, tp, midi); tp.hostPpq += 480.0 * 125.0 / 60.0 / 48000.0; }
    float during = 0.0f; for (int i = 0; i < 480; ++i) during = std::max (during, std::abs (l[i]));
    CHECK (during > 0.1f);
    eng.track (1).enabled = false;
    DrumCell empty;
    eng.setCell (0, empty);
    eng.process (l.data(), r.data(), 480, tp, midi);
    eng.process (l.data(), r.data(), 480, tp, midi);
    float peak = 0.0f; for (int i = 0; i < 480; ++i) peak = std::max (peak, std::abs (l[i]));
    CHECK (peak < 0.05f);

    // audition plays through the output with no track running
    eng.setCell (5, makeFmCell (fmPresets()[0]));
    eng.audition (5);
    eng.process (l.data(), r.data(), 480, tp, midi);
    float a = 0.0f; for (int i = 0; i < 480; ++i) a = std::max (a, std::abs (l[i]));
    CHECK (a > 0.01f);
}

// ---------------------------------------------------------------------------
// Effects and mixer

static void testEffects()
{
    const double sr = 48000.0;
    for (int ti = 1; ti < (int) FxType::count; ++ti)
        for (float gn : { 0.0f, 0.5f, 1.0f })
        {
            FxSlot fx; fx.prepare (sr, 128);
            FxParams p; p.type = (FxType) ti; p.a = 0.6f; p.b = 0.7f; p.gnarl = gn; p.mix = 1.0f;
            uint32_t x = 99;
            float peak = 0.0f, inEnergy = 0.0f, outEnergy = 0.0f, tail = 0.0f;
            bool finite = true;
            std::vector<float> l (128), r (128);
            for (int b = 0; b < 1500; ++b)   // 4 s: 1 s of signal, then silence
            {
                for (int i = 0; i < 128; ++i)
                {
                    float v = 0.0f;
                    if (b < 375)
                    {
                        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                        v = 0.3f * std::sin ((b * 128 + i) * 0.03f) + 0.1f * ((float) (x >> 8) / 8388608.0f - 1.0f);
                    }
                    l[(size_t) i] = v; r[(size_t) i] = v * 0.8f;
                    inEnergy += v * v;
                }
                fx.process (l.data(), r.data(), 128, p, 120.0);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                    peak = std::max (peak, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
                    outEnergy += l[(size_t) i] * l[(size_t) i];
                    if (b >= 1400) tail += l[(size_t) i] * l[(size_t) i];
                }
            }
            CHECK (finite);
            CHECK (peak < 8.0f);
            CHECK (outEnergy > inEnergy * 0.01f);   // the effect produces sound
            const bool sustains = gn > 0.1f && ((FxType) ti >= FxType::Plate && (FxType) ti <= FxType::GrainDelay);
            if (! sustains) CHECK (tail < outEnergy * 0.05f);   // tails die away unless gnarled into freeze / runaway feedback
            if (! finite || peak >= 8.0f || ! (outEnergy > inEnergy * 0.01f))
                std::printf ("  fx %s gnarl %.1f: peak %.2f in %.1f out %.1f\n", fxName ((FxType) ti), gn, peak, inEnergy, outEnergy);
        }

    // limiter
    Limiter lim; lim.prepare (sr);
    std::vector<float> l (4800), r (4800);
    for (int i = 0; i < 4800; ++i) { l[(size_t) i] = 3.0f * std::sin (i * 0.05f); r[(size_t) i] = -2.0f; }
    lim.process (l.data(), r.data(), 4800);
    float pk = 0.0f; for (int i = 0; i < 4800; ++i) pk = std::max (pk, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
    CHECK (pk <= 0.9661f);
}

static double runEnergy (Engine& eng, Transport& tp, int blocks, float* lastPeak = nullptr)
{
    std::vector<float> l (480), r (480);
    std::vector<MidiOut> midi;
    double e = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        eng.process (l.data(), r.data(), 480, tp, midi);
        tp.hostPpq += 480.0 * tp.hostBpm / 60.0 / 48000.0;
        for (int i = 0; i < 480; ++i) e += (double) l[(size_t) i] * l[(size_t) i];
    }
    if (lastPeak) { *lastPeak = 0.0f; for (int i = 0; i < 480; ++i) *lastPeak = std::max (*lastPeak, std::abs (l[(size_t) i])); }
    return e;
}

static void testMixer()
{
    Engine eng; eng.prepare (48000.0, 512);
    Transport tp; tp.hostAvailable = true; tp.hostPlaying = true; tp.hostBpm = 120.0;
    eng.track (0).seq.density = 0.8f;
    eng.global().returns[0].type = FxType::None;
    eng.global().returns[1].type = FxType::None;

    const double base = runEnergy (eng, tp, 100);
    CHECK (base > 0.1);

    eng.track (0).mute = true;
    CHECK (runEnergy (eng, tp, 100) < 1.0e-6);
    eng.track (0).mute = false;

    eng.track (1).enabled = true; eng.track (1).solo = true;   // soloing another track silences track 1
    eng.track (1).engine = EngineType::Synth; eng.track (1).seq.density = 0.0f;
    CHECK (runEnergy (eng, tp, 100) < 1.0e-6);
    eng.track (1).solo = false; eng.track (1).enabled = false;

    // reverb send: tail after the track stops
    eng.global().returns[0].type = FxType::Hall; eng.global().returns[0].a = 0.8f;
    eng.track (0).sendA = 1.0f;
    runEnergy (eng, tp, 200);
    eng.track (0).enabled = false;
    float pk = 0.0f;
    runEnergy (eng, tp, 20, &pk);
    CHECK (pk > 1.0e-3f);    // the hall is still ringing
    runEnergy (eng, tp, 3000, &pk);
    CHECK (pk < 1.0e-3f);    // ...and dies out

    // insert on a silent track rings out
    eng.track (0).enabled = true; eng.track (0).sendA = 0.0f;
    eng.track (0).inserts[0].type = FxType::DigitalDelay; eng.track (0).inserts[0].b = 0.5f;
    runEnergy (eng, tp, 100);
    eng.track (0).enabled = false;
    runEnergy (eng, tp, 5, &pk);
    CHECK (pk > 1.0e-3f);
    eng.track (0).inserts[0].type = FxType::None;

    // direct output takes the track out of the main mix
    eng.track (0).enabled = true;
    std::vector<float> dl (480, 0.0f), dr (480, 0.0f), l (480), r (480);
    std::vector<MidiOut> midi;
    double mainE = 0.0, directE = 0.0;
    for (int b = 0; b < 100; ++b)
    {
        std::fill (dl.begin(), dl.end(), 0.0f); std::fill (dr.begin(), dr.end(), 0.0f);
        eng.setDirectOutput (0, dl.data(), dr.data());
        eng.process (l.data(), r.data(), 480, tp, midi);
        tp.hostPpq += 0.02;
        for (int i = 0; i < 480; ++i) { mainE += l[(size_t) i] * l[(size_t) i]; directE += dl[(size_t) i] * dl[(size_t) i]; }
    }
    eng.setDirectOutput (0, nullptr, nullptr);
    CHECK (directE > 0.1 && mainE < 0.01 * directE);

    // limiter keeps the master under the ceiling even with everything cranked
    for (int t = 0; t < numTracks; ++t) { eng.track (t).enabled = true; eng.track (t).level = 1.0f; eng.track (t).seq.density = 1.0f; eng.track (t).voice.drive = 1.0f; }
    eng.global().master = 1.0f;
    eng.global().limiter.driveDb = 12.0f;
    eng.global().glue.on = true; eng.global().glue.makeupDb = 15.0f;
    float loud = 0.0f; std::vector<float> L2 (480), R2 (480);
    for (int b = 0; b < 200; ++b)
    {
        eng.process (L2.data(), R2.data(), 480, tp, midi);
        tp.hostPpq += 0.02;
        for (int i = 0; i < 480; ++i) loud = std::max (loud, std::max (std::abs (L2[(size_t) i]), std::abs (R2[(size_t) i])));
    }
    CHECK (loud <= 0.9661f && loud > 0.3f);
}

// ---------------------------------------------------------------------------
// Modulation

static void testModulation()
{
    for (int sh = 0; sh < (int) LfoShape::count; ++sh)
    {
        Lfo lfo; LfoParams p; p.shape = (LfoShape) sh; p.rateHz = 3.0f;
        float lo = 1.0f, hi = -1.0f;
        for (int i = 0; i < 2000; ++i) { float v = lfo.advance (p, 0.001, 0.0, false); lo = std::min (lo, v); hi = std::max (hi, v); }
        CHECK (lo >= -1.0001f && hi <= 1.0001f && hi - lo > 0.2f);
    }
    // synced: phase follows song position
    Lfo synced; LfoParams sp; sp.sync = true; sp.division = 4; sp.shape = LfoShape::Saw;   // 1 bar
    synced.advance (sp, 0.0, 2.0, true);
    CHECK (std::abs (synced.phase() - 0.5) < 1e-9);

    // Life: smooth and wandering
    Life life; life.setSeed (5);
    float prev = life.advance (0.0, 0.5f), maxStep = 0.0f, lo = 1.0f, hi = -1.0f;
    for (int i = 0; i < 20000; ++i)
    {
        const float v = life.advance (0.005, 0.5f);
        maxStep = std::max (maxStep, std::abs (v - prev)); prev = v;
        lo = std::min (lo, v); hi = std::max (hi, v);
    }
    CHECK (maxStep < 0.05f);
    CHECK (hi - lo > 0.8f && lo >= -1.0f && hi <= 1.0f);

    // matrix: mod wheel pulls track level to zero
    Engine eng; eng.prepare (48000.0, 512);
    eng.global().returns[0].type = eng.global().returns[1].type = FxType::None;
    Transport tp; tp.hostAvailable = true; tp.hostPlaying = true; tp.hostBpm = 120.0;
    eng.track (0).seq.density = 0.9f;
    CHECK (runEnergy (eng, tp, 100) > 0.1);
    eng.matrix (0).set (ModDest::Level, ModSource::ModWheel, -1.0f);
    eng.midiExpression (ModSource::ModWheel, 1.0f);
    CHECK (runEnergy (eng, tp, 100) < 1.0e-4);
    eng.midiExpression (ModSource::ModWheel, 0.0f);
    CHECK (runEnergy (eng, tp, 100) > 0.1);
    eng.matrix (0).clear();

    // Life -> Type: the pattern drifts over time compared with no modulation
    auto notesOver = [&] (bool mod)
    {
        Engine e2; e2.prepare (48000.0, 512);
        e2.track (0).seq.density = 1.0f; e2.track (0).lifeRate = 2.0f;
        if (mod) e2.matrix (0).set (ModDest::Type, ModSource::Life, 1.0f);
        Transport t2; t2.hostAvailable = true; t2.hostPlaying = true; t2.hostBpm = 120.0;
        std::vector<float> l (480), r (480); std::vector<MidiOut> midi; std::vector<int> notes;
        for (int b = 0; b < 400; ++b) { e2.process (l.data(), r.data(), 480, t2, midi); t2.hostPpq += 0.02; }
        for (auto& m : midi) if (m.noteOn) notes.push_back (m.note);
        return notes;
    };
    auto plain = notesOver (false), drifting = notesOver (true);
    int diff = 0;
    for (size_t i = 0; i < std::min (plain.size(), drifting.size()); ++i) if (plain[i] != drifting[i]) ++diff;
    CHECK (diff > 10);
    CHECK (eng.sourceValue (0, ModSource::ModWheel) == 0.0f);
}

// ---------------------------------------------------------------------------
// Swing, jitter, motifs, export

static void testTimingAndMotif()
{
    Pattern pat (31);
    Scale s (0, 0);
    SequencerParams p; p.density = 1.0f; p.gate = 0.4f;

    // swing delays odd steps by swing/2, jitter only ever makes steps late
    p.swing = 0.5f;
    CHECK (pat.stepStart (4, p) == 4.0 && std::abs (pat.stepStart (5, p) - 5.25) < 1e-9);
    p.swing = 0.0f; p.jitter = 1.0f;
    bool lateOnly = true, anyLate = false;
    for (int64_t n = 0; n < 200; ++n) { const double d = pat.stepStart (n, p) - (double) n; lateOnly = lateOnly && d >= 0.0 && d < 0.41; anyLate = anyLate || d > 0.05; }
    CHECK (lateOnly && anyLate);

    // swung playback is still block-size independent and notes land on the swung grid
    p.jitter = 0.0f; p.swing = 0.5f;
    for (int bs : { 50, 333 })
    {
        TrackPlayer pl; std::vector<NoteEvent> ev; std::vector<long> ons;
        for (long st = 0; st < 16 * 100; st += bs)
        {
            ev.clear();
            pl.process (st / 100.0, 0.01, (int) std::min<long> (bs, 1600 - st), pat, p, s, ev);
            for (auto& e : ev) if (e.noteOn) ons.push_back (st + e.sampleOffset);
        }
        CHECK (ons.size() >= 12);   // slides into the same pitch become ties
        for (long o : ons) CHECK (o % 100 == 0 || o % 100 == 25);   // straight or swung grid only
        CHECK (ons.size() > 2 && ons[1] == 125 && ons[2] == 200);
    }

    // motif: at Type 0 it plays exactly; Type morphs away; survives save/load
    std::vector<StepData> motif (8);
    const int notes[8] = { 48, 0, 51, 55, 0, 60, 58, 0 };
    for (int i = 0; i < 8; ++i) { motif[(size_t) i].active = notes[i] != 0; motif[(size_t) i].note = notes[i] != 0 ? notes[i] : 48; motif[(size_t) i].accent = i == 0; motif[(size_t) i].gate = 0.5f; }
    pat.setMotif (motif);
    SequencerParams mp; mp.density = 0.6f; mp.scaleLock = false; mp.type = 0.0f;
    for (uint32_t i = 0; i < 32; ++i)
    {
        auto st = pat.step (i, mp, s);
        CHECK (st.active == (notes[i % 8] != 0));
        if (st.active) CHECK (st.note == notes[i % 8]);
    }
    mp.type = 1.0f;
    int differ = 0;
    for (uint32_t i = 0; i < 64; ++i) { auto st = pat.step (i, mp, s); if (st.active != (notes[i % 8] != 0) || (st.active && st.note != notes[i % 8])) ++differ; }
    CHECK (differ > 20);
    Pattern back; CHECK (back.deserialise (pat.serialise()));
    CHECK (back.getMotif().size() == 8 && back.getMotif()[2].note == 51);

    // export: notes come out in order, in range, with lengths
    mp.type = 0.0f;
    auto notesOut = renderNotes (pat, mp, s, 32);
    CHECK (notesOut.size() == 20);   // 5 notes per 8 steps
    bool ordered = true;
    for (size_t i = 0; i < notesOut.size(); ++i)
    {
        CHECK (notesOut[i].end > notesOut[i].start && notesOut[i].start >= 0.0 && notesOut[i].end <= 32.0);
        if (i > 0) ordered = ordered && notesOut[i].start >= notesOut[i - 1].start;
    }
    CHECK (ordered);
    if (! notesOut.empty()) CHECK (notesOut[0].note == 48 && std::abs (notesOut[0].end - notesOut[0].start - 0.5) < 0.02);

    // export of a Counterpoint track carries both streams
    SequencerParams cp; cp.algorithm = Algorithm::Counterpoint; cp.density = 0.6f;
    Pattern cpPat (7);
    auto cpNotes = renderNotes (cpPat, cp, s, 64);
    bool s0 = false, s1 = false; for (auto& n : cpNotes) { s0 |= n.stream == 0; s1 |= n.stream == 1; }
    CHECK (s0 && s1);
}

static void testOfflineRender()
{
    Engine live; live.prepare (48000.0, 256);
    live.track (0).seq.density = 0.8f;
    live.track (1).enabled = true; live.track (1).engine = EngineType::Drums; live.track (1).seq.sliceMode = SliceMode::Groove;
    for (int c = 0; c < numCells; ++c) live.setCell (c, makeFmCell (fmPresets()[(size_t) factoryKits()[0].cells[(size_t) c]]));
    live.track (0).inserts[0].type = FxType::Hall; live.track (0).inserts[0].a = 0.8f;

    RenderOptions o; o.track = 0; o.bars = 2; o.bpm = 120.0; o.sampleRate = 48000.0;
    std::vector<float> l, r, l2, r2;
    Engine::renderOffline (live, o, l, r);
    CHECK (l.size() == 192000 && r.size() == 192000);
    double e = 0.0; bool finite = true; for (float v : l) { e += v * v; finite = finite && std::isfinite (v); }
    CHECK (finite && e > 1.0);

    Engine::renderOffline (live, o, l2, r2);
    CHECK (l == l2);                       // deterministic: same state, same audio

    o.withFx = false;
    Engine::renderOffline (live, o, l2, r2);
    double diff = 0.0; for (size_t i = 0; i < l.size(); ++i) diff += std::abs (l[i] - l2[i]);
    CHECK (diff > 1.0);                    // the insert reverb was left out

    // track 2 alone is the drums: track 1's synth must not be in it
    o.track = 1; o.withFx = true;
    Engine::renderOffline (live, o, l2, r2);
    double de = 0.0; for (float v : l2) de += v * v;
    CHECK (de > 1.0);

    // full mix is louder than either part on its own, and limited
    o.track = -1;
    Engine::renderOffline (live, o, l2, r2);
    float pk = 0.0f; double me = 0.0; for (float v : l2) { pk = std::max (pk, std::abs (v)); me += v * v; }
    CHECK (me > e * 0.5 && pk <= 0.97f * 1.6f);   // tail wrap can sum a little above the limiter at the seam

    // rendering must not disturb the live engine
    CHECK (live.track (0).enabled && live.track (1).enabled);
}

// Regression: reverbs used to drone forever (shimmer feedback leaking into every reverb)
// and pile their tail onto the left side. Every reverb and delay must die away when the
// input stops (unless frozen / pushed into self-oscillation on purpose), and a mono input
// must give a balanced tail.
static void testNoDronesOrLopsidedTails()
{
    const double sr = 48000.0; const int bs = 128;
    uint32_t seed = 12345;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
    for (int t = (int) FxType::Plate; t <= (int) FxType::GrainDelay; ++t)
        for (int trial = 0; trial < 6; ++trial)
        {
            FxSlot fx; fx.prepare (sr, bs);
            FxParams p; p.type = (FxType) t; p.a = rnd(); p.b = rnd(); p.gnarl = trial == 0 ? 0.0f : rnd() * 0.9f;
            if (t >= (int) FxType::TapeDelay) p.b = std::min (p.b, 0.9f);   // knob alone, below self-oscillation
            std::vector<float> l ((size_t) bs), r ((size_t) bs);
            double early = 0, late = 0, tailL = 0, tailR = 0;
            const int blocks = (int) (14.0 * sr / bs);
            for (int b = 0; b < blocks; ++b)
            {
                const double ts = (double) b * bs / sr;
                for (int i = 0; i < bs; ++i) { const float x = ts < 0.5 ? (rnd() * 2.0f - 1.0f) * 0.5f : 0.0f; l[(size_t) i] = r[(size_t) i] = x; }
                fx.process (l.data(), r.data(), bs, p, 120.0);
                for (int i = 0; i < bs; ++i)
                {
                    const double e = (double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i];
                    if (ts >= 8.0 && ts < 10.0) early += e;
                    if (ts >= 12.0)             late += e;
                    if (ts >= 0.5 && ts < 1.5)  { tailL += (double) l[(size_t) i] * l[(size_t) i]; tailR += (double) r[(size_t) i] * r[(size_t) i]; }
                }
            }
            const bool dies = late < 1.0e-9 * sr || late < early * 0.5;
            if (! dies) std::printf ("  drone: %s a %.2f b %.2f g %.2f\n", fxName ((FxType) t), p.a, p.b, p.gnarl);
            CHECK (dies);
            if (t <= (int) FxType::Shimmer && tailL + tailR > 1.0e-6)
            {
                const double imbalance = std::abs (10.0 * std::log10 ((tailL + 1e-12) / (tailR + 1e-12)));
                if (imbalance > 3.5) std::printf ("  lopsided: %s %.1f dB\n", fxName ((FxType) t), imbalance);
                CHECK (imbalance <= 3.5);
            }
        }
}

// ---- step sequencer -------------------------------------------------------------
namespace
{
    struct Hit { int64_t at; bool on; int note, vel; };

    // run a StepPlayer for `samples` at 120 bpm, 48 kHz, 1/16 steps; absolute sample times
    std::vector<Hit> runSteps (const StepPattern& p, StepContext c, int64_t samples, int block)
    {
        StepPlayer pl;
        std::vector<NoteEvent> ev;
        std::vector<Hit> hits;
        const double sps = 120.0 / 60.0 / 48000.0 * c.stepsPerQuarter;
        for (int64_t start = 0; start < samples; start += block)
        {
            const int n = (int) std::min<int64_t> (block, samples - start);
            ev.clear();
            pl.process ((double) start * sps, sps, n, p, c, ev);
            for (auto& e : ev) hits.push_back ({ start + e.sampleOffset, e.noteOn, e.note, e.velocity });
        }
        return hits;
    }
    StepContext drumCtx()
    {
        StepContext c; c.stepsPerQuarter = 4.0; c.oneShot = true;
        for (int l = 0; l < stepLanes; ++l) { c.laneNote[(size_t) l] = drumBaseNote + l; c.laneSlice[(size_t) l] = l; }
        return c;
    }
    int countOn (const std::vector<Hit>& h, int note = -1)
    {
        int n = 0; for (auto& x : h) if (x.on && (note < 0 || x.note == note)) ++n; return n;
    }
}

static void testStepSequencer()
{
    using namespace stepgen;
    // Euclid
    auto e38 = euclid (3, 8, 0);
    CHECK ((e38 == std::vector<bool> { true, false, false, true, false, false, true, false }));
    auto e4 = euclid (4, 16, 0);
    for (int i = 0; i < 16; ++i) CHECK (e4[(size_t) i] == (i % 4 == 0));
    for (int p = 0; p <= 16; ++p) { int n = 0; for (bool b : euclid (p, 16, 5)) n += b; CHECK (n == p); }
    CHECK (euclid (1, 8, 3)[3]);

    // four on the floor: exact sample positions (a 1/16 step = 6000 samples at 120 bpm)
    StepPattern p;
    for (int s : { 0, 4, 8, 12 }) { p.lanes[0].steps[(size_t) s].on = true; }
    auto h = runSteps (p, drumCtx(), 192000, 480);
    CHECK (countOn (h) == 8);
    for (size_t i = 0; i < h.size(); ++i) CHECK (h[i].at == (int64_t) i * 24000 && h[i].note == drumBaseNote);

    // block-size independent
    auto h64 = runSteps (p, drumCtx(), 192000, 64), h997 = runSteps (p, drumCtx(), 192000, 997);
    CHECK (h64.size() == h.size() && h997.size() == h.size());
    for (size_t i = 0; i < std::min (h.size(), h997.size()); ++i) CHECK (h64[i].at == h[i].at && h997[i].at == h[i].at);

    // ratchet: 4 hits inside one step, velocity rising
    StepPattern rp;
    auto& rs = rp.lanes[3].steps[2]; rs.on = true; rs.ratchet = 4; rs.shape = RatchetShape::Up; rs.vel = 120;
    auto rh = runSteps (rp, drumCtx(), 96000, 480);
    CHECK (countOn (rh) == 4);
    if (rh.size() == 4)
    {
        for (int i = 0; i < 4; ++i) CHECK (rh[(size_t) i].at == 12000 + i * 1500 && rh[(size_t) i].note == drumBaseNote + 3);
        CHECK (rh[0].vel < rh[1].vel && rh[1].vel < rh[2].vel && rh[3].vel == 120);
    }

    // nudge: -25% of a step = 1500 samples early
    StepPattern np; np.lanes[1].steps[4].on = true; np.lanes[1].steps[4].nudge = -25;
    auto nh = runSteps (np, drumCtx(), 96000, 333);
    CHECK (! nh.empty() && nh[0].at == 24000 - 1500);

    // conditions: 1:2 on a 4-step lane plays every other loop
    StepPattern cp; cp.lanes[0].length = 4; cp.lanes[0].steps[0].on = true; cp.lanes[0].steps[0].cond = TrigCond::R1of2;
    auto ch = runSteps (cp, drumCtx(), 8 * 24000, 480);
    CHECK (countOn (ch) == 4);
    for (auto& x : ch) CHECK (x.at % 48000 == 0);

    // fill: Fill steps only with the button; Not fill only without; auto fill every 2 bars plays in bar 2
    StepPattern fp;
    fp.lanes[0].steps[0].on = true; fp.lanes[0].steps[0].cond = TrigCond::Fill;
    fp.lanes[1].steps[0].on = true; fp.lanes[1].steps[0].cond = TrigCond::NotFill;
    auto c0 = drumCtx();
    auto f0 = runSteps (fp, c0, 4 * 96000, 480);
    CHECK (countOn (f0, drumBaseNote) == 0 && countOn (f0, drumBaseNote + 1) == 4);
    c0.fill = true;
    auto f1 = runSteps (fp, c0, 4 * 96000, 480);
    CHECK (countOn (f1, drumBaseNote) == 4 && countOn (f1, drumBaseNote + 1) == 0);
    c0.fill = false; c0.autoFillBars = 2;
    auto f2 = runSteps (fp, c0, 4 * 96000, 480);
    CHECK (countOn (f2, drumBaseNote) == 2 && countOn (f2, drumBaseNote + 1) == 2);
    for (auto& x : f2) if (x.note == drumBaseNote) CHECK ((x.at / 96000) % 2 == 1);

    // probability: ~50% over many loops, the same every time
    StepPattern pp; pp.lanes[0].length = 1; pp.lanes[0].steps[0].on = true; pp.lanes[0].steps[0].prob = 50; pp.lanes[0].seed = 99;
    auto p1 = runSteps (pp, drumCtx(), 600 * 6000, 4800), p2 = runSteps (pp, drumCtx(), 600 * 6000, 512);
    CHECK (countOn (p1) > 240 && countOn (p1) < 360);
    CHECK (p1.size() == p2.size());

    // polymeter + speed: a 3-step lane against 16, and x2 speed doubles the hits
    StepPattern mp; mp.lanes[0].length = 3; mp.lanes[0].steps[0].on = true;
    mp.lanes[1].steps[0].on = true; mp.lanes[1].speed = 5;   // x2
    auto mh = runSteps (mp, drumCtx(), 96000 * 3, 480);
    CHECK (countOn (mh, drumBaseNote) == 16);
    CHECK (countOn (mh, drumBaseNote + 1) == 6);
    CHECK (std::string (laneSpeeds()[5].first) == "x2");

    // mute
    mp.lanes[0].mute = true;
    CHECK (countOn (runSteps (mp, drumCtx(), 96000, 480), drumBaseNote) == 0);

    // melodic lanes: every note-on gets a note-off, nothing left hanging
    StepPattern sp; sp.lanes[2].steps[0].on = true; sp.lanes[5].steps[8].on = true; sp.lanes[5].steps[8].ratchet = 2;
    auto sc = drumCtx(); sc.oneShot = false; sc.gate = 0.9f;
    for (int l = 0; l < stepLanes; ++l) sc.laneNote[(size_t) l] = 48 + l;
    auto sh = runSteps (sp, sc, 96000 * 2, 480);
    int ons = 0, offs = 0;
    for (auto& x : sh) (x.on ? ons : offs)++;
    CHECK (ons == 6 && offs == ons);

    // evolve: loop 0 is the written pattern, later loops vary, always the same way
    Lane ev; ev.length = 16; for (int s : { 0, 4, 8, 12 }) ev.steps[(size_t) s].on = true; ev.evolve = 1.0f; ev.seed = 5;
    bool differs = false, same = true;
    for (int s = 0; s < 16; ++s) same = same && StepPlayer::stepPlays (ev, 0, s, 0, false) == ev.steps[(size_t) s].on;
    for (int loop = 1; loop < 8; ++loop)
        for (int s = 0; s < 16; ++s)
        {
            const bool a = StepPlayer::stepPlays (ev, 0, s, loop, false);
            differs = differs || a != ev.steps[(size_t) s].on;
            CHECK (a == StepPlayer::stepPlays (ev, 0, s, loop, false));
        }
    CHECK (same && differs);

    // generators
    Lane kick; kick.algo = LaneAlgo::Groove; kick.density = 0.0f;
    generate (kick, LaneRole::Kick, 1);
    for (int s = 0; s < 16; ++s) CHECK (kick.steps[(size_t) s].on == (s % 4 == 0));
    Lane eu; eu.algo = LaneAlgo::Euclid; eu.pulses = 5; eu.length = 13;
    generate (eu, LaneRole::Perc, 3);
    { int n = 0; for (int s = 0; s < 13; ++s) n += eu.steps[(size_t) s].on; CHECK (n == 5); }
    for (int s = 13; s < maxSteps; ++s) CHECK (! eu.steps[(size_t) s].on);
    Lane r1, r2; r1.algo = r2.algo = LaneAlgo::Euclid;
    rollLane (r1, LaneRole::Snare, 42); rollLane (r2, LaneRole::Snare, 42);
    CHECK (r1.steps == r2.steps && r1.pulses == r2.pulses);
    Lane rolls; rolls.algo = LaneAlgo::Rolls; rolls.density = 0.9f;
    generate (rolls, LaneRole::ClosedHat, 7);
    bool anyRatchet = false, fillRoll = false;
    for (auto& st : rolls.steps) { anyRatchet = anyRatchet || st.ratchet > 1; fillRoll = fillRoll || (st.on && st.cond == TrigCond::Fill); }
    CHECK (anyRatchet && fillRoll);
    for (int a = 0; a < (int) LaneAlgo::count; ++a)
        for (int role = 0; role <= (int) LaneRole::Melodic; ++role)
        {
            Lane l; l.algo = (LaneAlgo) a; l.length = (uint8_t) (5 + 7 * role % 60);
            rollLane (l, (LaneRole) role, (uint64_t) (a * 31 + role));
            for (int s = l.length; s < maxSteps; ++s) CHECK (! l.steps[(size_t) s].on || l.algo == LaneAlgo::Mutate || l.algo == LaneAlgo::Humanize);
        }

    // save / load
    StepPattern full;
    for (int l = 0; l < stepLanes; ++l) { full.lanes[(size_t) l].algo = LaneAlgo::Rolls; full.lanes[(size_t) l].length = (uint8_t) (16 + l); rollLane (full.lanes[(size_t) l], drumLaneRole (l), (uint64_t) l); }
    full.lanes[3].steps[7].nudge = -13; full.lanes[3].steps[7].prob = 37; full.lanes[3].mute = true; full.lanes[4].evolve = 0.3f;
    StepPattern back;
    CHECK (back.deserialise (full.serialise()));
    bool equal = true;
    for (int l = 0; l < stepLanes; ++l)
    {
        const auto& a = full.lanes[(size_t) l]; const auto& b = back.lanes[(size_t) l];
        equal = equal && a.steps == b.steps && a.length == b.length && a.speed == b.speed && a.mute == b.mute && a.algo == b.algo
                && a.seed == b.seed && std::abs (a.evolve - b.evolve) < 1e-4f;
    }
    CHECK (equal);
    CHECK (! back.deserialise ("nonsense"));

    // export matches playback
    auto exp = renderStepNotes (p, drumCtx(), 32);
    CHECK ((int) exp.size() == 8 && std::abs (exp[1].start - 4.0) < 1e-9);

    // engine: a drum track in step mode plays its pads and stops cleanly
    Engine eng;
    eng.prepare (48000.0, 256);
    eng.global().hostSync = false; eng.global().playing = true; eng.global().tempo = 120.0;
    for (int t = 0; t < numTracks; ++t) eng.track (t).enabled = false;
    eng.setCell (0, makeFmCell (fmPresets()[0]));
    eng.setCell (3, makeFmCell (fmPresets()[3]));
    auto& tr = eng.track (2);
    tr.enabled = true; tr.engine = EngineType::Drums; tr.stepMode = true; tr.rateIndex = 1;
    {
        std::lock_guard<SpinMutex> g (eng.stepMutex (2));
        auto& sp2 = eng.stepPattern (2);
        for (int s : { 0, 4, 8, 12 }) sp2.lanes[0].steps[(size_t) s].on = true;
        for (int s = 2; s < 16; s += 4) sp2.lanes[3].steps[(size_t) s].on = true;
    }
    std::vector<float> l (480), r (480);
    std::vector<MidiOut> midi;
    double peak = 0;
    for (int b = 0; b < 200; ++b)   // 2 s = 1 bar at 120
    {
        eng.process (l.data(), r.data(), 480, Transport{}, midi);
        for (int i = 0; i < 480; ++i) peak = std::max (peak, (double) std::abs (l[(size_t) i]));
    }
    int kicks = 0, hats = 0;
    for (auto& m : midi) if (m.noteOn && m.channel == 3) { if (m.note == drumBaseNote) ++kicks; if (m.note == drumBaseNote + 3) ++hats; }
    CHECK (kicks == 4 && hats == 4);
    CHECK (peak > 0.01);
    CHECK (eng.stepLanePosition (2, 0) >= 0);
    eng.global().playing = false;
    eng.process (l.data(), r.data(), 480, Transport{}, midi);
    CHECK (eng.stepLanePosition (2, 0) == -1);
}

// ---- warp ---------------------------------------------------------------------------
namespace
{
    // kick on every beat, hats on the off-beats: a simple loop at a known tempo
    SampleData makeBeat (double bpm, int beats, double sr, double extraSeconds)
    {
        SampleData d; d.sampleRate = sr;
        const int n = (int) std::lround (beats * 60.0 / bpm * sr + extraSeconds * sr);
        d.left.assign ((size_t) n, 0.0f);
        uint32_t seed = 3;
        for (int b = 0; b < beats * 2; ++b)
        {
            const int start = (int) std::lround (b * 30.0 / bpm * sr);
            const bool kick = b % 2 == 0;
            const int len = (int) (sr * (kick ? 0.18 : 0.05));
            for (int i = 0; i < len && start + i < n; ++i)
            {
                const double t = i / sr;
                float y;
                if (kick) y = 0.8f * (float) (std::sin (2.0 * 3.14159265 * (55.0 + 90.0 * std::exp (-t * 40.0)) * t) * std::exp (-t * 18.0));
                else { seed = seed * 1664525u + 1013904223u; y = 0.25f * ((float) (seed >> 8) / 8388608.0f - 1.0f) * (float) std::exp (-t * 70.0); }
                d.left[(size_t) (start + i)] += y;
            }
        }
        d.right = d.left;
        return d;
    }

    double rms (const std::vector<float>& x)
    {
        double s = 0; for (float v : x) s += (double) v * v; return std::sqrt (s / std::max<size_t> (1, x.size()));
    }
}

static void testWarp()
{
    using namespace warp;
    // loops snap to their exact length
    auto loop100 = makeBeat (100.0, 16, 44100.0, 0.0);
    auto e = detectTempo (loop100);
    CHECK (std::abs (e.bpm - 100.0) < 0.01 && e.beats == 16);
    auto loop174 = makeBeat (174.0, 32, 48000.0, 0.0);
    CHECK (std::abs (detectTempo (loop174).bpm - 174.0) < 0.05);

    // not a clean loop: the beat itself is found (within 2%)
    for (double bpm : { 87.0, 112.0, 128.0, 140.0 })
    {
        auto d = makeBeat (bpm, 16, 48000.0, 0.37);
        const double got = detectTempo (d).bpm;
        if (std::abs (got / bpm - 1.0) > 0.02) std::printf ("  tempo %.1f detected as %.2f\n", bpm, got);
        CHECK (std::abs (got / bpm - 1.0) <= 0.02);
    }

    // stretch: exact length, pitch kept, level kept
    SampleData sine; sine.sampleRate = 48000.0;
    for (int i = 0; i < 96000; ++i) sine.left.push_back (0.5f * (float) std::sin (2.0 * 3.14159265 * 440.0 * i / 48000.0));
    sine.right = sine.left;
    for (double ratio : { 0.8, 1.25, 1.6 })
    {
        auto s2 = stretch (sine, ratio);
        CHECK ((int) s2->numFrames() == (int) std::lround (96000 * ratio));
        int crossings = 0;
        for (size_t i = 4800; i + 1 < s2->left.size() - 4800; ++i) if (s2->left[i] < 0 && s2->left[i + 1] >= 0) ++crossings;
        const double hz = crossings / ((s2->left.size() - 9600) / 48000.0);
        CHECK (std::abs (hz - 440.0) < 5.0);
        const double level = 20.0 * std::log10 (rms (s2->left) / rms (sine.left));
        CHECK (std::abs (level) < 1.5);
    }

    // a 100 bpm loop warped to 125: kicks land on the 125 bpm grid
    auto w = stretch (loop100, ratioFor (100.0, 125.0));
    CHECK (std::abs ((double) w->numFrames() - loop100.numFrames() * 0.8) <= 1.0);
    for (int beat = 1; beat < 15; ++beat)
    {
        const int expected = (int) std::lround (beat * 60.0 / 125.0 * 44100.0);
        int onset = -1;
        for (int i = expected - 1500; i < expected + 1500; ++i)
            if (std::abs (w->left[(size_t) i]) > 0.3f) { onset = i; break; }
        const double errMs = onset < 0 ? 1e9 : std::abs (onset - expected) / 44.1;
        if (errMs > 12.0) std::printf ("  beat %d off by %.1f ms\n", beat, errMs);
        CHECK (errMs <= 12.0);
    }
    CHECK (stretch (loop100, 1.0)->left == loop100.left);   // no-op at the same tempo
    // tempo in file names
    CHECK (warp::tempoFromName ("Reese 174") == 174.0);
    CHECK (warp::tempoFromName ("dark_loop_140bpm_Fmin") == 140.0);
    CHECK (warp::tempoFromName ("Break 86.5 BPM") == 86.5);
    CHECK (warp::tempoFromName ("kick 01") == 0.0);
    CHECK (warp::tempoFromName ("909 hat") == 0.0);
    CHECK (warp::tempoFromName ("song 2024 mix") == 0.0);
    CHECK (warp::tempoFromName ("loop_120") == 120.0);
}

// ---- factory library and grooves ------------------------------------------------
static void testLibrary()
{
    const auto& shots = library::oneShots();
    CHECK (shots.size() >= 120);
    std::set<std::string> names;
    for (const auto& s : shots)
    {
        CHECK (names.insert (s.category + "/" + s.name).second);   // unique
        auto d = s.render();
        bool finite = d->numFrames() > 32 && d->right.size() == d->left.size();
        float peak = 0.0f;
        for (uint32_t i = 0; i < d->numFrames() && finite; ++i)
        {
            finite = std::isfinite (d->left[i]) && std::isfinite (d->right[i]);
            peak = std::max ({ peak, std::abs (d->left[i]), std::abs (d->right[i]) });
        }
        if (! finite || peak > 0.95f || peak < 0.05f) std::printf ("  bad one-shot %s/%s peak %.2f\n", s.category.c_str(), s.name.c_str(), peak);
        CHECK (finite && peak <= 0.95f && peak >= 0.05f);
        CHECK (d->sampleRate == library::renderRate);
    }
    // the same recipe renders the same sound every time
    CHECK (shots[0].render()->left == shots[0].render()->left);

    for (const auto& l : library::loops())
    {
        auto d = l.render();
        CHECK (std::abs ((double) d->numFrames() - l.bars * 4.0 * 60.0 / l.bpm * d->sampleRate) <= 1.0);
        CHECK (l.name.find (std::to_string ((int) l.bpm)) != std::string::npos);   // tempo in the name
        // (library loops load with their tempo from the name, so detection isn't needed for them)
        CHECK (std::abs (warp::tempoFromName (l.name) - l.bpm) < 1.0e-9);
    }
    CHECK (library::loops().size() >= 25);

    CHECK (library::kits().size() >= 10);
    for (const auto& k : library::kits())
    {
        int used = 0;
        for (const auto& p : k.pads)
        {
            if (p.sound.empty()) continue;
            ++used;
            if (library::findOneShot (p.sound) == nullptr) std::printf ("  kit %s: no sound %s\n", k.name.c_str(), p.sound.c_str());
            CHECK (library::findOneShot (p.sound) != nullptr);
        }
        CHECK (used >= 12);
        CHECK (k.pads[0].sound.rfind ("Kicks/", 0) == 0);   // lane 1 is a kick in every kit
    }

    // grooves
    const auto& gs = grooves::all();
    CHECK (gs.size() >= 40);
    std::set<std::string> styles;
    for (const auto& g : gs)
    {
        styles.insert (g.style);
        StepPattern p;
        grooves::apply (g, p);
        CHECK (p.activeLanes() >= 3);
        CHECK (p.lanes[0].steps[0].on || p.lanes[1].steps[0].on || g.lanes[0].empty() || g.lanes[0][0] != '.');
        for (int l = 0; l < stepLanes; ++l)
        {
            const auto& str = g.lanes[(size_t) l];
            for (char c : str) CHECK (std::string (".xXgortRfn2<>").find (c) != std::string::npos);
            if (! str.empty()) CHECK (p.lanes[(size_t) l].length == str.size());
        }
        CHECK (g.bpm >= 80 && g.bpm <= 180 && g.swing >= 0.0f && g.swing <= 0.75f);
    }
    CHECK (styles.size() >= 10);
    // a groove's letters become the right steps
    StepPattern p;
    grooves::apply (gs[0], p);
    CHECK (p.lanes[0].steps[0].on && ! p.lanes[0].steps[1].on && p.lanes[0].steps[10].on);
}

// ---- master: glue compressor and limiter -----------------------------------------
static void testMasterDynamics()
{
    auto runGlue = [] (GlueParams p, double hz, float amp, double seconds = 1.5)
    {
        GlueCompressor g; g.prepare (48000.0);
        std::vector<float> l (480), r (480);
        double e = 0; long n = 0;
        for (int b = 0; b < (int) (seconds * 100); ++b)
        {
            for (int i = 0; i < 480; ++i) l[(size_t) i] = r[(size_t) i] = amp * (float) std::sin (2.0 * 3.141592653589793 * hz * (b * 480 + i) / 48000.0);
            g.process (l.data(), r.data(), 480, p);
            if (b >= (int) (seconds * 100) - 30) for (float v : l) { e += v * v; ++n; }
        }
        return std::make_pair (20.0 * std::log10 (std::sqrt (e / n) * 1.41421356), g.reductionDb());
    };
    GlueParams p; p.on = true; p.thresholdDb = -20.0f; p.ratio = 1; p.attack = 2; p.release = 1; p.sidechainHpf = 0;
    // a -6 dB sine, 14 dB over the threshold at 4:1 -> about 10.5 dB of reduction
    auto [outDb, gr] = runGlue (p, 440.0, 0.5f);
    CHECK (std::abs (gr - 10.5f) < 1.5f);
    CHECK (std::abs (outDb - (-6.02 - gr)) < 0.5);
    // 10:1 squashes harder, 2:1 less
    p.ratio = 2; CHECK (runGlue (p, 440.0, 0.5f).second > gr + 1.5f);
    p.ratio = 0; CHECK (runGlue (p, 440.0, 0.5f).second < gr - 1.5f);
    p.ratio = 1;
    // below the threshold nothing happens; makeup adds gain; mix 0 is dry
    CHECK (runGlue (p, 440.0, 0.02f).second < 0.1f);
    p.makeupDb = 6.0f; CHECK (runGlue (p, 440.0, 0.5f).first > outDb + 5.0);
    p.makeupDb = 0.0f; p.mix = 0.0f; CHECK (std::abs (runGlue (p, 440.0, 0.5f).first + 6.02) < 0.2);
    p.mix = 1.0f;
    // the sidechain high-pass stops a sub-bass from pumping everything
    const float subGr = runGlue (p, 40.0, 0.5f).second;
    p.sidechainHpf = 3;
    CHECK (runGlue (p, 40.0, 0.5f).second < subGr - 3.0f);
    // off = untouched
    p.on = false; CHECK (std::abs (runGlue (p, 440.0, 0.5f).first + 6.02) < 0.1);

    // limiter: ceiling holds, drive pushes the level up to it
    Limiter lim; lim.prepare (48000.0);
    LimiterParams lp; lp.ceilingDb = -6.0f; lp.driveDb = 12.0f; lp.releaseMs = 50.0f;
    std::vector<float> l (480), r (480);
    float peak = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        for (int i = 0; i < 480; ++i) l[(size_t) i] = r[(size_t) i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (b * 480 + i) / 48000.0);
        lim.process (l.data(), r.data(), 480, lp);
        for (float v : l) peak = std::max (peak, std::abs (v));
    }
    CHECK (peak <= 0.5012f && peak > 0.45f);   // -6 dBFS
    CHECK (lim.reductionDb() > 3.0f);
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
    testSlicePlacement();
    testSlicePicking();
    testSliceVoice();
    testSlicerEngine();
    testScaleSteps();
    testAlgorithms();
    testMachines();
    testEngineMidiIn();
    testFmDrums();
    testDrumSamplesAndChoke();
    testDrumGroove();
    testDrumEngine();
    testEffects();
    testMixer();
    testModulation();
    testTimingAndMotif();
    testOfflineRender();
    testNoDronesOrLopsidedTails();
    testStepSequencer();
    testWarp();
    testLibrary();
    testMasterDynamics();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
