#include "PluginProcessor.h"
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
    layout.add (std::move (global));

    for (int t = 0; t < bash::numTracks; ++t)
    {
        const String n = "T" + String (t + 1) + " ";
        auto id = [t] (const char* s) { return ParameterID { pid (t, s), 1 }; };
        auto g = std::make_unique<AudioProcessorParameterGroup> ("track" + String (t + 1), "Track " + String (t + 1), "|");

        g->addChild (std::make_unique<AudioParameterBool>   (id ("on"),        n + "On", t == 0));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("algo"),      n + "Algorithm", algorithmNames(), t % 2));
        g->addChild (floatParam (id ("type"),      n + "Type",    NormalisableRange<float> (0.0f, 1.0f), 0.15f, 2));
        g->addChild (floatParam (id ("density"),   n + "Density", NormalisableRange<float> (0.0f, 1.0f), 0.6f, 2));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("steps"),     n + "Steps",   1, 256, 16));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("cycle"),     n + "Cycle",   cycleNames(), 0));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("offset"),    n + "Offset",  0, 255, 0));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("direction"), n + "Direction", directionNames(), 0));
        g->addChild (std::make_unique<AudioParameterChoice> (id ("rate"),      n + "Rate",    rateNames(), 1));
        g->addChild (floatParam (id ("accent"),    n + "Accent",  NormalisableRange<float> (0.0f, 1.0f), 0.6f, 2));
        g->addChild (floatParam (id ("gate"),      n + "Gate",    NormalisableRange<float> (0.05f, 2.0f), 0.5f, 2));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("octave"),    n + "Octave",  0, 6, 2));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("scalelock"), n + "Scale Lock", true));

        g->addChild (floatParam (id ("wave"),      n + "Wave",      NormalisableRange<float> (0.0f, 1.0f), t % 2 ? 1.0f : 0.0f, 2));
        g->addChild (floatParam (id ("cutoff"),    n + "Cutoff",    skewed (30.0f, 8000.0f, 500.0f), 380.0f, 0, " Hz"));
        g->addChild (floatParam (id ("reso"),      n + "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.72f, 2));
        g->addChild (floatParam (id ("envmod"),    n + "Env Mod",   NormalisableRange<float> (0.0f, 1.0f), 0.55f, 2));
        g->addChild (floatParam (id ("decay"),     n + "Decay",     skewed (0.02f, 2.0f, 0.3f), 0.3f, 3, " s"));
        g->addChild (floatParam (id ("drive"),     n + "Drive",     NormalisableRange<float> (0.0f, 1.0f), 0.35f, 2));
        g->addChild (floatParam (id ("glide"),     n + "Glide",     skewed (0.005f, 0.5f, 0.06f), 0.06f, 3, " s"));
        g->addChild (floatParam (id ("level"),     n + "Level",     NormalisableRange<float> (0.0f, 1.0f), 0.8f, 2));
        g->addChild (floatParam (id ("pan"),       n + "Pan",       NormalisableRange<float> (-1.0f, 1.0f), 0.0f, 2));

        layout.add (std::move (g));
    }
    return layout;
}

BashProcessor::BashProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BASH", createLayout())
{
    pRoot   = apvts.getRawParameterValue ("root");
    pScale  = apvts.getRawParameterValue ("scale");
    pTempo  = apvts.getRawParameterValue ("tempo");
    pSync   = apvts.getRawParameterValue ("sync");
    pPlay   = apvts.getRawParameterValue ("play");
    pMaster = apvts.getRawParameterValue ("master");

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
    }
}

bool BashProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void BashProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
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
    const uint64_t len = (uint64_t) juce::jmax (1, (int) load (p.steps)) * cycleMultiplier ((int) load (p.cycle));
    s.length       = (uint32_t) std::min<uint64_t> (len, 0xFFFFFFFFull);
    s.offset       = (uint32_t) juce::jmax (0, (int) load (p.offset));
    s.direction    = (bash::Direction) juce::jlimit (0, (int) bash::Direction::count - 1, (int) load (p.direction));
    s.accentAmount = load (p.accent);
    s.gate         = load (p.gate);
    s.baseNote     = 12 * ((int) load (p.octave) + 1) + (int) load (pRoot);
    s.scaleLock    = load (p.scaleLock) > 0.5f;
    return s;
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

    for (int t = 0; t < bash::numTracks; ++t)
    {
        const auto& p = tp[(size_t) t];
        auto& ts = engine.track (t);
        ts.enabled   = load (p.on) > 0.5f;
        ts.seq       = seqParams (t);
        ts.rateIndex = (int) load (p.rate);
        ts.voice.wave      = load (p.wave);
        ts.voice.cutoff    = load (p.cutoff);
        ts.voice.resonance = load (p.reso);
        ts.voice.envMod    = load (p.envMod);
        ts.voice.decay     = load (p.decay);
        ts.voice.accent    = load (p.accent);
        ts.voice.drive     = load (p.drive);
        ts.voice.glide     = load (p.glide);
        ts.level = load (p.level);
        ts.pan   = load (p.pan);
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
            }
        }
    }

    const int n = buffer.getNumSamples();
    if (scratch.getNumSamples() < n)
        scratch.setSize (1, n, false, false, true);   // only if the host lied about block size

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratch.getWritePointer (0);

    midiOut.clear();
    engine.process (left, right, n, transport, midiOut);

    if (buffer.getNumChannels() == 1)
        juce::FloatVectorOperations::add (left, right, n), juce::FloatVectorOperations::multiply (left, 0.5f, n);

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
std::vector<bash::StepData> BashProcessor::snapshot (int t, uint32_t first, uint32_t count)
{
    return engine.snapshot (t, seqParams (t), scaleNow(), first, count);
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
        engine.lockStep (t, seqParams (t), scaleNow(), index);
    }
}

void BashProcessor::toggleMute (int t, uint32_t index)
{
    const auto params = seqParams (t);
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
    apvts.replaceState (tree);

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
