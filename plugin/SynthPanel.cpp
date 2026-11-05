#include "SynthPanel.h"

using namespace bashui;

SynthPanel::SynthPanel (BashProcessor& p) : proc (p)
{
    styleComboLabel (machineLabel, "MACHINE");
    styleComboLabel (filterLabel, "FILTER");
    for (auto* c : { &machineBox, &filterBox }) addAndMakeVisible (*c);
    for (auto* l : { &machineLabel, &filterLabel }) addAndMakeVisible (*l);

    const std::pair<Knob*, const char*> knobs[] = {
        { &character, "CHARACTER" }, { &timbre, "TIMBRE" }, { &cutoff, "CUTOFF" }, { &reso, "RESO" },
        { &envMod, "ENV MOD" }, { &decay, "F. DECAY" }, { &drive, "DRIVE" }, { &glide, "GLIDE" },
        { &attack, "ATTACK" }, { &ampDecay, "DECAY" }, { &sustain, "SUSTAIN" }, { &release, "RELEASE" },
        { &level, "LEVEL" }, { &pan, "PAN" } };
    for (auto& [k, name] : knobs)
    {
        styleKnob (*k, name);
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
    }
}

void SynthPanel::setTrack (int t)
{
    track = t;
    auto& apvts = proc.apvts;
    knobAtts.clear(); comboAtts.clear();
    auto id = [t] (const char* s) { return BashProcessor::pid (t, s); };

    fillChoices (machineBox, apvts, id ("machine"));
    fillChoices (filterBox, apvts, id ("ftype"));
    comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id ("machine"), machineBox));
    comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id ("ftype"), filterBox));

    const std::pair<Knob*, const char*> knobs[] = {
        { &character, "wave" }, { &timbre, "timbre" }, { &cutoff, "cutoff" }, { &reso, "reso" },
        { &envMod, "envmod" }, { &decay, "decay" }, { &drive, "drive" }, { &glide, "glide" },
        { &attack, "aatk" }, { &ampDecay, "adec" }, { &sustain, "asus" }, { &release, "arel" },
        { &level, "level" }, { &pan, "pan" } };
    for (auto& [k, pidName] : knobs)
        knobAtts.push_back (std::make_unique<SliderAttachment> (apvts, id (pidName), k->slider));

    shownMachine = -1;
    refresh();
}

void SynthPanel::refresh()
{
    const int m = (int) *proc.apvts.getRawParameterValue (BashProcessor::pid (track, "machine"));
    if (m == shownMachine) return;
    shownMachine = m;
    character.label.setText (bash::machineCharacterName ((bash::Machine) m), juce::dontSendNotification);
    timbre.label.setText (bash::machineTimbreName ((bash::Machine) m), juce::dontSendNotification);
}

void SynthPanel::resized()
{
    auto area = getLocalBounds();
    auto row1 = area.removeFromTop (110);
    auto combos = row1.removeFromLeft (150);
    machineLabel.setBounds (combos.removeFromTop (18));
    machineBox.setBounds (combos.removeFromTop (30));
    combos.removeFromTop (8);
    filterLabel.setBounds (combos.removeFromTop (18));
    filterBox.setBounds (combos.removeFromTop (30));
    row1.removeFromLeft (12);
    for (auto* k : { &character, &timbre, &cutoff, &reso, &envMod, &decay, &drive, &glide })
    {
        placeKnob (*k, row1.removeFromLeft (98));
        row1.removeFromLeft (4);
    }

    area.removeFromTop (8);
    auto row2 = area.removeFromTop (110);
    row2.removeFromLeft (162);
    for (auto* k : { &attack, &ampDecay, &sustain, &release, &level, &pan })
    {
        placeKnob (*k, row2.removeFromLeft (98));
        row2.removeFromLeft (4);
    }
}

void SynthPanel::forEachKnob (const std::function<void (Knob&, const juce::String&)>& f)
{
    const std::pair<Knob*, const char*> knobs[] = {
        { &character, "wave" }, { &timbre, "timbre" }, { &cutoff, "cutoff" }, { &reso, "reso" },
        { &envMod, "envmod" }, { &decay, "decay" }, { &drive, "drive" }, { &glide, "glide" },
        { &attack, "aatk" }, { &ampDecay, "adec" }, { &sustain, "asus" }, { &release, "arel" },
        { &level, "level" }, { &pan, "pan" } };
    for (auto& [k, id] : knobs) f (*k, BashProcessor::pid (track, id));
}
