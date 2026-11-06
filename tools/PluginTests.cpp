// BASH! plugin-level tests: Roll the Dice, undo, protected knobs, state save/restore.
#include "../plugin/PluginProcessor.h"

#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static float value (BashProcessor& p, const juce::String& id) { return p.apvts.getParameter (id)->getValue(); }

static void testDice()
{
    BashProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    // the die rolls only the chosen track's sequencer
    std::vector<std::pair<juce::String, float>> before;
    for (auto* prm : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            before.emplace_back (rp->getParameterID(), rp->getValue());
    p.diceLocked.add ("t2_gate");
    const auto seed2 = p.patternSeed (1), seed1 = p.patternSeed (0);
    p.dice.chaos = 1.0f;
    p.rollDice (1);

    int seqChanged = 0, otherChanged = 0;
    for (const auto& [id, v] : before)
    {
        if (std::abs (value (p, id) - v) < 1.0e-6f) continue;
        if (BashProcessor::isDiceParam (id, 1)) ++seqChanged;
        else { ++otherChanged; std::printf ("  dice changed %s\n", id.toRawUTF8()); }
    }
    CHECK (seqChanged >= 5);
    CHECK (otherChanged == 0);                        // no sound, FX, mixer, key, tempo or other tracks
    CHECK (! BashProcessor::isDiceParam ("t2_cutoff", 1) && ! BashProcessor::isDiceParam ("t2_fx1gnarl", 1));
    CHECK (! BashProcessor::isDiceParam ("t2_algo", 1) && ! BashProcessor::isDiceParam ("t2_octave", 1));
    CHECK (BashProcessor::isDiceParam ("t2_density", 1) && ! BashProcessor::isDiceParam ("t2_density", 0));
    for (const auto& [id, v] : before) if (id == "t2_gate") CHECK (std::abs (value (p, id) - v) < 1.0e-6f);   // protected
    CHECK (p.patternSeed (1) != seed2);  // new pattern
    CHECK (p.patternSeed (0) == seed1);  // other track untouched

    // per-track undo
    CHECK (p.diceUndoCount (1) == 1 && p.diceUndoCount (0) == 0);
    CHECK (p.undoDice (1));
    for (const auto& [id, v] : before) CHECK (std::abs (value (p, id) - v) < 1.0e-6f);
    CHECK (p.patternSeed (1) == seed2);

    // gentle chaos stays near the current values
    p.dice.chaos = 0.1f;
    const float d0 = value (p, "t1_density");
    p.rollDice (0);
    CHECK (std::abs (value (p, "t1_density") - d0) <= 0.1001f);
    for (int i = 0; i < 20; ++i) p.rollDice (0);
    CHECK (p.diceUndoCount (0) == 16);
}

static void testState()
{
    BashProcessor a;
    a.setPlayConfigDetails (0, 2, 48000.0, 512);
    a.prepareToPlay (48000.0, 512);
    a.apvts.getParameter ("t3_density")->setValueNotifyingHost (0.123f);
    a.apvts.getParameter ("t3_engine")->setValueNotifyingHost (1.0f);   // drums
    a.matrix (2).set (bash::ModDest::Cutoff, bash::ModSource::Lfo2, -0.5f);
    a.toggleLock (0, 3);
    a.setCellFm (5, 21);
    a.setCellName (5, "My Metal");

    juce::MemoryBlock state;
    a.getStateInformation (state);

    BashProcessor b;
    b.setPlayConfigDetails (0, 2, 48000.0, 512);
    b.prepareToPlay (48000.0, 512);
    b.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (std::abs (value (b, "t3_density") - value (a, "t3_density")) < 1.0e-6f);
    CHECK (b.engineType (2) == bash::EngineType::Drums);
    CHECK (b.matrix (2).get (bash::ModDest::Cutoff, bash::ModSource::Lfo2) == -0.5f);
    CHECK (b.lockMask (0, 3) == bash::lockbit::all);
    CHECK (b.cell (5).kind == bash::DrumCell::Kind::Fm && b.cellName (5) == "My Metal");
    CHECK (std::abs (b.cell (5).fm.ratio - a.cell (5).fm.ratio) < 1.0e-4f);

    // same seed, same music: the patterns match after restore
    auto sa = a.snapshot (0, 0, 64), sb = b.snapshot (0, 0, 64);
    bool same = sa.size() == sb.size();
    for (size_t i = 0; same && i < sa.size(); ++i) same = sa[i].note == sb[i].note && sa[i].active == sb[i].active;
    CHECK (same);
}

static void testProcessing()
{
    // a few seconds through the real processBlock with every engine on, including drums and FX
    BashProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    p.apvts.getParameter ("sync")->setValueNotifyingHost (0.0f);
    p.apvts.getParameter ("play")->setValueNotifyingHost (1.0f);
    for (int t = 1; t <= 8; ++t) p.apvts.getParameter ("t" + juce::String (t) + "_on")->setValueNotifyingHost (1.0f);
    p.apvts.getParameter ("t1_fx1type")->setValueNotifyingHost (p.apvts.getParameter ("t1_fx1type")->convertTo0to1 ((float) bash::FxType::Shimmer));
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    float peak = 0.0f; bool finite = true; int midiEvents = 0;
    for (int b = 0; b < 600; ++b)
    {
        midi.clear();
        p.processBlock (buf, midi);
        midiEvents += midi.getNumEvents();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
            {
                finite = finite && std::isfinite (buf.getSample (c, i));
                peak = std::max (peak, std::abs (buf.getSample (c, i)));
            }
    }
    CHECK (finite);
    CHECK (peak > 0.05f && peak <= 0.9661f);
    CHECK (midiEvents > 50);
}

static void testExports()
{
    BashProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.apvts.getParameter ("tempo")->setValueNotifyingHost (p.apvts.getParameter ("tempo")->convertTo0to1 (120.0f));
    p.apvts.getParameter ("sync")->setValueNotifyingHost (0.0f);
    p.apvts.getParameter ("t1_density")->setValueNotifyingHost (0.8f);
    p.exportBars = 2;

    // MIDI out, then back in on another track: the phrase survives
    const auto mid = p.exportMidi (0);
    CHECK (mid.existsAsFile());
    CHECK (p.importMidi (2, mid).isEmpty());
    CHECK (p.hasMotif (2));
    p.apvts.getParameter ("t3_algo")->setValueNotifyingHost (p.apvts.getParameter ("t1_algo")->getValue());
    p.apvts.getParameter ("t3_density")->setValueNotifyingHost (0.6f);
    auto a = p.snapshot (0, 0, 32), b = p.snapshot (2, 0, 32);
    int matches = 0, activeA = 0;
    // track 1 loops every 16 steps, so the 2 exported bars are its 16 steps twice
    for (size_t i = 0; i < b.size(); ++i) { const auto& ai = a[i % 16]; if (ai.active) { ++activeA; if (b[i].active && b[i].note == ai.note) ++matches; } }
    CHECK (activeA > 5 && matches * 4 >= activeA * 3);   // tied steps export as one longer note

    p.clearMotif (2);
    CHECK (! p.hasMotif (2));

    // WAV: one track and the full mix
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    for (int t : { 0, -1 })
    {
        const auto wav = p.exportAudio (t);
        CHECK (wav.existsAsFile());
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wav));
        CHECK (r != nullptr);
        if (r != nullptr)
        {
            CHECK (r->numChannels == 2 && r->bitsPerSample == 24);
            CHECK (r->lengthInSamples == 192000);   // 2 bars at 120 bpm, 48 kHz
            juce::AudioBuffer<float> buf (2, (int) r->lengthInSamples);
            r->read (&buf, 0, buf.getNumSamples(), 0, true, true);
            CHECK (buf.getMagnitude (0, buf.getNumSamples()) > 0.01f);
        }
        wav.deleteFile();
    }
    mid.deleteFile();
}

static void testKeyLock()
{
    BashProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);

    // exact matches by note mask
    auto maskOf = [] (std::initializer_list<int> iv) { int m = 0; for (int i : iv) m |= 1 << i; return m; };
    CHECK (p.scaleForMask (maskOf ({ 0, 2, 4, 5, 7, 9, 11 })) == 1);     // Major
    CHECK (p.scaleForMask (maskOf ({ 0, 2, 3, 5, 7, 8, 10 })) == 0);     // Minor
    CHECK (p.scaleForMask (maskOf ({ 0, 3, 5, 7, 10 })) == 10);          // Minor pentatonic
    // a Live scale BASH! doesn't have (Lydian augmented) maps to its nearest relative
    CHECK (p.scaleForMask (maskOf ({ 0, 2, 4, 6, 8, 9, 11 })) == 4);     // -> Lydian

    // what the device sends: CC 102 root, CC 103 low bits, CC 104 high bits (channel 16)
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    const int dorian = maskOf ({ 0, 2, 3, 5, 7, 9, 10 });
    midi.addEvent (juce::MidiMessage::controllerEvent (16, 102, 7), 0);   // G
    midi.addEvent (juce::MidiMessage::controllerEvent (16, 103, dorian & 127), 1);
    midi.addEvent (juce::MidiMessage::controllerEvent (16, 104, dorian >> 7), 2);
    p.processBlock (buf, midi);
    CHECK ((int) *p.apvts.getRawParameterValue ("root") == 7);
    CHECK ((int) *p.apvts.getRawParameterValue ("scale") == 2);   // Dorian
}

static void testStepMode()
{
    BashProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 480);
    p.prepareToPlay (48000.0, 480);
    p.apvts.getParameter ("sync")->setValueNotifyingHost (0.0f);
    p.apvts.getParameter ("tempo")->setValueNotifyingHost (p.apvts.getParameter ("tempo")->convertTo0to1 (120.0f));
    for (int t = 1; t <= 8; ++t) p.apvts.getParameter ("t" + juce::String (t) + "_on")->setValueNotifyingHost (t == 2 ? 1.0f : 0.0f);
    CHECK (p.engineType (1) == bash::EngineType::Drums);
    p.apvts.getParameter ("t2_mode")->setValueNotifyingHost (1.0f);
    CHECK (p.stepMode (1) && ! p.stepMode (0));
    CHECK (p.stepPattern (1).activeLanes() == 0);

    // the die on an empty drum pattern rolls a starter groove; undo brings back the empty one
    const auto densityBefore = value (p, "t2_density");
    p.rollDice (1);
    CHECK (p.stepPattern (1).activeLanes() >= 4);
    CHECK (value (p, "t2_density") == densityBefore);   // step mode: the knobs aren't rolled
    CHECK (p.undoDice (1));
    CHECK (p.stepPattern (1).activeLanes() == 0);
    p.rollDice (1);

    // lane die changes only its lane
    const auto before = p.stepPattern (1);
    p.editSteps (1, [] (bash::StepPattern& sp) { sp.lanes[6].algo = bash::LaneAlgo::Euclid; });
    p.rollLane (1, 6);
    const auto after = p.stepPattern (1);
    CHECK (after.lanes[0].steps == before.lanes[0].steps && after.lanes[1].steps == before.lanes[1].steps);
    bool lane6 = false; for (auto& st : after.lanes[6].steps) lane6 = lane6 || st.on;
    CHECK (lane6);
    CHECK (p.laneName (1, 0).isNotEmpty() && p.laneName (0, 0).isNotEmpty());

    // plays pads on the track's MIDI channel
    juce::AudioBuffer<float> buf (2, 480);
    juce::MidiBuffer midi;
    p.apvts.getParameter ("play")->setValueNotifyingHost (1.0f);
    int notes = 0; float peak = 0.0f;
    for (int b = 0; b < 400; ++b)
    {
        midi.clear();
        p.processBlock (buf, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn() && m.getMessage().getChannel() == 2 && m.getMessage().getNoteNumber() >= 36 && m.getMessage().getNoteNumber() < 52) ++notes;
        peak = std::max (peak, buf.getMagnitude (0, 480));
    }
    CHECK (notes >= 8);
    CHECK (peak > 0.01f);
    CHECK (p.stepLanePosition (1, 0) >= 0);

    // fill parameter reaches the engine (Fill-only steps start playing)
    p.editSteps (1, [] (bash::StepPattern& sp) { sp.clear(); sp.lanes[15].steps[0].on = true; sp.lanes[15].steps[0].cond = bash::TrigCond::Fill; });
    auto countPad16 = [&]
    {
        int n = 0;
        for (int b = 0; b < 200; ++b) { midi.clear(); p.processBlock (buf, midi); for (const auto m : midi) if (m.getMessage().isNoteOn() && m.getMessage().getNoteNumber() == 51) ++n; }
        return n;
    };
    CHECK (countPad16() == 0);
    p.apvts.getParameter ("t2_fill")->setValueNotifyingHost (1.0f);
    CHECK (countPad16() >= 1);

    // save / load keeps the step pattern and the mode
    p.rollDice (1);
    const auto saved = p.stepPattern (1).serialise();
    juce::MemoryBlock state;
    p.getStateInformation (state);
    BashProcessor q;
    q.setPlayConfigDetails (0, 2, 48000.0, 480);
    q.prepareToPlay (48000.0, 480);
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.stepMode (1));
    CHECK (q.stepPattern (1).serialise() == saved);

    // MIDI export in step mode
    p.exportBars = 1;
    const auto mid = p.exportMidi (1);
    CHECK (mid.existsAsFile());
    juce::MidiFile mf;
    { juce::FileInputStream in (mid); CHECK (mf.readFrom (in)); }
    int exported = 0;
    for (int tr = 0; tr < mf.getNumTracks(); ++tr) for (auto* e : *mf.getTrack (tr)) if (e->message.isNoteOn()) ++exported;
    CHECK (exported > 0);
    mid.deleteFile();
}

static juce::File writeBeat (double bpm, int beats)
{
    const double sr = 44100.0;
    const int n = (int) std::lround (beats * 60.0 / bpm * sr);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    for (int beat = 0; beat < beats; ++beat)
    {
        const int start = (int) std::lround (beat * 60.0 / bpm * sr);
        for (int i = 0; i < (int) (sr * 0.15) && start + i < n; ++i)
        {
            const double t = i / sr;
            const float y = 0.8f * (float) (std::sin (2.0 * 3.14159265 * (55.0 + 90.0 * std::exp (-t * 40.0)) * t) * std::exp (-t * 18.0));
            b.setSample (0, start + i, y); b.setSample (1, start + i, y);
        }
    }
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bash-warp-test.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (b, 0, n);
    return f;
}

static void testWarp()
{
    BashProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.apvts.getParameter ("sync")->setValueNotifyingHost (0.0f);
    auto setTempo = [&] (float bpm) { p.apvts.getParameter ("tempo")->setValueNotifyingHost (p.apvts.getParameter ("tempo")->convertTo0to1 (bpm)); };
    setTempo (125.0f);
    const auto file = writeBeat (100.0, 16);
    const int frames = (int) std::lround (16 * 0.6 * 44100.0);

    CHECK (p.loadSample (0, file).isEmpty());
    CHECK (std::abs (p.sourceBpm (0) - 100.0) < 0.05);
    CHECK (p.warpOn (0));
    CHECK (std::abs ((double) p.sample (0)->numFrames() - frames * 0.8) < 2.0);   // 100 -> 125 bpm
    const auto slicesWarped = p.slices (0);
    CHECK (slicesWarped.size() >= 12);

    // warp off: the original length, slices follow
    p.apvts.getParameter ("t1_warp")->setValueNotifyingHost (0.0f);
    p.updateWarps (true);
    CHECK ((int) p.sample (0)->numFrames() == frames);
    const auto slicesOrig = p.slices (0);
    CHECK (slicesOrig.size() == slicesWarped.size());
    if (slicesOrig.size() > 4) CHECK (std::abs ((double) slicesOrig[4] * 0.8 - slicesWarped[4]) < 3.0);

    // on again, then a tempo change re-warps
    p.apvts.getParameter ("t1_warp")->setValueNotifyingHost (1.0f);
    setTempo (150.0f);
    p.updateWarps (true);
    CHECK (std::abs ((double) p.sample (0)->numFrames() - frames * 100.0 / 150.0) < 2.0);
    CHECK (std::abs (p.warpRatio (0) - 100.0 / 150.0) < 1.0e-6);

    // the user corrects the tempo (e.g. it's really half-time)
    p.setSourceBpm (0, 50.0);
    p.updateWarps (true);
    CHECK (std::abs ((double) p.sample (0)->numFrames() - frames * 50.0 / 150.0) < 2.0);
    p.setSourceBpm (0, 100.0);
    p.updateWarps (true);

    // save / load: the source tempo and the slices (on the file's timeline) come back
    setTempo (150.0f);
    p.updateWarps (true);
    const auto before = p.slices (0);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    BashProcessor q;
    q.setPlayConfigDetails (0, 2, 48000.0, 512);
    q.prepareToPlay (48000.0, 512);
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (q.sourceBpm (0) - 100.0) < 0.05);
    CHECK (q.sample (0) != nullptr && std::abs ((double) q.sample (0)->numFrames() - frames * 100.0 / 150.0) < 2.0);
    const auto after = q.slices (0);
    CHECK (after.size() == before.size());
    for (size_t i = 0; i < std::min (after.size(), before.size()); ++i) CHECK (std::abs ((double) after[i] - before[i]) <= 2.0);
    file.deleteFile();
}

static void testLibrary()
{
    BashProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    for (int i = 0; i < 600 && ! p.library().ready(); ++i) juce::Thread::sleep (50);   // first run renders it
    CHECK (p.library().ready());
    CHECK (FactoryLibrary::root().getChildFile ("Drums/Kicks/Sub Kick F.wav").existsAsFile());
    CHECK (FactoryLibrary::root().getChildFile ("Loops/Breaks/Jungle Break 170.wav").existsAsFile());

    // a kit with its starter beat on track 2 (a drum track)
    int rollers = -1;
    for (int k = 0; k < (int) bash::library::kits().size(); ++k) if (bash::library::kits()[(size_t) k].name == "Rollers") rollers = k;
    CHECK (rollers >= 0);
    CHECK (p.loadLibraryKit (rollers, 1).isEmpty());
    CHECK (p.cellName (0) == "Roller Kick 2");
    CHECK (p.cell (0).kind == bash::DrumCell::Kind::Sample && p.cellSample (0) != nullptr);
    CHECK (p.cell (3).choke == 1 && p.cell (4).choke == 1);   // hats choke each other
    CHECK (p.stepMode (1));
    CHECK (p.stepPattern (1).activeLanes() >= 3);
    CHECK (p.currentKitName() == "Rollers");
    CHECK (p.undoDice (1));                                   // the beat can be undone
    CHECK (p.stepPattern (1).activeLanes() == 0);

    // a single sound on a pad
    CHECK (p.loadLibraryShot (7, "Percussion/Glass 1").isEmpty());
    CHECK (p.cellName (7) == "Glass 1");

    // a loop in the slicer comes in at its own tempo and is warped
    int reese = -1;
    for (int i = 0; i < (int) bash::library::loops().size(); ++i) if (bash::library::loops()[(size_t) i].name == "Reese 174") reese = i;
    CHECK (reese >= 0);
    CHECK (p.loadLibraryLoop (0, reese).isEmpty());
    CHECK (std::abs (p.sourceBpm (0) - 174.0) < 1.0e-6);
    CHECK (p.slices (0).size() >= 2);

    // a groove: pattern, swing and rate
    int garage = -1;
    for (int i = 0; i < (int) bash::grooves::all().size(); ++i) if (bash::grooves::all()[(size_t) i].name == "2-Step Classic") garage = i;
    p.applyGroove (2, garage);
    CHECK (p.stepMode (2));
    CHECK (std::abs (value (p, "t3_swing") - p.apvts.getParameter ("t3_swing")->convertTo0to1 (0.28f)) < 0.01f);
    const auto sp = p.stepPattern (2);
    CHECK (sp.lanes[0].steps[0].on && sp.lanes[0].steps[10].on && sp.lanes[1].steps[4].on);

    // the kit plays
    for (int t = 1; t <= 8; ++t) p.apvts.getParameter ("t" + juce::String (t) + "_on")->setValueNotifyingHost (t == 3 ? 1.0f : 0.0f);
    p.apvts.getParameter ("t3_engine")->setValueNotifyingHost (p.apvts.getParameter ("t3_engine")->convertTo0to1 (2.0f));
    p.apvts.getParameter ("sync")->setValueNotifyingHost (0.0f);
    p.apvts.getParameter ("play")->setValueNotifyingHost (1.0f);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < 300; ++b) { p.processBlock (buf, midi); peak = std::max (peak, buf.getMagnitude (0, 512)); }
    CHECK (peak > 0.05f);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testDice();
    testState();
    testProcessing();
    testExports();
    testKeyLock();
    testStepMode();
    testWarp();
    testLibrary();
    std::printf ("%d plugin checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
