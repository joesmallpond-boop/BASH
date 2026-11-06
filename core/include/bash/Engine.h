#pragma once
// BASH! core — the engine: 8 tracks, each a generative sequencer driving one
// sound engine (synth, slicer, drums), a shared clock (host sync or internal),
// and the global key/scale. Pure C++ with no JUCE dependency, so it runs the
// same in every plugin format, on iPad, and in unit tests.

#include "bash/Drums.h"
#include "bash/Fx.h"
#include "bash/Modulation.h"
#include "bash/MachineSynth.h"
#include "bash/Micro.h"
#include "bash/Scale.h"
#include "bash/Sequencer.h"
#include "bash/Slices.h"
#include "bash/StepSeq.h"

#include <array>
#include <atomic>
#include <vector>

namespace bash
{
    constexpr int numTracks = 8;

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

    struct TrackSettings
    {
        bool              enabled = false;
        EngineType        engine  = EngineType::Synth;
        SequencerParams   seq;
        int               rateIndex = 1;
        MachineVoice::Params voice;
        SlicerSettings    slicer;
        float             level = 0.8f;
        float             pan   = 0.0f;   // -1..1

        // mixer
        std::array<FxParams, 2> inserts {};
        float sendA = 0.0f, sendB = 0.0f; // post-fader sends to the two return buses
        bool  mute = false, solo = false;
        float lifeRate = 0.08f;           // Hz, the track's Life drift speed

        // step sequencer (instead of the generative one)
        bool  stepMode = false;
        bool  fill = false;               // Fill latched
        int   autoFillBars = 0;           // 0 off, else the last bar of every N is a fill
    };

    struct GlobalSettings
    {
        int    root        = 0;     // pitch class of the key
        int    scaleIndex  = 0;     // into allScales()
        double tempo       = 128.0; // internal clock BPM
        bool   hostSync    = true;  // follow the DAW when it provides a transport
        bool   playing     = false; // internal clock run/stop
        float  master      = 0.8f;

        std::array<FxParams, 2> returns {};          // return A / B effect
        std::array<float, 2>    returnLevel { 0.8f, 0.8f };
        MicroParams   micro {};           // MICRO: everything from the tracks + returns, before the master
        GlueParams    glue {};            // master bus compressor (before the limiter)
        LimiterParams limiter {};
        std::array<LfoParams, 2> lfo {};
    };

    struct Transport
    {
        bool   hostAvailable = false;
        bool   hostPlaying   = false;
        double hostPpq       = 0.0;
        double hostBpm       = 120.0;
    };

    struct MidiOut
    {
        int  sampleOffset;
        int  channel;     // 1..16 (track n on channel n; Counterpoint bass on n + 8)
        bool noteOn;
        int  note;
        int  velocity;
    };

    struct RenderOptions
    {
        int    track = -1;          // -1 = the whole mix (master bus), else one track
        int    bars = 4;
        double bpm = 120.0;
        double sampleRate = 48000.0;
        bool   withFx = true;       // track: its inserts + its sends through the returns. mix: everything
        double tailSeconds = 4.0;   // rendered past the end, then wrapped to the start for seamless loops
    };

    /** How a track's step lanes map to notes / pads / slices (used by playback and export). */
    StepContext stepContextFor (EngineType, const SequencerParams&, const Scale&, double stepsPerQuarter,
                                bool fill, int autoFillBars);

    class Engine
    {
    public:
        Engine();

        /** Copy everything that defines the sound (settings, patterns, slices, samples, kit,
            modulation) from another engine. Message thread; sample data is shared, so the
            caller keeps it alive for as long as this engine renders. */
        void copyStateFrom (Engine& other);

        /** Render bars of audio offline from a copy of `live`. Message thread. */
        static void renderOffline (Engine& live, const RenderOptions&, std::vector<float>& left, std::vector<float>& right);

        void prepare (double sampleRate, int maxBlockSize);

        /** Live MIDI input (audio thread, before process): feeds the arpeggiator. velocity 0 = note off. */
        void midiNoteIn (int note, int velocity);
        void allInputNotesOff()              { heldInCount = 0; }
        int  heldInputCount() const          { return heldInCount; }

        /** Writes (overwrites) stereo output and appends generated MIDI. */
        void process (float* left, float* right, int numSamples,
                      const Transport&, std::vector<MidiOut>& midiOut);

        TrackSettings&  track (int t)        { return tracks[(size_t) t]; }
        GlobalSettings& global()             { return globals; }
        Pattern&        pattern (int t)      { return patterns[(size_t) t]; }
        /** Master gain reduction in dB (glue compressor, limiter), for the meters. */
        float glueReduction() const noexcept    { return glueMeter.load(); }
        float limiterReduction() const noexcept { return limiterMeter.load(); }
        MicroEngine& micro() noexcept { return microFx; }
        const MicroEngine& micro() const noexcept { return microFx; }

        /** Step sequencer pattern for a track: lock stepMutex(t) while reading or editing. */
        StepPattern&    stepPattern (int t)  { return stepStores[(size_t) t].pattern; }
        SpinMutex&      stepMutex (int t)    { return stepStores[(size_t) t].m; }
        /** Step each lane is on (UI playhead), -1 when stopped. */
        int  stepLanePosition (int t, int lane) const { return lanePos[(size_t) t][(size_t) lane].load(); }

        /** Steps for the UI, computed under the pattern lock. The UI passes its own
            copy of the params and scale so it never reads audio-thread state. */
        std::vector<StepData> snapshot (int t, const SequencerParams&, const Scale&,
                                        uint32_t first, uint32_t count);

        /** Last pattern index each track started, and whether the clock is running. */
        uint32_t playIndex (int t) const     { return playIdx[(size_t) t].load(); }
        bool     isRunning() const           { return running.load(); }

        /** Lock everything about a step at its currently playing values. */
        void lockStep (int t, const SequencerParams&, const Scale&, uint32_t index,
                       uint8_t mask = lockbit::all);

        // ---- drum kit (message thread) ----
        void     setCell (int index, const DrumCell&);
        DrumCell cell (int index);
        /** Plays a cell once through the master output (UI preview). */
        void     audition (int index)          { auditionRequest.store (index); }

        // ---- slicer (message thread) ----
        /** The caller owns the sample. After replacing one, keep the old one alive
            until blocksProcessed() has advanced by 2 (the audio thread drops it at
            the start of its next block). nullptr clears the track. */
        void setSample (int t, const SampleData* s)  { samples[(size_t) t].store (s); }
        const SampleData* sample (int t) const       { return samples[(size_t) t].load(); }
        void setSlices (int t, std::vector<uint32_t> slices);
        std::vector<uint32_t> slices (int t);
        uint32_t sliceCount (int t);
        uint64_t blocksProcessed() const         { return blockCount.load(); }

        // ---- mixer ----
        /** Optional per-track outputs (audio thread, before process): 2 pointers per track,
            nullptr = mix into the main output. Buffers must hold the whole block and be zeroed. */
        void setDirectOutput (int t, float* left, float* right) { directOuts[(size_t) (2 * t)] = left; directOuts[(size_t) (2 * t + 1)] = right; }
        float trackLevelMeter (int t) const      { return trackPeak[(size_t) t].load(); }

        // ---- modulation ----
        ModMatrix& matrix (int t)                { return matrices[(size_t) t]; }
        /** MIDI expression in (audio thread): Velocity/Aftertouch/ModWheel 0..1, PitchBend -1..1. */
        void  midiExpression (ModSource s, float v) { expression[(size_t) s] = v; }
        /** Current value of a source for track t (UI display), -1..1. */
        float sourceValue (int t, ModSource s) const;
        double lfoPhase (int i) const            { return lfoPhaseUi[(size_t) i].load(); }
        /** Slice currently playing on a slicer track (-1 if none) and its position in frames. */
        int    playingSlice (int t) const        { return sliceNow[(size_t) t].load(); }
        double playingFrame (int t) const        { return framePos[(size_t) t].load(); }

    private:
        void refreshScale();
        void renderChunk (float* l, float* r, int n, int offsetInBlock, bool run,
                          double ppq, double bpm, std::vector<MidiOut>& midiOut);
        void handleEvent (int t, const NoteEvent&);
        void mixTrack (int t, float* l, float* r, int n, int offsetInBlock, double bpm);
        void applyModulation (int t);

        struct SliceStore
        {
            SpinMutex m;
            std::vector<uint32_t> points;
        };

        double sr = 44100.0;
        int    maxBlock = 512;

        std::array<TrackSettings, numTracks> tracks;
        std::array<Pattern,       numTracks> patterns;
        std::array<TrackPlayer,   numTracks> players;
        struct StepStore { SpinMutex m; StepPattern pattern; };
        std::array<StepStore,     numTracks> stepStores;
        std::array<StepPlayer,    numTracks> stepPlayers;
        std::array<std::array<std::atomic<int>, stepLanes>, numTracks> lanePos {};
        std::array<MachineSynth,  numTracks> synths;
        std::array<SliceVoice,    numTracks> slicers;
        std::array<SliceStore,    numTracks> sliceStores;
        std::array<DrumPlayer,    numTracks> drums;
        std::array<DrumCell,      numCells>  kit;
        SpinMutex kitMutex;
        std::array<std::atomic<const SampleData*>, numCells> retiredCellSamples {};
        std::atomic<int> auditionRequest { -1 };
        DrumPlayer auditionPlayer;
        std::array<std::atomic<const SampleData*>, numTracks> samples {};
        std::array<const SampleData*, numTracks> lastSample {};
        std::array<int, numTracks> slicerNote {};
        std::array<std::atomic<uint32_t>, numTracks> playIdx {};
        std::array<std::atomic<int>,      numTracks> sliceNow {};
        std::array<std::atomic<double>,   numTracks> framePos {};
        std::atomic<uint64_t> blockCount { 0 };
        std::atomic<bool> running { false };

        GlobalSettings globals;
        Scale  currentScale;
        int    scaleRoot = -1, scaleIdx = -1;
        double internalPpq = 0.0;
        bool   wasRunning = false;

        std::array<int, 16> heldIn {};
        int heldInCount = 0;

        std::vector<float>     bufL, bufR, bufMono;   // bufMono: the mono synth, added to both sides
        std::array<std::vector<float>, 2> retL, retR;
        std::array<std::array<FxSlot, 2>, numTracks> trackFx;
        std::array<FxSlot, 2> returnFx;
        MicroEngine microFx;
        GlueCompressor glue;
        Limiter limiter;
        std::atomic<float> glueMeter { 0.0f }, limiterMeter { 0.0f };
        bool anySolo = false;
        std::array<float*, 2 * numTracks> directOuts {};
        std::array<std::atomic<float>, numTracks> trackPeak {};

        std::array<TrackSettings, numTracks> live;   // modulated copy used while rendering a chunk
        std::array<ModMatrix, numTracks> matrices;
        std::array<Lfo, 2> lfos;
        std::array<Life, numTracks> lifes;
        std::array<float, numModSources> expression {};
        std::array<std::atomic<float>, numModSources * numTracks> sourceUi {};
        std::array<std::atomic<double>, 2> lfoPhaseUi {};
        std::vector<NoteEvent> events;
    };
}
