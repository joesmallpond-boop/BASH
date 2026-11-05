#pragma once

#include "PluginProcessor.h"
#include "UiCommon.h"

namespace bashui
{
    /** The 4x4 kit grid. Click to select and audition; drop audio files on a
        cell to load them (several files fill consecutive cells). */
    class DrumGrid : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        explicit DrumGrid (BashProcessor& p) : proc (p) {}

        std::function<void (int)> onSelect;
        std::function<void (const juce::String&)> onMessage;

        void setTrack (int t)     { track = t; repaint(); }
        void setSelected (int c)  { selected = c; repaint(); }
        int  getSelected() const  { return selected; }
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override { hover = cellAt ({ (float) x, (float) y }); repaint(); }
        void fileDragExit (const juce::StringArray&) override              { hover = -1; repaint(); }
        void filesDropped (const juce::StringArray&, int x, int y) override;

    private:
        int cellAt (juce::Point<float>) const;
        juce::Rectangle<float> cellBounds (int c) const;

        BashProcessor& proc;
        int track = 0, selected = 0, hover = -1, flash = -1;
        juce::uint32 flashTime = 0;
        bash::SequencerParams params;
    };

    /** Small waveform with draggable start / end handles for a sample cell. */
    class CellWaveform : public juce::Component
    {
    public:
        explicit CellWaveform (BashProcessor& p) : proc (p) {}
        void setCell (int c);
        std::function<void (float start, float end)> onRangeChanged;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        BashProcessor& proc;
        int cell = 0;
        std::vector<float> peaks;
        float start = 0.0f, end = 1.0f;
        int dragging = 0;   // 1 = start, 2 = end
    };

    class DrumPanel : public juce::Component
    {
    public:
        explicit DrumPanel (BashProcessor&);
        void setTrack (int t);
        void refresh();
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void selectCell (int c);
        void pushToCell();          // knob values -> processor
        void pullFromCell();        // processor -> knob values
        void showMessage (const juce::String&);
        void browse();

        BashProcessor& proc;
        int track = 0, cell = 0;
        bool updating = false;
        DrumGrid grid;
        CellWaveform wave;

        juce::Label title, info;
        juce::TextButton sampleButton { "Sample" }, fmButton { "FM" }, clearButton { "Clear" }, loadButton { "Load" };
        juce::ComboBox kitBox, presetBox;
        std::vector<std::unique_ptr<juce::TextButton>> startPoints;

        // sample editor
        Knob sStart, sEnd, sFadeIn, sFadeOut, sAttack, sSustain, sTune, sCutoff;
        juce::ToggleButton sReverse { "Reverse" };
        // fm editor
        Knob fPitch, fSweep, fSweepTime, fRatio, fAmt, fFmDecay, fNoise, fTone, fNoiseDecay, fBursts, fDecay, fClick, fDrive;
        // shared
        Knob cGain, cPan, cChoke;

        std::vector<Knob*> sampleKnobs, fmKnobs;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String message;
        juce::uint32 messageTime = 0;
        bash::DrumCell::Kind shownKind = bash::DrumCell::Kind::Empty;
    };
}
