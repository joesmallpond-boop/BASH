#pragma once
// BASH! core — the drum kit: 16 cells (4x4), each a sample with an editor
// (start, end, fades, transient shaping, tune, filter) or an FM drum voice.

#include "bash/Sequencer.h"
#include "bash/Slices.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bash
{
    constexpr int numCells = 16;
    constexpr int gridSize = 4;    // 4x4
    constexpr int drumBaseNote = 36;          // cell 0 = MIDI note 36 (C1), GM-style

    struct FmDrumParams
    {
        float pitch      = 55.0f;   // Hz
        float pitchAmt   = 24.0f;   // semitones of pitch sweep
        float pitchTime  = 0.04f;   // s
        float ratio      = 1.0f;    // modulator ratio
        float fmAmt      = 0.0f;    // modulation index
        float fmDecay    = 0.05f;   // s
        float noise      = 0.0f;    // noise level 0..1
        float tone       = 0.5f;    // noise filter 0 (dark) .. 1 (bright)
        float noiseDecay = 0.08f;   // s
        int   bursts     = 1;       // 1 = normal, 2-4 = clap-style retriggers
        float decay      = 0.4f;    // body amp decay, s
        float click      = 0.3f;    // transient click 0..1
        float drive      = 0.2f;    // 0..1
    };

    struct DrumCell
    {
        enum class Kind : int { Empty = 0, Sample, Fm };
        Kind kind = Kind::Empty;   // (cell names live in the plugin: no strings on the audio thread)

        // sample cells
        const SampleData* sample = nullptr;
        float start = 0.0f, end = 1.0f;        // 0..1 of the file
        float fadeIn = 0.0f, fadeOut = 0.005f; // s
        float attack = 0.0f, sustain = 0.0f;   // transient shaper, -1..1
        float tune = 0.0f;                     // semitones
        float cutoff = 20000.0f;               // low-pass Hz
        bool  reverse = false;

        // FM cells
        FmDrumParams fm;

        // both
        float gain = 1.0f;    // linear
        float pan  = 0.0f;    // -1..1
        int   choke = 0;      // 0 = none, 1..8 groups
    };

    class DrumVoice
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; active = false; }
        void start (const DrumCell&, int cellIndex, float velocityGain, uint64_t order);
        void choke() { if (active) choking = true; }
        void stop()  { active = false; }
        void render (float* l, float* r, int start, int n);

        bool isActive() const noexcept        { return active; }
        int  cellIndex() const noexcept       { return cell; }
        int  chokeGroup() const noexcept      { return params.choke; }
        uint64_t order() const noexcept       { return startOrder; }
        const SampleData* sample() const      { return params.kind == DrumCell::Kind::Sample ? params.sample : nullptr; }

    private:
        float renderSample();
        float renderFm();

        double sr = 44100.0;
        DrumCell params;
        bool   active = false, choking = false;
        int    cell = -1;
        uint64_t startOrder = 0;
        float  vel = 1.0f, chokeGain = 1.0f;
        double t = 0.0;              // seconds since start
        // sample playback
        double pos = 0.0, inc = 1.0, lo = 0.0, hi = 0.0;
        float  lpState[2] {}, lpCoef = 1.0f;
        float  lastR = 0.0f;
        // fm
        double carPhase = 0.0, modPhase = 0.0;
        uint32_t noise = 0x9E3779B9u;
        float  nIc1 = 0.0f, nIc2 = 0.0f;
    };

    /** Polyphonic drum player for one track (8 voices, choke groups). */
    class DrumPlayer
    {
    public:
        static constexpr int voicesPerTrack = 8;
        void prepare (double sampleRate)   { for (auto& v : voices) v.prepare (sampleRate); }
        void trigger (const DrumCell&, int cellIndex, int velocity, bool accent);
        void render (float* l, float* r, int start, int n) { for (auto& v : voices) if (v.isActive()) v.render (l, r, start, n); }
        void stopVoicesUsing (const SampleData*);
        void stopAll()                     { for (auto& v : voices) v.stop(); }
        bool isSilent() const noexcept     { for (auto& v : voices) if (v.isActive()) return false; return true; }

    private:
        std::array<DrumVoice, voicesPerTrack> voices;
        uint64_t counter = 0;
    };

    // ---- factory sounds -----------------------------------------------------
    struct FmPreset { const char* name; FmDrumParams p; float gain; int choke; };
    const std::vector<FmPreset>& fmPresets();

    struct FactoryKit { const char* name; std::array<int, numCells> cells; };   // preset index per cell (-1 empty)
    const std::vector<FactoryKit>& factoryKits();

    DrumCell makeFmCell (const FmPreset&);

    /** Templates for the FM drum editor's starting-point buttons. */
    struct FmStartPoint { const char* name; int preset; };
    const std::vector<FmStartPoint>& fmStartPoints();
}
