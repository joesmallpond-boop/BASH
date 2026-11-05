#pragma once
// Shared look and small helpers for the BASH! editor panels.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace bashui
{
    // Charcoal + amber (see design/ui-reference.png): one accent colour, dimmed for secondary states.
    namespace colour
    {
        const juce::Colour background { 0xff1d1d1d };   // window
        const juce::Colour panel      { 0xff252525 };   // panels, cells
        const juce::Colour raised     { 0xff2e2e2e };   // wells, inactive buttons, waveform beds
        const juce::Colour line       { 0xff3a3a3a };   // borders, knob tracks, grid lines
        const juce::Colour ink        { 0xffe9e6df };   // main text
        const juce::Colour quiet      { 0xff8b8a86 };   // labels
        const juce::Colour accent     { 0xffffb800 };   // amber: values, selection, accents, playhead
        const juce::Colour accentDim  { 0xff8a6508 };   // dim amber: normal steps, inactive fills
        const juce::Colour active     { 0xff4a3910 };   // dark amber: toggled-on backgrounds, playhead cell
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

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
    };

    inline void styleKnob (Knob& k, const juce::String& name)
    {
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setColour (juce::Slider::textBoxTextColourId, colour::accent);
        k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.slider.setColour (juce::Slider::textBoxHighlightColourId, colour::active);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k.label.setText (name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        k.label.setColour (juce::Label::textColourId, colour::quiet);
    }

    inline void placeKnob (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    }

    inline void styleComboLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, colour::quiet);
    }

    inline void fillChoices (juce::ComboBox& box, juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        box.clear (juce::dontSendNotification);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
            box.addItemList (p->choices, 1);
    }

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
}
