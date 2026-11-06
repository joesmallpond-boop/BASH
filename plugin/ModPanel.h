#pragma once

#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    /** Draws an LFO's shape with a dot at the current phase. */
    class LfoView : public juce::Component
    {
    public:
        LfoView (BashProcessor& p, int index) : proc (p), lfo (index) {}
        void paint (juce::Graphics&) override;
    private:
        BashProcessor& proc;
        int lfo;
    };

    /** Destinations x sources. Drag a cell up/down to set its amount, double-click to clear. */
    class MatrixGrid : public juce::Component
    {
    public:
        explicit MatrixGrid (BashProcessor& p) : proc (p) {}
        void setTrack (int t) { track = t; repaint(); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
    private:
        bool cellAt (juce::Point<float>, int& dest, int& src) const;
        juce::Rectangle<float> cellBounds (int dest, int src) const;
        BashProcessor& proc;
        int track = 0, dragDest = -1, dragSrc = -1;
        float dragStart = 0.0f;
        static constexpr int labelW = 110, headerH = 40;
    };

    class ModPanel : public juce::Component
    {
    public:
        explicit ModPanel (BashProcessor&);
        void setTrack (int t);
        void refresh();
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        BashProcessor& proc;
        int track = 0;
        struct LfoUI
        {
            juce::ComboBox shape, division;
            juce::ToggleButton sync { "Sync" };
            Knob rate;
            std::unique_ptr<LfoView> view;
            std::unique_ptr<ComboAttachment> shapeAtt, divAtt;
            std::unique_ptr<ButtonAttachment> syncAtt;
            std::unique_ptr<SliderAttachment> rateAtt;
        };
        std::array<LfoUI, 2> lfos;
        Knob lifeRate;
        std::unique_ptr<SliderAttachment> lifeAtt;
        std::vector<float> lifeTrace;
        MatrixGrid grid;
        juce::Rectangle<int> lfoBoxes[2], lifeBox, lifeTraceArea, matrixBox;
    };

    /** Roll the Dice: a die on the Sequencer panel. Click to roll the current track's
        sequencer (only that), right-click for chaos amount and undo. */
    class DiceButton : public juce::Component
    {
    public:
        explicit DiceButton (BashProcessor& p) : proc (p) { setTooltip ("Roll this track's sequencer (right-click for options)"); }
        std::function<int()> currentTrack;
        std::function<void()> onRolled;
        /** Set these to make the die roll something else (the GRANULAR section's die). */
        std::function<void()> rollAction, menuAction;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void setTooltip (const juce::String& t) { tip = t; }
    private:
        void showMenu();
        BashProcessor& proc;
        juce::String tip;
        bool pressed = false;
        int face = 5;
    };

    /** Right-click any registered knob: "Protect from Roll the Dice". */
    class DiceLocks
    {
    public:
        explicit DiceLocks (BashProcessor& p);
        ~DiceLocks();
        void bind (juce::Slider&, juce::Label&, const juce::String& paramId);
        void clear();
        void refresh();
    private:
        struct Listener;
        BashProcessor& proc;
        std::vector<std::unique_ptr<Listener>> listeners;
    };
}
