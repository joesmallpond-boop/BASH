#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "bash/Engine.h"

#include <array>

class BashProcessor : public juce::AudioProcessor
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

    std::vector<bash::StepData> snapshot (int track, uint32_t first, uint32_t count);
    uint32_t playIndex (int track) const { return engine.playIndex (track); }
    bool     isRunning() const           { return engine.isRunning(); }

    void regenerate (int track);
    void toggleLock (int track, uint32_t index);
    void toggleMute (int track, uint32_t index);
    void clearLocks (int track);
    uint8_t lockMask (int track, uint32_t index);

    static const juce::StringArray& cycleNames();
    static uint32_t cycleMultiplier (int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParameters();

    struct TrackParams
    {
        std::atomic<float> *on, *algo, *type, *density, *steps, *cycle, *offset, *direction, *rate,
                           *accent, *gate, *octave, *scaleLock,
                           *wave, *cutoff, *reso, *envMod, *decay, *drive, *glide, *level, *pan;
    };
    std::array<TrackParams, bash::numTracks> tp {};
    std::atomic<float> *pRoot = nullptr, *pScale = nullptr, *pTempo = nullptr, *pSync = nullptr,
                       *pPlay = nullptr, *pMaster = nullptr;

    bash::Engine engine;
    std::vector<bash::MidiOut> midiOut;
    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BashProcessor)
};
