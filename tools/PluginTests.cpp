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

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testDice();
    testState();
    testProcessing();
    testExports();
    testKeyLock();
    std::printf ("%d plugin checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
