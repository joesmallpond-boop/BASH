#pragma once
// BASH! core — a mono acid voice (saw/square into a resonant 4-pole low-pass,
// accent, slide). Milestone 1 uses it as every track's sound so patterns are
// playable straight away; it becomes one of the machine-synth machines later.

namespace bash
{
    class AcidVoice
    {
    public:
        struct Params
        {
            float wave      = 0.0f;   // 0 = saw, 1 = square (blend in between)
            float cutoff    = 400.0f; // Hz
            float resonance = 0.7f;   // 0..1
            float envMod    = 0.6f;   // 0..1
            float decay     = 0.35f;  // seconds
            float accent    = 0.6f;   // 0..1, how hard accented notes hit
            float drive     = 0.3f;   // 0..1
            float glide     = 0.06f;  // seconds
        };

        void prepare (double sampleRate);
        void reset();

        void noteOn (int note, int velocity, bool accent, bool legato);
        void noteOff (int note);
        void allNotesOff() { gate = false; }

        /** Adds n samples of mono output to out[start..start+n). */
        void render (float* out, int start, int n, const Params&);

        bool isSilent() const noexcept { return ! gate && amp < 1.0e-4f; }

    private:
        double sr = 44100.0;
        int    note = -1;
        bool   gate = false;
        bool   isAccent = false;
        float  velocityGain = 0.8f;

        double phase = 0.0;
        float  freq = 110.0f, targetFreq = 110.0f;
        float  amp = 0.0f, filterEnv = 0.0f, accentEnv = 0.0f;

        // two cascaded TPT state-variable filters (24 dB/oct)
        float ic1a = 0, ic2a = 0, ic1b = 0, ic2b = 0;
        float a1a = 0, a2a = 0, a3a = 0, a1b = 0, a2b = 0, a3b = 0;
        int   coeffCountdown = 0;
    };
}
