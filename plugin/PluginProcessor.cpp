#include "PluginProcessor.h"
#include "bash/Warp.h"
#include "PluginEditor.h"

#include <mutex>

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    juce::StringArray namesOf (const std::vector<bash::ScaleDef>& v)
    {
        juce::StringArray s;
        for (auto& d : v) s.add (d.name);
        return s;
    }

    juce::StringArray rateNames()
    {
        juce::StringArray s;
        for (auto& r : bash::allRates()) s.add (r.name);
        return s;
    }

    juce::StringArray algorithmNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::Algorithm::count; ++i) s.add (bash::algorithmName ((bash::Algorithm) i));
        return s;
    }

    juce::StringArray directionNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::Direction::count; ++i) s.add (bash::directionName ((bash::Direction) i));
        return s;
    }

    juce::StringArray engineNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::EngineType::count; ++i) s.add (bash::engineTypeName ((bash::EngineType) i));
        return s;
    }

    juce::StringArray machineNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::Machine::count; ++i) s.add (bash::machineName ((bash::Machine) i));
        return s;
    }

    juce::StringArray filterNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::FilterType::count; ++i) s.add (bash::filterTypeName ((bash::FilterType) i));
        return s;
    }

    juce::StringArray sliceModeNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::SliceMode::count; ++i) s.add (bash::sliceModeName ((bash::SliceMode) i));
        return s;
    }

    juce::StringArray fxNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::FxType::count; ++i) s.add (bash::fxName ((bash::FxType) i));
        return s;
    }

    /** type, a, b, gnarl, mix for one effect slot, ids prefixed with `prefix`. */
    void addFxParams (juce::AudioProcessorParameterGroup& g, const juce::String& prefix, const juce::String& name, int defType, float defA, float defB);

    juce::StringArray lfoShapeNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::LfoShape::count; ++i) s.add (bash::lfoShapeName ((bash::LfoShape) i));
        return s;
    }

    juce::StringArray lfoDivNames()
    {
        juce::StringArray s;
        for (auto& d : bash::lfoDivisions()) s.add (d.first);
        return s;
    }

    juce::StringArray keyNames()
    {
        juce::StringArray s;
        for (int i = 0; i < 12; ++i) s.add (bash::noteName (i));
        return s;
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    // Float parameter that displays a sensible number of decimals.
    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::ParameterID& id, const juce::String& name,
                                                           juce::NormalisableRange<float> range, float def,
                                                           int decimals, const juce::String& suffix = {})
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, range, def,
            juce::AudioParameterFloatAttributes()
                .withLabel (suffix)
                .withStringFromValueFunction ([decimals, suffix] (float v, int) { return juce::String (v, decimals) + suffix; }));
    }

    float load (std::atomic<float>* p) { return p != nullptr ? p->load (std::memory_order_relaxed) : 0.0f; }
}

const juce::StringArray& BashProcessor::cycleNames()
{
    static const juce::StringArray names { "x1", "x4", "x16", "x64", "x256", "x4096", "x65536", "x16.7M" };
    return names;
}

uint32_t BashProcessor::cycleMultiplier (int index)
{
    static const uint32_t m[] = { 1u, 4u, 16u, 64u, 256u, 4096u, 65536u, 16777216u };
    return m[juce::jlimit (0, 7, index)];
}

namespace
{
    void addFxParams (juce::AudioProcessorParameterGroup& g, const juce::String& prefix, const juce::String& name, int defType, float defA, float defB)
    {
        using namespace juce;
        g.addChild (std::make_unique<AudioParameterChoice> (ParameterID { prefix + "type", 1 }, name + " Type", fxNames(), defType));
        g.addChild (floatParam (ParameterID { prefix + "a", 1 },     name + " A",     NormalisableRange<float> (0.0f, 1.0f), defA, 2));
        g.addChild (floatParam (ParameterID { prefix + "b", 1 },     name + " B",     NormalisableRange<float> (0.0f, 1.0f), defB, 2));
        g.addChild (floatParam (ParameterID { prefix + "gnarl", 1 }, name + " Gnarl", NormalisableRange<float> (0.0f, 1.0f), 0.0f, 2));
        g.addChild (floatParam (ParameterID { prefix + "mix", 1 },   name + " Mix",   NormalisableRange<float> (0.0f, 1.0f), 1.0f, 2));
    }
}

APVTS::ParameterLayout BashProcessor::createLayout()
{
    APVTS::ParameterLayout layout;
    using namespace juce;

    auto global = std::make_unique<AudioProcessorParameterGroup> ("global", "Global", "|");
    global->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "root", 1 },   "Key",   keyNames(), 0));
    global->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 },  "Scale", namesOf (bash::allScales()), 0));
    global->addChild (floatParam (ParameterID { "tempo", 1 },  "Tempo", NormalisableRange<float> (40.0f, 300.0f, 0.1f), 128.0f, 1, " bpm"));
    global->addChild (std::make_unique<AudioParameterBool>   (ParameterID { "sync", 1 },   "Host Sync", true));
    global->addChild (std::make_unique<AudioParameterBool>   (ParameterID { "play", 1 },   "Play", false));
    global->addChild (floatParam (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (0.0f, 1.0f), 0.8f, 2));
    global->addChild (std::make_unique<AudioParameterBool> (ParameterID { "limiter", 1 }, "Limiter", true));
    layout.add (std::move (global));

    auto mix = std::make_unique<AudioProcessorParameterGroup> ("mixer", "Mixer", "|");
    addFxParams (*mix, "reta",  "Return A", (int) bash::FxType::Plate, 0.6f, 0.4f);
    addFxParams (*mix, "retb",  "Return B", (int) bash::FxType::TapeDelay, 0.55f, 0.4f);
    mix->addChild (floatParam (ParameterID { "retalevel", 1 }, "Return A Level", NormalisableRange<float> (0.0f, 1.0f), 0.8f, 2));
    mix->addChild (floatParam (ParameterID { "retblevel", 1 }, "Return B Level", NormalisableRange<float> (0.0f, 1.0f), 0.8f, 2));
    addFxParams (*mix, "mfx1", "Master FX 1", 0, 0.5f, 0.5f);
    addFxParams (*mix, "mfx2", "Master FX 2", 0, 0.5f, 0.5f);
    layout.add (std::move (mix));

    auto mod = std::make_unique<AudioProcessorParameterGroup> ("mod", "Modulation", "|");
    for (int i = 1; i <= 2; ++i)
    {
        const String pre = "lfo" + String (i), name = "LFO " + String (i);
        mod->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pre + "shape", 1 }, name + " Shape", lfoShapeNames(), i == 1 ? 0 : 5));
        mod->addChild (floatParam (ParameterID { pre + "rate", 1 }, name + " Rate", skewed (0.01f, 20.0f, 1.0f), i == 1 ? 0.5f : 0.2f, 2, " Hz"));
        mod->addChild (std::make_unique<AudioParameterBool> (ParameterID { pre + "sync", 1 }, name + " Sync", false));
        mod->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pre + "div", 1 }, name + " Division", lfoDivNames(), 4));
    }
    layout.add (std::move (mod));

    for (int t = 0; t < bash::numTracks; ++t)
    {
        const String n = "T" + String (t + 1) + " ";
        auto id = [t] (const char* s) { return ParameterID { pid (t, s), 1 }; };
        auto g = std::make_unique<AudioProcessorParameterGroup> ("track" + String (t + 1), "Track " + String (t + 1), "|");

        g->addChild (std::make_unique<AudioParameterBool>   (id ("on"),        n + "On", t == 0));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("engine"),    n + "Engine", engineNames(), t == 1 ? 2 : 0));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("algo"),      n + "Algorithm", algorithmNames(), t % 2));
        g->addChild (floatParam (id ("type"),      n + "Type",    NormalisableRange<float> (0.0f, 1.0f), 0.15f, 2));
        g->addChild (floatParam (id ("density"),   n + "Density", NormalisableRange<float> (0.0f, 1.0f), 0.6f, 2));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("steps"),     n + "Steps",   1, 64, 16));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("cycle"),     n + "Cycle",   cycleNames(), 0));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("offset"),    n + "Offset",  0, 63, 0));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("direction"), n + "Direction", directionNames(), 0));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("rate"),      n + "Rate",    rateNames(), 1));
        g->addChild (floatParam (id ("accent"),    n + "Accent",  NormalisableRange<float> (0.0f, 1.0f), 0.6f, 2));
        g->addChild (floatParam (id ("gate"),      n + "Gate",    NormalisableRange<float> (0.05f, 2.0f), 0.5f, 2));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("octave"),    n + "Octave",  0, 6, 2));
        g->addChild (floatParam (id ("swing"),  n + "Swing",  NormalisableRange<float> (0.0f, 0.75f), 0.0f, 2));
        g->addChild (floatParam (id ("jitter"), n + "Jitter", NormalisableRange<float> (0.0f, 1.0f), 0.0f, 2));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("scalelock"), n + "Scale Lock", true));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("mode"),      n + "Sequencer", juce::StringArray { "Generative", "Step" }, 0));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("fill"),      n + "Fill", false));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("autofill"),  n + "Auto Fill", autoFillNames(), 0));

        g->addChild (std::make_unique<AudioParameterChoice> (id ("machine"), n + "Machine", machineNames(), 0));
        g->addChild (floatParam (id ("wave"),      n + "Character", NormalisableRange<float> (0.0f, 1.0f), t % 2 ? 1.0f : 0.0f, 2));
        g->addChild (floatParam (id ("timbre"),    n + "Timbre",    NormalisableRange<float> (0.0f, 1.0f), 0.5f, 2));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("ftype"), n + "Filter Type", filterNames(), 0));
        g->addChild (floatParam (id ("cutoff"),    n + "Cutoff",    skewed (30.0f, 8000.0f, 500.0f), 380.0f, 0, " Hz"));
        g->addChild (floatParam (id ("reso"),      n + "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.72f, 2));
        g->addChild (floatParam (id ("envmod"),    n + "Env Mod",   NormalisableRange<float> (0.0f, 1.0f), 0.55f, 2));
        g->addChild (floatParam (id ("decay"),     n + "Decay",     skewed (0.02f, 2.0f, 0.3f), 0.3f, 3, " s"));
        g->addChild (floatParam (id ("drive"),     n + "Drive",     NormalisableRange<float> (0.0f, 1.0f), 0.35f, 2));
        g->addChild (floatParam (id ("glide"),     n + "Glide",     skewed (0.005f, 0.5f, 0.06f), 0.06f, 3, " s"));
        g->addChild (floatParam (id ("aatk"),      n + "Amp Attack",  skewed (0.0005f, 2.0f, 0.05f), 0.001f, 3, " s"));
        g->addChild (floatParam (id ("adec"),      n + "Amp Decay",   skewed (0.01f, 4.0f, 0.4f), 0.5f, 2, " s"));
        g->addChild (floatParam (id ("asus"),      n + "Amp Sustain", NormalisableRange<float> (0.0f, 1.0f), 1.0f, 2));
        g->addChild (floatParam (id ("arel"),      n + "Amp Release", skewed (0.002f, 4.0f, 0.2f), 0.01f, 3, " s"));
        // slicer
        g->addChild (std::make_unique<AudioParameterChoice> (id ("slicemode"), n + "Slice Mode",   sliceModeNames(), t == 1 ? 3 : 0));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("winstart"),  n + "Window Start", 1, 256, 1));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("winsize"),   n + "Window Size",  1, 256, 4));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("pitchfollow"), n + "Pitch Follow", false));
        g->addChild (floatParam (id ("tune"),      n + "Tune",          NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, 2, " st"));
        g->addChild (floatParam (id ("sattack"),   n + "Slice Attack",  skewed (0.0005f, 0.5f, 0.02f), 0.002f, 3, " s"));
        g->addChild (floatParam (id ("srelease"),  n + "Slice Release", skewed (0.002f, 2.0f, 0.1f), 0.03f, 3, " s"));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("reverse"),   n + "Reverse", false));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("warp"),      n + "Warp", true));

        g->addChild (floatParam (id ("level"),     n + "Level",     NormalisableRange<float> (0.0f, 1.0f), 0.8f, 2));
        g->addChild (floatParam (id ("pan"),       n + "Pan",       NormalisableRange<float> (-1.0f, 1.0f), 0.0f, 2));
        g->addChild (floatParam (id ("liferate"), n + "Life Rate", skewed (0.005f, 2.0f, 0.1f), 0.08f, 3, " Hz"));
        g->addChild (std::make_unique<AudioParameterBool> (id ("mute"), n + "Mute", false));
        g->addChild (std::make_unique<AudioParameterBool> (id ("solo"), n + "Solo", false));
        g->addChild (floatParam (id ("senda"), n + "Send A", NormalisableRange<float> (0.0f, 1.0f), 0.0f, 2));
        g->addChild (floatParam (id ("sendb"), n + "Send B", NormalisableRange<float> (0.0f, 1.0f), 0.0f, 2));
        addFxParams (*g, pid (t, "fx1"), n + "Insert 1", 0, 0.5f, 0.5f);
        addFxParams (*g, pid (t, "fx2"), n + "Insert 2", 0, 0.5f, 0.5f);

        layout.add (std::move (g));
    }
    return layout;
}

juce::AudioProcessor::BusesProperties BashProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
   #if ! JUCE_IOS
    for (int t = 0; t < bash::numTracks; ++t)
        b = b.withOutput ("Track " + juce::String (t + 1), juce::AudioChannelSet::stereo(), false);
   #endif
    return b;
}

BashProcessor::BashProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "BASH", createLayout())
{
    formats.registerBasicFormats();
    startTimer (100);
    loadFactoryKit (0);

    pRoot   = apvts.getRawParameterValue ("root");
    pScale  = apvts.getRawParameterValue ("scale");
    pTempo  = apvts.getRawParameterValue ("tempo");
    pSync   = apvts.getRawParameterValue ("sync");
    pPlay   = apvts.getRawParameterValue ("play");
    pMaster = apvts.getRawParameterValue ("master");
    pLimiter = apvts.getRawParameterValue ("limiter");
    pRetALevel = apvts.getRawParameterValue ("retalevel");
    pRetBLevel = apvts.getRawParameterValue ("retblevel");
    retA = fxPtrs ("reta"); retB = fxPtrs ("retb");
    for (int i = 0; i < 2; ++i)
    {
        const juce::String pre = "lfo" + juce::String (i + 1);
        lfoP[(size_t) i] = { apvts.getRawParameterValue (pre + "shape"), apvts.getRawParameterValue (pre + "rate"),
                             apvts.getRawParameterValue (pre + "sync"), apvts.getRawParameterValue (pre + "div") };
    }
    mfx[0] = fxPtrs ("mfx1"); mfx[1] = fxPtrs ("mfx2");

    for (int t = 0; t < bash::numTracks; ++t)
    {
        auto get = [this, t] (const char* s) { return apvts.getRawParameterValue (pid (t, s)); };
        auto& p = tp[(size_t) t];
        p.on = get ("on"); p.algo = get ("algo"); p.type = get ("type"); p.density = get ("density");
        p.steps = get ("steps"); p.cycle = get ("cycle"); p.offset = get ("offset"); p.direction = get ("direction");
        p.rate = get ("rate"); p.accent = get ("accent"); p.gate = get ("gate"); p.octave = get ("octave");
        p.scaleLock = get ("scalelock"); p.wave = get ("wave"); p.cutoff = get ("cutoff"); p.reso = get ("reso");
        p.envMod = get ("envmod"); p.decay = get ("decay"); p.drive = get ("drive"); p.glide = get ("glide");
        p.level = get ("level"); p.pan = get ("pan");
        p.lifeRate = get ("liferate"); p.swing = get ("swing"); p.jitter = get ("jitter");
        p.mute = get ("mute"); p.solo = get ("solo"); p.sendA = get ("senda"); p.sendB = get ("sendb");
        p.mode = get ("mode"); p.fill = get ("fill"); p.autoFill = get ("autofill"); p.warp = get ("warp");
        p.fx[0] = fxPtrs (pid (t, "fx1")); p.fx[1] = fxPtrs (pid (t, "fx2"));
        p.machine = get ("machine"); p.timbre = get ("timbre"); p.ftype = get ("ftype");
        p.aAtk = get ("aatk"); p.aDec = get ("adec"); p.aSus = get ("asus"); p.aRel = get ("arel");
        p.engine = get ("engine"); p.sliceMode = get ("slicemode"); p.winStart = get ("winstart");
        p.winSize = get ("winsize"); p.pitchFollow = get ("pitchfollow"); p.tune = get ("tune");
        p.sAttack = get ("sattack"); p.sRelease = get ("srelease"); p.reverse = get ("reverse");
    }
}

bool BashProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty();
}

void BashProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    isPrepared = true;
    scratch.setSize (1, juce::jmax (1, samplesPerBlock));
    midiOut.reserve (512);
}

bash::SequencerParams BashProcessor::seqParams (int t) const
{
    const auto& p = tp[(size_t) t];
    bash::SequencerParams s;
    s.algorithm    = (bash::Algorithm) juce::jlimit (0, (int) bash::Algorithm::count - 1, (int) load (p.algo));
    s.type         = load (p.type);
    s.density      = load (p.density);
    const uint64_t len = (uint64_t) juce::jlimit (1, 64, (int) load (p.steps));
    s.length       = (uint32_t) std::min<uint64_t> (len, 0xFFFFFFFFull);
    s.offset       = (uint32_t) juce::jmax (0, (int) load (p.offset));
    s.direction    = (bash::Direction) juce::jlimit (0, (int) bash::Direction::count - 1, (int) load (p.direction));
    s.accentAmount = load (p.accent);
    s.gate         = load (p.gate);
    s.baseNote     = 12 * ((int) load (p.octave) + 1) + (int) load (pRoot);
    s.scaleLock    = load (p.scaleLock) > 0.5f;
    s.sliceMode    = (bash::SliceMode) juce::jlimit (0, (int) bash::SliceMode::count - 1, (int) load (p.sliceMode));
    s.windowStart  = (uint32_t) juce::jmax (0, (int) load (p.winStart) - 1);
    s.windowSize   = (uint32_t) juce::jmax (1, (int) load (p.winSize));
    s.sliceCount   = 0;   // the engine fills this in on the audio thread
    s.swing        = load (p.swing);
    s.jitter       = load (p.jitter);
    return s;
}

BashProcessor::FxPtrs BashProcessor::fxPtrs (const juce::String& prefix)
{
    return { apvts.getRawParameterValue (prefix + "type"), apvts.getRawParameterValue (prefix + "a"),
             apvts.getRawParameterValue (prefix + "b"), apvts.getRawParameterValue (prefix + "gnarl"),
             apvts.getRawParameterValue (prefix + "mix") };
}

bash::FxParams BashProcessor::fxFrom (const FxPtrs& f)
{
    bash::FxParams p;
    p.type  = (bash::FxType) juce::jlimit (0, (int) bash::FxType::count - 1, (int) load (f.type));
    p.a     = load (f.a);
    p.b     = load (f.b);
    p.gnarl = load (f.gnarl);
    p.mix   = load (f.mix);
    return p;
}

bash::Scale BashProcessor::scaleNow() const
{
    return bash::Scale ((int) load (pRoot), (int) load (pScale));
}

void BashProcessor::pullParameters()
{
    auto& g = engine.global();
    g.root       = (int) load (pRoot);
    g.scaleIndex = (int) load (pScale);
    g.tempo      = load (pTempo);
    g.hostSync   = load (pSync) > 0.5f;
    g.playing    = load (pPlay) > 0.5f;
    g.master     = load (pMaster);
    g.limiter    = load (pLimiter) > 0.5f;
    g.returns[0] = fxFrom (retA);
    g.returns[1] = fxFrom (retB);
    g.returnLevel[0] = load (pRetALevel);
    g.returnLevel[1] = load (pRetBLevel);
    g.masterFx[0] = fxFrom (mfx[0]);
    g.masterFx[1] = fxFrom (mfx[1]);
    for (int i = 0; i < 2; ++i)
    {
        auto& lp = g.lfo[(size_t) i];
        lp.shape    = (bash::LfoShape) juce::jlimit (0, (int) bash::LfoShape::count - 1, (int) load (lfoP[(size_t) i].shape));
        lp.rateHz   = load (lfoP[(size_t) i].rate);
        lp.sync     = load (lfoP[(size_t) i].sync) > 0.5f;
        lp.division = (int) load (lfoP[(size_t) i].div);
    }

    for (int t = 0; t < bash::numTracks; ++t)
    {
        const auto& p = tp[(size_t) t];
        auto& ts = engine.track (t);
        ts.enabled   = load (p.on) > 0.5f;
        ts.engine    = (bash::EngineType) juce::jlimit (0, (int) bash::EngineType::count - 1, (int) load (p.engine));
        ts.slicer.pitchFollow   = load (p.pitchFollow) > 0.5f;
        ts.slicer.tune          = load (p.tune);
        ts.slicer.voice.attack  = load (p.sAttack);
        ts.slicer.voice.release = load (p.sRelease);
        ts.slicer.voice.reverse = load (p.reverse) > 0.5f;
        ts.seq       = seqParams (t);
        ts.rateIndex = (int) load (p.rate);
        ts.voice.machine    = (bash::Machine) juce::jlimit (0, (int) bash::Machine::count - 1, (int) load (p.machine));
        ts.voice.character  = load (p.wave);
        ts.voice.timbre     = load (p.timbre);
        ts.voice.filterType = (bash::FilterType) juce::jlimit (0, (int) bash::FilterType::count - 1, (int) load (p.ftype));
        ts.voice.attack     = load (p.aAtk);
        ts.voice.ampDecay   = load (p.aDec);
        ts.voice.sustain    = load (p.aSus);
        ts.voice.release    = load (p.aRel);
        ts.voice.cutoff    = load (p.cutoff);
        ts.voice.resonance = load (p.reso);
        ts.voice.envMod    = load (p.envMod);
        ts.voice.decay     = load (p.decay);
        ts.voice.accent    = load (p.accent);
        ts.voice.drive     = load (p.drive);
        ts.voice.glide     = load (p.glide);
        ts.level = load (p.level);
        ts.pan   = load (p.pan);
        ts.mute  = load (p.mute) > 0.5f;
        ts.lifeRate = load (p.lifeRate);
        ts.solo  = load (p.solo) > 0.5f;
        ts.stepMode = load (p.mode) > 0.5f;
        ts.fill     = load (p.fill) > 0.5f;
        ts.autoFillBars = autoFillBars ((int) load (p.autoFill));
        ts.sendA = load (p.sendA);
        ts.sendB = load (p.sendB);
        ts.inserts[0] = fxFrom (p.fx[0]);
        ts.inserts[1] = fxFrom (p.fx[1]);
    }
}

void BashProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    pullParameters();

    bash::Transport transport;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getPpqPosition().hasValue() && pos->getBpm().hasValue())
            {
                transport.hostAvailable = true;
                transport.hostPpq       = *pos->getPpqPosition();
                transport.hostBpm       = *pos->getBpm();
                transport.hostPlaying   = pos->getIsPlaying();
                lastHostBpm.store (transport.hostBpm);
            }
        }
    }

    const int n = buffer.getNumSamples();
    if (scratch.getNumSamples() < n)
        scratch.setSize (1, n, false, false, true);   // only if the host lied about block size

    auto mainBus = getBusBuffer (buffer, false, 0);
    float* left  = mainBus.getWritePointer (0);
    float* right = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : scratch.getWritePointer (0);

    for (int t = 0; t < bash::numTracks; ++t)
    {
        auto* bus = getBus (false, t + 1);
        if (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() == 2)
        {
            auto out = getBusBuffer (buffer, false, t + 1);
            out.clear();
            engine.setDirectOutput (t, out.getWritePointer (0), out.getWritePointer (1));
        }
        else
        {
            engine.setDirectOutput (t, nullptr, nullptr);
        }
    }

    // MIDI in: held notes feed the arpeggiator; CC 102 / 103 set key / scale
    // (sent by the BASH! Key Lock device in Ableton so BASH! follows the set's key).
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            engine.midiNoteIn (msg.getNoteNumber(), msg.getVelocity());
            engine.midiExpression (bash::ModSource::Velocity, (float) msg.getVelocity() / 127.0f);
        }
        else if (msg.isChannelPressure())    engine.midiExpression (bash::ModSource::Aftertouch, (float) msg.getChannelPressureValue() / 127.0f);
        else if (msg.isAftertouch())         engine.midiExpression (bash::ModSource::Aftertouch, (float) msg.getAfterTouchValue() / 127.0f);
        else if (msg.isPitchWheel())         engine.midiExpression (bash::ModSource::PitchBend, (float) (msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController() && msg.getControllerNumber() == 1)  engine.midiExpression (bash::ModSource::ModWheel, (float) msg.getControllerValue() / 127.0f);
        else if (msg.isController() && msg.getControllerNumber() == 74) engine.midiExpression (bash::ModSource::Aftertouch, (float) msg.getControllerValue() / 127.0f);   // MPE slide
        else if (msg.isNoteOff())  engine.midiNoteIn (msg.getNoteNumber(), 0);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allInputNotesOff();
        else if (msg.isController() && msg.getControllerNumber() == 102)
        {
            setParamFromAudio ("root", (float) (msg.getControllerValue() % 12));
        }
        else if (msg.isController() && msg.getControllerNumber() == 103)
        {
            keyLockMaskLow = msg.getControllerValue();   // scale notes 0-6 above the root
        }
        else if (msg.isController() && msg.getControllerNumber() == 104)
        {
            const int mask = keyLockMaskLow | (msg.getControllerValue() << 7);   // notes 7-11
            setParamFromAudio ("scale", (float) scaleForMask (mask));
        }
    }

    midiOut.clear();
    engine.process (left, right, n, transport, midiOut);

    if (mainBus.getNumChannels() == 1)
    {
        juce::FloatVectorOperations::add (left, right, n);
        juce::FloatVectorOperations::multiply (left, 0.5f, n);
    }

    // Generated notes go out on channel = track number. Incoming MIDI is reserved
    // for the arpeggiator and modulation matrix (later milestones).
    midi.clear();
    for (const auto& m : midiOut)
    {
        const auto msg = m.noteOn ? juce::MidiMessage::noteOn (m.channel, m.note, (juce::uint8) m.velocity)
                                  : juce::MidiMessage::noteOff (m.channel, m.note);
        midi.addEvent (msg, juce::jlimit (0, juce::jmax (0, n - 1), m.sampleOffset));
    }
}

// ---- pattern edits from the editor ----------------------------------------
bash::SequencerParams BashProcessor::uiSeqParams (int t)
{
    auto p = seqParams (t);
    if (engineType (t) == bash::EngineType::Slicer && engine.sample (t) != nullptr)
        p.sliceCount = engine.sliceCount (t);
    if (engineType (t) == bash::EngineType::Drums)
    {
        p.sliceCount = bash::numCells;
        p.drumTrack = true;
    }
    return p;
}

bash::EngineType BashProcessor::engineType (int t) const
{
    return (bash::EngineType) juce::jlimit (0, (int) bash::EngineType::count - 1, (int) load (tp[(size_t) t].engine));
}

std::vector<bash::StepData> BashProcessor::snapshot (int t, uint32_t first, uint32_t count)
{
    return engine.snapshot (t, uiSeqParams (t), scaleNow(), first, count);
}

void BashProcessor::regenerate (int t)
{
    auto& pat = engine.pattern (t);
    std::lock_guard<bash::SpinMutex> g (pat.mutex());
    pat.regenerate();
}

uint8_t BashProcessor::lockMask (int t, uint32_t index)
{
    auto& pat = engine.pattern (t);
    std::lock_guard<bash::SpinMutex> g (pat.mutex());
    return pat.lockMask (index);
}

void BashProcessor::toggleLock (int t, uint32_t index)
{
    if (lockMask (t, index) != 0)
    {
        auto& pat = engine.pattern (t);
        std::lock_guard<bash::SpinMutex> g (pat.mutex());
        pat.unlock (index);
    }
    else
    {
        engine.lockStep (t, uiSeqParams (t), scaleNow(), index);
    }
}

void BashProcessor::toggleMute (int t, uint32_t index)
{
    const auto params = uiSeqParams (t);
    const auto sc = scaleNow();
    auto& pat = engine.pattern (t);
    std::lock_guard<bash::SpinMutex> g (pat.mutex());
    bash::StepData s = pat.step (index, params, sc);
    s.active = ! s.active;
    pat.lock (index, bash::lockbit::active, s);
}

void BashProcessor::clearLocks (int t)
{
    auto& pat = engine.pattern (t);
    std::lock_guard<bash::SpinMutex> g (pat.mutex());
    pat.clearLocks();
}

// ---- state -------------------------------------------------------------------
void BashProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree patterns ("Patterns");
    for (int t = 0; t < bash::numTracks; ++t)
    {
        auto& pat = engine.pattern (t);
        std::string text;
        {
            std::lock_guard<bash::SpinMutex> g (pat.mutex());
            text = pat.serialise();
        }
        patterns.setProperty ("t" + juce::String (t + 1), juce::String (text), nullptr);
    }
    state.removeChild (state.getChildWithName ("Patterns"), nullptr);
    state.appendChild (patterns, nullptr);

    juce::ValueTree stepTree ("Steps");
    for (int t = 0; t < bash::numTracks; ++t)
        stepTree.setProperty ("t" + juce::String (t + 1), juce::String (stepPattern (t).serialise()), nullptr);
    state.removeChild (state.getChildWithName ("Steps"), nullptr);
    state.appendChild (stepTree, nullptr);

    juce::ValueTree samplesTree ("Samples");
    for (int t = 0; t < bash::numTracks; ++t)
    {
        const auto* d = engine.sample (t);
        if (d == nullptr) continue;
        // slices are saved on the file's own timeline (unwarped), so they survive any tempo
        const double ratio = std::max (1.0e-6, warps[(size_t) t].applied);
        juce::String sl;
        for (auto f : engine.slices (t)) sl << (juce::int64) std::llround (f / ratio) << ' ';
        juce::ValueTree st ("Sample");
        st.setProperty ("track", t, nullptr);
        st.setProperty ("path", juce::String (d->path), nullptr);
        st.setProperty ("slices", sl.trim(), nullptr);
        st.setProperty ("bpm", warps[(size_t) t].sourceBpm, nullptr);
        samplesTree.appendChild (st, nullptr);
    }
    state.removeChild (state.getChildWithName ("Samples"), nullptr);
    state.appendChild (samplesTree, nullptr);
    state.removeChild (state.getChildWithName ("Kit"), nullptr);
    state.appendChild (kitToTree(), nullptr);

    juce::ValueTree modTree ("Mod");
    for (int t = 0; t < bash::numTracks; ++t)
        for (int d = 0; d < bash::numModDests; ++d)
            for (int s = 0; s < bash::numModSources; ++s)
            {
                const float a = engine.matrix (t).get ((bash::ModDest) d, (bash::ModSource) s);
                if (a == 0.0f) continue;
                juce::ValueTree c ("R");
                c.setProperty ("t", t, nullptr); c.setProperty ("d", d, nullptr);
                c.setProperty ("s", s, nullptr); c.setProperty ("a", a, nullptr);
                modTree.appendChild (c, nullptr);
            }
    state.removeChild (state.getChildWithName ("Mod"), nullptr);
    state.appendChild (modTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void BashProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto patterns = tree.getChildWithName ("Patterns");
    const auto stepTree = tree.getChildWithName ("Steps").createCopy();
    const auto samplesTree = tree.getChildWithName ("Samples").createCopy();
    const auto kitTree = tree.getChildWithName ("Kit").createCopy();
    const auto modTree = tree.getChildWithName ("Mod").createCopy();
    apvts.replaceState (tree);

    for (int t = 0; t < bash::numTracks; ++t)
        clearSample (t);
    for (const auto& st : samplesTree)
    {
        const int t = juce::jlimit (0, bash::numTracks - 1, (int) st.getProperty ("track"));
        const juce::File file (st.getProperty ("path").toString());
        if (file.existsAsFile() && loadSample (t, file, false, (double) st.getProperty ("bpm", 0.0)).isEmpty())
        {
            const double ratio = warps[(size_t) t].applied;
            std::vector<uint32_t> sl;
            for (auto& tok : juce::StringArray::fromTokens (st.getProperty ("slices").toString(), " ", ""))
                if (tok.isNotEmpty()) sl.push_back ((uint32_t) std::llround ((double) tok.getLargeIntValue() * ratio));
            setSlices (t, std::move (sl));
        }
        else
        {
            missingSamples[(size_t) t] = file.getFullPathName();
        }
    }
    if (kitTree.isValid())
        kitFromTree (kitTree);
    for (int t = 0; t < bash::numTracks; ++t) engine.matrix (t).clear();
    for (const auto& c : modTree)
    {
        const int t = (int) c.getProperty ("t"), d = (int) c.getProperty ("d"), s = (int) c.getProperty ("s");
        if (t >= 0 && t < bash::numTracks && d >= 0 && d < bash::numModDests && s >= 0 && s < bash::numModSources)
            engine.matrix (t).set ((bash::ModDest) d, (bash::ModSource) s, (float) c.getProperty ("a"));
    }

    for (int t = 0; t < bash::numTracks; ++t)
    {
        bash::StepPattern sp;
        if (stepTree.isValid()) sp.deserialise (stepTree.getProperty ("t" + juce::String (t + 1)).toString().toStdString());
        setStepPattern (t, sp);
    }

    if (patterns.isValid())
    {
        for (int t = 0; t < bash::numTracks; ++t)
        {
            const auto text = patterns.getProperty ("t" + juce::String (t + 1)).toString().toStdString();
            auto& pat = engine.pattern (t);
            std::lock_guard<bash::SpinMutex> g (pat.mutex());
            pat.deserialise (text);
        }
    }
}

juce::AudioProcessorEditor* BashProcessor::createEditor()
{
    return new BashEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BashProcessor();
}

// ---- samples and slices (message thread) -------------------------------------
std::shared_ptr<bash::SampleData> BashProcessor::decode (const juce::File& file, double maxSeconds, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Couldn't read " + file.getFileName(); return nullptr; }
    if (reader->lengthInSamples <= 0) { error = file.getFileName() + " is empty"; return nullptr; }

    const auto maxFrames = (juce::int64) (reader->sampleRate * maxSeconds);
    const int frames = (int) juce::jmin (reader->lengthInSamples, maxFrames);
    juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), frames);
    reader->read (&buf, 0, frames, 0, true, buf.getNumChannels() > 1);

    auto data = std::make_shared<bash::SampleData>();
    data->sampleRate = reader->sampleRate;
    data->path = file.getFullPathName().toStdString();
    data->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + frames);
    if (buf.getNumChannels() > 1) data->right.assign (buf.getReadPointer (1), buf.getReadPointer (1) + frames);
    else                          data->right = data->left;
    return data;
}

juce::String BashProcessor::loadSample (int t, const juce::File& file, bool autoSlice, double knownBpm)
{
    juce::String error;
    auto data = decode (file, 600.0, error);   // up to 10 minutes
    if (data == nullptr) return error;

    // Warp: find the file's tempo (or use the one saved with the session) and stretch it to ours
    auto& w = warps[(size_t) t];
    w.original = data;
    w.sourceBpm = knownBpm > 0.0 ? knownBpm : bash::warp::detectTempo (*data).bpm;
    const double ratio = desiredWarpRatio (t);
    w.requested = ratio;
    auto playing = std::abs (ratio - 1.0) > 1.0e-4 ? bash::warp::stretch (*data, ratio) : data;

    auto& slot = sampleSlots[(size_t) t];
    if (slot != nullptr)
        graveyard.push_back ({ slot, engine.blocksProcessed() });
    slot = playing;
    w.applied = ratio;
    engine.setSample (t, playing.get());

    missingSamples[(size_t) t] = {};
    if (autoSlice)
    {
        auto s = bash::slicing::transients (*playing, 0.5f);
        if (s.size() < 2) s = bash::slicing::grid (playing->numFrames(), 16);   // no clear hits: even grid
        setSlices (t, std::move (s));
    }
    return {};
}

void BashProcessor::clearSample (int t)
{
    auto& slot = sampleSlots[(size_t) t];
    if (slot != nullptr)
        graveyard.push_back ({ slot, engine.blocksProcessed() });
    slot = nullptr;
    auto& w = warps[(size_t) t];
    w.original = nullptr;
    w.applied = w.requested = 1.0;
    { std::lock_guard<std::mutex> g (w.m); w.ready = nullptr; }
    engine.setSample (t, nullptr);
    engine.setSlices (t, {});
}

// ---- warp (message thread) ----------------------------------------------------
bool BashProcessor::warpOn (int t) const { return load (tp[(size_t) t].warp) > 0.5f; }

double BashProcessor::desiredWarpRatio (int t) const
{
    if (warps[(size_t) t].original == nullptr || ! warpOn (t)) return 1.0;
    return bash::warp::ratioFor (warps[(size_t) t].sourceBpm, exportBpm());
}

void BashProcessor::setSourceBpm (int t, double bpm)
{
    warps[(size_t) t].sourceBpm = juce::jlimit (40.0, 250.0, bpm);
    updateWarps (false);
}

void BashProcessor::installWarped (int t, std::shared_ptr<bash::SampleData> data, double ratio)
{
    auto& w = warps[(size_t) t];
    auto& slot = sampleSlots[(size_t) t];
    if (data == nullptr || w.original == nullptr) return;

    // slices keep their place in the music: scale them with the new length
    const double oldLen = slot != nullptr ? (double) slot->numFrames() : 1.0;
    const double scale = (double) data->numFrames() / std::max (1.0, oldLen);
    auto sl = engine.slices (t);
    for (auto& f : sl) f = (uint32_t) std::llround (f * scale);

    if (slot != nullptr)
        graveyard.push_back ({ slot, engine.blocksProcessed() });
    slot = data;
    w.applied = ratio;
    engine.setSample (t, data.get());
    setSlices (t, std::move (sl));
}

void BashProcessor::updateWarps (bool synchronous)
{
    for (int t = 0; t < bash::numTracks; ++t)
    {
        auto& w = warps[(size_t) t];
        if (w.original == nullptr) continue;
        const double want = desiredWarpRatio (t);
        if (std::abs (want - w.requested) < 1.0e-4 && std::abs (want - w.applied) < 1.0e-4) continue;
        if (std::abs (want - w.requested) < 1.0e-4 && ! synchronous) continue;   // already on its way
        w.requested = want;

        auto original = w.original;
        if (synchronous)
        {
            installWarped (t, std::abs (want - 1.0) > 1.0e-4 ? bash::warp::stretch (*original, want) : original, want);
            continue;
        }
        warpPool.addJob ([&w, original, want]
        {
            auto result = std::abs (want - 1.0) > 1.0e-4 ? bash::warp::stretch (*original, want) : original;
            std::lock_guard<std::mutex> g (w.m);
            w.ready = result;
            w.readyRatio = want;
        });
    }
}

void BashProcessor::setSlices (int t, std::vector<uint32_t> s)
{
    const auto* d = engine.sample (t);
    bash::slicing::normalise (s, d != nullptr ? d->numFrames() : 0u);
    engine.setSlices (t, std::move (s));
}

void BashProcessor::timerCallback()
{
    // warp: follow tempo changes once the tempo has settled, and pick up finished stretches
    const double target = exportBpm();
    if (std::abs (target - lastWarpTarget) > 1.0e-3) { lastWarpTarget = target; warpTargetChanged = juce::Time::getMillisecondCounter(); }
    if (juce::Time::getMillisecondCounter() - warpTargetChanged > 300)
        updateWarps (false);
    for (int t = 0; t < bash::numTracks; ++t)
    {
        auto& w = warps[(size_t) t];
        std::shared_ptr<bash::SampleData> ready;
        double ratio = 1.0;
        {
            std::lock_guard<std::mutex> g (w.m);
            ready.swap (w.ready);
            ratio = w.readyRatio;
        }
        if (ready != nullptr && std::abs (ratio - w.requested) < 1.0e-4)
            installWarped (t, ready, ratio);
    }

    // free replaced samples once the audio thread has moved past them (or isn't running)
    const auto now = engine.blocksProcessed();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now, this] (const auto& g) { return now >= g.second + 2 || ! isPrepared; }),
                     graveyard.end());
}

// ---- drum kit (message thread) ------------------------------------------------
void BashProcessor::setCell (int i, const bash::DrumCell& c)
{
    if (i < 0 || i >= bash::numCells) return;
    engine.setCell (i, c);
}

void BashProcessor::retireCellSample (int i)
{
    auto& info = cells[(size_t) i];
    if (info.sample != nullptr)
        graveyard.push_back ({ info.sample, engine.blocksProcessed() });
    info.sample = nullptr;
}

juce::String BashProcessor::loadCellSample (int i, const juce::File& file)
{
    if (i < 0 || i >= bash::numCells) return {};
    juce::String error;
    auto data = decode (file, 60.0, error);   // one-shots: up to a minute
    if (data == nullptr) return error;

    bash::DrumCell c;
    c.kind = bash::DrumCell::Kind::Sample;
    c.sample = data.get();
    c.choke = engine.cell (i).choke;
    retireCellSample (i);
    cells[(size_t) i].sample = data;
    cells[(size_t) i].name = file.getFileNameWithoutExtension();
    engine.setCell (i, c);
    return {};
}

void BashProcessor::setCellFm (int i, int presetIndex)
{
    const auto& presets = bash::fmPresets();
    if (i < 0 || i >= bash::numCells || presetIndex < 0 || presetIndex >= (int) presets.size()) return;
    retireCellSample (i);
    cells[(size_t) i].name = presets[(size_t) presetIndex].name;
    engine.setCell (i, bash::makeFmCell (presets[(size_t) presetIndex]));
}

void BashProcessor::clearCell (int i)
{
    if (i < 0 || i >= bash::numCells) return;
    retireCellSample (i);
    cells[(size_t) i].name = {};
    engine.setCell (i, bash::DrumCell());
}

void BashProcessor::loadFactoryKit (int k)
{
    const auto& kits = bash::factoryKits();
    if (k < 0 || k >= (int) kits.size()) return;
    for (int i = 0; i < bash::numCells; ++i) clearCell (i);
    for (int i = 0; i < bash::numCells; ++i)
        if (kits[(size_t) k].cells[(size_t) i] >= 0)
            setCellFm (i, kits[(size_t) k].cells[(size_t) i]);
}

juce::ValueTree BashProcessor::kitToTree()
{
    juce::ValueTree kit ("Kit");
    for (int i = 0; i < bash::numCells; ++i)
    {
        const auto c = engine.cell (i);
        if (c.kind == bash::DrumCell::Kind::Empty) continue;
        juce::ValueTree v ("Cell");
        v.setProperty ("i", i, nullptr);
        v.setProperty ("kind", (int) c.kind, nullptr);
        v.setProperty ("name", cells[(size_t) i].name, nullptr);
        if (c.kind == bash::DrumCell::Kind::Sample && cells[(size_t) i].sample != nullptr)
            v.setProperty ("path", juce::String (cells[(size_t) i].sample->path), nullptr);
        const float values[] = { c.start, c.end, c.fadeIn, c.fadeOut, c.attack, c.sustain, c.tune, c.cutoff,
                                 c.reverse ? 1.0f : 0.0f, c.gain, c.pan, (float) c.choke,
                                 c.fm.pitch, c.fm.pitchAmt, c.fm.pitchTime, c.fm.ratio, c.fm.fmAmt, c.fm.fmDecay,
                                 c.fm.noise, c.fm.tone, c.fm.noiseDecay, (float) c.fm.bursts, c.fm.decay, c.fm.click, c.fm.drive };
        juce::String s;
        for (float f : values) s << juce::String (f, 6) << ' ';
        v.setProperty ("p", s.trim(), nullptr);
        kit.appendChild (v, nullptr);
    }
    return kit;
}

void BashProcessor::kitFromTree (const juce::ValueTree& kit)
{
    for (int i = 0; i < bash::numCells; ++i) clearCell (i);
    for (const auto& v : kit)
    {
        const int i = (int) v.getProperty ("i");
        if (i < 0 || i >= bash::numCells) continue;
        auto tokens = juce::StringArray::fromTokens (v.getProperty ("p").toString(), " ", "");
        std::vector<float> f;
        for (auto& tk : tokens) if (tk.isNotEmpty()) f.push_back (tk.getFloatValue());
        if (f.size() < 25) continue;

        bash::DrumCell c;
        c.kind = (bash::DrumCell::Kind) juce::jlimit (0, 2, (int) v.getProperty ("kind"));
        c.start = f[0]; c.end = f[1]; c.fadeIn = f[2]; c.fadeOut = f[3]; c.attack = f[4]; c.sustain = f[5];
        c.tune = f[6]; c.cutoff = f[7]; c.reverse = f[8] > 0.5f; c.gain = f[9]; c.pan = f[10]; c.choke = (int) f[11];
        c.fm.pitch = f[12]; c.fm.pitchAmt = f[13]; c.fm.pitchTime = f[14]; c.fm.ratio = f[15]; c.fm.fmAmt = f[16];
        c.fm.fmDecay = f[17]; c.fm.noise = f[18]; c.fm.tone = f[19]; c.fm.noiseDecay = f[20]; c.fm.bursts = (int) f[21];
        c.fm.decay = f[22]; c.fm.click = f[23]; c.fm.drive = f[24];

        cells[(size_t) i].name = v.getProperty ("name").toString();
        if (c.kind == bash::DrumCell::Kind::Sample)
        {
            const juce::File file (v.getProperty ("path").toString());
            juce::String error;
            auto data = file.existsAsFile() ? decode (file, 60.0, error) : nullptr;
            if (data == nullptr) { cells[(size_t) i].name = "Missing: " + file.getFileName(); c.kind = bash::DrumCell::Kind::Empty; }
            else { cells[(size_t) i].sample = data; c.sample = data.get(); }
        }
        engine.setCell (i, c);
    }
}

// ---- step sequencer (message thread) ------------------------------------------------
const juce::StringArray& BashProcessor::autoFillNames()
{
    static const juce::StringArray n { "Off", "Every 2 bars", "Every 4 bars", "Every 8 bars", "Every 16 bars" };
    return n;
}

int BashProcessor::autoFillBars (int index)
{
    static constexpr int bars[] = { 0, 2, 4, 8, 16 };
    return bars[juce::jlimit (0, 4, index)];
}

bool BashProcessor::stepMode (int t) const { return load (tp[(size_t) t].mode) > 0.5f; }

bash::StepPattern BashProcessor::stepPattern (int t)
{
    std::lock_guard<bash::SpinMutex> g (engine.stepMutex (t));
    return engine.stepPattern (t);
}

void BashProcessor::setStepPattern (int t, const bash::StepPattern& p)
{
    std::lock_guard<bash::SpinMutex> g (engine.stepMutex (t));
    engine.stepPattern (t) = p;
}

void BashProcessor::editSteps (int t, const std::function<void (bash::StepPattern&)>& fn)
{
    auto p = stepPattern (t);   // edit a copy, swap it in under the lock (the audio thread never waits long)
    fn (p);
    setStepPattern (t, p);
}

bash::LaneRole BashProcessor::laneRole (int t, int lane) const
{
    return engineType (t) == bash::EngineType::Drums ? bash::drumLaneRole (lane) : bash::LaneRole::Melodic;
}

juce::String BashProcessor::laneName (int t, int lane)
{
    switch (engineType (t))
    {
        case bash::EngineType::Drums:
        {
            const auto name = cellName (lane);
            return name.isNotEmpty() ? name : "Pad " + juce::String (lane + 1);
        }
        case bash::EngineType::Slicer:
            return "Slice " + juce::String ((int) load (tp[(size_t) t].winStart) + lane);
        case bash::EngineType::Synth:
        default:
        {
            const auto sp = seqParams (t);
            const bash::Scale sc ((int) load (pRoot), (int) load (pScale));
            const int note = sp.scaleLock ? sc.stepFrom (sp.baseNote, lane) : sp.baseNote + lane;
            return juce::String (bash::noteName (((note % 12) + 12) % 12)) + juce::String (note / 12 - 1);
        }
    }
}

void BashProcessor::generateLane (int t, int lane)
{
    const auto role = laneRole (t, lane);
    const auto roll = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    editSteps (t, [&] (bash::StepPattern& p) { bash::stepgen::generate (p.lanes[(size_t) lane], role, roll); });
}

void BashProcessor::rollLane (int t, int lane)
{
    pushDiceSnapshot (t);
    const auto role = laneRole (t, lane);
    const auto roll = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    editSteps (t, [&] (bash::StepPattern& p) { bash::stepgen::rollLane (p.lanes[(size_t) lane], role, roll); });
}

void BashProcessor::rollStepTrack (int t)
{
    // The track die in step mode: re-roll every lane that's in use. On an empty drum
    // pattern, roll a starter kit groove (kick, snare, clap, hats).
    const auto roll = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    const bool drums = engineType (t) == bash::EngineType::Drums;
    editSteps (t, [&] (bash::StepPattern& p)
    {
        const bool empty = p.activeLanes() == 0;
        for (int l = 0; l < bash::stepLanes; ++l)
        {
            auto& lane = p.lanes[(size_t) l];
            bool used = false;
            for (int i = 0; i < lane.length; ++i) used = used || lane.steps[(size_t) i].on;
            if (empty) used = drums ? l < 5 : l == 0 || l == 2 || l == 4;
            if (! used) continue;
            if (empty && drums) lane.algo = l == 3 ? bash::LaneAlgo::Rolls : bash::LaneAlgo::Groove;
            if (empty && ! drums) lane.algo = bash::LaneAlgo::Random;
            bash::stepgen::rollLane (lane, laneRole (t, l), roll + (uint64_t) l * 7919u);
        }
    });
}

// ---- Roll the Dice (message thread) --------------------------------------------
// Each track has its own die and it only touches that track's sequencer: rhythm,
// melody and playback settings plus a new pattern seed. Sound, effects, mixer,
// key and tempo are never rolled. Algorithm, rate, octave and on/off stay as you set them.
bool BashProcessor::isDiceParam (const juce::String& id, int track)
{
    const juce::String pre = "t" + juce::String (track + 1) + "_";
    if (! id.startsWith (pre)) return false;
    // rate and octave stay put: a roll to a 1-bar step or a sub-bass register sounds broken, not random
    static const juce::StringArray seq { "type", "density", "steps", "offset", "direction", "accent", "gate",
                                         "slicemode", "winstart", "winsize", "swing", "jitter" };
    return seq.contains (id.substring (pre.length()));
}

void BashProcessor::pushDiceSnapshot (int track)
{
    DiceSnapshot snap;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isDiceParam (rp->getParameterID(), track))
                snap.params.emplace_back (rp->getParameterID(), rp->getValue());
    {
        auto& pat = engine.pattern (track);
        std::lock_guard<bash::SpinMutex> g (pat.mutex());
        snap.seed = pat.getSeed();
    }
    snap.steps = stepPattern (track).serialise();
    auto& history = diceHistory[(size_t) track];
    history.push_back (std::move (snap));
    if (history.size() > 16) history.erase (history.begin());
}

void BashProcessor::rollDice (int track)
{
    track = juce::jlimit (0, bash::numTracks - 1, track);

    if (stepMode (track))
    {
        pushDiceSnapshot (track);
        rollStepTrack (track);
        return;
    }

    DiceSnapshot snap;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isDiceParam (rp->getParameterID(), track))
                snap.params.emplace_back (rp->getParameterID(), rp->getValue());
    {
        auto& pat = engine.pattern (track);
        std::lock_guard<bash::SpinMutex> g (pat.mutex());
        snap.seed = pat.getSeed();
    }
    snap.steps = stepPattern (track).serialise();
    auto& history = diceHistory[(size_t) track];
    history.push_back (std::move (snap));
    if (history.size() > 16) history.erase (history.begin());

    const auto rollSeed = (juce::int64) juce::Random::getSystemRandom().nextInt64();
    juce::Random rnd (rollSeed);
    const float chaos = juce::jlimit (0.0f, 1.0f, dice.chaos);

    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || ! isDiceParam (rp->getParameterID(), track) || diceLocked.contains (rp->getParameterID()))
            continue;

        const float cur = rp->getValue();
        float next;
        if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr)
            next = rnd.nextFloat() < chaos * 0.5f ? 1.0f - cur : cur;
        else if (chaos >= 0.999f)
            next = rnd.nextFloat();
        else
            next = juce::jlimit (0.0f, 1.0f, cur + (rnd.nextFloat() * 2.0f - 1.0f) * chaos);

        if (next != cur)
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (next);
            rp->endChangeGesture();
        }
    }

    if (dice.includePatterns)
    {
        auto& pat = engine.pattern (track);
        std::lock_guard<bash::SpinMutex> g (pat.mutex());
        pat.setSeed (bash::mix64 ((uint64_t) rollSeed + (uint64_t) track));   // locks are kept
    }
}

bool BashProcessor::undoDice (int track)
{
    track = juce::jlimit (0, bash::numTracks - 1, track);
    auto& history = diceHistory[(size_t) track];
    if (history.empty()) return false;
    const auto snap = history.back();
    history.pop_back();
    for (const auto& [id, v] : snap.params)
        if (auto* p = apvts.getParameter (id))
            if (std::abs (p->getValue() - v) > 1.0e-6f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (v);
                p->endChangeGesture();
            }
    {
        bash::StepPattern sp;
        if (sp.deserialise (snap.steps)) setStepPattern (track, sp);
    }
    auto& pat = engine.pattern (track);
    std::lock_guard<bash::SpinMutex> g (pat.mutex());
    pat.setSeed (snap.seed);
    return true;
}

// ---- MIDI import / export, audio export (message thread) -------------------------
juce::File BashProcessor::exportFolder()
{
   #if JUCE_IOS
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("BASH Exports");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("BASH Exports");
   #endif
    dir.createDirectory();
    return dir;
}

double BashProcessor::exportBpm() const
{
    const bool synced = load (pSync) > 0.5f && lastHostBpm.load() > 0.0;
    return synced ? lastHostBpm.load() : (double) load (pTempo);
}

static double stepsPerQuarterFor (int rateIndex)
{
    const auto& rates = bash::allRates();
    return rates[(size_t) juce::jlimit (0, (int) rates.size() - 1, rateIndex)].stepsPerQuarter;
}

juce::String BashProcessor::importMidi (int t, const juce::File& file)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) return "Couldn't read " + file.getFileName();
    const int tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
    juce::MidiMessageSequence found;
    bool any = false;
    for (int i = 0; i < mf.getNumTracks() && ! any; ++i)
    {
        const auto* tr = mf.getTrack (i);
        for (int e = 0; e < tr->getNumEvents(); ++e)
            if (tr->getEventPointer (e)->message.isNoteOn()) { found = *tr; any = true; break; }
    }
    if (! any) return file.getFileName() + " has no notes";
    found.updateMatchedPairs();
    const juce::MidiMessageSequence* seq = &found;

    const double spq = stepsPerQuarterFor ((int) load (tp[(size_t) t].rate));
    double lastBeat = 0.0;
    struct N { double step, len; int note, vel; };
    std::vector<N> notes;
    for (int e = 0; e < seq->getNumEvents(); ++e)
    {
        const auto* ev = seq->getEventPointer (e);
        if (! ev->message.isNoteOn()) continue;
        const double startBeat = ev->message.getTimeStamp() / tpq;
        const double endBeat = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() / tpq : startBeat + 0.25;
        notes.push_back ({ startBeat * spq, (endBeat - startBeat) * spq, ev->message.getNoteNumber(), ev->message.getVelocity() });
        lastBeat = std::max (lastBeat, endBeat);
    }

    const int stepsPerBar = (int) std::lround (spq * 4.0);
    int length = (int) std::ceil (lastBeat * spq / stepsPerBar - 1.0e-6) * stepsPerBar;
    length = juce::jlimit (1, 4096, std::max (length, stepsPerBar));
    std::vector<bash::StepData> motif ((size_t) length);
    for (auto& m : motif) { m.active = false; m.note = notes.front().note; m.gate = 0.5f; }
    for (const auto& n : notes)
    {
        const int s = (int) std::lround (n.step);
        if (s < 0 || s >= length) continue;
        auto& m = motif[(size_t) s];
        if (m.active) continue;   // first note on a step wins
        m.active = true;
        m.note = n.note;
        m.accent = n.vel >= 100;
        m.gate = (float) juce::jlimit (0.05, 2.0, n.len);
    }
    {
        auto& pat = engine.pattern (t);
        std::lock_guard<bash::SpinMutex> g (pat.mutex());
        pat.setMotif (std::move (motif));
    }
    auto* steps = apvts.getParameter (pid (t, "steps"));
    steps->setValueNotifyingHost (steps->convertTo0to1 ((float) std::min (length, 64)));
    auto* type = apvts.getParameter (pid (t, "type"));
    type->setValueNotifyingHost (0.0f);   // start exactly on the imported phrase
    return {};
}

void BashProcessor::clearMotif (int t)
{
    auto& pat = engine.pattern (t);
    std::lock_guard<bash::SpinMutex> g (pat.mutex());
    pat.clearMotif();
}

bool BashProcessor::hasMotif (int t)
{
    auto& pat = engine.pattern (t);
    std::lock_guard<bash::SpinMutex> g (pat.mutex());
    return ! pat.getMotif().empty();
}

juce::String BashProcessor::exportName (int t, const juce::String& ext) const
{
    const auto when = juce::Time::getCurrentTime().formatted ("%H%M%S");
    const juce::String what = t < 0 ? juce::String ("Mix") : "Track " + juce::String (t + 1);
    return "BASH " + what + " " + juce::String (exportBars) + " bars " + juce::String (exportBpm(), 0) + "bpm " + when + ext;
}

juce::File BashProcessor::exportMidi (int t)
{
    const double spq = stepsPerQuarterFor ((int) load (tp[(size_t) t].rate));
    const int64_t steps = (int64_t) std::llround (spq * 4.0 * exportBars);
    std::vector<bash::ExportNote> notes;
    if (stepMode (t))
    {
        const auto ctx = bash::stepContextFor (engineType (t), uiSeqParams (t), scaleNow(), spq,
                                               load (tp[(size_t) t].fill) > 0.5f, autoFillBars ((int) load (tp[(size_t) t].autoFill)));
        notes = bash::renderStepNotes (stepPattern (t), ctx, steps);
    }
    else
    {
        auto& pat = engine.pattern (t);
        const auto params = uiSeqParams (t);
        const auto sc = scaleNow();
        std::lock_guard<bash::SpinMutex> g (pat.mutex());
        notes = bash::renderNotes (pat, params, sc, steps);
    }

    const int tpq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / exportBpm())), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (const auto& n : notes)
    {
        const int ch = n.stream == 1 ? 2 : 1;
        seq.addEvent (juce::MidiMessage::noteOn (ch, n.note, (juce::uint8) juce::jlimit (1, 127, n.velocity)), n.start / spq * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (ch, n.note), n.end / spq * tpq);
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), (double) steps / spq * tpq);
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    auto file = exportFolder().getChildFile (exportName (t, ".mid"));
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out)) return {};
    return file;
}

juce::File BashProcessor::exportAudio (int t)
{
    bash::RenderOptions o;
    o.track = t;
    o.bars = exportBars;
    o.bpm = exportBpm();
    o.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    o.withFx = exportWithFx;
    o.tailSeconds = exportWithFx ? 6.0 : 1.0;
    std::vector<float> l, r;
    bash::Engine::renderOffline (engine, o, l, r);

    juce::AudioBuffer<float> buf (2, (int) l.size());
    buf.copyFrom (0, 0, l.data(), (int) l.size());
    buf.copyFrom (1, 0, r.data(), (int) r.size());

    auto file = exportFolder().getChildFile (exportName (t, ".wav"));
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr) return {};
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), o.sampleRate, 2, 24, {}, 0));
    if (writer == nullptr) return {};
    stream.release();   // the writer owns it now
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    return file;
}

// ---- Ableton key lock --------------------------------------------------------------
int BashProcessor::scaleForMask (int mask)
{
    // The BASH! Key Lock device sends Live's scale as a 12-bit mask of the notes above the
    // root. Pick the BASH! scale with exactly those notes, or else the closest one.
    mask |= 1;
    const auto& scales = bash::allScales();
    int best = 0;
    float bestScore = -1.0f;
    for (int i = 0; i < (int) scales.size(); ++i)
    {
        int m = 0;
        for (int iv : scales[(size_t) i].intervals) m |= 1 << (iv % 12);
        if (m == mask) return i;
        const float common = (float) juce::countNumberOfBits ((juce::uint32) (m & mask));
        const float either = (float) juce::countNumberOfBits ((juce::uint32) (m | mask));
        const float score = common / either;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    return best;
}

void BashProcessor::setParamFromAudio (const juce::String& id, float value)
{
    auto* param = apvts.getParameter (id);
    const float norm = param->convertTo0to1 (value);
    if (std::abs (param->getValue() - norm) > 1.0e-4f)
        param->setValueNotifyingHost (norm);
}
