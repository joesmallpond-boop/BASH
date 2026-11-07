#pragma once
// BASH! core — the engine: one channel. A sequencer (generative acid or step) drives one
// voice (synth, slicer or drums), with two LFOs + Life + MIDI expression modulating it,
// on a clock that follows the host (Ableton) or runs on its own. Pure C++, no framework,
// so it runs the same inside the Max for Live device and in the unit tests.

#include "bash/Drums.h"
#include "bash/Modulation.h"
#include "bash/MachineSynth.h"
#include "bash/Scale.h"
#include "bash/Sequencer.h"
#include "bash/Slices.h"
#include "bash/StepSeq.h"

#include <array>
#include <atomic>
#include <vector>

namespace bash
{
    enum class EngineType : int { Synth = 0, Slicer, Drums, count };
    const char* engineTypeName (EngineType);

    /** Step rates: steps per quarter note. */
    struct Rate { const char* name; double stepsPerQuarter; };
    const std::vector<Rate>& allRates();   // index 1 = 1/16 (default)

    struct SlicerSettings
    {
        bool  pitchFollow = false;   // sequencer pitch repitches slices
        float tune        = 0.0f;    // semitones
        SliceVoice::Params voice;
    };

    struct Settings
    {
        // the voice
        EngineType        engine  = EngineType::Synth;
        MachineVoice::Params voice;
        SlicerSettings    slicer;
        float             level = 0.8f;
        float             pan   = 0.0f;   // -1..1

        // the sequencer
        bool              sequencerOn = true;
        SequencerParams   seq;
        int               rateIndex = 1;
        bool              stepMode = false;   // step sequencer instead of the generative one
        bool              fill = false;       // Fill latched
        int               autoFillBars = 0;   // 0 off, else the last bar of every N is a fill
        bool              midiThru = true;    // notes in play the voice (except into the Arpeggiator)

        // key and clock
        int    root       = 0;     // pitch class of the key
        int    scaleIndex = 0;     // into allScales()
        double tempo      = 128.0; // internal clock BPM (no host)
        bool   hostSync   = true;  // follow the host's tempo and position
        bool   playing    = false; // the device's own run/stop (while the host is stopped)

        // modulation
        std::array<LfoParams, 2> lfo {};
        float lifeRate = 0.08f;    // Hz, Life drift speed
    };

    struct Transport
    {
        bool   hostAvailable = false;
        bool   hostPlaying   = false;
        double hostPpq       = 0.0;
        bool   hostPpqValid  = true;    // false: the host gives a tempo but no position
        double hostBpm       = 120.0;
    };

    /** How the step lanes map to notes / pads / slices. */
    StepContext stepContextFor (EngineType, const SequencerParams&, const Scale&, double stepsPerQuarter,
                                bool fill, int autoFillBars);

    class Engine
    {
    public:
        Engine();

        void prepare (double sampleRate, int maxBlockSize);

        /** MIDI in, from any thread: queued and played at the start of the next block.
            Held notes feed the Arpeggiator; with midiThru they also play the voice. velocity 0 = off. */
        void queueNote (int note, int velocity);
        void queueAllNotesOff();
        /** MIDI expression in: Velocity/Aftertouch/ModWheel 0..1, PitchBend -1..1. */
        void midiExpression (ModSource s, float v) { expression[(size_t) s] = v; }

        /** Writes (overwrites) stereo output. Audio thread. */
        void process (float* left, float* right, int numSamples, const Transport&);

        Settings& settings()                 { return set; }
        const Settings& settings() const     { return set; }
        Pattern&  pattern()                  { return pat; }

        /** Step pattern: lock stepMutex() while reading or editing. */
        StepPattern& stepPattern()           { return stepStore.pattern; }
        SpinMutex&   stepMutex()             { return stepStore.m; }
        int  stepLanePosition (int lane) const { return lanePos[(size_t) lane].load(); }

        /** Steps of the generative pattern for the UI (pattern lock taken inside). */
        std::vector<StepData> snapshot (const SequencerParams&, const Scale&, uint32_t first, uint32_t count);
        void lockStep (const SequencerParams&, const Scale&, uint32_t index, uint8_t mask = lockbit::all);
        uint32_t playIndex() const           { return playIdx.load(); }
        bool     isRunning() const           { return running.load(); }
        double   currentBpm() const          { return bpmNow.load(); }
        double   currentBeat() const         { return beatNow.load(); }

        // ---- drum kit (message thread) ----
        void     setCell (int index, const DrumCell&);
        DrumCell cell (int index);
        void     audition (int index)        { auditionRequest.store (index); }

        // ---- slicer (message thread) ----
        /** The caller owns the sample; keep a replaced one alive until blocksProcessed()
            has advanced by 2. nullptr clears it. */
        void setSample (const SampleData* s) { sampleNow.store (s); }
        const SampleData* sample() const     { return sampleNow.load(); }
        void setSlices (std::vector<uint32_t>);
        std::vector<uint32_t> slices();
        uint32_t sliceCount();
        uint64_t blocksProcessed() const     { return blockCount.load(); }
        void     auditionSlice (int index)   { sliceAudition.store (index); }

        // ---- modulation ----
        ModMatrix& matrix()                  { return mods; }
        float sourceValue (ModSource s) const { return sourceUi[(size_t) s].load (std::memory_order_relaxed); }
        double lfoPhase (int i) const        { return lfoPhaseUi[(size_t) i].load(); }

        // ---- visual feedback (any thread) ----
        int    playingSlice() const          { return sliceNow.load(); }
        double playingFrame() const          { return framePos.load(); }
        /** Counts up every time pad / lane `i` fires (the UI flashes on a change). */
        uint32_t hitCount (int i) const      { return hits[(size_t) i].load(); }
        int    lastNote() const              { return lastNoteUi.load(); }
        uint32_t noteCount() const           { return noteCounter.load(); }
        float  outputPeak() const            { return peak.load(); }

    private:
        void refreshScale();
        void renderChunk (float* l, float* r, int n, bool run, double ppq, double bpm);
        void handleEvent (const NoteEvent&);
        void applyModulation();

        double sr = 44100.0;
        int    maxBlock = 128;

        Settings set;
        Settings live;               // modulated copy used while rendering a chunk
        Pattern pat;
        TrackPlayer player;
        struct StepStore { SpinMutex m; StepPattern pattern; } stepStore;
        StepPlayer stepPlayer;
        std::array<std::atomic<int>, stepLanes> lanePos {};

        MachineSynth synth;
        SliceVoice   slicer;
        DrumPlayer   drums;
        std::array<DrumCell, numCells> kit;
        SpinMutex kitMutex;
        std::array<std::atomic<const SampleData*>, numCells> retiredCellSamples {};
        std::atomic<int> auditionRequest { -1 }, sliceAudition { -1 };
        DrumPlayer auditionPlayer;
        SliceVoice sliceAuditioner;

        std::atomic<const SampleData*> sampleNow { nullptr };
        const SampleData* lastSample = nullptr;
        struct SliceStore { SpinMutex m; std::vector<uint32_t> points; } sliceStore;
        int slicerNote = -1;

        std::atomic<uint32_t> playIdx { 0 };
        std::atomic<int>      sliceNow { -1 };
        std::atomic<double>   framePos { -1.0 };
        std::atomic<uint64_t> blockCount { 0 };
        std::atomic<bool>     running { false };
        std::atomic<double>   bpmNow { 120.0 }, beatNow { 0.0 };

        Scale  currentScale;
        int    scaleRoot = -1, scaleIdx = -1;
        double internalPpq = 0.0;
        bool   wasRunning = false;

        // MIDI in
        struct InNote { int note, velocity; };
        SpinMutex inMutex;
        std::array<InNote, 128> inQueue {};
        int inCount = 0;
        bool inAllOff = false;
        std::array<int, 16> heldIn {};
        int heldInCount = 0;
        void heldNote (int note, int velocity);

        std::vector<float> bufL, bufR, bufMono;

        ModMatrix mods;
        std::array<Lfo, 2> lfos;
        Life life;
        std::array<float, numModSources> expression {};
        std::array<std::atomic<float>, numModSources> sourceUi {};
        std::array<std::atomic<double>, 2> lfoPhaseUi {};
        std::vector<NoteEvent> events;

        std::array<std::atomic<uint32_t>, numCells> hits {};
        std::atomic<int> lastNoteUi { -1 };
        std::atomic<uint32_t> noteCounter { 0 };
        std::atomic<float> peak { 0.0f };
    };
}
