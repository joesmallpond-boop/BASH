#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace bashui
{
    // Soft-club palette shared with the Decompression Tapes / BASH VIDEO DESTROYER family.
    namespace colour
    {
        const juce::Colour background { 0xffeef3fa };   // ice white
        const juce::Colour panel      { 0xffffffff };
        const juce::Colour mint       { 0xffcdefe3 };
        const juce::Colour periwinkle { 0xffa9b4f2 };
        const juce::Colour iceBlue    { 0xffcfe6f7 };
        const juce::Colour coral      { 0xffff7a6b };
        const juce::Colour ink        { 0xff2a2f45 };
        const juce::Colour quiet      { 0xff7a8199 };
        const juce::Colour line       { 0xffdde3ee };
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float start, float end, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                           juce::ComboBox&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    };

    /** The step grid: pitch bars, accents, slides, locks and the playhead. */
    class StepView : public juce::Component
    {
    public:
        explicit StepView (BashProcessor& p) : proc (p) {}
        void setTrack (int t) { track = t; refresh(); }
        void refresh();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        int indexAt (juce::Point<float>) const;
        juce::Rectangle<float> cellBounds (int visibleIndex) const;

        BashProcessor& proc;
        int track = 0;
        uint32_t first = 0, visible = 16, length = 16, playIdx = 0;
        bool running = false;
        int baseNote = 36;
        std::vector<bash::StepData> steps;
        std::vector<uint8_t> masks;
        juce::uint32 pressTime = 0;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
    };
}

class BashEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BashEditor (BashProcessor&);
    ~BashEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectTrack (int t);
    void bindTrack();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    BashProcessor& proc;
    bashui::LookAndFeel lnf;
    int currentTrack = 0;

    // header
    juce::ComboBox keyBox, scaleBox;
    juce::ToggleButton syncButton { "Sync" }, playButton { "Play" };
    bashui::Knob tempo, master;
    std::unique_ptr<ComboAttachment>  keyAtt, scaleAtt;
    std::unique_ptr<ButtonAttachment> syncAtt, playAtt;
    std::unique_ptr<SliderAttachment> tempoAtt, masterAtt;

    // tracks
    std::array<juce::TextButton, bash::numTracks> trackButtons;
    bashui::StepView stepView;
    juce::TextButton regenButton { "Regenerate" }, clearButton { "Clear locks" };
    juce::Label hint;

    // per-track controls (re-bound when the track changes)
    juce::ToggleButton onButton { "Track on" }, scaleLockButton { "Scale lock" };
    juce::ComboBox algoBox, cycleBox, directionBox, rateBox;
    juce::Label algoLabel, cycleLabel, directionLabel, rateLabel;
    std::vector<std::unique_ptr<bashui::Knob>> seqKnobs, voiceKnobs;
    std::vector<juce::String> seqKnobIds, voiceKnobIds;
    std::vector<std::unique_ptr<SliderAttachment>> knobAtts;
    std::vector<std::unique_ptr<ComboAttachment>>  comboAtts;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;

    juce::Rectangle<int> seqPanel, voicePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BashEditor)
};
