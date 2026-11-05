// BASH! core unit tests — no framework, just run the binary.
#include "bash/Engine.h"
#include "bash/Scale.h"
#include "bash/Sequencer.h"
#include "bash/Slices.h"
#include "bash/MachineSynth.h"

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
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
