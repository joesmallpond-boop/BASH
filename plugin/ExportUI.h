#pragma once

#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    /** A small pill you drag out of the plugin: it renders a file and hands it to the
        DAW or desktop. Clicking it saves the file and shows it in Finder / Explorer. */
    class DragChip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        DragChip (const juce::String& text, std::function<juce::File()> make) : label (text), makeFile (std::move (make)) {}
        std::function<void (const juce::String&)> onMessage;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { dragged = false; pressed = true; repaint(); }
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::String label;
        std::function<juce::File()> makeFile;
        bool dragged = false, pressed = false, busy = false;
        juce::ScopedMessageBox shareBox;   // iOS share sheet
    };

    /** Bars + FX choice and the per-track drag chips (MIDI, WAV). */
    class ExportBar : public juce::Component
    {
    public:
        ExportBar (BashProcessor&, std::function<int()> currentTrack);
        void resized() override;
        std::function<void (const juce::String&)> onMessage;
    private:
        BashProcessor& proc;
        juce::Label title;
        juce::ComboBox bars;
        juce::ToggleButton fx { "FX" };
        DragChip midi, wav;
    };
}
