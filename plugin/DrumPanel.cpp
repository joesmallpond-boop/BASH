#include "DrumPanel.h"

using namespace bashui;

namespace
{
    const juce::StringArray audioExtensions { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a", ".caf" };

    void setupKnob (Knob& k, const juce::String& name, double lo, double hi, double centre, int decimals,
                    const juce::String& suffix = {}, double interval = 0.0)
    {
        styleKnob (k, name);
        k.slider.setRange (lo, hi, interval);
        if (centre > lo && centre < hi) k.slider.setSkewFactorFromMidPoint (centre);
        k.slider.textFromValueFunction = [decimals, suffix] (double v) { return juce::String (v, decimals) + suffix; };
        k.slider.updateText();
    }
}

// ============================================================================
juce::Rectangle<float> DrumGrid::cellBounds (int c) const
{
    const float w = (float) getWidth() / (float) bash::gridSize, h = (float) getHeight() / (float) bash::gridSize;
    return { (float) (c % bash::gridSize) * w, (float) (c / bash::gridSize) * h, w, h };
}

int DrumGrid::cellAt (juce::Point<float> p) const
{
    for (int c = 0; c < bash::numCells; ++c)
        if (cellBounds (c).contains (p)) return c;
    return -1;
}

void DrumGrid::refresh()
{
    params = proc.uiSeqParams (track);
    const int playing = proc.engineType (track) == bash::EngineType::Drums ? proc.playingSlice (track) : -1;
    if (playing != flash) { flash = playing; flashTime = juce::Time::getMillisecondCounter(); }
    repaint();
}

void DrumGrid::paint (juce::Graphics& g)
{
    const bool drumTrack = proc.engineType (track) == bash::EngineType::Drums;
    const int ws = (int) params.windowStart;
    const int wz = params.sliceMode == bash::SliceMode::Groove ? std::min (8, (int) params.windowSize) : (int) params.windowSize;
    const bool windowed = drumTrack && (params.sliceMode == bash::SliceMode::Window || params.sliceMode == bash::SliceMode::Groove);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));

    for (int c = 0; c < bash::numCells; ++c)
    {
        auto r = cellBounds (c).reduced (3.0f);
        const auto cellData = proc.cell (c);
        juce::Colour fill = colour::panel;
        if (cellData.kind == bash::DrumCell::Kind::Fm)     fill = colour::periwinkle.withAlpha (0.45f);
        if (cellData.kind == bash::DrumCell::Kind::Sample) fill = colour::mint;
        if (c == flash && juce::Time::getMillisecondCounter() - flashTime < 150) fill = colour::coral;
        g.setColour (fill);
        g.fillRoundedRectangle (r, 8.0f);

        const bool inWindow = windowed && c >= ws && c < ws + wz;
        g.setColour (c == hover ? colour::coral : c == selected ? colour::ink : inWindow ? colour::coral.withAlpha (0.6f) : colour::line);
        g.drawRoundedRectangle (r, 8.0f, c == selected || c == hover || inWindow ? 2.0f : 1.0f);

        const auto name = proc.cellName (c);
        g.setColour (colour::ink.withAlpha (name.isEmpty() ? 0.3f : 0.9f));
        g.drawFittedText (name.isEmpty() ? juce::String (c + 1) : name, r.reduced (4.0f).toNearestInt(),
                          juce::Justification::centred, 2, 0.8f);
    }
}

void DrumGrid::mouseDown (const juce::MouseEvent& e)
{
    const int c = cellAt (e.position);
    if (c < 0) return;
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Clear pad");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, c] (int r)
        {
            if (r == 1) { proc.clearCell (c); if (onSelect) onSelect (c); }
        });
        return;
    }
    selected = c;
    proc.auditionCell (c);
    if (onSelect) onSelect (c);
    repaint();
}

bool DrumGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        for (auto& ext : audioExtensions)
            if (f.endsWithIgnoreCase (ext)) return true;
    return false;
}

void DrumGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    int c = cellAt ({ (float) x, (float) y });
    hover = -1;
    if (c < 0) return;
    const int first = c;
    for (auto& f : files)
    {
        if (c >= bash::numCells) break;
        const auto err = proc.loadCellSample (c, juce::File (f));
        if (err.isNotEmpty()) { if (onMessage) onMessage (err); continue; }
        ++c;
    }
    selected = first;
    if (onSelect) onSelect (first);
    repaint();
}

// ============================================================================
void CellWaveform::setCell (int c)
{
    cell = c;
    peaks.clear();
    const auto cd = proc.cell (c);
    start = cd.start; end = cd.end;
    if (auto s = proc.cellSample (c))
    {
        const int buckets = 512;
        const uint32_t n = s->numFrames();
        peaks.assign (buckets, 0.0f);
        for (int b = 0; b < buckets; ++b)
        {
            const uint32_t a = (uint32_t) ((uint64_t) n * (uint64_t) b / buckets);
            const uint32_t e = std::max (a + 1, (uint32_t) ((uint64_t) n * (uint64_t) (b + 1) / buckets));
            float m = 0.0f;
            for (uint32_t i = a; i < e && i < n; ++i) m = std::max (m, std::abs (s->mono (i)));
            peaks[(size_t) b] = m;
        }
    }
    repaint();
}

void CellWaveform::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colour::iceBlue.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 8.0f);
    if (peaks.empty()) return;

    const float w = r.getWidth(), mid = r.getCentreY(), half = r.getHeight() * 0.45f;
    g.setColour (colour::mint);
    g.fillRect (juce::Rectangle<float> (start * w, 0.0f, (end - start) * w, r.getHeight()));
    g.setColour (colour::periwinkle.darker (0.3f));
    for (int x = 0; x < (int) w; ++x)
    {
        const float v = peaks[(size_t) std::min ((int) peaks.size() - 1, (int) ((float) x / w * (float) peaks.size()))];
        g.drawVerticalLine (x, mid - v * half, mid + v * half + 1.0f);
    }
    g.setColour (colour::coral);
    g.fillRect (juce::Rectangle<float> (start * w - 1.5f, 0.0f, 3.0f, r.getHeight()));
    g.fillRect (juce::Rectangle<float> (end * w - 1.5f, 0.0f, 3.0f, r.getHeight()));
}

void CellWaveform::mouseDown (const juce::MouseEvent& e)
{
    const float x = e.position.x / (float) std::max (1, getWidth());
    dragging = std::abs (x - start) < std::abs (x - end) ? 1 : 2;
    mouseDrag (e);
}

void CellWaveform::mouseDrag (const juce::MouseEvent& e)
{
    const float x = juce::jlimit (0.0f, 1.0f, e.position.x / (float) std::max (1, getWidth()));
    if (dragging == 1) start = std::min (x, end - 0.001f);
    else               end   = std::max (x, start + 0.001f);
    if (onRangeChanged) onRangeChanged (start, end);
    repaint();
}

// ============================================================================
DrumPanel::DrumPanel (BashProcessor& p) : proc (p), grid (p), wave (p)
{
    addAndMakeVisible (grid);
    grid.onSelect  = [this] (int c) { selectCell (c); };
    grid.onMessage = [this] (const juce::String& m) { showMessage (m); };
    addChildComponent (wave);
    wave.onRangeChanged = [this] (float s, float e)
    {
        updating = true;
        sStart.slider.setValue (s, juce::dontSendNotification);
        sEnd.slider.setValue (e, juce::dontSendNotification);
        updating = false;
        pushToCell();
    };

    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colour::ink);
    addAndMakeVisible (title);
    info.setFont (juce::FontOptions (12.0f));
    info.setColour (juce::Label::textColourId, colour::quiet);
    addAndMakeVisible (info);

    for (auto* b : { &sampleButton, &fmButton, &clearButton }) addAndMakeVisible (*b);
    addChildComponent (loadButton);
    sampleButton.onClick = [this] { browse(); };
    loadButton.onClick   = [this] { browse(); };
    fmButton.onClick     = [this] { proc.setCellFm (cell, bash::fmStartPoints()[0].preset); selectCell (cell); proc.auditionCell (cell); };
    clearButton.onClick  = [this] { proc.clearCell (cell); selectCell (cell); };

    kitBox.setTextWhenNothingSelected ("Factory kits");
    for (int k = 0; k < (int) bash::factoryKits().size(); ++k) kitBox.addItem (bash::factoryKits()[(size_t) k].name, k + 1);
    kitBox.onChange = [this]
    {
        if (kitBox.getSelectedId() > 0)
        {
            proc.loadFactoryKit (kitBox.getSelectedId() - 1);
            kitBox.setSelectedId (0, juce::dontSendNotification);
            selectCell (cell);
            showMessage ("Loaded kit");
        }
    };
    addAndMakeVisible (kitBox);

    presetBox.setTextWhenNothingSelected ("All FM sounds");
    for (int i = 0; i < (int) bash::fmPresets().size(); ++i) presetBox.addItem (bash::fmPresets()[(size_t) i].name, i + 1);
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() > 0)
        {
            proc.setCellFm (cell, presetBox.getSelectedId() - 1);
            presetBox.setSelectedId (0, juce::dontSendNotification);
            selectCell (cell);
            proc.auditionCell (cell);
        }
    };
    addChildComponent (presetBox);

    for (auto& sp : bash::fmStartPoints())
    {
        auto b = std::make_unique<juce::TextButton> (sp.name);
        const int preset = sp.preset;
        const juce::String name = sp.name;
        b->onClick = [this, preset, name]
        {
            // load the template's sound, keep the pad's pan
            const auto before = proc.cell (cell);
            proc.setCellFm (cell, preset);
            if (before.kind == bash::DrumCell::Kind::Fm)
            {
                auto c = proc.cell (cell);
                c.pan = before.pan;
                proc.setCell (cell, c);
            }
            proc.setCellName (cell, name);
            selectCell (cell);
            proc.auditionCell (cell);
        };
        b->setColour (juce::TextButton::buttonColourId, colour::iceBlue.withAlpha (0.5f));
        addChildComponent (*b);
        startPoints.push_back (std::move (b));
    }

    setupKnob (sStart,   "START",    0.0, 1.0, 0.0, 3);
    setupKnob (sEnd,     "END",      0.0, 1.0, 0.0, 3);
    setupKnob (sFadeIn,  "FADE IN",  0.0, 0.5, 0.02, 3, " s");
    setupKnob (sFadeOut, "FADE OUT", 0.001, 2.0, 0.05, 3, " s");
    setupKnob (sAttack,  "ATTACK",   -1.0, 1.0, 0.0, 2);
    setupKnob (sSustain, "SUSTAIN",  -1.0, 1.0, 0.0, 2);
    setupKnob (sTune,    "TUNE",     -24.0, 24.0, 0.0, 1, " st");
    setupKnob (sCutoff,  "CUTOFF",   40.0, 20000.0, 1500.0, 0, " Hz");
    setupKnob (fPitch,     "PITCH",   20.0, 5000.0, 200.0, 0, " Hz");
    setupKnob (fSweep,     "SWEEP",   0.0, 48.0, 0.0, 1, " st");
    setupKnob (fSweepTime, "SWEEP T", 0.002, 0.5, 0.05, 3, " s");
    setupKnob (fRatio,     "RATIO",   0.25, 8.0, 1.5, 2);
    setupKnob (fAmt,       "FM AMT",  0.0, 10.0, 2.0, 2);
    setupKnob (fFmDecay,   "FM DEC",  0.002, 1.0, 0.05, 3, " s");
    setupKnob (fNoise,     "NOISE",   0.0, 1.0, 0.0, 2);
    setupKnob (fTone,      "TONE",    0.0, 1.0, 0.0, 2);
    setupKnob (fNoiseDecay,"N. DECAY",0.005, 2.0, 0.1, 3, " s");
    setupKnob (fBursts,    "BURSTS",  1.0, 4.0, 0.0, 0, {}, 1.0);
    setupKnob (fDecay,     "DECAY",   0.001, 2.5, 0.3, 3, " s");
    setupKnob (fClick,     "CLICK",   0.0, 1.0, 0.0, 2);
    setupKnob (fDrive,     "DRIVE",   0.0, 1.0, 0.0, 2);
    setupKnob (cGain,      "LEVEL",   0.0, 2.0, 0.0, 2);
    setupKnob (cPan,       "PAN",     -1.0, 1.0, 0.0, 2);
    setupKnob (cChoke,     "CHOKE",   0.0, 8.0, 0.0, 0, {}, 1.0);
    cChoke.slider.textFromValueFunction = [] (double v) { return v < 0.5 ? juce::String ("Off") : "Group " + juce::String ((int) v); };
    cChoke.slider.updateText();

    sampleKnobs = { &sStart, &sEnd, &sFadeIn, &sFadeOut, &sAttack, &sSustain, &sTune, &sCutoff };
    fmKnobs = { &fPitch, &fSweep, &fSweepTime, &fRatio, &fAmt, &fFmDecay, &fNoise, &fTone,
                &fNoiseDecay, &fBursts, &fDecay, &fClick, &fDrive };
    for (auto* k : sampleKnobs) { addChildComponent (k->slider); addChildComponent (k->label); }
    for (auto* k : fmKnobs)     { addChildComponent (k->slider); addChildComponent (k->label); }
    for (auto* k : { &cGain, &cPan, &cChoke }) { addChildComponent (k->slider); addChildComponent (k->label); }
    addChildComponent (sReverse);

    auto onChange = [this] { if (! updating) pushToCell(); };
    for (auto* k : sampleKnobs) k->slider.onValueChange = onChange;
    for (auto* k : fmKnobs)     k->slider.onValueChange = onChange;
    for (auto* k : { &cGain, &cPan, &cChoke }) k->slider.onValueChange = onChange;
    sReverse.onClick = onChange;
    // re-audition when a knob is released, so edits can be heard
    for (auto* k : fmKnobs)     k->slider.onDragEnd = [this] { proc.auditionCell (cell); };
    for (auto* k : sampleKnobs) k->slider.onDragEnd = [this] { proc.auditionCell (cell); };

    selectCell (0);
}

void DrumPanel::setTrack (int t)
{
    track = t;
    grid.setTrack (t);
}

void DrumPanel::selectCell (int c)
{
    cell = juce::jlimit (0, bash::numCells - 1, c);
    grid.setSelected (cell);
    pullFromCell();
    resized();
    repaint();
}

void DrumPanel::pullFromCell()
{
    const auto c = proc.cell (cell);
    updating = true;
    sStart.slider.setValue (c.start, juce::dontSendNotification);
    sEnd.slider.setValue (c.end, juce::dontSendNotification);
    sFadeIn.slider.setValue (c.fadeIn, juce::dontSendNotification);
    sFadeOut.slider.setValue (c.fadeOut, juce::dontSendNotification);
    sAttack.slider.setValue (c.attack, juce::dontSendNotification);
    sSustain.slider.setValue (c.sustain, juce::dontSendNotification);
    sTune.slider.setValue (c.tune, juce::dontSendNotification);
    sCutoff.slider.setValue (c.cutoff, juce::dontSendNotification);
    sReverse.setToggleState (c.reverse, juce::dontSendNotification);
    fPitch.slider.setValue (c.fm.pitch, juce::dontSendNotification);
    fSweep.slider.setValue (c.fm.pitchAmt, juce::dontSendNotification);
    fSweepTime.slider.setValue (c.fm.pitchTime, juce::dontSendNotification);
    fRatio.slider.setValue (c.fm.ratio, juce::dontSendNotification);
    fAmt.slider.setValue (c.fm.fmAmt, juce::dontSendNotification);
    fFmDecay.slider.setValue (c.fm.fmDecay, juce::dontSendNotification);
    fNoise.slider.setValue (c.fm.noise, juce::dontSendNotification);
    fTone.slider.setValue (c.fm.tone, juce::dontSendNotification);
    fNoiseDecay.slider.setValue (c.fm.noiseDecay, juce::dontSendNotification);
    fBursts.slider.setValue (c.fm.bursts, juce::dontSendNotification);
    fDecay.slider.setValue (c.fm.decay, juce::dontSendNotification);
    fClick.slider.setValue (c.fm.click, juce::dontSendNotification);
    fDrive.slider.setValue (c.fm.drive, juce::dontSendNotification);
    cGain.slider.setValue (c.gain, juce::dontSendNotification);
    cPan.slider.setValue (c.pan, juce::dontSendNotification);
    cChoke.slider.setValue (c.choke, juce::dontSendNotification);
    updating = false;

    shownKind = c.kind;
    const bool isSample = c.kind == bash::DrumCell::Kind::Sample, isFm = c.kind == bash::DrumCell::Kind::Fm;
    for (auto* k : sampleKnobs) { k->slider.setVisible (isSample); k->label.setVisible (isSample); }
    for (auto* k : fmKnobs)     { k->slider.setVisible (isFm); k->label.setVisible (isFm); }
    for (auto* k : { &cGain, &cPan, &cChoke }) { k->slider.setVisible (isSample || isFm); k->label.setVisible (isSample || isFm); }
    sReverse.setVisible (isSample);
    wave.setVisible (isSample);
    if (isSample) wave.setCell (cell);
    presetBox.setVisible (isFm);
    for (auto& b : startPoints) b->setVisible (isFm);

    const auto name = proc.cellName (cell);
    title.setText ("Pad " + juce::String (cell + 1) + " (note " + juce::String (bash::drumBaseNote + cell) + ")"
                   + (name.isNotEmpty() ? "   " + name : juce::String()), juce::dontSendNotification);
    sampleButton.setToggleState (isSample, juce::dontSendNotification);
    fmButton.setToggleState (isFm, juce::dontSendNotification);
    refresh();
}

void DrumPanel::pushToCell()
{
    auto c = proc.cell (cell);
    if (c.kind == bash::DrumCell::Kind::Sample)
    {
        c.start = (float) sStart.slider.getValue();  c.end = (float) sEnd.slider.getValue();
        c.fadeIn = (float) sFadeIn.slider.getValue(); c.fadeOut = (float) sFadeOut.slider.getValue();
        c.attack = (float) sAttack.slider.getValue(); c.sustain = (float) sSustain.slider.getValue();
        c.tune = (float) sTune.slider.getValue();     c.cutoff = (float) sCutoff.slider.getValue();
        c.reverse = sReverse.getToggleState();
        wave.setCell (cell);
    }
    else if (c.kind == bash::DrumCell::Kind::Fm)
    {
        c.fm.pitch = (float) fPitch.slider.getValue();         c.fm.pitchAmt = (float) fSweep.slider.getValue();
        c.fm.pitchTime = (float) fSweepTime.slider.getValue(); c.fm.ratio = (float) fRatio.slider.getValue();
        c.fm.fmAmt = (float) fAmt.slider.getValue();           c.fm.fmDecay = (float) fFmDecay.slider.getValue();
        c.fm.noise = (float) fNoise.slider.getValue();         c.fm.tone = (float) fTone.slider.getValue();
        c.fm.noiseDecay = (float) fNoiseDecay.slider.getValue(); c.fm.bursts = (int) fBursts.slider.getValue();
        c.fm.decay = (float) fDecay.slider.getValue();         c.fm.click = (float) fClick.slider.getValue();
        c.fm.drive = (float) fDrive.slider.getValue();
    }
    c.gain = (float) cGain.slider.getValue();
    c.pan = (float) cPan.slider.getValue();
    c.choke = (int) cChoke.slider.getValue();
    proc.setCell (cell, c);
}

void DrumPanel::refresh()
{
    grid.refresh();
    juce::String text;
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 4000)
        text = message;
    else if (shownKind == bash::DrumCell::Kind::Empty)
        text = "Empty pad: drop a sample on it, or press FM. Drop several files to fill several pads.";
    else
        text = "Click a pad to hear it. Drum tracks play the pads in their window (coral outline).";
    info.setText (text, juce::dontSendNotification);
}

void DrumPanel::showMessage (const juce::String& m)
{
    message = m;
    messageTime = juce::Time::getMillisecondCounter();
    refresh();
}

void DrumPanel::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into pad " + juce::String (cell + 1),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (! fc.getResult().existsAsFile()) return;
                              showMessage (proc.loadCellSample (cell, fc.getResult()));
                              selectCell (cell);
                              proc.auditionCell (cell);
                          });
}

void DrumPanel::paint (juce::Graphics&) {}

void DrumPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft (256);
    const int side = std::min (256, left.getHeight());
    grid.setBounds (left.removeFromTop (side).withWidth (side));
    area.removeFromLeft (20);

    auto header = area.removeFromTop (30);
    title.setBounds (header.removeFromLeft (250));
    for (auto* b : { &sampleButton, &fmButton, &clearButton })
    {
        b->setBounds (header.removeFromLeft (70)); header.removeFromLeft (6);
    }
    kitBox.setBounds (header.removeFromRight (150));

    info.setBounds (area.removeFromBottom (18));
    area.removeFromTop (8);

    const int knobW = area.getWidth() / 8;
    auto placeRow = [&] (juce::Rectangle<int> row, std::initializer_list<Knob*> knobs)
    {
        for (auto* k : knobs) placeKnob (*k, row.removeFromLeft (knobW).reduced (2, 0));
        return row;
    };

    if (shownKind == bash::DrumCell::Kind::Fm)
    {
        auto sp = area.removeFromTop (28);
        for (auto& b : startPoints) { b->setBounds (sp.removeFromLeft (76)); sp.removeFromLeft (6); }
        presetBox.setBounds (sp.removeFromRight (160));
        area.removeFromTop (6);
        const int rowH = area.getHeight() / 2;
        placeRow (area.removeFromTop (rowH), { &fPitch, &fSweep, &fSweepTime, &fRatio, &fAmt, &fFmDecay, &fNoise, &fTone });
        placeRow (area, { &fNoiseDecay, &fBursts, &fDecay, &fClick, &fDrive, &cGain, &cPan, &cChoke });
    }
    else if (shownKind == bash::DrumCell::Kind::Sample)
    {
        wave.setBounds (area.removeFromTop (50));
        area.removeFromTop (6);
        const int rowH = area.getHeight() / 2;
        placeRow (area.removeFromTop (rowH), { &sStart, &sEnd, &sFadeIn, &sFadeOut, &sAttack, &sSustain, &sTune, &sCutoff });
        auto rest = placeRow (area, { &cGain, &cPan, &cChoke });
        sReverse.setBounds (rest.removeFromLeft (120).withSizeKeepingCentre (110, 30));
    }
}
