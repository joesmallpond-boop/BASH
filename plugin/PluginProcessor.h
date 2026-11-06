#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include "bash/Engine.h"
#include "bash/Slices.h"
#include "bash/Drums.h"

#include <array>

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
    juce::String loadSample (int track, const juce::File&, bool autoSlice = true);  // returns error or empty
    void clearSample (int track);
    void setSlices (int track, std::vector<uint32_t>);
    std::vector<uint32_t> slices (int track)           { return engine.slices (track); }
    const bash::SampleData* sample (int track) const   { return engine.sample (track); }
    std::shared_ptr<const bash::SampleData> sampleRef (int track) const { return sampleSlots[(size_t) track]; }
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
    void auditionCell (int i)                          { engine.audition (i); }

    // modulation
    bash::ModMatrix& matrix (int t)                    { return engine.matrix (t); }
    float  modSourceValue (int t, bash::ModSource s) const { return engine.sourceValue (t, s); }
    double lfoPhase (int i) const                      { return engine.lfoPhase (i); }

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
    std::array<FxPtrs, 2> mfx;
    std::atomic<float> *pLimiter = nullptr, *pRetALevel = nullptr, *pRetBLevel = nullptr;
    struct LfoPtrs { std::atomic<float> *shape = nullptr, *rate = nullptr, *sync = nullptr, *div = nullptr; };
    std::array<LfoPtrs, 2> lfoP;
    struct DiceSnapshot { std::vector<std::pair<juce::String, float>> params; uint64_t seed = 0; };
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
                           *engine, *sliceMode, *winStart, *winSize, *pitchFollow, *tune, *sAttack, *sRelease, *reverse;
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
    std::array<juce::String, bash::numTracks> missingSamples;
    std::vector<std::pair<std::shared_ptr<bash::SampleData>, uint64_t>> graveyard;
    bool isPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BashProcessor)
};
