// Renders the BASH! editor to a PNG for visual checks (developer tool, not shipped).
// usage: bash_ui_snapshot out.png [sample.wav] [engine 0..2] [play-blocks]
#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: %s out.png [sample] [engine] [blocks]\n", argv[0]); return 1; }

    BashProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (argc > 2 && juce::String (argv[2]).isNotEmpty())
    {
        const auto err = proc.loadSample (0, juce::File (juce::String (argv[2])));
        if (err.isNotEmpty()) std::printf ("load: %s\n", err.toRawUTF8());
    }
    const int track = argc > 5 ? juce::String (argv[5]).getIntValue() : 1;
    if (argc > 3)
    {
        auto* p = proc.apvts.getParameter ("t" + juce::String (track) + "_engine");
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::String (argv[3]).getIntValue()));
    }

    if (std::getenv ("BASH_SNAPSHOT_KIT"))
    {
        for (int i = 0; i < 600 && ! proc.library().ready(); ++i) juce::Thread::sleep (50);
        proc.loadLibraryKit (std::atoi (std::getenv ("BASH_SNAPSHOT_KIT")), track - 1);
    }
    if (std::getenv ("BASH_SNAPSHOT_STEP"))
    {
        proc.apvts.getParameter ("t" + juce::String (track) + "_mode")->setValueNotifyingHost (1.0f);
        proc.rollDice (track - 1);   // starter groove
        proc.editSteps (track - 1, [] (bash::StepPattern& sp)
        {
            sp.lanes[1].steps[14].ratchet = 3; sp.lanes[1].steps[14].on = true; sp.lanes[1].steps[14].cond = bash::TrigCond::Fill;
            sp.lanes[5].length = 12; sp.lanes[5].speed = 5; sp.lanes[5].steps[3].on = true; sp.lanes[5].steps[3].prob = 50;
            sp.lanes[6].mute = true; sp.lanes[6].steps[2].on = true;
        });
    }

    // run the internal clock for a while so the UI shows a playhead
    const int blocks = argc > 4 ? juce::String (argv[4]).getIntValue() : 0;
    if (blocks > 0)
    {
        auto* sync = proc.apvts.getParameter ("sync");  sync->setValueNotifyingHost (0.0f);
        auto* play = proc.apvts.getParameter ("play");  play->setValueNotifyingHost (1.0f);
        auto* on = proc.apvts.getParameter ("t" + juce::String (track) + "_on"); on->setValueNotifyingHost (1.0f);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i) proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (auto* ed = dynamic_cast<BashEditor*> (editor.get()))
    {
        ed->showTrack (track - 1);
        if (argc > 6 && juce::String (argv[6]) == "mixer") ed->showMixer();
        if (argc > 6 && juce::String (argv[6]) == "mod")
        {
            proc.matrix (track - 1).set (bash::ModDest::Cutoff, bash::ModSource::Lfo1, 0.6f);
            proc.matrix (track - 1).set (bash::ModDest::Type, bash::ModSource::Life, 0.4f);
            proc.matrix (track - 1).set (bash::ModDest::Density, bash::ModSource::Velocity, -0.3f);
            ed->showMod();
        }
    }
    if (auto* sc = std::getenv ("BASH_SNAPSHOT_WIDTH"))
        editor->setSize (juce::String (sc).getIntValue(), juce::String (sc).getIntValue() * 944 / 1040);
    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])));
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    return 0;
}
