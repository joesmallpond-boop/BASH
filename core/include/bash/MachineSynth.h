#pragma once
// BASH! core — the machine synth. Pick a "machine" (oscillator model), shape it
// with a shared 24 dB multimode filter, envelopes, drive, accent and glide.
// Up to 8 voices for chords (Chords / Counterpoint, step lanes, the poly machines);
// mono with legato glide for acid lines.

#include <array>
#include <cstdint>

namespace bash
{
    enum class Machine : int
    {
        Acid = 0,       // saw <-> square, the 303 voice
        Sid,            // chip pulse: width + bit-crush
        FmPlus,         // two-operator FM with feedback
        SuperWave,      // seven detuned saws
        Vo,             // vowel formants
        PulseInterval,  // two pulses at an interval
        Noise,          // resonant pitched noise
        // poly voices (always polyphonic: chords, pads, keys)
        PolySaw,        // two detuned saws + a sub: the classic poly synth
        Pad,            // drifting saws and a moving pulse, slow to swell and fade
        Keys,           // FM electric piano: tine and bark
        Organ,          // drawbar organ with a rotary wobble
        count
    };
    inline bool isPolyMachine (Machine m) { return m == Machine::PolySaw || m == Machine::Pad || m == Machine::Keys || m == Machine::Organ; }
    const char* machineName (Machine);
    const char* machineCharacterName (Machine);   // what the Character knob does
    const char* machineTimbreName (Machine);      // what the Timbre knob does

    enum class FilterType : int { LowPass = 0, BandPass, HighPass, count };
    const char* filterTypeName (FilterType);

    class MachineVoice
    {
    public:
        struct Params
        {
            Machine    machine    = Machine::Acid;
            float      character  = 0.0f;    // 0..1, per-machine (see machineCharacterName)
            float      timbre     = 0.5f;    // 0..1, per-machine
            FilterType filterType = FilterType::LowPass;
            float      cutoff     = 400.0f;  // Hz
            float      resonance  = 0.7f;    // 0..1
            float      envMod     = 0.6f;    // 0..1
            float      decay      = 0.35f;   // filter env decay, s
            float      accent     = 0.6f;    // 0..1
            float      drive      = 0.3f;    // 0..1
            float      glide      = 0.06f;   // s
            float      attack     = 0.001f;  // amp env, s
            float      ampDecay   = 0.5f;    // s
            float      sustain    = 1.0f;    // 0..1
            float      release    = 0.01f;   // s
        };

        void prepare (double sampleRate);
        void reset();

        void noteOn (int note, int velocity, bool accent, bool legato);
        void noteOff (int note);
        void release()                     { gate = false; }
        bool isSilent() const noexcept     { return ! gate && amp < 1.0e-4f && ampStage == Stage::Idle; }
        int  currentNote() const noexcept  { return note; }
        bool isGated() const noexcept      { return gate; }
        uint64_t age() const noexcept      { return startedAt; }

        /** Adds n samples of mono output to out[start..). */
        void render (float* out, int start, int n, const Params&);

    private:
        float oscillator (const Params&, double dt);
        void  updateFilter (const Params&, float fc);

        enum class Stage { Idle, Attack, Decay, Sustain, Release };

        double sr = 44100.0;
        int    note = -1;
        bool   gate = false, isAccent = false;
        float  velocityGain = 0.8f;
        uint64_t startedAt = 0;
        static inline uint64_t counter = 0;

        float  freq = 110.0f, targetFreq = 110.0f;
        std::array<double, 7> phase {};
        double modPhase = 0.0, lfoPhase = 0.0;
        float  fmFeedback = 0.0f;
        float  modEnv = 0.0f, keyEnv = 0.0f;   // Keys: tine brightness, natural decay
        float  drift = 0.0f;                   // Pad / PolySaw: a little per-voice pitch drift
        uint32_t noiseState = 0x12345678u;

        Stage  ampStage = Stage::Idle;
        float  amp = 0.0f, filterEnv = 0.0f, accentEnv = 0.0f;

        // main filter: two cascaded TPT SVFs
        float ic1a = 0, ic2a = 0, ic1b = 0, ic2b = 0;
        float g = 0, kA = 1, kB = 1.414f;
        int   coeffCountdown = 0;

        // VO formant bank
        struct Bp { float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 0.1f; };
        std::array<Bp, 3> formants {};
        float formantPos = -1.0f, formantShift = -1.0f;

        // Noise machine pitched band-pass
        Bp noiseBp;
        float noiseLp = 0.0f;
    };

    class MachineSynth
    {
    public:
        static constexpr int maxVoices = 8;

        void prepare (double sampleRate)   { for (auto& v : voices) v.prepare (sampleRate); }
        void reset()                       { for (auto& v : voices) v.reset(); }
        void setPolyphonic (bool p)        { if (p != poly) { allNotesOff(); poly = p; } }

        void noteOn (int note, int velocity, bool accent, bool legato);
        void noteOff (int note);
        void allNotesOff()                 { for (auto& v : voices) v.release(); }
        bool isSilent() const noexcept;

        void render (float* out, int start, int n, const MachineVoice::Params&);

    private:
        std::array<MachineVoice, maxVoices> voices;
        bool poly = false;
    };
}
