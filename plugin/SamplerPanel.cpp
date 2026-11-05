#include "SamplerPanel.h"

using namespace bashui;

namespace
{
    const juce::StringArray audioExtensions { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a", ".caf" };
    const int gridChoices[] = { 4, 8, 16, 32, 64, 128, 256 };
}

// ============================================================================
void WaveformView::rebuildPeaks()
{
    peakMin.clear(); peakMax.clear();
    auto ref = proc.sampleRef (track);
    if (ref == nullptr) return;

    const int buckets = 2048;
    const uint32_t n = ref->numFrames();
    peakMin.assign (buckets, 0.0f);
    peakMax.assign (buckets, 0.0f);
    for (int b = 0; b < buckets; ++b)
    {
        const uint32_t a = (uint32_t) ((uint64_t) n * (uint64_t) b / buckets);
        const uint32_t e = std::max (a + 1, (uint32_t) ((uint64_t) n * (uint64_t) (b + 1) / buckets));
        float lo = 0.0f, hi = 0.0f;
        for (uint32_t i = a; i < e && i < n; ++i)
        {
            const float v = ref->mono (i);
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        peakMin[(size_t) b] = lo; peakMax[(size_t) b] = hi;
    }
}

void WaveformView::refresh()
{
    const auto* now = proc.sample (track);
    if (now != shown) { shown = now; rebuildPeaks(); }
    slices    = proc.slices (track);
    params    = proc.seqParams (track);
    playing   = proc.playingSlice (track);
    playFrame = proc.playingFrame (track);
    repaint();
}

float WaveformView::xForFrame (double frame) const
{
    const double n = shown != nullptr ? (double) shown->numFrames() : 1.0;
    return (float) (frame / std::max (1.0, n) * getWidth());
}

uint32_t WaveformView::frameForX (float x) const
{
    if (shown == nullptr) return 0;
    const double f = juce::jlimit (0.0, 1.0, (double) x / std::max (1, getWidth())) * (shown->numFrames() - 1);
    return (uint32_t) f;
}

int WaveformView::markerNear (float x) const
{
    int best = -1; float bestDist = 6.0f;
    for (int i = 0; i < (int) slices.size(); ++i)
    {
        const float d = std::abs (xForFrame (slices[(size_t) i]) - x);
        if (d < bestDist) { best = i; bestDist = d; }
    }
    return best;
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colour::iceBlue.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (dropHover ? colour::coral : colour::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, dropHover ? 2.0f : 1.0f);

    if (shown == nullptr || peakMin.empty())
    {
        g.setColour (colour::quiet);
        g.setFont (juce::FontOptions (14.0f));
        const auto missing = proc.missingSample (track);
        g.drawText (missing.isNotEmpty() ? "Sample not found: " + missing + "  (drop it here again)"
                                         : "Drop an audio file here, or click to browse",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto n = shown->numFrames();
    auto sliceEnd = [&] (size_t i) { return i + 1 < slices.size() ? (double) slices[i + 1] : (double) n; };

    // window region
    if (params.sliceMode == bash::SliceMode::Window && ! slices.empty())
    {
        const size_t ws = std::min<size_t> (params.windowStart, slices.size() - 1);
        const size_t we = std::min<size_t> (ws + params.windowSize, slices.size());
        g.setColour (colour::mint.withAlpha (0.8f));
        const float x0 = xForFrame (slices[ws]), x1 = xForFrame (sliceEnd (we - 1));
        g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, (float) getHeight()));
    }

    // playing slice
    if (playing >= 0 && playing < (int) slices.size() && playFrame >= 0.0)
    {
        g.setColour (colour::coral.withAlpha (0.18f));
        const float x0 = xForFrame (slices[(size_t) playing]), x1 = xForFrame (sliceEnd ((size_t) playing));
        g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, (float) getHeight()));
    }

    // waveform
    const float mid = r.getCentreY(), half = r.getHeight() * 0.44f;
    g.setColour (colour::periwinkle.darker (0.25f));
    const int w = getWidth();
    for (int x = 0; x < w; ++x)
    {
        const int b0 = (int) ((int64_t) x * (int64_t) peakMin.size() / w);
        const int b1 = std::max (b0 + 1, (int) ((int64_t) (x + 1) * (int64_t) peakMin.size() / w));
        float lo = 0.0f, hi = 0.0f;
        for (int b = b0; b < b1 && b < (int) peakMin.size(); ++b) { lo = std::min (lo, peakMin[(size_t) b]); hi = std::max (hi, peakMax[(size_t) b]); }
        g.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.0f);
    }

    // slice markers
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    float lastLabel = -100.0f;
    for (size_t i = 0; i < slices.size(); ++i)
    {
        const float x = xForFrame (slices[i]);
        g.setColour (colour::ink.withAlpha ((int) i == dragIndex ? 1.0f : 0.55f));
        g.drawLine (x, 0.0f, x, (float) getHeight(), (int) i == dragIndex ? 2.0f : 1.0f);
        if (x - lastLabel > 18.0f)
        {
            g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (x + 2.0f, 2.0f, 24.0f, 12.0f), juce::Justification::centredLeft);
            lastLabel = x;
        }
    }

    // playhead
    if (playFrame >= 0.0)
    {
        g.setColour (colour::coral);
        const float x = xForFrame (playFrame);
        g.drawLine (x, 0.0f, x, (float) getHeight(), 2.0f);
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (shown == nullptr) { if (onBrowse) onBrowse(); return; }

    const int i = markerNear (e.position.x);
    if (e.mods.isPopupMenu())
    {
        if (i > 0)   // slice 1 always starts at 0
        {
            slices.erase (slices.begin() + i);
            proc.setSlices (track, slices);
            refresh();
        }
        return;
    }
    dragIndex = i > 0 ? i : -1;
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex <= 0 || dragIndex >= (int) slices.size()) return;
    const uint32_t lo = slices[(size_t) dragIndex - 1] + 32;
    const uint32_t hi = (size_t) dragIndex + 1 < slices.size() ? slices[(size_t) dragIndex + 1] - 32 : shown->numFrames() - 32;
    if (hi <= lo) return;
    slices[(size_t) dragIndex] = juce::jlimit (lo, hi, frameForX (e.position.x));
    proc.setSlices (track, slices);
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    dragIndex = -1;
    refresh();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (shown == nullptr || markerNear (e.position.x) >= 0) return;
    if (slices.size() >= (size_t) bash::maxSlices)
    {
        if (onMessage) onMessage ("256 slices is the maximum");
        return;
    }
    slices.push_back (frameForX (e.position.x));
    proc.setSlices (track, slices);
    refresh();
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (shown != nullptr && markerNear (e.position.x) > 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                                      : juce::MouseCursor::NormalCursor);
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        for (auto& ext : audioExtensions)
            if (f.endsWithIgnoreCase (ext)) return true;
    return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (auto& f : files)
    {
        const auto err = proc.loadSample (track, juce::File (f));
        if (onMessage) onMessage (err);
        if (err.isEmpty()) break;
    }
    shown = nullptr;
    refresh();
}

// ============================================================================
SamplerPanel::SamplerPanel (BashProcessor& p) : proc (p), wave (p)
{
    addAndMakeVisible (wave);
    wave.onBrowse  = [this] { browse(); };
    wave.onMessage = [this] (const juce::String& m) { showMessage (m); };

    for (auto* b : { &loadButton, &transientButton, &gridButton, &randomButton, &clearButton })
        addAndMakeVisible (*b);

    sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivity.setRange (0.0, 100.0, 1.0);
    sensitivity.setValue (50.0, juce::dontSendNotification);
    sensitivity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
    sensitivity.setColour (juce::Slider::trackColourId, colour::periwinkle);
    sensitivity.setColour (juce::Slider::thumbColourId, colour::coral);
    sensitivity.setColour (juce::Slider::backgroundColourId, colour::line);
    sensitivity.setColour (juce::Slider::textBoxTextColourId, colour::ink);
    sensitivity.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    sensitivity.setTooltip ("Transient sensitivity");
    addAndMakeVisible (sensitivity);

    randomCount.setSliderStyle (juce::Slider::LinearHorizontal);
    randomCount.setRange (2.0, 256.0, 1.0);
    randomCount.setValue (16.0, juce::dontSendNotification);
    randomCount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
    for (auto id : { juce::Slider::trackColourId, juce::Slider::thumbColourId, juce::Slider::backgroundColourId })
        randomCount.setColour (id, sensitivity.findColour (id));
    randomCount.setColour (juce::Slider::textBoxTextColourId, colour::ink);
    randomCount.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    randomCount.setTooltip ("How many random slices");
    addAndMakeVisible (randomCount);

    for (int i = 0; i < 7; ++i) gridDivisions.addItem (juce::String (gridChoices[i]), i + 1);
    gridDivisions.setSelectedId (3, juce::dontSendNotification);
    addAndMakeVisible (gridDivisions);

    info.setFont (juce::FontOptions (12.0f));
    info.setColour (juce::Label::textColourId, colour::quiet);
    info.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (info);

    loadButton.onClick = [this] { browse(); };
    transientButton.onClick = [this]
    {
        if (auto ref = proc.sampleRef (track))
            proc.setSlices (track, bash::slicing::transients (*ref, (float) sensitivity.getValue() / 100.0f));
        refresh();
    };
    gridButton.onClick = [this]
    {
        if (auto ref = proc.sampleRef (track))
            proc.setSlices (track, bash::slicing::grid (ref->numFrames(), gridChoices[juce::jlimit (0, 6, gridDivisions.getSelectedId() - 1)]));
        refresh();
    };
    randomButton.onClick = [this]
    {
        if (auto ref = proc.sampleRef (track))
            proc.setSlices (track, bash::slicing::random (ref->numFrames(), (int) randomCount.getValue(),
                                                          (uint64_t) juce::Random::getSystemRandom().nextInt64(), ref->sampleRate));
        refresh();
    };
    clearButton.onClick = [this] { proc.setSlices (track, { 0 }); refresh(); };

    styleComboLabel (modeLabel, "SLICE MODE");
    addAndMakeVisible (modeLabel);
    addAndMakeVisible (modeBox);
    styleKnob (winStart, "WIN START");
    styleKnob (winSize, "WIN SIZE");
    styleKnob (tune, "TUNE");
    styleKnob (attack, "ATTACK");
    styleKnob (release, "RELEASE");
    styleKnob (level, "LEVEL");
    styleKnob (pan, "PAN");
    for (auto* k : { &winStart, &winSize, &tune, &attack, &release, &level, &pan })
    {
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
    }
    addAndMakeVisible (pitchFollow);
    addAndMakeVisible (reverse);
}

void SamplerPanel::setTrack (int t)
{
    track = t;
    auto& apvts = proc.apvts;
    knobAtts.clear(); buttonAtts.clear(); modeAtt.reset();
    auto id = [t] (const char* s) { return BashProcessor::pid (t, s); };

    fillChoices (modeBox, apvts, id ("slicemode"));
    modeAtt = std::make_unique<ComboAttachment> (apvts, id ("slicemode"), modeBox);
    const std::pair<Knob*, const char*> knobs[] = {
        { &winStart, "winstart" }, { &winSize, "winsize" }, { &tune, "tune" }, { &attack, "sattack" },
        { &release, "srelease" }, { &level, "level" }, { &pan, "pan" } };
    for (auto& [k, pidName] : knobs)
        knobAtts.push_back (std::make_unique<SliderAttachment> (apvts, id (pidName), k->slider));
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, id ("pitchfollow"), pitchFollow));
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, id ("reverse"), reverse));

    wave.setTrack (t);
    refresh();
}

void SamplerPanel::refresh()
{
    wave.refresh();
    juce::String text;
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 4000)
        text = message;
    else if (auto ref = proc.sampleRef (track))
        text = juce::File (juce::String (ref->path)).getFileName() + "   |   "
             + juce::String ((int) proc.slices (track).size()) + " slices   |   "
             + juce::String (ref->numFrames() / ref->sampleRate, 2) + " s";
    else
        text = "No sample loaded";
    info.setText (text, juce::dontSendNotification);
}

void SamplerPanel::showMessage (const juce::String& m)
{
    message = m;
    messageTime = juce::Time::getMillisecondCounter();
    refresh();
}

void SamplerPanel::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into track " + juce::String (track + 1),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadFile (fc.getResult());
                          });
}

void SamplerPanel::loadFile (const juce::File& f)
{
    showMessage (proc.loadSample (track, f));
    wave.setTrack (track);
}

void SamplerPanel::resized()
{
    auto area = getLocalBounds();
    wave.setBounds (area.removeFromTop (100));
    area.removeFromTop (8);

    auto tools = area.removeFromTop (30);
    loadButton.setBounds (tools.removeFromLeft (70)); tools.removeFromLeft (12);
    transientButton.setBounds (tools.removeFromLeft (96)); tools.removeFromLeft (4);
    sensitivity.setBounds (tools.removeFromLeft (120)); tools.removeFromLeft (12);
    gridButton.setBounds (tools.removeFromLeft (60)); tools.removeFromLeft (4);
    gridDivisions.setBounds (tools.removeFromLeft (70)); tools.removeFromLeft (12);
    randomButton.setBounds (tools.removeFromLeft (80)); tools.removeFromLeft (4);
    randomCount.setBounds (tools.removeFromLeft (120)); tools.removeFromLeft (12);
    clearButton.setBounds (tools.removeFromLeft (64)); tools.removeFromLeft (12);
    info.setBounds (tools);

    area.removeFromTop (8);
    auto mode = area.removeFromLeft (140);
    modeLabel.setBounds (mode.removeFromTop (18));
    modeBox.setBounds (mode.removeFromTop (30));
    area.removeFromLeft (8);
    for (auto* k : { &winStart, &winSize, &tune, &attack, &release, &level, &pan })
    {
        placeKnob (*k, area.removeFromLeft (80));
        area.removeFromLeft (4);
    }
    area.removeFromLeft (8);
    auto toggles = area.removeFromLeft (130);
    pitchFollow.setBounds (toggles.removeFromTop (30));
    toggles.removeFromTop (8);
    reverse.setBounds (toggles.removeFromTop (30));
}

void SamplerPanel::forEachKnob (const std::function<void (Knob&, const juce::String&)>& f)
{
    const std::pair<Knob*, const char*> knobs[] = {
        { &winStart, "winstart" }, { &winSize, "winsize" }, { &tune, "tune" }, { &attack, "sattack" },
        { &release, "srelease" }, { &level, "level" }, { &pan, "pan" } };
    for (auto& [k, id] : knobs) f (*k, BashProcessor::pid (track, id));
}
