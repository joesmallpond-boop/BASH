#pragma once

#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    /** A small knob with its label above and the value in a popup while dragging. */
    struct MiniKnob
    {
        juce::Slider slider;
        juce::Label  label;
        void setup (const juce::String& name);
        void place (juce::Rectangle<int> r) { label.setBounds (r.removeFromTop (12)); slider.setBounds (r); }
    };

    /** One effect slot: type menu + A / B / Gnarl / Mix. */
    class FxSlotUI : public juce::Component
    {
    public:
        FxSlotUI (juce::AudioProcessorValueTreeState&, const juce::String& prefix);
        void resized() override;
        void refreshLabels();

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::String prefix;
        juce::ComboBox type;
        MiniKnob a, b, gnarl, mix;
        std::unique_ptr<ComboAttachment> typeAtt;
        std::vector<std::unique_ptr<SliderAttachment>> atts;
        int shownType = -1;
    };

    class LevelMeter : public juce::Component
    {
    public:
        void setLevel (float v) { if (std::abs (v - level) > 0.002f) { level = v; repaint(); } }
        void paint (juce::Graphics&) override;
    private:
        float level = 0.0f;
    };

    class MixerPanel : public juce::Component
    {
    public:
        explicit MixerPanel (BashProcessor&);
        void refresh();
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        struct Strip
        {
            juce::Label name;
            std::vector<std::unique_ptr<FxSlotUI>> fx;
            MiniKnob sendA, sendB, pan;
            juce::TextButton mute { "M" }, solo { "S" };
            juce::ToggleButton limiter { "Limit" };
            juce::Slider fader;
            LevelMeter meter;
            std::vector<std::unique_ptr<SliderAttachment>> atts;
            std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;
            bool isTrack = false, isMaster = false;
            int track = -1;
        };

        void buildTrackStrip (Strip&, int t);
        void buildReturnStrip (Strip&, const juce::String& prefix, const juce::String& levelId, const juce::String& title);
        void buildMasterStrip (Strip&);
        void addCommon (Strip&);

        BashProcessor& proc;
        std::vector<std::unique_ptr<Strip>> strips;
    };
}
