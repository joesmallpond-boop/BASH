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

    const float root = value (p, "root"), tempo = value (p, "tempo");
    const float t2type = value (p, "t2_type"), t1level = value (p, "t1_level");
    p.diceLocked.add ("t1_cutoff");
    const float cutoff = value (p, "t1_cutoff");

    // scope: this track only
    p.dice.scope = 1; p.dice.chaos = 1.0f;
    int changed = 0;
    const float before[] = { value (p, "t1_type"), value (p, "t1_density"), value (p, "t1_reso"), value (p, "t1_envmod") };
    p.rollDice (0);
    const float after[] = { value (p, "t1_type"), value (p, "t1_density"), value (p, "t1_reso"), value (p, "t1_envmod") };
    for (int i = 0; i < 4; ++i) if (before[i] != after[i]) ++changed;
    CHECK (changed >= 3);
    CHECK (value (p, "t2_type") == t2type);     // other tracks untouched
    CHECK (value (p, "root") == root);          // key excluded by default
    CHECK (value (p, "tempo") == tempo);        // tempo excluded by default
    CHECK (value (p, "t1_level") == t1level);   // levels excluded by default
    CHECK (value (p, "t1_cutoff") == cutoff);   // protected knob
    CHECK (value (p, "t1_on") > 0.5f);          // never switches tracks off

    // undo restores
    CHECK (p.diceUndoCount() == 1);
    CHECK (p.undoDice());
    CHECK (value (p, "t1_type") == before[0] && value (p, "t1_reso") == before[2]);

    // gentle chaos stays near the current values
    p.dice.chaos = 0.1f;
    const float d0 = value (p, "t1_density");
    p.rollDice (0);
    CHECK (std::abs (value (p, "t1_density") - d0) <= 0.1001f);

    // everything + key
    p.dice.scope = 0; p.dice.chaos = 1.0f; p.dice.includeKey = true;
    int rootChanged = 0;
    for (int i = 0; i < 8; ++i) { const float r0 = value (p, "root"); p.rollDice (0); if (value (p, "root") != r0) ++rootChanged; }
    CHECK (rootChanged > 0);
    CHECK (p.diceUndoCount() <= 16);
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

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testDice();
    testState();
    testProcessing();
    std::printf ("%d plugin checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
