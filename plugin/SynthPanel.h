#pragma once

#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    /** Machine synth controls for one track. */
    class SynthPanel : public juce::Component
    {
    public:
        explicit SynthPanel (BashProcessor&);
        void setTrack (int t);
        void refresh();   // updates the per-machine knob names
        void resized() override;

    private:
        BashProcessor& proc;
        int track = 0;
        int shownMachine = -1;

        juce::ComboBox machineBox, filterBox;
        juce::Label machineLabel, filterLabel;
        Knob character, timbre, cutoff, reso, envMod, decay, drive, glide,
             attack, ampDecay, sustain, release, level, pan;

        std::vector<std::unique_ptr<SliderAttachment>> knobAtts;
        std::vector<std::unique_ptr<ComboAttachment>>  comboAtts;
    };
}
