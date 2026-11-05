#pragma once
// BASH! core — the engine: 8 tracks, each a generative sequencer driving a voice,
// a shared clock (host sync or internal), and the global key/scale.
// Pure C++ with no JUCE dependency, so it runs the same in every plugin format,
// on iPad, and in unit tests.

#include "bash/AcidVoice.h"
#include "bash/Scale.h"
#include "bash/Sequencer.h"

#include <array>
#include <atomic>
#include <vector>

namespace bash
{
    constexpr int numTracks = 8;

    /** Step rates: steps per quarter note. */
    struct Rate { const char* name; double stepsPerQuarter; };
    const std::vector<Rate>& allRates();   // index 1 = 1/16 (default)

    struct TrackSettings
    {
        bool              enabled = false;
        SequencerParams   seq;
        int               rateIndex = 1;
        AcidVoice::Params voice;
        float             level = 0.8f;
        float             pan   = 0.0f;   // -1..1
    };

    struct GlobalSettings
    {
        int    root        = 0;     // pitch class of the key
        int    scaleIndex  = 0;     // into allScales()
        double tempo       = 128.0; // internal clock BPM
        bool   hostSync    = true;  // follow the DAW when it provides a transport
        bool   playing     = false; // internal clock run/stop
        float  master      = 0.8f;
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
        int  channel;     // 1..16
        bool noteOn;
        int  note;
        int  velocity;
    };

    class Engine
    {
    public:
        Engine();

        void prepare (double sampleRate, int maxBlockSize);

        /** Writes (overwrites) stereo output and appends generated MIDI. */
        void process (float* left, float* right, int numSamples,
                      const Transport&, std::vector<MidiOut>& midiOut);

        TrackSettings&  track (int t)        { return tracks[(size_t) t]; }
        GlobalSettings& global()             { return globals; }
        Pattern&        pattern (int t)      { return patterns[(size_t) t]; }

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

    private:
        void refreshScale();
        void renderChunk (float* l, float* r, int n, int offsetInBlock, bool run,
                          double ppq, double bpm, std::vector<MidiOut>& midiOut);

        double sr = 44100.0;
        int    maxBlock = 512;

        std::array<TrackSettings, numTracks> tracks;
        std::array<Pattern,       numTracks> patterns;
        std::array<TrackPlayer,   numTracks> players;
        std::array<AcidVoice,     numTracks> voices;
        std::array<std::atomic<uint32_t>, numTracks> playIdx {};
        std::atomic<bool> running { false };

        GlobalSettings globals;
        Scale  currentScale;
        int    scaleRoot = -1, scaleIdx = -1;
        double internalPpq = 0.0;
        bool   wasRunning = false;

        std::vector<float>     mono;
        std::vector<NoteEvent> events;
    };
}
