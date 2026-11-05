#pragma once
// BASH! core — keys and scales.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bash
{
    struct ScaleDef
    {
        const char* name;
        std::vector<int> intervals; // semitones above the root, ascending, starting at 0
    };

    const std::vector<ScaleDef>& allScales();

    class Scale
    {
    public:
        Scale() : Scale (0, 0) {}
        Scale (int rootNote, int scaleIndex);

        int root() const noexcept           { return rootNote; }
        int index() const noexcept          { return scaleIdx; }
        int size() const noexcept           { return (int) steps.size(); }
        const std::vector<int>& intervals() const noexcept { return steps; }

        /** Semitone offset from the root for a scale degree (negative and >size wrap by octave). */
        int degreeToSemitone (int degree) const noexcept;

        /** True if the MIDI note belongs to the scale. */
        bool contains (int midiNote) const noexcept;

        /** Nearest in-scale MIDI note (ties resolve downward). */
        int quantize (int midiNote) const noexcept;

        /** The in-scale note `degrees` scale steps above (or below) midiNote. */
        int stepFrom (int midiNote, int degrees) const noexcept;

    private:
        int rootNote = 0, scaleIdx = 0;
        std::vector<int> steps;
        std::array<bool, 12> member {};
    };

    const char* noteName (int pitchClass);
}
