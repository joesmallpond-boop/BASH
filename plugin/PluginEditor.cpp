#include "PluginEditor.h"

using namespace bashui;

namespace
{
    constexpr int editorW = 1040, editorH = 920;
    constexpr int margin = 24;

    juce::String noteText (int note)
    {
        return juce::String (bash::noteName (note % 12)) + juce::String (note / 12 - 1);
    }
}

// ============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colour::background);
    setColour (juce::Label::textColourId, colour::ink);
    setColour (juce::Slider::textBoxTextColourId, colour::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colour::periwinkle);
    setColour (juce::ComboBox::textColourId, colour::ink);
    setColour (juce::ComboBox::backgroundColourId, colour::panel);
    setColour (juce::ComboBox::outlineColourId, colour::line);
    setColour (juce::ComboBox::arrowColourId, colour::quiet);
    setColour (juce::PopupMenu::backgroundColourId, colour::panel);
    setColour (juce::PopupMenu::textColourId, colour::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::periwinkle);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::ink);
    setColour (juce::TextButton::buttonColourId, colour::panel);
    setColour (juce::TextButton::buttonOnColourId, colour::periwinkle);
    setColour (juce::TextButton::textColourOffId, colour::ink);
    setColour (juce::TextButton::textColourOnId, colour::ink);
    setColour (juce::ToggleButton::textColourId, colour::ink);
    setColour (juce::TextEditor::backgroundColourId, colour::panel);
    setColour (juce::TextEditor::textColourId, colour::ink);
    setColour (juce::TextEditor::highlightColourId, colour::periwinkle);
    setColour (juce::CaretComponent::caretColourId, colour::ink);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float start, float end, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float stroke = 4.0f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - stroke, radius - stroke, 0.0f, start, end, true);
    g.setColour (colour::line);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - stroke, radius - stroke, 0.0f, start, angle, true);
    g.setColour (colour::periwinkle);
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colour::panel);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre));
    g.setColour (colour::line);
    g.drawEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre), 1.0f);

    const auto dot = centre.getPointOnCircumference (radius * 0.42f, angle);
    g.setColour (colour::coral);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                                        bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (down)             c = c.darker (0.08f);
    else if (highlighted) c = c.brighter (0.04f).overlaidWith (colour::iceBlue.withAlpha (0.25f));
    g.setColour (c);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (colour::line);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour::line);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    juce::Path chevron;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (colour::quiet);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colour::mint : (highlighted ? colour::iceBlue.withAlpha (0.4f) : colour::panel));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? colour::mint.darker (0.15f) : colour::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 14.0f, r.getCentreY() });
    g.setColour (on ? colour::coral : colour::line);
    g.fillEllipse (dot);

    g.setColour (colour::ink);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
}

// ============================================================================
void StepView::refresh()
{
    const auto params = proc.uiSeqParams (track);
    sliceCount = params.sliceCount;
    length   = params.length;
    visible  = juce::jmin<uint32_t> (length, 64u);
    running  = proc.isRunning();
    playIdx  = proc.playIndex (track);
    first    = length > 64 ? (playIdx / 64u) * 64u : 0u;
    baseNote = params.baseNote;
    steps    = proc.snapshot (track, first, visible);
    masks.resize (visible);
    for (uint32_t i = 0; i < visible; ++i)
        masks[i] = proc.lockMask (track, first + i);
    repaint();
}

juce::Rectangle<float> StepView::cellBounds (int i) const
{
    const int cols = (int) juce::jmin<uint32_t> (visible, 16u);
    const int rows = (int) ((visible + 15) / 16);
    const auto area = getLocalBounds().toFloat();
    const float cw = area.getWidth() / (float) cols;
    const float ch = area.getHeight() / (float) juce::jmax (1, rows);
    return { area.getX() + (float) (i % cols) * cw, area.getY() + (float) (i / cols) * ch, cw, ch };
}

int StepView::indexAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) visible; ++i)
        if (cellBounds (i).contains (p))
            return i;
    return -1;
}

void StepView::paint (juce::Graphics& g)
{
    for (int i = 0; i < (int) steps.size(); ++i)
    {
        const auto& s = steps[(size_t) i];
        const auto mask = masks[(size_t) i];
        auto cell = cellBounds (i).reduced (3.0f);
        const bool isPlayhead = running && first + (uint32_t) i == playIdx;

        g.setColour (isPlayhead ? colour::mint : colour::panel);
        g.fillRoundedRectangle (cell, 8.0f);
        g.setColour (i % 4 == 0 ? colour::line.darker (0.1f) : colour::line);
        g.drawRoundedRectangle (cell, 8.0f, 1.0f);

        auto inner = cell.reduced (5.0f);
        if (s.active)
        {
            const float frac = sliceCount > 0
                ? juce::jmap ((float) (s.slice + 1) / (float) sliceCount, 0.12f, 0.92f)
                : juce::jlimit (0.12f, 0.92f, juce::jmap ((float) (s.note - baseNote), -12.0f, 24.0f, 0.12f, 0.92f));
            auto bar = inner.withTrimmedTop (16.0f);
            bar = bar.removeFromBottom (bar.getHeight() * frac);
            g.setColour (s.accent ? colour::coral : colour::periwinkle);
            g.fillRoundedRectangle (bar, 5.0f);

            g.setColour (colour::ink);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText (sliceCount > 0 ? "S" + juce::String (s.slice + 1) : noteText (s.note),
                        inner.removeFromTop (14.0f), juce::Justification::centred);

            if (s.slide)
            {
                g.setColour (colour::ink.withAlpha (0.6f));
                const float yy = bar.getY() + 2.0f;
                g.drawLine (bar.getRight() - 4.0f, yy, cell.getRight() + 3.0f, yy, 2.0f);
            }
        }
        else
        {
            g.setColour (colour::line.darker (0.2f));
            const auto c = inner.getCentre();
            if ((mask & bash::lockbit::active) != 0)
            {
                g.drawLine (c.x - 5, c.y - 5, c.x + 5, c.y + 5, 1.5f);   // muted by hand
                g.drawLine (c.x - 5, c.y + 5, c.x + 5, c.y - 5, 1.5f);
            }
            else
            {
                g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 2.0f).withCentre (c), 1.0f);
            }
        }

        if (mask != 0)
        {
            g.setColour (colour::ink);
            g.drawRoundedRectangle (cell.reduced (0.5f), 8.0f, 2.0f);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withPosition (cell.getX() + 5.0f, cell.getY() + 5.0f));
        }
    }

    if (length > visible)
    {
        g.setColour (colour::quiet);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("steps " + juce::String (first + 1) + "-" + juce::String (first + visible) + " of " + juce::String ((juce::int64) length),
                    getLocalBounds().removeFromBottom (14), juce::Justification::bottomRight);
    }
}

void StepView::mouseDown (const juce::MouseEvent&)
{
    pressTime = juce::Time::getMillisecondCounter();
}

void StepView::mouseUp (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i < 0) return;
    const bool longPress = juce::Time::getMillisecondCounter() - pressTime > 450;   // touch: hold to mute
    if (e.mods.isPopupMenu() || longPress)
        proc.toggleMute (track, first + (uint32_t) i);
    else
        proc.toggleLock (track, first + (uint32_t) i);
    refresh();
}

// ============================================================================
BashEditor::BashEditor (BashProcessor& p)
    : AudioProcessorEditor (&p), proc (p), stepView (p), samplerPanel (p)
{
    setLookAndFeel (&lnf);
    auto& apvts = proc.apvts;

    // header
    for (auto* b : { &keyBox, &scaleBox }) addAndMakeVisible (*b);
    fillChoices (keyBox, apvts, "root");
    fillChoices (scaleBox, apvts, "scale");
    keyAtt   = std::make_unique<ComboAttachment> (apvts, "root", keyBox);
    scaleAtt = std::make_unique<ComboAttachment> (apvts, "scale", scaleBox);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (playButton);
    syncAtt = std::make_unique<ButtonAttachment> (apvts, "sync", syncButton);
    playAtt = std::make_unique<ButtonAttachment> (apvts, "play", playButton);
    styleKnob (tempo, "TEMPO");
    styleKnob (master, "MASTER");
    for (auto* k : { &tempo, &master }) { addAndMakeVisible (k->slider); addAndMakeVisible (k->label); }
    tempoAtt  = std::make_unique<SliderAttachment> (apvts, "tempo", tempo.slider);
    masterAtt = std::make_unique<SliderAttachment> (apvts, "master", master.slider);

    // tracks
    for (int t = 0; t < bash::numTracks; ++t)
    {
        auto& b = trackButtons[(size_t) t];
        b.setButtonText ("TRACK " + juce::String (t + 1));
        b.onClick = [this, t] { selectTrack (t); };
        addAndMakeVisible (b);
    }
    addAndMakeVisible (stepView);
    regenButton.onClick = [this] { proc.regenerate (currentTrack); stepView.refresh(); };
    clearButton.onClick = [this] { proc.clearLocks (currentTrack); stepView.refresh(); };
    regenButton.setColour (juce::TextButton::buttonColourId, colour::coral.withAlpha (0.85f));
    addAndMakeVisible (regenButton);
    addAndMakeVisible (clearButton);
    hint.setText ("Click a step to lock it so Regenerate keeps it. Right-click or hold to mute or unmute a step.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colour::quiet);
    hint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint);

    // per-track controls
    addAndMakeVisible (onButton);
    addAndMakeVisible (scaleLockButton);
    for (auto* b : { &engineBox, &algoBox, &cycleBox, &directionBox, &rateBox }) addAndMakeVisible (*b);
    styleComboLabel (engineLabel, "ENGINE");
    addAndMakeVisible (engineLabel);
    addChildComponent (samplerPanel);
    styleComboLabel (algoLabel, "ALGORITHM");
    styleComboLabel (rateLabel, "RATE");
    styleComboLabel (directionLabel, "DIRECTION");
    styleComboLabel (cycleLabel, "CYCLE");
    for (auto* l : { &algoLabel, &rateLabel, &directionLabel, &cycleLabel }) addAndMakeVisible (*l);

    const std::vector<std::pair<const char*, const char*>> seqDefs {
        { "type", "TYPE" }, { "density", "DENSITY" }, { "steps", "STEPS" }, { "offset", "OFFSET" },
        { "accent", "ACCENT" }, { "gate", "GATE" }, { "octave", "OCTAVE" } };
    const std::vector<std::pair<const char*, const char*>> voiceDefs {
        { "wave", "WAVE" }, { "cutoff", "CUTOFF" }, { "reso", "RESO" }, { "envmod", "ENV MOD" },
        { "decay", "DECAY" }, { "drive", "DRIVE" }, { "glide", "GLIDE" }, { "level", "LEVEL" }, { "pan", "PAN" } };

    for (auto& [id, name] : seqDefs)
    {
        auto k = std::make_unique<Knob>();
        styleKnob (*k, name);
        addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
        seqKnobs.push_back (std::move (k)); seqKnobIds.push_back (id);
    }
    for (auto& [id, name] : voiceDefs)
    {
        auto k = std::make_unique<Knob>();
        styleKnob (*k, name);
        addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
        voiceKnobs.push_back (std::move (k)); voiceKnobIds.push_back (id);
    }

    selectTrack (0);
    setSize (editorW, editorH);
    startTimerHz (30);
}

BashEditor::~BashEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BashEditor::selectTrack (int t)
{
    currentTrack = t;
    for (int i = 0; i < bash::numTracks; ++i)
        trackButtons[(size_t) i].setToggleState (i == t, juce::dontSendNotification);
    bindTrack();
    stepView.setTrack (t);
    samplerPanel.setTrack (t);
    updateEnginePanel();
}

void BashEditor::bindTrack()
{
    auto& apvts = proc.apvts;
    knobAtts.clear(); comboAtts.clear(); buttonAtts.clear();

    auto id = [this] (const char* s) { return BashProcessor::pid (currentTrack, s); };

    fillChoices (engineBox, apvts, id ("engine"));
    comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id ("engine"), engineBox));
    fillChoices (algoBox, apvts, id ("algo"));
    fillChoices (rateBox, apvts, id ("rate"));
    fillChoices (directionBox, apvts, id ("direction"));
    fillChoices (cycleBox, apvts, id ("cycle"));
    comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id ("algo"), algoBox));
    comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id ("rate"), rateBox));
    comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id ("direction"), directionBox));
    comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id ("cycle"), cycleBox));
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, id ("on"), onButton));
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, id ("scalelock"), scaleLockButton));

    for (size_t i = 0; i < seqKnobs.size(); ++i)
        knobAtts.push_back (std::make_unique<SliderAttachment> (apvts, id (seqKnobIds[i].toRawUTF8()), seqKnobs[i]->slider));
    for (size_t i = 0; i < voiceKnobs.size(); ++i)
        knobAtts.push_back (std::make_unique<SliderAttachment> (apvts, id (voiceKnobIds[i].toRawUTF8()), voiceKnobs[i]->slider));
}

void BashEditor::updateEnginePanel()
{
    shownEngine = proc.engineType (currentTrack);
    const bool slicer = shownEngine == bash::EngineType::Slicer;
    samplerPanel.setVisible (slicer);
    for (auto& k : voiceKnobs) { k->slider.setVisible (! slicer); k->label.setVisible (! slicer); }
    repaint();
}

void BashEditor::timerCallback()
{
    if (proc.engineType (currentTrack) != shownEngine)
        updateEnginePanel();
    if (samplerPanel.isVisible())
        samplerPanel.refresh();
    stepView.refresh();
    for (int t = 0; t < bash::numTracks; ++t)
    {
        const bool on = *proc.apvts.getRawParameterValue (BashProcessor::pid (t, "on")) > 0.5f;
        auto& b = trackButtons[(size_t) t];
        b.setColour (juce::TextButton::buttonColourId, on ? colour::mint : colour::panel);
    }
}

void BashEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    g.setColour (colour::ink);
    g.setFont (juce::FontOptions (36.0f, juce::Font::bold));
    g.drawText ("BASH!", margin, 14, 160, 40, juce::Justification::centredLeft);
    g.setColour (colour::quiet);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("an all-in-one kalabash solution", margin + 2, 52, 260, 18, juce::Justification::centredLeft);

    for (auto [r, title] : { std::pair { seqPanel, "SEQUENCER" }, std::pair { voicePanel, shownEngine == bash::EngineType::Slicer ? "SLICE SAMPLER" : "VOICE  (acid test synth)" } })
    {
        g.setColour (colour::panel);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (colour::line);
        g.drawRoundedRectangle (r.toFloat(), 14.0f, 1.0f);
        g.setColour (colour::ink);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 16, r.getY() + 10, 300, 16, juce::Justification::centredLeft);
    }
}

void BashEditor::resized()
{
    auto area = getLocalBounds().reduced (margin, 0);

    // header (right side)
    auto header = area.removeFromTop (84).withTrimmedTop (12);
    placeKnob (master, header.removeFromRight (72));
    header.removeFromRight (8);
    placeKnob (tempo, header.removeFromRight (72));
    header.removeFromRight (12);
    auto row = header.withSizeKeepingCentre (header.getWidth(), 32);
    playButton.setBounds (row.removeFromRight (84)); row.removeFromRight (8);
    syncButton.setBounds (row.removeFromRight (84)); row.removeFromRight (16);
    scaleBox.setBounds (row.removeFromRight (170)); row.removeFromRight (8);
    keyBox.setBounds (row.removeFromRight (72));

    // track selector
    auto tracksRow = area.removeFromTop (44).withTrimmedTop (4);
    const int bw = (tracksRow.getWidth() - 7 * 8) / bash::numTracks;
    for (auto& b : trackButtons) { b.setBounds (tracksRow.removeFromLeft (bw)); tracksRow.removeFromLeft (8); }

    // steps
    area.removeFromTop (12);
    stepView.setBounds (area.removeFromTop (176));
    auto buttons = area.removeFromTop (44).withTrimmedTop (8);
    regenButton.setBounds (buttons.removeFromLeft (130));
    buttons.removeFromLeft (8);
    clearButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (16);
    hint.setBounds (buttons);

    // sequencer panel
    area.removeFromTop (12);
    seqPanel = area.removeFromTop (224);
    auto inner = seqPanel.reduced (16).withTrimmedTop (22);
    auto combos = inner.removeFromTop (52);
    onButton.setBounds (combos.removeFromLeft (110).removeFromBottom (30));
    combos.removeFromLeft (12);
    {
        auto r = combos.removeFromLeft (110);
        engineLabel.setBounds (r.removeFromTop (18));
        engineBox.setBounds (r.removeFromTop (30));
        combos.removeFromLeft (12);
    }
    auto placeCombo = [&combos] (juce::Label& l, juce::ComboBox& b, int w)
    {
        auto r = combos.removeFromLeft (w);
        l.setBounds (r.removeFromTop (18));
        b.setBounds (r.removeFromTop (30));
        combos.removeFromLeft (12);
    };
    placeCombo (algoLabel, algoBox, 150);
    placeCombo (rateLabel, rateBox, 100);
    placeCombo (directionLabel, directionBox, 130);
    placeCombo (cycleLabel, cycleBox, 100);
    scaleLockButton.setBounds (combos.removeFromLeft (120).removeFromBottom (30));

    inner.removeFromTop (12);
    for (auto& k : seqKnobs) { placeKnob (*k, inner.removeFromLeft (84)); inner.removeFromLeft (8); }

    // voice panel
    area.removeFromTop (12);
    voicePanel = area.removeFromTop (278);
    samplerPanel.setBounds (voicePanel.reduced (16).withTrimmedTop (22));
    auto vInner = voicePanel.reduced (16).withTrimmedTop (18).withHeight (100);
    for (auto& k : voiceKnobs) { placeKnob (*k, vInner.removeFromLeft (84)); vInner.removeFromLeft (8); }
}
