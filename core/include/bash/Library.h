#pragma once
// BASH! core — the factory library. Every sound is synthesised and processed here
// (nothing sampled from anyone else's records), voiced for UK bass, drum & bass,
// dubstep, UK garage, broken beat, techno and wonky / IDM beat-making.
//
// The plugin renders these once into a library folder on disk (so they're ordinary
// WAVs you can also drag into a DAW), then kits and the slicer load them from there.

#include "bash/Slices.h"
#include "bash/StepSeq.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bash
{
    namespace library
    {
        /** Bump when any sound changes, so installed libraries re-render. */
        constexpr int version = 1;
        constexpr double renderRate = 44100.0;

        struct OneShot
        {
            std::string category;   // folder under Drums/, e.g. "Kicks"
            std::string name;       // file name without extension
            std::function<std::shared_ptr<SampleData>()> render;
        };
        /** All drum one-shots, in browsing order. */
        const std::vector<OneShot>& oneShots();

        struct Loop
        {
            std::string category;   // folder under Loops/, e.g. "Breaks"
            std::string name;       // file name without extension (ends with the tempo)
            double bpm;
            int bars;
            std::function<std::shared_ptr<SampleData>()> render;
        };
        /** All slicer loops, in browsing order. */
        const std::vector<Loop>& loops();

        /** "Drums/Kicks/Sub Kick F" style relative path (no extension) for a one-shot. */
        std::string oneShotPath (const OneShot&);
        std::string loopPath (const Loop&);
        /** Find a one-shot by "Category/Name". */
        const OneShot* findOneShot (const std::string& categoryAndName);

        struct KitPad
        {
            std::string sound;   // "Category/Name" of a one-shot; empty = empty pad
            float tune = 0.0f;   // semitones
            float gainDb = 0.0f;
            float pan = 0.0f;
            int   choke = 0;
        };
        struct Kit
        {
            std::string name;
            std::string style;   // shown in the menu
            double bpm;          // where it sits best
            std::array<KitPad, 16> pads;   // lanes follow drumLaneRole(): kick, snare, clap, hats, ...
            std::string groove {};         // its starter beat (a groove name)
        };
        const std::vector<Kit>& kits();

        /** Peak-normalise, trim silence and fade the end (used by every render). */
        void finish (SampleData&, float peakDb, bool trimTail);
    }

    /** Step-sequencer grooves: patterns for a 16-pad kit in a given style. */
    namespace grooves
    {
        struct Groove
        {
            std::string style;   // menu section, e.g. "UK Garage"
            std::string name;
            double bpm;          // tempo it's written for
            float swing;         // 0..0.75, the track's swing
            int rateIndex;       // index into allRates() (1/16 = the usual)
            /** One string per lane (up to 16). Each char is a step:
                .  rest           x  hit            X  accent        g  ghost (soft)
                o  50% chance     r  ratchet x2     t  ratchet x3    R  roll x4 (rising)
                f  fill only      n  not on fills   2  every 2nd loop  < nudged early   > nudged late
                A lane's string length is its length (polymeter is fine). */
            std::array<std::string, 16> lanes;
        };
        const std::vector<Groove>& all();
        /** Write a groove into a step pattern (lengths, steps; lane algorithms untouched). */
        void apply (const Groove&, StepPattern&);
    }
}
