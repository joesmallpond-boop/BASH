#include "PluginEditor.h"

using namespace bashui;

namespace
{
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
    setColour (juce::Slider::textBoxTextColourId, colour::accent);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colour::active);
    setColour (juce::Slider::trackColourId, colour::accent);
    setColour (juce::Slider::backgroundColourId, colour::line);
    setColour (juce::Slider::thumbColourId, colour::accent);
    setColour (juce::Slider::rotarySliderFillColourId, colour::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, colour::line);
    setColour (juce::ComboBox::textColourId, colour::accent);
    setColour (juce::ComboBox::backgroundColourId, colour::raised);
    setColour (juce::ComboBox::outlineColourId, colour::line);
    setColour (juce::ComboBox::arrowColourId, colour::accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff2a2a2a));
    setColour (juce::PopupMenu::textColourId, colour::ink);
    setColour (juce::PopupMenu::headerTextColourId, colour::quiet);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::active);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::accent);
    setColour (juce::TextButton::buttonColourId, colour::raised);
    setColour (juce::TextButton::buttonOnColourId, colour::accent);
    setColour (juce::TextButton::textColourOffId, colour::ink);
    setColour (juce::TextButton::textColourOnId, colour::background);
    setColour (juce::ToggleButton::textColourId, colour::ink);
    setColour (juce::TextEditor::backgroundColourId, colour::raised);
    setColour (juce::TextEditor::textColourId, colour::accent);
    setColour (juce::TextEditor::highlightColourId, colour::active);
    setColour (juce::TextEditor::outlineColourId, colour::line);
    setColour (juce::CaretComponent::caretColourId, colour::accent);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2a2a2a));
    setColour (juce::TooltipWindow::textColourId, colour::ink);
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xff2a2a2a));
    setColour (juce::BubbleComponent::outlineColourId, colour::line);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float start, float end, juce::Slider& s)
{
    // thick amber arc over a dark track, no pointer (design reference)
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float stroke = juce::jlimit (3.0f, 9.0f, radius * 0.24f);
    const float arcR = radius - stroke * 0.5f;
    const float angle = start + pos * (end - start);
    const auto fill = s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : colour::line;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (colour::line);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // bipolar parameters (range below and above zero) grow from the top
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (fill);
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                                        bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (down)             c = c.darker (0.15f);
    else if (highlighted) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 8.0f);
    if (! b.getToggleState())
    {
        g.setColour (colour::line);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
    }
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
    chevron.startNewSubPath (cx - 4.5f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (colour::accent);
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colour::active : (highlighted ? colour::raised.brighter (0.06f) : colour::raised));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? colour::accentDim : colour::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 14.0f, r.getCentreY() });
    g.setColour (on ? colour::accent : colour::quiet.withAlpha (0.5f));
    g.fillEllipse (dot);

    g.setColour (on ? colour::accent : colour::ink);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
}

// ============================================================================
void StepView::refresh()
{
    const auto params = proc.uiSeqParams (track);
    sliceCount = params.sliceCount;
    drumTrack = params.drumTrack;
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

        g.setColour (isPlayhead ? colour::active : colour::panel);
        g.fillRoundedRectangle (cell, 8.0f);
        g.setColour (i % 4 == 0 ? colour::line.darker (0.1f) : colour::line);
        g.drawRoundedRectangle (cell, 8.0f, 1.0f);

        auto inner = cell.reduced (5.0f);
        if (s.active)
        {
            const float frac = drumTrack
                ? juce::jmap ((float) (1 + s.chordExtra) / 4.0f, 0.12f, 0.92f)
                : sliceCount > 0
                ? juce::jmap ((float) (s.slice + 1) / (float) sliceCount, 0.12f, 0.92f)
                : juce::jlimit (0.12f, 0.92f, juce::jmap ((float) (s.note - baseNote), -12.0f, 24.0f, 0.12f, 0.92f));
            auto bar = inner.withTrimmedTop (16.0f);
            bar = bar.removeFromBottom (bar.getHeight() * frac);
            g.setColour (s.accent ? colour::accent : colour::accentDim);
            g.fillRoundedRectangle (bar, 5.0f);

            g.setColour (colour::ink);
            g.setFont (juce::FontOptions (10.5f));
            juce::String label = noteText (s.note);
            if (drumTrack)
            {
                label = proc.cellName (s.slice).substring (0, 5);
                if (label.isEmpty()) label = juce::String (s.slice + 1);
                if (s.chordExtra > 0) label << "+" << s.chordExtra;
            }
            else if (sliceCount > 0)
            {
                label = "S" + juce::String (s.slice + 1);
            }
            g.drawText (label,
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
            g.setColour (colour::accent);
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
BashView::BashView (BashProcessor& p)
    : proc (p), stepView (p), samplerPanel (p), synthPanel (p), drumPanel (p), mixerPanel (p), modPanel (p), microPanel (p), diceButton (p), stepPanel (p)
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
    regenButton.setColour (juce::TextButton::buttonColourId, colour::accent);
    regenButton.setColour (juce::TextButton::textColourOffId, colour::background);
    addAndMakeVisible (regenButton);
    addAndMakeVisible (clearButton);
    addChildComponent (clearMidiButton);
    clearMidiButton.onClick = [this] { proc.clearMotif (currentTrack); stepView.refresh(); };
    exportBar = std::make_unique<bashui::ExportBar> (proc, [this] { return currentTrack; });
    addAndMakeVisible (*exportBar);
    auto showMessage = [this] (const juce::String& m) { hint.setText (m, juce::dontSendNotification); };
    exportBar->onMessage = showMessage;
    stepView.onMessage = showMessage;
    hint.setText ("Click a step to lock it. Right-click or hold to mute it. Drop a MIDI file here to use it as the motif.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colour::quiet);
    hint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint);

    // per-track controls
    addAndMakeVisible (onButton);
    addAndMakeVisible (scaleLockButton);
    for (auto* b : { &engineBox, &algoBox, &directionBox, &rateBox }) addAndMakeVisible (*b);
    styleComboLabel (engineLabel, "ENGINE");
    addAndMakeVisible (engineLabel);
    addChildComponent (samplerPanel);
    styleComboLabel (algoLabel, "ALGORITHM");
    styleComboLabel (rateLabel, "RATE");
    styleComboLabel (directionLabel, "DIRECTION");
    styleComboLabel (cycleLabel, "CYCLE");
    for (auto* l : { &algoLabel, &rateLabel, &directionLabel }) addAndMakeVisible (*l);

    const std::vector<std::pair<const char*, const char*>> seqDefs {
        { "type", "TYPE" }, { "density", "DENSITY" }, { "steps", "STEPS" }, { "offset", "OFFSET" },
        { "accent", "ACCENT" }, { "gate", "GATE" }, { "octave", "OCTAVE" }, { "swing", "SWING" }, { "jitter", "JITTER" } };
    for (auto& [id, name] : seqDefs)
    {
        auto k = std::make_unique<Knob>();
        styleKnob (*k, name);
        addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
        seqKnobs.push_back (std::move (k)); seqKnobIds.push_back (id);
    }
    addChildComponent (synthPanel);
    addChildComponent (drumPanel);
    addChildComponent (mixerPanel);
    mixerPanel.onMessage = [this] (const juce::String& m) { hint.setText (m, juce::dontSendNotification); };
    addChildComponent (modPanel);
    addChildComponent (microPanel);
    microPanel.onMessage = [this] (const juce::String& m) { hint.setText (m, juce::dontSendNotification); };
    mixerPanel.onOpenMicro = [this] { setView (3); };
    addAndMakeVisible (diceButton);
    diceButton.currentTrack = [this] { return currentTrack; };
    diceButton.onRolled = [this] { stepView.refresh(); stepPanel.refresh(); repaint(); };
    addChildComponent (stepPanel);
    stepPanel.onMessage = [this] (const juce::String& m) { hint.setText (m, juce::dontSendNotification); };
    for (auto* b : { &genModeButton, &stepModeButton })
    {
        b->setClickingTogglesState (false);
        b->setRadioGroupId (0);
        addAndMakeVisible (*b);
    }
    genModeButton.setTooltip ("The generative acid sequencer");
    stepModeButton.setTooltip ("A step sequencer: 16 lanes, ratchets, conditions, fills");
    genModeButton.onClick  = [this] { setStepMode (false); };
    stepModeButton.onClick = [this] { setStepMode (true); };
    for (auto* b : { &trackViewButton, &mixerViewButton, &modViewButton, &microViewButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    trackViewButton.onClick = [this] { setView (0); };
    mixerViewButton.onClick = [this] { setView (1); };
    modViewButton.onClick   = [this] { setView (2); };
    microViewButton.onClick = [this] { setView (3); };
    microViewButton.setTooltip ("MICRO: loops, grains, glitches and delays on everything from the tracks, plus a phrase looper");
    trackViewButton.setToggleState (true, juce::dontSendNotification);

    selectTrack (0);
    setSize (designWidth, designHeight);
    startTimerHz (30);
}

BashView::~BashView()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

std::vector<juce::Component*> BashView::trackViewComponents()
{
    std::vector<juce::Component*> c { &stepView, &regenButton, &clearButton, &hint, &onButton, &scaleLockButton,
                                      &engineBox, &algoBox, &directionBox, &rateBox,
                                      &engineLabel, &algoLabel, &rateLabel, &directionLabel, exportBar.get(),
                                      &genModeButton, &stepModeButton, &diceButton, &stepPanel };
    for (auto& k : seqKnobs) { c.push_back (&k->slider); c.push_back (&k->label); }
    return c;
}

void BashView::applyVisibility()
{
    const bool track = view == 0;
    const bool step = shownStepMode;
    // generative-only and step-only parts of the sequencer box
    std::vector<juce::Component*> genOnly { &stepView, &regenButton, &clearButton, &algoBox, &directionBox, &scaleLockButton,
                                            &engineLabel, &algoLabel, &rateLabel, &directionLabel };
    for (auto& k : seqKnobs) { genOnly.push_back (&k->slider); genOnly.push_back (&k->label); }
    for (auto* c : trackViewComponents()) c->setVisible (track);
    if (track)
    {
        for (auto* c : genOnly) c->setVisible (! step);
        stepPanel.setVisible (step);
    }
    clearMidiButton.setVisible (track && ! step && proc.hasMotif (currentTrack));
    if (step != hintIsStep)
    {
        hintIsStep = step;
        hint.setText (step ? "Click steps to switch them on, drag across to paint, Shift-drag for velocity, right-click a step for ratchets, conditions and chance."
                           : "Click a step to lock it. Right-click or hold to mute it. Drop a MIDI file here to use it as the motif.",
                      juce::dontSendNotification);
    }
    genModeButton.setToggleState (! step, juce::dontSendNotification);
    stepModeButton.setToggleState (step, juce::dontSendNotification);
}

void BashView::setStepMode (bool step)
{
    if (auto* p = proc.apvts.getParameter (BashProcessor::pid (currentTrack, "mode")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (step ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
    shownStepMode = step;
    applyVisibility();
    resized();
    repaint();
}

void BashView::setView (int v)
{
    view = v;
    const bool mixer = v != 0;   // anything but the track view hides the track controls
    mixerView = mixer;
    trackViewButton.setToggleState (v == 0, juce::dontSendNotification);
    mixerViewButton.setToggleState (v == 1, juce::dontSendNotification);
    modViewButton.setToggleState (v == 2, juce::dontSendNotification);
    microViewButton.setToggleState (v == 3, juce::dontSendNotification);
    applyVisibility();
    mixerPanel.setVisible (v == 1);
    modPanel.setVisible (v == 2);
    microPanel.setVisible (v == 3);
    if (mixer)
    {
        samplerPanel.setVisible (false);
        synthPanel.setVisible (false);
        drumPanel.setVisible (false);
    }
    else
    {
        updateEnginePanel();
    }
    repaint();
}

void BashView::selectTrack (int t)
{
    if (view == 1 || view == 3) setView (0);
    currentTrack = t;
    for (int i = 0; i < bash::numTracks; ++i)
        trackButtons[(size_t) i].setToggleState (i == t, juce::dontSendNotification);
    bindTrack();
    stepView.setTrack (t);
    stepPanel.setTrack (t);
    shownStepMode = proc.stepMode (t);
    applyVisibility();
    resized();
    samplerPanel.setTrack (t);
    synthPanel.setTrack (t);
    drumPanel.setTrack (t);
    modPanel.setTrack (t);
    diceLocks.clear();
    for (size_t i = 0; i < seqKnobs.size(); ++i)
        diceLocks.bind (seqKnobs[i]->slider, seqKnobs[i]->label, BashProcessor::pid (t, seqKnobIds[i].toRawUTF8()));
    updateEnginePanel();
}

void BashView::bindTrack()
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
}

void BashView::updateEnginePanel()
{
    shownEngine = proc.engineType (currentTrack);
    if (mixerView) return;
    samplerPanel.setVisible (shownEngine == bash::EngineType::Slicer);
    synthPanel.setVisible (shownEngine == bash::EngineType::Synth);
    drumPanel.setVisible (shownEngine == bash::EngineType::Drums);
    repaint();
}

void BashView::timerCallback()
{
    if (proc.stepMode (currentTrack) != shownStepMode)
        setStepMode (proc.stepMode (currentTrack));   // changed by automation or a loaded session
    clearMidiButton.setVisible (view == 0 && ! shownStepMode && proc.hasMotif (currentTrack));
    if (stepPanel.isVisible())
        stepPanel.refresh();
    if (proc.engineType (currentTrack) != shownEngine)
        updateEnginePanel();
    if (samplerPanel.isVisible())
        samplerPanel.refresh();
    if (synthPanel.isVisible())
        synthPanel.refresh();
    if (drumPanel.isVisible())
        drumPanel.refresh();
    if (mixerPanel.isVisible())
        mixerPanel.refresh();
    if (modPanel.isVisible())
        modPanel.refresh();
    if (microPanel.isVisible())
        microPanel.refresh();
    stepView.refresh();
    for (int t = 0; t < bash::numTracks; ++t)
    {
        const bool on = *proc.apvts.getRawParameterValue (BashProcessor::pid (t, "on")) > 0.5f;
        auto& b = trackButtons[(size_t) t];
        b.setColour (juce::TextButton::buttonColourId, on ? colour::active : colour::panel);
    }
}

void BashView::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);

    g.setColour (colour::ink);
    g.setFont (juce::FontOptions (36.0f, juce::Font::bold));
    g.drawText ("BASH!", margin, 14, 160, 40, juce::Justification::centredLeft);
    g.setColour (colour::quiet);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("an all-in-one kalabash solution", margin + 2, 52, 260, 18, juce::Justification::centredLeft);

    if (! mixerView)
    for (auto [r, title] : { std::pair { seqPanel, "SEQUENCER" }, std::pair { voicePanel, shownEngine == bash::EngineType::Slicer ? "SLICE SAMPLER"
                                                       : shownEngine == bash::EngineType::Drums ? "DRUM KIT" : "MACHINE SYNTH" } })
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

void BashView::resized()
{
    auto area = getLocalBounds().reduced (margin, 0);

    // header (right side)
    auto header = area.removeFromTop (84).withTrimmedTop (12);
    placeKnob (master, header.removeFromRight (72));
    header.removeFromRight (8);
    placeKnob (tempo, header.removeFromRight (72));
    header.removeFromRight (12);
    auto row = header.withSizeKeepingCentre (header.getWidth(), 32);
    playButton.setBounds (row.removeFromRight (74)); row.removeFromRight (8);
    syncButton.setBounds (row.removeFromRight (74)); row.removeFromRight (12);
    scaleBox.setBounds (row.removeFromRight (124)); row.removeFromRight (8);
    keyBox.setBounds (row.removeFromRight (64));

    trackViewButton.setBounds (194, 36, 68, 28);
    mixerViewButton.setBounds (266, 36, 60, 28);
    modViewButton.setBounds (330, 36, 50, 28);
    microViewButton.setBounds (384, 36, 62, 28);

    // track selector
    auto tracksRow = area.removeFromTop (44).withTrimmedTop (4);
    const int bw = (tracksRow.getWidth() - 7 * 8) / bash::numTracks;
    for (auto& b : trackButtons) { b.setBounds (tracksRow.removeFromLeft (bw)); tracksRow.removeFromLeft (8); }

    // steps
    area.removeFromTop (12);
    mixerPanel.setBounds (area.withTrimmedBottom (14));
    modPanel.setBounds (area.withTrimmedBottom (14));
    microPanel.setBounds (area.withTrimmedBottom (14));
    const auto stepModeBox = area.withHeight (176 + 44 + 12 + 224);   // step mode: one big sequencer box
    stepView.setBounds (area.removeFromTop (176));
    auto buttons = area.removeFromTop (44).withTrimmedTop (8);
    regenButton.setBounds (buttons.removeFromLeft (130));
    buttons.removeFromLeft (8);
    clearButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    clearMidiButton.setBounds (buttons.removeFromLeft (100));
    buttons.removeFromLeft (12);
    hint.setBounds (buttons);

    // sequencer panel
    area.removeFromTop (12);
    seqPanel = area.removeFromTop (224);
    exportBar->setBounds (seqPanel.getRight() - 16 - 440, seqPanel.getY() + 6, 440, 26);
    if (shownStepMode)
    {
        seqPanel = stepModeBox;
        exportBar->setBounds (seqPanel.getRight() - 16 - 440, seqPanel.getY() + 6, 440, 26);
    }
    diceButton.setBounds (seqPanel.getX() + 108, seqPanel.getY() + 4, 32, 32);   // beside the SEQUENCER title
    genModeButton.setBounds (seqPanel.getX() + 150, seqPanel.getY() + 7, 104, 24);
    stepModeButton.setBounds (seqPanel.getX() + 256, seqPanel.getY() + 7, 60, 24);
    if (shownStepMode)
    {
        auto inner = seqPanel.reduced (16).withTrimmedTop (26);
        hint.setBounds (inner.removeFromBottom (16));
        inner.removeFromBottom (4);
        stepPanel.setBounds (inner);
        // ON / ENGINE / RATE sit in the left of the step toolbar
        auto bar = inner.withHeight (28);
        onButton.setBounds (bar.removeFromLeft (92)); bar.removeFromLeft (6);
        engineBox.setBounds (bar.removeFromLeft (100)); bar.removeFromLeft (6);
        rateBox.setBounds (bar.removeFromLeft (90));
    }
    else
    {
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
        scaleLockButton.setBounds (combos.removeFromLeft (120).removeFromBottom (30));

        inner.removeFromTop (12);
        for (auto& k : seqKnobs) { placeKnob (*k, inner.removeFromLeft (100)); inner.removeFromLeft (8); }
    }

    // voice panel
    area.removeFromTop (12);
    voicePanel = area.removeFromTop (302);
    samplerPanel.setBounds (voicePanel.reduced (16).withTrimmedTop (22));
    synthPanel.setBounds (voicePanel.reduced (16).withTrimmedTop (22));
    drumPanel.setBounds (voicePanel.reduced (16).withTrimmedTop (22));
}

// ============================================================================
BashEditor::BashEditor (BashProcessor& p) : AudioProcessorEditor (&p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (BashView::designWidth / 2, BashView::designHeight / 2,
                     BashView::designWidth * 2, BashView::designHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) BashView::designWidth / BashView::designHeight);
   #if JUCE_IOS
    setSize (BashView::designWidth * 3 / 4, BashView::designHeight * 3 / 4);
   #else
    setSize (BashView::designWidth, BashView::designHeight);
   #endif
}

void BashEditor::paint (juce::Graphics& g)
{
    g.fillAll (bashui::colour::background);
}

void BashEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / BashView::designWidth, (float) getHeight() / BashView::designHeight);
    const float x = ((float) getWidth() - BashView::designWidth * scale) * 0.5f;
    const float y = ((float) getHeight() - BashView::designHeight * scale) * 0.5f;
    view.setBounds (0, 0, BashView::designWidth, BashView::designHeight);
    view.setTransform (juce::AffineTransform::scale (scale).translated (x, y));
}
