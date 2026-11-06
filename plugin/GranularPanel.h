#pragma once
// The GRANULAR window: a live view of the master buffer with four draggable grain
// playheads, then the heads, filter, resonator, space, glitch and looper / recorder.

#include "ExportUI.h"
#include "ModPanel.h"
#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    /** The capture buffer as a waveform, with the playheads on it. */
    class GrainView : public juce::Component
    {
    public:
        explicit GrainView (BashProcessor& p) : proc (p) {}
        std::function<void (int)> onSelectHead;
        int selected = 0;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        void setPosition (int head, float x);
        BashProcessor& proc;
        int dragging = -1;
    };

    /** A button that's on only while you hold it (Stutter). */
    class HoldButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void (bool)> onHold;
        void buttonStateChanged() override
        {
            const bool down = isDown();
            if (down != held) { held = down; if (onHold) onHold (down); }
            juce::TextButton::buttonStateChanged();
        }
    private:
        bool held = false;
    };

    class GranularPanel : public juce::Component
    {
    public:
        explicit GranularPanel (BashProcessor&);
        ~GranularPanel() override;
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
        std::function<int()> currentTrack;

    private:
        void selectHead (int h);
        void showTakeMenu();
        void showMessage (const juce::String&);
        Knob& knob (const juce::String& id, const juce::String& name, std::vector<std::unique_ptr<Knob>>& into);

        BashProcessor& proc;
        GrainView view;
        int head = 0;

        // top bar
        juce::ToggleButton onButton { "GRANULAR" }, freezeButton { "Freeze" }, loopButton { "Loop take" };
        juce::Slider mix;
        juce::Label mixLabel, status;
        DiceButton die;
        juce::TextButton recButton { "Record" }, takeButton { "Take" };
        DragChip takeChip, mixChip;

        // heads
        std::array<juce::TextButton, bash::grainHeads> headTabs;
        juce::ToggleButton headOn { "Head on" }, keyLock { "Key lock" };
        juce::ComboBox windowBox;
        juce::Label windowLabel;
        std::vector<std::unique_ptr<Knob>> headKnobs, knobs;
        std::vector<juce::String> headKnobIds;

        juce::ComboBox ftypeBox, chordBox, octBox, dlyDivBox, gDivBox;
        juce::Label ftypeLabel, chordLabel, octLabel, dlyDivLabel, gDivLabel;
        juce::ToggleButton revFreeze { "Freeze" };
        std::array<juce::ToggleButton, 5> glitchModes;
        HoldButton stutter { "STUTTER" };

        std::vector<std::unique_ptr<SliderAttachment>> sliderAtts, headAtts;
        std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;
        std::unique_ptr<ButtonAttachment> headOnAtt;
        std::vector<std::unique_ptr<ComboAttachment>> comboAtts;

        juce::Rectangle<int> barBox, headBox, filterBox, resBox, delayBox, reverbBox, glitchBox, outBox;
        juce::String message;
        juce::uint32 messageTime = 0;
    };
}
