#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include "bash/Engine.h"
#include "FactoryLibrary.h"
#include "bash/Slices.h"
#include "bash/Drums.h"

#include <array>
#include <mutex>

class BashProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BashProcessor();
    ~BashProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "BASH!"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return true; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.5; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread) ----
    juce::AudioProcessorValueTreeState apvts;

    static juce::String pid (int track, const char* name) { return "t" + juce::String (track + 1) + "_" + name; }

    bash::SequencerParams seqParams (int track) const;
    bash::Scale           scaleNow() const;

    bash::SequencerParams uiSeqParams (int track);   // seqParams + current slice count
    bash::EngineType      engineType (int track) const;

    std::vector<bash::StepData> snapshot (int track, uint32_t first, uint32_t count);
    uint32_t playIndex (int track) const { return engine.playIndex (track); }
    float    trackMeter (int track) const { return engine.trackLevelMeter (track); }
    bool     isRunning() const           { return engine.isRunning(); }

    void regenerate (int track);
    void toggleLock (int track, uint32_t index);
    void toggleMute (int track, uint32_t index);
    void clearLocks (int track);
    uint8_t lockMask (int track, uint32_t index);

    // samples and slices
    juce::String loadSample (int track, const juce::File&, bool autoSlice = true, double knownBpm = 0.0);  // returns error or empty
    void clearSample (int track);
    void setSlices (int track, std::vector<uint32_t>);
    std::vector<uint32_t> slices (int track)           { return engine.slices (track); }
    const bash::SampleData* sample (int track) const   { return engine.sample (track); }
    std::shared_ptr<const bash::SampleData> sampleRef (int track) const { return sampleSlots[(size_t) track]; }

    // Warp: slicer samples are time-stretched (pitch kept) from their own tempo to the session tempo
    bool   warpOn (int track) const;
    double sourceBpm (int track) const                 { return warps[(size_t) track].sourceBpm; }
    void   setSourceBpm (int track, double bpm);        // the user corrects the detected tempo
    double warpTargetBpm() const                        { return exportBpm(); }
    double warpRatio (int track) const                  { return warps[(size_t) track].applied; }
    bool   warpBusy (int track) const                   { return warps[(size_t) track].requested != warps[(size_t) track].applied; }
    /** Re-warp any track whose tempo, source tempo or warp switch changed (message thread).
        synchronous = do it now (tests, session load); otherwise on a background thread. */
    void   updateWarps (bool synchronous);
    int    playingSlice (int track) const              { return engine.playingSlice (track); }
    double playingFrame (int track) const              { return engine.playingFrame (track); }
    juce::String missingSample (int track) const       { return missingSamples[(size_t) track]; }
    juce::AudioFormatManager& formatManager()          { return formats; }

    // drum kit
    bash::DrumCell cell (int i)                        { return engine.cell (i); }
    void setCell (int i, const bash::DrumCell&);       // keeps the cell's sample
    juce::String cellName (int i) const                { return cells[(size_t) i].name; }
    void setCellName (int i, const juce::String& n)    { cells[(size_t) i].name = n; }
    std::shared_ptr<const bash::SampleData> cellSample (int i) const { return cells[(size_t) i].sample; }
    juce::String loadCellSample (int i, const juce::File&);
    void setCellFm (int i, int presetIndex);
    void clearCell (int i);
    void loadFactoryKit (int k);

    // master meters (dB of gain reduction)
    float glueReduction() const noexcept    { return engine.glueReduction(); }
    float limiterReduction() const noexcept { return engine.limiterReduction(); }

    // MICRO
    bash::MicroEngine& micro() noexcept               { return engine.micro(); }
    juce::File exportMicroLoop();                      // the looper's loop as a WAV (empty File if none)
    void rollMicro();                                  // the MICRO die: a new effect / variation / tone
    static constexpr const char* microParamIds[] = { "m_on", "m_algo", "m_var", "m_act", "m_rep", "m_shape", "m_time", "m_filter", "m_reso",
        "m_modrate", "m_moddepth", "m_space", "m_verb", "m_decay", "m_mix", "m_level", "m_rev", "m_hold",
        "m_looplevel", "m_loopfade", "m_loopspeed", "m_looprev", "m_quant", "m_loopmode" };

    // factory library (rendered to disk in the background on first run)
    FactoryLibrary& library()                          { return *factoryLibrary; }
    /** Load library kit k onto the pads; optionally its starter beat onto `track` (step mode). */
    juce::String loadLibraryKit (int k, int track = -1);
    juce::String loadLibraryShot (int pad, const std::string& categoryAndName);
    juce::String loadLibraryLoop (int track, int loopIndex);
    /** Write a groove into a track's step sequencer (switches it to step mode, sets swing and rate). */
    void applyGroove (int track, int grooveIndex);
    juce::String currentKitName() const                { return kitName; }
    void auditionCell (int i)                          { engine.audition (i); }

    // modulation
    bash::ModMatrix& matrix (int t)                    { return engine.matrix (t); }
    float  modSourceValue (int t, bash::ModSource s) const { return engine.sourceValue (t, s); }
    double lfoPhase (int i) const                      { return engine.lfoPhase (i); }

    // Step sequencer (per track, instead of the generative one)
    bool stepMode (int track) const;
    bash::StepPattern stepPattern (int track);
    void setStepPattern (int track, const bash::StepPattern&);
    void editSteps (int track, const std::function<void (bash::StepPattern&)>&);
    int  stepLanePosition (int track, int lane) const  { return engine.stepLanePosition (track, lane); }
    juce::String laneName (int track, int lane);
    bash::LaneRole laneRole (int track, int lane) const;
    void generateLane (int track, int lane);   // rewrite the lane with its algorithm
    void rollLane (int track, int lane);       // the lane die: new algorithm settings, then generate
    void rollStepTrack (int track);            // the track die in step mode
    static const juce::StringArray& autoFillNames();
    static int autoFillBars (int index);

    // Roll the Dice: one die per track, rolls only that track's sequencer
    struct DiceSettings
    {
        float chaos = 1.0f;          // 0..1: how far a roll may move each sequencer setting
        bool  includePatterns = true; // also pick a new pattern seed (locked steps are kept)
    };
    DiceSettings dice;
    juce::StringArray diceLocked;  // parameter ids protected from the dice
    void rollDice (int track);
    bool undoDice (int track);
    int  diceUndoCount (int track) const               { return (int) diceHistory[(size_t) juce::jlimit (0, bash::numTracks - 1, track)].size(); }
    uint64_t patternSeed (int track)                   { auto& pat = engine.pattern (track); std::lock_guard<bash::SpinMutex> g (pat.mutex()); return pat.getSeed(); }
    static bool isDiceParam (const juce::String& paramId, int track);

    // MIDI import / export, audio export
    juce::String importMidi (int track, const juce::File&);   // error or empty
    void clearMotif (int track);
    bool hasMotif (int track);
    juce::File exportMidi (int track);              // writes to exportFolder(); empty File on failure
    juce::File exportAudio (int track);             // track -1 = full mix
    static juce::File exportFolder();
    int  exportBars = 4;
    bool exportWithFx = true;

    static int scaleForMask (int twelveBitMask);   // used by the Ableton key lock

    static const juce::StringArray& cycleNames();
    static uint32_t cycleMultiplier (int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    struct FxPtrs { std::atomic<float> *type = nullptr, *a = nullptr, *b = nullptr, *gnarl = nullptr, *mix = nullptr; };
    FxPtrs fxPtrs (const juce::String& prefix);
    static bash::FxParams fxFrom (const FxPtrs&);
    FxPtrs retA, retB;
    std::vector<std::atomic<float>*> microP;
    std::atomic<float> *pLimDrive = nullptr, *pLimCeil = nullptr, *pLimRel = nullptr, *pGlueOn = nullptr, *pGlueThr = nullptr,
                       *pGlueRatio = nullptr, *pGlueAtt = nullptr, *pGlueRel = nullptr, *pGlueMakeup = nullptr, *pGlueMix = nullptr, *pGlueHpf = nullptr;
    std::atomic<float> *pLimiter = nullptr, *pRetALevel = nullptr, *pRetBLevel = nullptr;
    struct LfoPtrs { std::atomic<float> *shape = nullptr, *rate = nullptr, *sync = nullptr, *div = nullptr; };
    std::array<LfoPtrs, 2> lfoP;
    struct DiceSnapshot { std::vector<std::pair<juce::String, float>> params; uint64_t seed = 0; std::string steps; };
    void pushDiceSnapshot (int track);
    std::array<std::vector<DiceSnapshot>, bash::numTracks> diceHistory;
    void pullParameters();
    void timerCallback() override;
    std::shared_ptr<bash::SampleData> decode (const juce::File&, double maxSeconds, juce::String& error);
    void retireCellSample (int i);
    double exportBpm() const;
    juce::String exportName (int track, const juce::String& ext) const;
    std::atomic<double> lastHostBpm { 0.0 };
    int keyLockMaskLow = 0;
    void setParamFromAudio (const juce::String& id, float value);
    juce::ValueTree kitToTree();
    void kitFromTree (const juce::ValueTree&);

    struct CellInfo { juce::String name; std::shared_ptr<bash::SampleData> sample; };
    std::array<CellInfo, bash::numCells> cells;

    struct TrackParams
    {
        std::atomic<float> *on, *algo, *type, *density, *steps, *cycle, *offset, *direction, *rate,
                           *accent, *gate, *octave, *scaleLock,
                           *wave, *cutoff, *reso, *envMod, *decay, *drive, *glide, *level, *pan,
                           *machine, *timbre, *ftype, *aAtk, *aDec, *aSus, *aRel,
                           *mute, *solo, *sendA, *sendB, *lifeRate, *swing, *jitter,
                           *engine, *sliceMode, *winStart, *winSize, *pitchFollow, *tune, *sAttack, *sRelease, *reverse,
                           *mode, *fill, *autoFill, *warp;
        std::array<FxPtrs, 2> fx;
    };
    std::array<TrackParams, bash::numTracks> tp {};
    std::atomic<float> *pRoot = nullptr, *pScale = nullptr, *pTempo = nullptr, *pSync = nullptr,
                       *pPlay = nullptr, *pMaster = nullptr;

    bash::Engine engine;
    std::vector<bash::MidiOut> midiOut;
    juce::AudioBuffer<float> scratch;

    juce::AudioFormatManager formats;
    std::array<std::shared_ptr<bash::SampleData>, bash::numTracks> sampleSlots;
    struct WarpState
    {
        std::shared_ptr<bash::SampleData> original;   // the file as decoded
        double sourceBpm = 120.0;
        double applied = 1.0, requested = 1.0;        // stretch ratio playing now / asked for
        std::mutex m;                                  // guards `ready` (background job -> message thread)
        std::shared_ptr<bash::SampleData> ready;
        double readyRatio = 1.0;
    };
    std::array<WarpState, bash::numTracks> warps;
    juce::SharedResourcePointer<FactoryLibrary> factoryLibrary;
    juce::String kitName;
    double lastWarpTarget = 0.0;
    juce::uint32 warpTargetChanged = 0;
    void installWarped (int t, std::shared_ptr<bash::SampleData>, double ratio);
    double desiredWarpRatio (int t) const;
    std::array<juce::String, bash::numTracks> missingSamples;
    std::vector<std::pair<std::shared_ptr<bash::SampleData>, uint64_t>> graveyard;
    juce::ThreadPool warpPool { 1 };   // after `warps`: destroyed (and its jobs finished) first
    bool isPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BashProcessor)
};
