#pragma once

#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    /** Waveform with slice markers. Drop a file here, double-click to add a
        slice, drag a marker to move it, right-click a marker to delete it. */
    class WaveformView : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        explicit WaveformView (BashProcessor& p) : proc (p) {}

        void setTrack (int t) { track = t; shown = nullptr; refresh(); }
        void refresh();

        std::function<void()> onBrowse;
        std::function<void (const juce::String&)> onMessage;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override              { dropHover = false; repaint(); }
        void filesDropped (const juce::StringArray&, int, int) override;

    private:
        float  xForFrame (double frame) const;
        uint32_t frameForX (float x) const;
        int    markerNear (float x) const;   // slice index, or -1
        void   rebuildPeaks();

        BashProcessor& proc;
        int track = 0;
        const bash::SampleData* shown = nullptr;
        std::vector<float> peakMin, peakMax;
        std::vector<uint32_t> slices;
        int    playing = -1;
        double playFrame = -1.0;
        int    dragIndex = -1;
        bool   dropHover = false;
        bash::SequencerParams params;
    };

    class SamplerPanel : public juce::Component
    {
    public:
        explicit SamplerPanel (BashProcessor&);
        void setTrack (int t);
        void refresh();
        void resized() override;
        void forEachKnob (const std::function<void (Knob&, const juce::String&)>& f);

    private:
        void browse();
        void loadFile (const juce::File&);
        void showMessage (const juce::String&);

        BashProcessor& proc;
        int track = 0;
        WaveformView wave;

        juce::TextButton loadButton { "Load" }, transientButton { "Transients" }, gridButton { "Grid" },
                         randomButton { "Random" }, clearButton { "Clear" };
        juce::Slider sensitivity, randomCount;
        juce::ComboBox gridDivisions;
        juce::Label info;

        juce::ComboBox modeBox;
        juce::Label modeLabel;
        Knob winStart, winSize, tune, attack, release, level, pan;
        juce::ToggleButton pitchFollow { "Pitch follow" }, reverse { "Reverse" };
        juce::ToggleButton warpButton { "Warp" };
        juce::TextButton loopsButton { "Loops" };
        void showLoopsMenu();
        juce::Label bpmLabel;                       // the file's tempo, editable
        juce::TextButton halfButton { "/2" }, doubleButton { "x2" };
        double shownBpm = -1.0;

        std::vector<std::unique_ptr<SliderAttachment>> knobAtts;
        std::unique_ptr<ComboAttachment> modeAtt;
        std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String message;
        juce::uint32 messageTime = 0;
    };
}
