#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UiCommon.h"
#include "SamplerPanel.h"
#include "SynthPanel.h"
#include "DrumPanel.h"
#include "MixerPanel.h"
#include "ModPanel.h"
#include "ExportUI.h"

namespace bashui
{
    /** The step grid: pitch bars, accents, slides, locks and the playhead. */
    class StepView : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        explicit StepView (BashProcessor& p) : proc (p) {}
        std::function<void (const juce::String&)> onMessage;
        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            for (auto& f : files) if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi")) return true;
            return false;
        }
        void filesDropped (const juce::StringArray& files, int, int) override
        {
            for (auto& f : files)
                if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
                {
                    const auto err = proc.importMidi (track, juce::File (f));
                    if (onMessage) onMessage (err.isEmpty() ? "Imported " + juce::File (f).getFileName() + ": Type 0 plays it, turn Type up to morph away" : err);
                    refresh();
                    return;
                }
        }
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
        uint32_t sliceCount = 0;
        bool drumTrack = false;
        std::vector<bash::StepData> steps;
        std::vector<uint8_t> masks;
        juce::uint32 pressTime = 0;
    };

}

/** The whole BASH! interface at its design size (designWidth x designHeight).
    BashEditor scales it to whatever size the host or iPad gives it. */
class BashView : public juce::Component, private juce::Timer
{
public:
    static constexpr int designWidth = 1040, designHeight = 944;

    explicit BashView (BashProcessor&);
    ~BashView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showTrack (int t) { selectTrack (t); }
    void showMixer()       { setView (1); }
    void showMod()         { setView (2); }

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
    juce::TextButton regenButton { "Regenerate" }, clearButton { "Clear locks" }, clearMidiButton { "Clear MIDI" };
    std::unique_ptr<bashui::ExportBar> exportBar;
    juce::Label hint;

    // per-track controls (re-bound when the track changes)
    juce::ToggleButton onButton { "Track on" }, scaleLockButton { "Scale lock" };
    juce::ComboBox engineBox, algoBox, cycleBox, directionBox, rateBox;
    juce::Label engineLabel, algoLabel, cycleLabel, directionLabel, rateLabel;
    bashui::SamplerPanel samplerPanel;
    bashui::SynthPanel synthPanel;
    bashui::DrumPanel drumPanel;
    bashui::MixerPanel mixerPanel;
    bashui::ModPanel modPanel;
    bashui::DiceButton diceButton;
    juce::TextButton trackViewButton { "TRACKS" }, mixerViewButton { "MIXER" }, modViewButton { "MOD" };
    int view = 0;   // 0 tracks, 1 mixer, 2 mod
    bool mixerView = false;
    void setView (int v);
    std::vector<juce::Component*> trackViewComponents();
    bash::EngineType shownEngine = bash::EngineType::Synth;
    void updateEnginePanel();
    std::vector<std::unique_ptr<bashui::Knob>> seqKnobs;
    std::vector<juce::String> seqKnobIds;
    std::vector<std::unique_ptr<SliderAttachment>> knobAtts;
    std::vector<std::unique_ptr<ComboAttachment>>  comboAtts;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;

    juce::Rectangle<int> seqPanel, voicePanel;

    bashui::DiceLocks diceLocks { proc };   // must stay last: unbinds before any knob is destroyed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BashView)
};

class BashEditor : public juce::AudioProcessorEditor
{
public:
    explicit BashEditor (BashProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

    void showTrack (int t) { view.showTrack (t); }
    void showMixer()       { view.showMixer(); }
    void showMod()         { view.showMod(); }

private:
    BashView view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BashEditor)
};
