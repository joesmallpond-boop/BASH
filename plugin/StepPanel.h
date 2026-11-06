#pragma once
// BASH! step sequencer panel: 16 lanes x up to 64 steps (4 pages of 16), per-lane
// length / speed / algorithm / die, a Fill latch and auto fill, and an inspector for
// the selected lane's algorithm and the selected step (velocity, probability,
// ratchet, condition, nudge).

#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    class StepPanel : public juce::Component
    {
    public:
        explicit StepPanel (BashProcessor&);
        ~StepPanel() override;

        void setTrack (int t);
        /** Pull the pattern and playheads from the processor (UI timer). */
        void refresh();
        /** Left part of the toolbar is left free for the editor's ON / ENGINE / RATE controls. */
        static constexpr int toolbarReserved = 300;
        std::function<void (const juce::String&)> onMessage;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        enum class Hit { None, Step, Name, Mute, Length, Speed, Algo, Die };
        struct HitInfo { Hit what = Hit::None; int lane = -1, step = -1; };
        HitInfo hitAt (juce::Point<int>) const;
        bool hitTest (int x, int y) override { return ! (y < 28 && x < toolbarReserved); }   // let clicks reach ON / ENGINE / RATE
        juce::Rectangle<int> laneRow (int lane) const;
        juce::Rectangle<int> stepCell (int lane, int visibleIndex) const;
        juce::Rectangle<int> headerPart (int lane, Hit part) const;

        void edit (const std::function<void (bash::StepPattern&)>&);
        void select (int lane, int step);
        void updateInspector();
        void showStepMenu (int lane, int step);
        void showLengthMenu (int lane);
        void showSpeedMenu (int lane);
        void showAlgoMenu (int lane);
        int  pages() const;

        BashProcessor& proc;
        int track = 0;
        bash::StepPattern pattern;
        std::array<int, bash::stepLanes> playPos {};
        std::array<juce::String, bash::stepLanes> names;
        int page = 0, selLane = 0, selStep = 0;

        // drag state
        bool painting = false, paintOn = true, velocityDrag = false;
        int dragLane = -1, lastPaintStep = -1, dragStartVel = 100;

        // toolbar
        juce::ToggleButton fillButton { "FILL" };
        juce::ComboBox autoFillBox;
        juce::Slider swing;
        juce::Label swingLabel, autoFillLabel;
        std::array<juce::TextButton, 4> pageButtons;
        juce::TextButton clearButton { "Clear" }, groovesButton { "Grooves" };
        void showGroovesMenu();
        std::unique_ptr<ButtonAttachment> fillAtt;
        std::unique_ptr<ComboAttachment> autoFillAtt;
        std::unique_ptr<SliderAttachment> swingAtt;

        // inspector
        struct Field { juce::Label label; juce::Slider slider; };
        Field pulses, rotate, amount, evolve, vel, prob, ratchet, nudge;
        juce::ComboBox shapeBox, condBox;
        juce::Label shapeLabel, condLabel, laneTitle, stepTitle;
        juce::TextButton generateButton { "Generate" };
        bool updatingInspector = false;

        juce::Rectangle<int> gridArea, inspectorArea;
        static constexpr int headerWidth = 262, laneGap = 2;
    };
}
