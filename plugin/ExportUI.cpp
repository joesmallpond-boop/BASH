#include "ExportUI.h"

using namespace bashui;

void DragChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (busy ? colour::accent : pressed ? colour::accentDim : colour::raised);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (colour::ink.withAlpha (0.25f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    // grip dots: "drag me"
    g.setColour (colour::ink.withAlpha (0.5f));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (r.getX() + 9.0f + (float) j * 4.0f, r.getCentreY() - 5.0f + (float) i * 4.0f, 2.2f, 2.2f);

    g.setColour (colour::ink);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (busy ? "..." : label, r.withTrimmedLeft (18.0f), juce::Justification::centred);
}

void DragChip::mouseDrag (const juce::MouseEvent& e)
{
   #if JUCE_IOS
    juce::ignoreUnused (e);
    return;   // no drag between apps on iOS: a tap opens the share sheet instead
   #endif
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    busy = true; repaint();
    const auto file = makeFile();
    busy = false; pressed = false; repaint();
    if (! file.existsAsFile())
    {
        if (onMessage) onMessage ("Couldn't render the file");
        return;
    }
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void DragChip::mouseUp (const juce::MouseEvent&)
{
    pressed = false;
    if (! dragged)
    {
        busy = true; repaint();
        const auto file = makeFile();
        busy = false;
        if (file.existsAsFile())
        {
           #if JUCE_IOS
            // AirDrop, Files, AudioShare, other apps... (also kept in the app's BASH Exports folder)
            shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (file) }, [] (bool, const juce::String&) {}, this);
            if (onMessage) onMessage ("Sharing " + file.getFileName());
           #else
            if (onMessage) onMessage ("Saved " + file.getFileName() + " to " + file.getParentDirectory().getFullPathName());
            file.revealToUser();
           #endif
        }
        else if (onMessage) onMessage ("Couldn't render the file");
    }
    repaint();
}

// ============================================================================
ExportBar::ExportBar (BashProcessor& p, std::function<int()> currentTrack)
    : proc (p),
      midi ("MIDI", [this, currentTrack] { return proc.exportMidi (currentTrack()); }),
      wav  ("WAV",  [this, currentTrack] { return proc.exportAudio (currentTrack()); })
{
    title.setText ("DRAG OUT", juce::dontSendNotification);
    title.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colour::quiet);
    title.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (title);

    for (int b : { 1, 2, 4, 8, 16, 32 }) bars.addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
    bars.setSelectedId (proc.exportBars, juce::dontSendNotification);
    bars.onChange = [this] { proc.exportBars = bars.getSelectedId(); };
    addAndMakeVisible (bars);

    fx.setToggleState (proc.exportWithFx, juce::dontSendNotification);
    fx.onClick = [this] { proc.exportWithFx = fx.getToggleState(); };
    fx.setTooltip ("Render with the track's inserts and sends");
    addAndMakeVisible (fx);

   #if JUCE_IOS
    title.setText ("SHARE", juce::dontSendNotification);
   #endif
    midi.setTooltip ("Drag into your DAW or onto the desktop as a MIDI file. Click to save it.");
    wav.setTooltip ("Drag out this track as a WAV loop. Click to save it.");
    for (auto* c : { &midi, &wav })
    {
        c->onMessage = [this] (const juce::String& m) { if (onMessage) onMessage (m); };
        addAndMakeVisible (*c);
    }
}

void ExportBar::resized()
{
    auto r = getLocalBounds();
    wav.setBounds (r.removeFromRight (70)); r.removeFromRight (6);
    midi.setBounds (r.removeFromRight (70)); r.removeFromRight (10);
    fx.setBounds (r.removeFromRight (64)); r.removeFromRight (6);
    bars.setBounds (r.removeFromRight (90)); r.removeFromRight (8);
    title.setBounds (r);
}
