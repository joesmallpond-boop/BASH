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
    if (argc > 3)
    {
        auto* p = proc.apvts.getParameter ("t1_engine");
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::String (argv[3]).getIntValue()));
    }

    // run the internal clock for a while so the UI shows a playhead
    const int blocks = argc > 4 ? juce::String (argv[4]).getIntValue() : 0;
    if (blocks > 0)
    {
        auto* sync = proc.apvts.getParameter ("sync");  sync->setValueNotifyingHost (0.0f);
        auto* play = proc.apvts.getParameter ("play");  play->setValueNotifyingHost (1.0f);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i) proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])));
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    return 0;
}
