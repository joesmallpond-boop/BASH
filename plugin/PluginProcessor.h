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

    static const juce::StringArray& cycleNames();
    static uint32_t cycleMultiplier (int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParameters();
    void timerCallback() override;
    std::shared_ptr<bash::SampleData> decode (const juce::File&, double maxSeconds, juce::String& error);
    void retireCellSample (int i);
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
                           *engine, *sliceMode, *winStart, *winSize, *pitchFollow, *tune, *sAttack, *sRelease, *reverse;
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
