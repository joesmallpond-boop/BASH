#include "MicroPanel.h"

using namespace bashui;

// ============================================================================
void MicroScope::paint (juce::Graphics& g)
{
    auto& m = proc.micro();
    auto r = getLocalBounds().toFloat();
    g.setColour (colour::raised.withAlpha (0.45f));
    g.fillRoundedRectangle (r, small ? 6.0f : 10.0f);

    const bool on = *proc.apvts.getRawParameterValue ("m_on") > 0.5f;
    const bool hold = *proc.apvts.getRawParameterValue ("m_hold") > 0.5f;
    const float loopH = small ? 14.0f : 64.0f;
    auto in = r.reduced (small ? 4.0f : 10.0f);
    auto loop = in.removeFromBottom (loopH);
    in.removeFromBottom (small ? 3.0f : 10.0f);

    // input history: oldest on the left, now on the right
    const int bins = bash::MicroEngine::viewBins;
    const float cy = in.getCentreY(), half = in.getHeight() * 0.48f;
    g.setColour (on ? (hold ? colour::accent.withAlpha (0.55f) : colour::accentDim) : colour::line);
    const int cols = std::max (1, (int) in.getWidth());
    for (int x = 0; x < cols; ++x)
    {
        const int k0 = x * bins / cols, k1 = std::max (k0 + 1, (x + 1) * bins / cols);
        float pk = 0.0f;
        for (int k = k0; k < k1 && k < bins; ++k) pk = std::max (pk, m.inputOverview (k));
        const float h = std::max (0.5f, std::pow (pk, 0.75f) * half);
        g.fillRect (in.getX() + (float) x, cy - h, 1.0f, h * 2.0f);
    }
    // where each voice is reading
    if (on)
        for (int v = 0; v < 24; ++v)
        {
            const float p = m.voicePosition (v);
            if (p < 0.0f) continue;
            const float x = in.getX() + in.getWidth() * p;
            g.setColour (colour::accent.withAlpha (0.8f));
            g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
            g.fillEllipse (juce::Rectangle<float> (small ? 4.0f : 7.0f, small ? 4.0f : 7.0f).withCentre ({ x, in.getY() + (small ? 3.0f : 5.0f) }));
        }
    g.setColour (colour::ink.withAlpha (0.6f));
    g.drawVerticalLine ((int) in.getRight() - 1, in.getY(), in.getBottom());
    if (! small)
    {
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (colour::quiet);
        g.drawText ("-8 s", in.removeFromTop (14).removeFromLeft (40), juce::Justification::centredLeft);
        if (hold) { g.setColour (colour::accent); g.drawText ("HOLD", getLocalBounds().reduced (14).removeFromTop (16), juce::Justification::centredRight); }
        else if (! on) { g.setColour (colour::quiet); g.drawText ("MICRO is off: switch it on to hear it", getLocalBounds().reduced (14).removeFromTop (16), juce::Justification::centredRight); }
    }

    // the loop lane
    g.setColour (colour::panel);
    g.fillRoundedRectangle (loop, 4.0f);
    const auto st = m.loopState();
    if (st != bash::MicroEngine::LoopState::Empty)
    {
        const float lc = loop.getCentreY(), lh = loop.getHeight() * 0.46f;
        const int lcols = std::max (1, (int) loop.getWidth());
        g.setColour (st == bash::MicroEngine::LoopState::Recording ? juce::Colour (0xffd9534f).withAlpha (0.8f) : colour::ink.withAlpha (0.55f));
        for (int x = 0; x < lcols; ++x)
        {
            const int k0 = x * bins / lcols, k1 = std::max (k0 + 1, (x + 1) * bins / lcols);
            float pk = 0.0f;
            for (int k = k0; k < k1 && k < bins; ++k) pk = std::max (pk, m.loopOverview (k));
            const float h = std::max (0.5f, std::sqrt (pk) * lh);
            g.fillRect (loop.getX() + (float) x, lc - h, 1.0f, h * 2.0f);
        }
        if (st == bash::MicroEngine::LoopState::Playing || st == bash::MicroEngine::LoopState::Overdubbing)
        {
            const float x = loop.getX() + loop.getWidth() * m.loopPosition();
            g.setColour (st == bash::MicroEngine::LoopState::Overdubbing ? juce::Colour (0xffd9534f) : colour::accent);
            g.fillRect (x - 1.0f, loop.getY(), 2.0f, loop.getHeight());
        }
    }
    else if (! small)
    {
        g.setColour (colour::quiet);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("LOOPER: press REC to record a phrase", loop.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
    }
}

// ============================================================================
MicroPanel::MicroPanel (BashProcessor& p)
    : proc (p), scope (p), dice (p),
      loopChip ("LOOP WAV", [this] { return proc.exportMicroLoop(); })
{
    auto& apvts = proc.apvts;
    addAndMakeVisible (scope);

    addAndMakeVisible (onButton);
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "m_on", onButton));

    for (int i = 0; i < (int) bash::MicroAlgo::count; ++i)
    {
        auto& b = algoButtons[(size_t) i];
        b.setButtonText (bash::microAlgoName ((bash::MicroAlgo) i));
        b.setColour (juce::TextButton::buttonOnColourId, colour::active);
        b.setColour (juce::TextButton::textColourOnId, colour::accent);
        b.onClick = [this, i] { setChoice ("m_algo", i); };
        addAndMakeVisible (b);
    }
    for (int v = 0; v < 4; ++v)
    {
        auto& b = varButtons[(size_t) v];
        b.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + v)));
        b.setColour (juce::TextButton::buttonOnColourId, colour::active);
        b.setColour (juce::TextButton::textColourOnId, colour::accent);
        b.onClick = [this, v] { setChoice ("m_var", v); };
        addAndMakeVisible (b);
    }
    varText.setFont (juce::FontOptions (12.0f));
    varText.setColour (juce::Label::textColourId, colour::ink);
    varText.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (varText);

    const std::pair<Knob*, std::pair<const char*, const char*>> knobs[] = {
        { &activity, { "ACTIVITY", "m_act" } }, { &repeats, { "REPEATS", "m_rep" } }, { &shape, { "SHAPE", "m_shape" } },
        { &filter, { "FILTER", "m_filter" } }, { &reso, { "RESO", "m_reso" } }, { &modRate, { "MOD RATE", "m_modrate" } },
        { &modDepth, { "MOD DEPTH", "m_moddepth" } }, { &space, { "SPACE", "m_space" } }, { &decay, { "DECAY", "m_decay" } },
        { &mix, { "MIX", "m_mix" } }, { &level, { "LEVEL", "m_level" } },
        { &loopLevel, { "LOOP LEVEL", "m_looplevel" } }, { &loopFade, { "FADE", "m_loopfade" } } };
    for (auto& [k, ids] : knobs)
    {
        styleKnob (*k, ids.first);
        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->slider.setPopupDisplayEnabled (true, true, nullptr);
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        atts.push_back (std::make_unique<SliderAttachment> (apvts, ids.second, k->slider));
    }
    activity.slider.setTooltip ("How busy the effect is: more loops, grains, taps or glitches");
    repeats.slider.setTooltip ("How long things repeat: loop life, delay feedback, how much history grains pick from");
    shape.slider.setTooltip ("The envelope of each loop / grain: soft, flat, plucked, swelling");

    const std::tuple<juce::ComboBox*, juce::Label*, const char*, const char*> combos[] = {
        { &timeBox, &timeLabel, "TIME", "m_time" }, { &verbBox, &verbLabel, "REVERB", "m_verb" }, { &reverseBox, &reverseLabel, "REVERSE", "m_rev" },
        { &speedBox, &speedLabel, "SPEED", "m_loopspeed" }, { &modeBox, &modeLabel, "MODE", "m_loopmode" } };
    for (auto& [box, label, name, id] : combos)
    {
        fillChoices (*box, apvts, id);
        comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id, *box));
        styleComboLabel (*label, name);
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*box);
        addAndMakeVisible (*label);
    }
    timeBox.setTooltip ("The division everything locks to, as a multiple of the beat (x1 = quarter notes)");
    modeBox.setTooltip ("Post FX: loop what you hear. Pre FX: the loop feeds the effect. Looper Only: effect bypassed");

    holdButton.setClickingTogglesState (true);
    holdButton.setColour (juce::TextButton::buttonOnColourId, colour::accent);
    holdButton.setColour (juce::TextButton::textColourOnId, colour::background);
    holdButton.setTooltip ("Freeze what MICRO is listening to and keep playing it");
    addAndMakeVisible (holdButton);
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "m_hold", holdButton));

    dice.setTooltip ("Roll a new MICRO sound: effect, variation and tone (never the mix or the looper)");
    dice.rollAction = [this] { proc.rollMicro(); };
    dice.menuAction = [this] { proc.rollMicro(); };
    addAndMakeVisible (dice);

    // looper
    auto& m = proc.micro();
    recButton.onClick = [&m] { m.loopPress(); };
    stopButton.onClick = [&m] { m.loopStop(); };
    undoButton.onClick = [&m] { m.loopUndo(); };
    eraseButton.onClick = [&m] { m.loopErase(); };
    burstButton.setTooltip ("Hold to record (or overdub) only while it's held down");
    burstButton.onStateChange = [this, &m]
    {
        const bool down = burstButton.isDown();
        if (down != burstDown) { burstDown = down; m.burst (down); }
    };
    recButton.setColour (juce::TextButton::buttonColourId, colour::active);
    for (auto* b : { &recButton, &stopButton, &undoButton, &eraseButton, &burstButton }) addAndMakeVisible (*b);
    for (auto [b, id] : { std::pair { &loopReverse, "m_looprev" }, std::pair { &quantize, "m_quant" } })
    {
        addAndMakeVisible (*b);
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, id, *b));
    }
    quantize.setTooltip ("Round the loop's length to whole beats");
    loopStatus.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    loopStatus.setColour (juce::Label::textColourId, colour::accent);
    addAndMakeVisible (loopStatus);
    loopChip.setTooltip ("Drag the looper's phrase out as a WAV");
    loopChip.onMessage = [this] (const juce::String& s) { if (onMessage) onMessage (s); };
    addAndMakeVisible (loopChip);
    refresh();
}

void MicroPanel::setChoice (const char* id, int index)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
    refresh();
}

void MicroPanel::refresh()
{
    const int algo = (int) *proc.apvts.getRawParameterValue ("m_algo");
    const int var = (int) *proc.apvts.getRawParameterValue ("m_var");
    if (algo != shownAlgo || var != shownVar)
    {
        shownAlgo = algo; shownVar = var;
        for (int i = 0; i < (int) algoButtons.size(); ++i) algoButtons[(size_t) i].setToggleState (i == algo, juce::dontSendNotification);
        for (int v = 0; v < 4; ++v) varButtons[(size_t) v].setToggleState (v == var, juce::dontSendNotification);
        varText.setText (juce::String (bash::microAlgoName ((bash::MicroAlgo) algo)) + " " + juce::String::charToString ((juce::juce_wchar) ('A' + var))
                         + ": " + bash::microVariationText ((bash::MicroAlgo) algo, var), juce::dontSendNotification);
    }
    auto& m = proc.micro();
    using S = bash::MicroEngine::LoopState;
    const auto st = m.loopState();
    if ((int) st != shownState)
    {
        shownState = (int) st;
        recButton.setButtonText (st == S::Empty ? "REC" : st == S::Recording ? "PLAY" : st == S::Playing ? "DUB" : "PLAY");
        recButton.setTooltip (st == S::Empty ? "Record a phrase" : st == S::Recording ? "Close the loop and play it"
                              : st == S::Playing ? "Overdub on top" : "Play");
    }
    juce::String status;
    switch (st)
    {
        case S::Empty:       status = "EMPTY"; break;
        case S::Recording:   status = "RECORDING " + juce::String (m.recordedSeconds(), 1) + " s"; break;
        case S::Playing:     status = "PLAYING " + juce::String (m.loopSeconds(), 1) + " s"; break;
        case S::Overdubbing: status = "OVERDUB " + juce::String (m.loopSeconds(), 1) + " s"; break;
        case S::Stopped:     status = "STOPPED " + juce::String (m.loopSeconds(), 1) + " s"; break;
    }
    loopStatus.setText (status, juce::dontSendNotification);
    undoButton.setEnabled (m.canUndo());
    stopButton.setEnabled (st != S::Empty && st != S::Stopped);
    eraseButton.setEnabled (st != S::Empty);
    scope.repaint();
}

void MicroPanel::paint (juce::Graphics& g)
{
    for (auto [r, title] : { std::pair { scopeBox, "MICRO" }, std::pair { fxBox, "EFFECT" }, std::pair { toneBox, "SHAPE, TONE + SPACE" }, std::pair { looperBox, "LOOPER" } })
    {
        g.setColour (colour::panel);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (colour::line);
        g.drawRoundedRectangle (r.toFloat(), 14.0f, 1.0f);
        g.setColour (colour::ink);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 16, r.getY() + 10, 300, 16, juce::Justification::centredLeft);
    }
    // family headings over the effect buttons
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (colour::quiet);
    for (int i : { 0, 3, 6, 9 })
    {
        auto b = algoButtons[(size_t) i].getBounds();
        g.drawText (juce::String (bash::microFamilyName ((bash::MicroAlgo) i)).toUpperCase(), b.getX(), b.getY() - 18, b.getWidth(), 14, juce::Justification::centredLeft);
    }
    g.drawText ("VARIATION", varButtons[0].getX(), varButtons[0].getY() - 18, 120, 14, juce::Justification::centredLeft);
    g.drawText ("from the tracks  >  MICRO  >  master", scopeBox.getRight() - 300, scopeBox.getY() + 10, 284, 16, juce::Justification::centredRight);
}

void MicroPanel::resized()
{
    auto area = getLocalBounds();
    scopeBox = area.removeFromTop (250);
    area.removeFromTop (10);
    fxBox = area.removeFromTop (196);
    area.removeFromTop (10);
    toneBox = area.removeFromTop (130);
    area.removeFromTop (10);
    looperBox = area;

    scope.setBounds (scopeBox.reduced (14).withTrimmedTop (22));

    // effect: four families of buttons on the left, variation + switches on the right
    {
        auto r = fxBox.reduced (16).withTrimmedTop (30);
        auto left = r.removeFromLeft (600);
        const int colW = 600 / 4;
        for (int fam = 0; fam < 4; ++fam)
        {
            auto col = left.removeFromLeft (colW).withTrimmedRight (10).withTrimmedTop (16);
            const int first = fam * 3, count = fam == 3 ? 2 : 3;
            for (int k = 0; k < count; ++k) { algoButtons[(size_t) (first + k)].setBounds (col.removeFromTop (30)); col.removeFromTop (6); }
        }
        r.removeFromLeft (10);
        auto top = r.removeFromTop (32);
        onButton.setBounds (top.removeFromLeft (110));
        dice.setBounds (top.removeFromRight (32));
        top.removeFromRight (8);
        holdButton.setBounds (top.removeFromRight (80));
        r.removeFromTop (24);
        auto vrow = r.removeFromTop (30);
        for (auto& b : varButtons) { b.setBounds (vrow.removeFromLeft (46)); vrow.removeFromLeft (6); }
        vrow.removeFromLeft (6);
        reverseLabel.setJustificationType (juce::Justification::centredLeft);
        reverseLabel.setBounds (vrow.getX(), vrow.getY() - 20, 120, 16);
        reverseBox.setBounds (vrow.withWidth (std::min (130, vrow.getWidth())).reduced (0, 2));
        r.removeFromTop (6);
        varText.setBounds (r.removeFromTop (40));
    }

    // tone row: activity, repeats, shape, time | filter, reso, mod | space, reverb, decay | mix, level
    {
        auto r = toneBox.reduced (14).withTrimmedTop (24);
        const int w = r.getWidth() / 13;
        auto cell = [&] { return r.removeFromLeft (w); };
        for (auto* k : { &activity, &repeats, &shape }) placeKnob (*k, cell());
        { auto c = cell(); timeLabel.setBounds (c.removeFromTop (16)); timeBox.setBounds (c.withSizeKeepingCentre (w - 6, 26)); }
        for (auto* k : { &filter, &reso, &modRate, &modDepth, &space }) placeKnob (*k, cell());
        { auto c = cell(); verbLabel.setBounds (c.removeFromTop (16)); verbBox.setBounds (c.withSizeKeepingCentre (w - 6, 26)); }
        for (auto* k : { &decay, &mix, &level }) placeKnob (*k, cell());
    }

    // looper
    {
        auto r = looperBox.reduced (16).withTrimmedTop (28);
        auto row = r.removeFromTop (44);
        recButton.setBounds (row.removeFromLeft (120)); row.removeFromLeft (8);
        for (auto* b : { &stopButton, &undoButton, &eraseButton }) { b->setBounds (row.removeFromLeft (84)); row.removeFromLeft (8); }
        burstButton.setBounds (row.removeFromLeft (100)); row.removeFromLeft (16);
        loopChip.setBounds (row.removeFromRight (96).withSizeKeepingCentre (96, 26));
        row.removeFromRight (12);
        loopStatus.setBounds (row);
        r.removeFromTop (10);
        auto opts = r.removeFromTop (std::min (70, r.getHeight()));
        loopReverse.setBounds (opts.removeFromLeft (100).withSizeKeepingCentre (100, 26)); opts.removeFromLeft (6);
        quantize.setBounds (opts.removeFromLeft (110).withSizeKeepingCentre (110, 26)); opts.removeFromLeft (10);
        for (auto [box, label] : { std::pair { &speedBox, &speedLabel }, std::pair { &modeBox, &modeLabel } })
        {
            auto c = opts.removeFromLeft (box == &modeBox ? 130 : 90);
            label->setBounds (c.removeFromTop (16));
            box->setBounds (c.withSizeKeepingCentre (c.getWidth() - 6, 26));
            opts.removeFromLeft (10);
        }
        placeKnob (loopLevel, opts.removeFromLeft (80));
        placeKnob (loopFade, opts.removeFromLeft (80));
    }
}
