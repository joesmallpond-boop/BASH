#include "MixerPanel.h"

using namespace bashui;

void MiniKnob::setup (const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, colour::quiet);
    label.setMinimumHorizontalScale (0.7f);
}

// ============================================================================
FxSlotUI::FxSlotUI (juce::AudioProcessorValueTreeState& s, const juce::String& p) : apvts (s), prefix (p)
{
    fillChoices (type, apvts, prefix + "type");
    typeAtt = std::make_unique<ComboAttachment> (apvts, prefix + "type", type);
    addAndMakeVisible (type);
    a.setup ("A"); b.setup ("B"); gnarl.setup ("GNARL"); mix.setup ("MIX");
    const std::pair<MiniKnob*, const char*> knobs[] = { { &a, "a" }, { &b, "b" }, { &gnarl, "gnarl" }, { &mix, "mix" } };
    for (auto& [k, id] : knobs)
    {
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        atts.push_back (std::make_unique<SliderAttachment> (apvts, prefix + id, k->slider));
    }
    gnarl.slider.setColour (juce::Slider::rotarySliderFillColourId, colour::accent);
    refreshLabels();
}

void FxSlotUI::refreshLabels()
{
    const int t = (int) *apvts.getRawParameterValue (prefix + "type");
    if (t == shownType) return;
    shownType = t;
    a.label.setText (bash::fxKnobAName ((bash::FxType) t), juce::dontSendNotification);
    b.label.setText (bash::fxKnobBName ((bash::FxType) t), juce::dontSendNotification);
    const bool on = t != 0;
    for (auto* k : { &a, &b, &gnarl, &mix }) k->slider.setEnabled (on);
}

void FxSlotUI::resized()
{
    auto r = getLocalBounds();
    type.setBounds (r.removeFromTop (24));
    r.removeFromTop (2);
    auto top = r.removeFromTop (r.getHeight() / 2);
    a.place (top.removeFromLeft (top.getWidth() / 2));
    b.place (top);
    gnarl.place (r.removeFromLeft (r.getWidth() / 2));
    mix.place (r);
}

// ============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colour::line);
    g.fillRoundedRectangle (r, 3.0f);
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
    auto lit = r.removeFromBottom (r.getHeight() * frac);
    g.setColour (db > -1.0f ? colour::accent : colour::accentDim);
    g.fillRoundedRectangle (lit, 3.0f);
}

void GrMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colour::line);
    g.fillRoundedRectangle (r, 3.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, db / 20.0f);
    g.setColour (colour::accent);
    g.fillRoundedRectangle (r.withLeft (r.getRight() - r.getWidth() * frac), 3.0f);
    g.setColour (colour::ink);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("GR " + juce::String (db, 1), getLocalBounds().reduced (3, 0), juce::Justification::centredLeft);
}

// ============================================================================
MixerPanel::MixerPanel (BashProcessor& p) : proc (p)
{
    for (int t = 0; t < bash::numTracks; ++t)
    {
        auto s = std::make_unique<Strip>();
        buildTrackStrip (*s, t);
        strips.push_back (std::move (s));
    }
    {
        auto s = std::make_unique<Strip>();
        buildReturnStrip (*s, "reta", "retalevel", "RETURN A");
        strips.push_back (std::move (s));
    }
    {
        auto s = std::make_unique<Strip>();
        buildReturnStrip (*s, "retb", "retblevel", "RETURN B");
        strips.push_back (std::move (s));
    }
    {
        auto s = std::make_unique<Strip>();
        buildMicroStrip (*s);
        strips.push_back (std::move (s));
    }
    {
        auto s = std::make_unique<Strip>();
        buildMasterStrip (*s);
        strips.push_back (std::move (s));
    }
}

void MixerPanel::addCommon (Strip& s)
{
    s.name.setJustificationType (juce::Justification::centred);
    s.name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    s.name.setColour (juce::Label::textColourId, colour::ink);
    addAndMakeVisible (s.name);
    s.fader.setSliderStyle (juce::Slider::LinearVertical);
    s.fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    s.fader.setColour (juce::Slider::trackColourId, colour::accentDim);
    s.fader.setColour (juce::Slider::backgroundColourId, colour::line);
    s.fader.setColour (juce::Slider::thumbColourId, colour::accent);
    s.fader.setColour (juce::Slider::textBoxTextColourId, colour::ink);
    s.fader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s.fader);
    addAndMakeVisible (s.meter);
    if (s.chip != nullptr)
    {
        s.chip->onMessage = [this] (const juce::String& m) { if (onMessage) onMessage (m); };
        addAndMakeVisible (*s.chip);
    }
    for (auto& f : s.fx) addAndMakeVisible (*f);
}

void MixerPanel::buildTrackStrip (Strip& s, int t)
{
    auto& apvts = proc.apvts;
    s.isTrack = true;
    s.track = t;
    s.chip = std::make_unique<DragChip> ("WAV", [this, t] { return proc.exportAudio (t); });
    s.chip->setTooltip ("Drag this track out as a WAV loop (bars and FX from the track view)");
    s.name.setText ("TRACK " + juce::String (t + 1), juce::dontSendNotification);
    s.fx.push_back (std::make_unique<FxSlotUI> (apvts, BashProcessor::pid (t, "fx1")));
    s.fx.push_back (std::make_unique<FxSlotUI> (apvts, BashProcessor::pid (t, "fx2")));
    s.sendA.setup ("SEND A"); s.sendB.setup ("SEND B"); s.pan.setup ("PAN");
    for (auto* k : { &s.sendA, &s.sendB, &s.pan }) { addAndMakeVisible (k->slider); addAndMakeVisible (k->label); }
    s.atts.push_back (std::make_unique<SliderAttachment> (apvts, BashProcessor::pid (t, "senda"), s.sendA.slider));
    s.atts.push_back (std::make_unique<SliderAttachment> (apvts, BashProcessor::pid (t, "sendb"), s.sendB.slider));
    s.atts.push_back (std::make_unique<SliderAttachment> (apvts, BashProcessor::pid (t, "pan"), s.pan.slider));
    s.mute.setClickingTogglesState (true);
    s.solo.setClickingTogglesState (true);
    s.mute.setColour (juce::TextButton::buttonOnColourId, colour::accent);
    s.solo.setColour (juce::TextButton::buttonOnColourId, colour::active.darker (0.1f));
    addAndMakeVisible (s.mute); addAndMakeVisible (s.solo);
    s.buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, BashProcessor::pid (t, "mute"), s.mute));
    s.buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, BashProcessor::pid (t, "solo"), s.solo));
    addCommon (s);
    s.atts.push_back (std::make_unique<SliderAttachment> (apvts, BashProcessor::pid (t, "level"), s.fader));
}

void MixerPanel::buildReturnStrip (Strip& s, const juce::String& prefix, const juce::String& levelId, const juce::String& title)
{
    s.name.setText (title, juce::dontSendNotification);
    s.fx.push_back (std::make_unique<FxSlotUI> (proc.apvts, prefix));
    addCommon (s);
    s.atts.push_back (std::make_unique<SliderAttachment> (proc.apvts, levelId, s.fader));
}

void MixerPanel::buildMicroStrip (Strip& s)
{
    auto& apvts = proc.apvts;
    s.isMicro = true;
    s.name.setText ("MICRO", juce::dontSendNotification);
    s.chip = std::make_unique<DragChip> ("LOOP", [this] { return proc.exportMicroLoop(); });
    s.chip->setTooltip ("Drag MICRO's looper phrase out as a WAV");
    s.scope = std::make_unique<MicroScope> (proc, true);
    s.scope->onClick = [this] { if (onOpenMicro) onOpenMicro(); };
    addAndMakeVisible (*s.scope);
    addAndMakeVisible (s.microOn);
    s.buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "m_on", s.microOn));
    fillChoices (s.microAlgo, apvts, "m_algo");
    s.comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, "m_algo", s.microAlgo));
    addAndMakeVisible (s.microAlgo);
    s.microMix.setup ("MIX");
    addAndMakeVisible (s.microMix.slider); addAndMakeVisible (s.microMix.label);
    s.atts.push_back (std::make_unique<SliderAttachment> (apvts, "m_mix", s.microMix.slider));
    s.open.setTooltip ("Open the MICRO tab");
    s.open.onClick = [this] { if (onOpenMicro) onOpenMicro(); };
    addAndMakeVisible (s.open);
    addCommon (s);
    s.atts.push_back (std::make_unique<SliderAttachment> (apvts, "m_level", s.fader));
}

void MixerPanel::buildMasterStrip (Strip& s)
{
    auto& apvts = proc.apvts;
    s.isMaster = true;
    s.name.setText ("MASTER", juce::dontSendNotification);
    s.chip = std::make_unique<DragChip> ("MIX", [this] { return proc.exportAudio (-1); });
    s.chip->setTooltip ("Drag the whole mix out as one WAV");
    s.glue.setTooltip ("SSL-style bus compressor, before the fader and limiter");
    s.limiter.setTooltip ("Brickwall limiter at the end of the chain");
    for (auto [b, id] : { std::pair { &s.glue, "glue_on" }, std::pair { &s.limiter, "limiter" } })
    {
        addAndMakeVisible (*b);
        s.buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, id, *b));
    }
    const std::tuple<MiniKnob*, const char*, const char*> knobs[] = {
        { &s.glueThr, "THRESH", "glue_thr" }, { &s.glueMakeup, "MAKEUP", "glue_makeup" }, { &s.glueMix, "MIX", "glue_mix" },
        { &s.limDrive, "DRIVE", "lim_drive" }, { &s.limCeil, "CEILING", "lim_ceil" }, { &s.limRel, "RELEASE", "lim_rel" } };
    for (auto& [k, name, id] : knobs)
    {
        k->setup (name);
        addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
        s.atts.push_back (std::make_unique<SliderAttachment> (apvts, id, k->slider));
    }
    const std::tuple<juce::ComboBox*, juce::Label*, const char*, const char*> combos[] = {
        { &s.glueRatio, &s.glueRatioL, "RATIO", "glue_ratio" }, { &s.glueAtt, &s.glueAttL, "ATTACK", "glue_att" },
        { &s.glueRel, &s.glueRelL, "RELEASE", "glue_rel" }, { &s.glueHpf, &s.glueHpfL, "SC HPF", "glue_hpf" } };
    for (auto& [box, label, name, id] : combos)
    {
        fillChoices (*box, apvts, id);
        s.comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id, *box));
        label->setText (name, juce::dontSendNotification);
        label->setFont (juce::FontOptions (9.5f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, colour::quiet);
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*box); addAndMakeVisible (*label);
    }
    s.sepOuts.setTooltip ("Separate track outs: when your DAW uses BASH!'s per-track outputs, take those tracks out of the main mix. Off: the main output always has everything");
    addAndMakeVisible (s.sepOuts);
    s.buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "sepouts", s.sepOuts));
    addAndMakeVisible (s.glueGr);
    addAndMakeVisible (s.limGr);
    addCommon (s);
    s.atts.push_back (std::make_unique<SliderAttachment> (apvts, "master", s.fader));
}

void MixerPanel::refresh()
{
    for (auto& s : strips)
    {
        for (auto& f : s->fx) f->refreshLabels();
        if (s->isTrack) s->meter.setLevel (proc.trackMeter (s->track));
        if (s->isMicro) { s->meter.setLevel (proc.micro().wetLevel()); s->scope->repaint(); }
        if (s->isMaster)
        {
            s->glueGr.setDb (*proc.apvts.getRawParameterValue ("glue_on") > 0.5f ? proc.glueReduction() : 0.0f);
            s->limGr.setDb (*proc.apvts.getRawParameterValue ("limiter") > 0.5f ? proc.limiterReduction() : 0.0f);
        }
        s->meter.setVisible (s->isTrack || s->isMicro);
    }
}

// strips are one unit wide; the master is two
int MixerPanel::stripW (int i) const
{
    const int units = (int) strips.size() + 1;
    const int u = getWidth() / units;
    return strips[(size_t) i]->isMaster ? getWidth() - u * ((int) strips.size() - 1) : u;
}

int MixerPanel::stripX (int i) const
{
    const int u = getWidth() / ((int) strips.size() + 1);
    return i * u;
}

void MixerPanel::paint (juce::Graphics& g)
{
    const int n = (int) strips.size();
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) stripX (i), 0.0f, (float) stripW (i), (float) getHeight()).reduced (3.0f, 0.0f);
        const auto& s = *strips[(size_t) i];
        g.setColour (s.isMicro ? colour::active.withAlpha (0.55f) : i >= bash::numTracks ? colour::raised.withAlpha (0.45f) : colour::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (s.isMicro ? colour::accentDim : colour::line);
        g.drawRoundedRectangle (r, 12.0f, 1.0f);
        if (s.isMaster)
        {
            g.setColour (colour::line);
            for (auto* b : { &s.glue, &s.limiter })
            {
                auto box = (b == &s.glue ? s.glue.getBounds().getUnion (s.glueHpf.getBounds()).getUnion (s.glueGr.getBounds())
                                         : s.limiter.getBounds().getUnion (s.limRel.slider.getBounds()).getUnion (s.limGr.getBounds())).toFloat().expanded (4.0f, 4.0f);
                g.drawRoundedRectangle (box, 8.0f, 1.0f);
            }
        }
    }
}

void MixerPanel::resized()
{
    const int n = (int) strips.size();
    for (int i = 0; i < n; ++i)
    {
        auto& s = *strips[(size_t) i];
        auto r = juce::Rectangle<int> (stripX (i), 0, stripW (i), getHeight()).reduced (8, 10);
        s.name.setBounds (r.removeFromTop (20));
        r.removeFromTop (2);
        auto chipRow = r.removeFromTop (22);
        if (s.chip != nullptr) s.chip->setBounds (chipRow.withSizeKeepingCentre (std::min (64, chipRow.getWidth()), 22));
        r.removeFromTop (6);
        for (auto& f : s.fx) { f->setBounds (r.removeFromTop (120)); r.removeFromTop (8); }
        if (s.fx.size() == 1) r.removeFromTop (128);   // returns line up with the tracks
        if (s.isTrack)
        {
            auto sends = r.removeFromTop (50);
            s.sendA.place (sends.removeFromLeft (sends.getWidth() / 2));
            s.sendB.place (sends);
            r.removeFromTop (4);
            s.pan.place (r.removeFromTop (50).withSizeKeepingCentre (48, 50));
            r.removeFromTop (6);
            auto ms = r.removeFromTop (24);
            s.mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
            s.solo.setBounds (ms.reduced (2, 0));
            r.removeFromTop (8);
        }
        else if (s.isMicro)
        {
            s.microOn.setBounds (r.removeFromTop (24));
            r.removeFromTop (4);
            s.microAlgo.setBounds (r.removeFromTop (24));
            r.removeFromTop (8);
            s.scope->setBounds (r.removeFromTop (150));
            r.removeFromTop (8);
            s.open.setBounds (r.removeFromTop (24));
            r.removeFromTop (8);
            s.microMix.place (r.removeFromTop (54).withSizeKeepingCentre (52, 54));
            const int used = 24 + 4 + 24 + 8 + 150 + 8 + 24 + 8 + 54;
            r.removeFromTop (std::max (0, 398 - used));   // fader lines up with the tracks
        }
        else if (s.isMaster)
        {
            const int half = r.getWidth() / 2, third = r.getWidth() / 3;
            auto glueRow = r.removeFromTop (22);
            s.glue.setBounds (glueRow.removeFromLeft (half));
            s.glueGr.setBounds (glueRow.reduced (0, 3));
            r.removeFromTop (4);
            auto k = r.removeFromTop (50);
            for (auto* m : { &s.glueThr, &s.glueMakeup, &s.glueMix }) m->place (k.removeFromLeft (third));
            r.removeFromTop (2);
            for (auto pair : { std::pair { std::pair { &s.glueRatio, &s.glueRatioL }, std::pair { &s.glueAtt, &s.glueAttL } },
                               std::pair { std::pair { &s.glueRel, &s.glueRelL }, std::pair { &s.glueHpf, &s.glueHpfL } } })
            {
                auto row = r.removeFromTop (36);
                auto lft = row.removeFromLeft (half).reduced (2, 0), rgt = row.reduced (2, 0);
                pair.first.second->setBounds (lft.removeFromTop (12)); pair.first.first->setBounds (lft);
                pair.second.second->setBounds (rgt.removeFromTop (12)); pair.second.first->setBounds (rgt);
                r.removeFromTop (2);
            }
            r.removeFromTop (16);
            auto limRow = r.removeFromTop (22);
            s.limiter.setBounds (limRow.removeFromLeft (half));
            s.limGr.setBounds (limRow.reduced (0, 3));
            r.removeFromTop (4);
            auto lk = r.removeFromTop (50);
            for (auto* m : { &s.limDrive, &s.limCeil, &s.limRel }) m->place (lk.removeFromLeft (third));
            r.removeFromTop (10);
            s.sepOuts.setBounds (r.removeFromTop (22));
            r.removeFromTop (6);
            const int used = 22 + 4 + 50 + 2 + 2 * 38 + 16 + 22 + 4 + 50 + 10 + 22 + 6;
            r.removeFromTop (std::max (0, 398 - used));
        }
        else
        {
            r.removeFromTop (148 - 30);
        }
        auto fader = r;
        s.meter.setBounds (fader.removeFromRight (8).withTrimmedBottom (20));
        if (s.isMaster) s.fader.setBounds (fader.withSizeKeepingCentre (std::min (90, fader.getWidth()), fader.getHeight()));
        else s.fader.setBounds (fader);
    }
}
