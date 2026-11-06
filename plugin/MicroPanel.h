#pragma once
// The MICRO tab: a micro-loop / granular / glitch / multi-delay section with a phrase
// looper. Everything from the tracks (and returns) runs through it before the master.

#include "PluginProcessor.h"
#include "UiCommon.h"
#include "ExportUI.h"
#include "ModPanel.h"

namespace bashui
{
    /** The last 8 s of input with where every voice is reading, plus the loop lane. */
    class MicroScope : public juce::Component
    {
    public:
        explicit MicroScope (BashProcessor& p, bool compact = false) : proc (p), small (compact) {}
        void paint (juce::Graphics&) override;
        std::function<void()> onClick;
        void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    private:
        BashProcessor& proc;
        bool small;
    };

    class MicroPanel : public juce::Component
    {
    public:
        explicit MicroPanel (BashProcessor&);
        void refresh();
        void resized() override;
        void paint (juce::Graphics&) override;
        std::function<void (const juce::String&)> onMessage;

    private:
        BashProcessor& proc;
        MicroScope scope;
        juce::ToggleButton onButton { "MICRO on" };
        std::array<juce::TextButton, (size_t) bash::MicroAlgo::count> algoButtons;
        std::array<juce::TextButton, 4> varButtons;
        juce::Label varText;
        Knob activity, repeats, shape, filter, reso, modRate, modDepth, space, decay, mix, level;
        juce::ComboBox timeBox, verbBox, reverseBox;
        juce::Label timeLabel, verbLabel, reverseLabel;
        juce::TextButton holdButton { "HOLD" };
        DiceButton dice;

        // looper
        juce::TextButton recButton { "REC" }, stopButton { "STOP" }, undoButton { "UNDO" }, eraseButton { "ERASE" }, burstButton { "BURST" };
        juce::ToggleButton loopReverse { "Reverse" }, quantize { "Quantize" };
        juce::ComboBox speedBox, modeBox;
        juce::Label speedLabel, modeLabel, loopStatus;
        Knob loopLevel, loopFade;
        DragChip loopChip;
        bool burstDown = false;

        std::vector<std::unique_ptr<SliderAttachment>> atts;
        std::vector<std::unique_ptr<ComboAttachment>> comboAtts;
        std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;
        juce::Rectangle<int> scopeBox, fxBox, toneBox, looperBox;
        int shownAlgo = -1, shownVar = -1, shownState = -1;
        void setChoice (const char* id, int index);
    };
}
