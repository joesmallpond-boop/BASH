#include "bash/Scale.h"

#include <algorithm>

namespace bash
{
    const std::vector<ScaleDef>& allScales()
    {
        static const std::vector<ScaleDef> scales {
            { "Minor",            { 0, 2, 3, 5, 7, 8, 10 } },
            { "Major",            { 0, 2, 4, 5, 7, 9, 11 } },
            { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian",         { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian",           { 0, 2, 4, 6, 7, 9, 11 } },
            { "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 } },
            { "Locrian",          { 0, 1, 3, 5, 6, 8, 10 } },
            { "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 } },
            { "Melodic Minor",    { 0, 2, 3, 5, 7, 9, 11 } },
            { "Phrygian Dominant",{ 0, 1, 4, 5, 7, 8, 10 } },
            { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
            { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
            { "Blues",            { 0, 3, 5, 6, 7, 10 } },
            { "Whole Tone",       { 0, 2, 4, 6, 8, 10 } },
            { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        };
        return scales;
    }

    static int floorDiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    static int posMod (int a, int b)   { int m = a % b; return m < 0 ? m + b : m; }

    Scale::Scale (int r, int s)
    {
        const auto& defs = allScales();
        scaleIdx = std::clamp (s, 0, (int) defs.size() - 1);
        rootNote = posMod (r, 12);
        steps = defs[(size_t) scaleIdx].intervals;
        member.fill (false);
        for (int i : steps)
            member[(size_t) posMod (rootNote + i, 12)] = true;
    }

    int Scale::degreeToSemitone (int degree) const noexcept
    {
        const int n = size();
        return floorDiv (degree, n) * 12 + steps[(size_t) posMod (degree, n)];
    }

    bool Scale::contains (int midiNote) const noexcept
    {
        return member[(size_t) posMod (midiNote, 12)];
    }

    int Scale::quantize (int midiNote) const noexcept
    {
        if (contains (midiNote))
            return midiNote;
        for (int d = 1; d < 12; ++d)
        {
            if (contains (midiNote - d)) return midiNote - d;
            if (contains (midiNote + d)) return midiNote + d;
        }
        return midiNote;
    }

    const char* noteName (int pc)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[posMod (pc, 12)];
    }
}
