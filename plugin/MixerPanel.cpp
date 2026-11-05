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
    gnarl.slider.setColour (juce::Slider::rotarySliderFillColourId, colour::coral);
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
    g.setColour (db > -1.0f ? colour::coral : colour::periwinkle);
    g.fillRoundedRectangle (lit, 3.0f);
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
    s.fader.setColour (juce::Slider::trackColourId, colour::periwinkle);
    s.fader.setColour (juce::Slider::backgroundColourId, colour::line);
    s.fader.setColour (juce::Slider::thumbColourId, colour::coral);
    s.fader.setColour (juce::Slider::textBoxTextColourId, colour::ink);
    s.fader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s.fader);
    addAndMakeVisible (s.meter);
    for (auto& f : s.fx) addAndMakeVisible (*f);
}

void MixerPanel::buildTrackStrip (Strip& s, int t)
{
    auto& apvts = proc.apvts;
    s.isTrack = true;
    s.track = t;
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
    s.mute.setColour (juce::TextButton::buttonOnColourId, colour::coral);
    s.solo.setColour (juce::TextButton::buttonOnColourId, colour::mint.darker (0.1f));
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

void MixerPanel::buildMasterStrip (Strip& s)
{
    s.isMaster = true;
    s.name.setText ("MASTER", juce::dontSendNotification);
    s.fx.push_back (std::make_unique<FxSlotUI> (proc.apvts, "mfx1"));
    s.fx.push_back (std::make_unique<FxSlotUI> (proc.apvts, "mfx2"));
    addAndMakeVisible (s.limiter);
    s.buttonAtts.push_back (std::make_unique<ButtonAttachment> (proc.apvts, "limiter", s.limiter));
    addCommon (s);
    s.atts.push_back (std::make_unique<SliderAttachment> (proc.apvts, "master", s.fader));
}

void MixerPanel::refresh()
{
    for (auto& s : strips)
    {
        for (auto& f : s->fx) f->refreshLabels();
        if (s->isTrack) s->meter.setLevel (proc.trackMeter (s->track));
        s->meter.setVisible (s->isTrack);
    }
}

void MixerPanel::paint (juce::Graphics& g)
{
    const int n = (int) strips.size();
    const float w = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (3.0f, 0.0f);
        g.setColour (i >= bash::numTracks ? colour::iceBlue.withAlpha (0.45f) : colour::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colour::line);
        g.drawRoundedRectangle (r, 12.0f, 1.0f);
    }
}

void MixerPanel::resized()
{
    const int n = (int) strips.size();
    const int w = getWidth() / n;
    for (int i = 0; i < n; ++i)
    {
        auto& s = *strips[(size_t) i];
        auto r = juce::Rectangle<int> (i * w, 0, w, getHeight()).reduced (8, 10);
        s.name.setBounds (r.removeFromTop (20));
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
        else if (s.isMaster)
        {
            r.removeFromTop (110);
            s.limiter.setBounds (r.removeFromTop (26));
            r.removeFromTop (12);
        }
        else
        {
            r.removeFromTop (148);
        }
        auto fader = r;
        s.meter.setBounds (fader.removeFromRight (8).withTrimmedBottom (20));
        s.fader.setBounds (fader);
    }
}
