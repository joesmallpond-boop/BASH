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

    juce::StringArray engineNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::EngineType::count; ++i) s.add (bash::engineTypeName ((bash::EngineType) i));
        return s;
    }

    juce::StringArray sliceModeNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) bash::SliceMode::count; ++i) s.add (bash::sliceModeName ((bash::SliceMode) i));
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
        g->addChild (std::make_unique<AudioParameterChoice> (id ("engine"),    n + "Engine", engineNames(), 0));
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
        // slicer
        g->addChild (std::make_unique<AudioParameterChoice> (id ("slicemode"), n + "Slice Mode",   sliceModeNames(), 0));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("winstart"),  n + "Window Start", 1, 256, 1));
        g->addChild (std::make_unique<AudioParameterInt>    (id ("winsize"),   n + "Window Size",  1, 256, 4));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("pitchfollow"), n + "Pitch Follow", false));
        g->addChild (floatParam (id ("tune"),      n + "Tune",          NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, 2, " st"));
        g->addChild (floatParam (id ("sattack"),   n + "Slice Attack",  skewed (0.0005f, 0.5f, 0.02f), 0.002f, 3, " s"));
        g->addChild (floatParam (id ("srelease"),  n + "Slice Release", skewed (0.002f, 2.0f, 0.1f), 0.03f, 3, " s"));
        g->addChild (std::make_unique<AudioParameterBool>   (id ("reverse"),   n + "Reverse", false));

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
    formats.registerBasicFormats();
    startTimer (1000);

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
        p.engine = get ("engine"); p.sliceMode = get ("slicemode"); p.winStart = get ("winstart");
        p.winSize = get ("winsize"); p.pitchFollow = get ("pitchfollow"); p.tune = get ("tune");
        p.sAttack = get ("sattack"); p.sRelease = get ("srelease"); p.reverse = get ("reverse");
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
    const uint64_t len = (uint64_t) juce::jmax (1, (int) load (p.steps)) * cycleMultiplier ((int) load (p.cycle));
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
        ts.engine    = (bash::EngineType) juce::jlimit (0, (int) bash::EngineType::count - 1, (int) load (p.engine));
        ts.slicer.pitchFollow   = load (p.pitchFollow) > 0.5f;
        ts.slicer.tune          = load (p.tune);
        ts.slicer.voice.attack  = load (p.sAttack);
        ts.slicer.voice.release = load (p.sRelease);
        ts.slicer.voice.reverse = load (p.reverse) > 0.5f;
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
bash::SequencerParams BashProcessor::uiSeqParams (int t)
{
    auto p = seqParams (t);
    if (engineType (t) == bash::EngineType::Slicer && engine.sample (t) != nullptr)
        p.sliceCount = engine.sliceCount (t);
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

    juce::ValueTree samplesTree ("Samples");
    for (int t = 0; t < bash::numTracks; ++t)
    {
        const auto* d = engine.sample (t);
        if (d == nullptr) continue;
        juce::String sl;
        for (auto f : engine.slices (t)) sl << (juce::int64) f << ' ';
        juce::ValueTree st ("Sample");
        st.setProperty ("track", t, nullptr);
        st.setProperty ("path", juce::String (d->path), nullptr);
        st.setProperty ("slices", sl.trim(), nullptr);
        samplesTree.appendChild (st, nullptr);
    }
    state.removeChild (state.getChildWithName ("Samples"), nullptr);
    state.appendChild (samplesTree, nullptr);

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
    const auto samplesTree = tree.getChildWithName ("Samples").createCopy();
    apvts.replaceState (tree);

    for (int t = 0; t < bash::numTracks; ++t)
        clearSample (t);
    for (const auto& st : samplesTree)
    {
        const int t = juce::jlimit (0, bash::numTracks - 1, (int) st.getProperty ("track"));
        const juce::File file (st.getProperty ("path").toString());
        if (file.existsAsFile() && loadSample (t, file, false).isEmpty())
        {
            std::vector<uint32_t> sl;
            for (auto& tok : juce::StringArray::fromTokens (st.getProperty ("slices").toString(), " ", ""))
                if (tok.isNotEmpty()) sl.push_back ((uint32_t) tok.getLargeIntValue());
            setSlices (t, std::move (sl));
        }
        else
        {
            missingSamples[(size_t) t] = file.getFullPathName();
        }
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
juce::String BashProcessor::loadSample (int t, const juce::File& file, bool autoSlice)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Couldn't read " + file.getFileName();

    const auto maxFrames = (juce::int64) (reader->sampleRate * 600.0);   // 10 minutes
    if (reader->lengthInSamples <= 0)
        return file.getFileName() + " is empty";
    const int frames = (int) juce::jmin (reader->lengthInSamples, maxFrames);

    juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), frames);
    reader->read (&buf, 0, frames, 0, true, buf.getNumChannels() > 1);

    auto data = std::make_shared<bash::SampleData>();
    data->sampleRate = reader->sampleRate;
    data->path = file.getFullPathName().toStdString();
    data->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + frames);
    if (buf.getNumChannels() > 1) data->right.assign (buf.getReadPointer (1), buf.getReadPointer (1) + frames);
    else                          data->right = data->left;

    auto& slot = sampleSlots[(size_t) t];
    if (slot != nullptr)
        graveyard.push_back ({ slot, engine.blocksProcessed() });
    slot = data;
    engine.setSample (t, data.get());

    missingSamples[(size_t) t] = {};
    if (autoSlice)
    {
        auto s = bash::slicing::transients (*data, 0.5f);
        if (s.size() < 2) s = bash::slicing::grid (data->numFrames(), 16);   // no clear hits: even grid
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
    engine.setSample (t, nullptr);
    engine.setSlices (t, {});
}

void BashProcessor::setSlices (int t, std::vector<uint32_t> s)
{
    const auto* d = engine.sample (t);
    bash::slicing::normalise (s, d != nullptr ? d->numFrames() : 0u);
    engine.setSlices (t, std::move (s));
}

void BashProcessor::timerCallback()
{
    // free replaced samples once the audio thread has moved past them (or isn't running)
    const auto now = engine.blocksProcessed();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now, this] (const auto& g) { return now >= g.second + 2 || ! isPrepared; }),
                     graveyard.end());
}
