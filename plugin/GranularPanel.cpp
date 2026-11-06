#include "GranularPanel.h"

namespace bashui
{
    // ---- the waveform with playheads ---------------------------------------------------
    void GrainView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colour::raised.withAlpha (0.45f));
        g.fillRoundedRectangle (r, 10.0f);
        auto& gr = proc.granular();
        const bool frozen = proc.apvts.getRawParameterValue ("g_freeze")->load() > 0.5f;
        const bool on = proc.apvts.getRawParameterValue ("g_on")->load() > 0.5f;

        // buffer, oldest on the left
        const int bins = bash::GranularEngine::overviewBins;
        const float mid = r.getCentreY(), half = r.getHeight() * 0.42f, bw = r.getWidth() / bins;
        g.setColour ((frozen ? colour::accent : colour::accentDim).withAlpha (on ? 1.0f : 0.5f));
        for (int k = 0; k < bins; ++k)
        {
            const float h = std::max (0.5f, std::sqrt (gr.overview (k)) * half);
            g.fillRect (r.getX() + k * bw, mid - h, std::max (1.0f, bw - 0.4f), h * 2.0f);
        }

        // playheads: spray range, line, numbered handle
        for (int h = bash::grainHeads - 1; h >= 0; --h)
        {
            const auto pre = "g_h" + juce::String (h + 1) + "_";
            const bool headOn = proc.apvts.getRawParameterValue (pre + "on")->load() > 0.5f;
            const float set = proc.apvts.getRawParameterValue (pre + "pos")->load();
            const float live = gr.headPosition (h);
            const float spray = proc.apvts.getRawParameterValue (pre + "spray")->load();
            const bool sel = h == selected;
            const float x = r.getX() + live * r.getWidth();
            if (headOn && sel)
            {
                g.setColour (colour::accent.withAlpha (0.12f));
                const float w = spray * r.getWidth() * 0.5f;
                g.fillRect (juce::Rectangle<float> (x - w, r.getY(), w * 2.0f, r.getHeight()).getIntersection (r));
            }
            const auto c = ! headOn ? colour::quiet.withAlpha (0.35f) : sel ? colour::accent : colour::ink.withAlpha (0.7f);
            g.setColour (c);
            g.fillRect (x - (sel ? 1.0f : 0.5f), r.getY() + 20.0f, sel ? 2.0f : 1.0f, r.getHeight() - 20.0f);
            if (std::abs (set - live) > 0.002f)   // scanning / drifting: show where it's set too
            {
                g.setColour (c.withAlpha (0.35f));
                g.fillRect (r.getX() + set * r.getWidth() - 0.5f, r.getY() + 20.0f, 1.0f, r.getHeight() - 20.0f);
            }
            auto handle = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x, r.getY() + 11.0f });
            g.setColour (c);
            g.fillRoundedRectangle (handle, 5.0f);
            g.setColour (colour::background);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (h + 1), handle, juce::Justification::centred);
        }

        g.setColour (colour::quiet);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (frozen ? "FROZEN" : "LIVE: last 16 s of the master", r.reduced (10.0f, 6.0f), juce::Justification::bottomLeft);
        g.drawText ("now", r.reduced (10.0f, 6.0f), juce::Justification::bottomRight);
    }

    void GrainView::setPosition (int h, float x)
    {
        if (auto* p = proc.apvts.getParameter ("g_h" + juce::String (h + 1) + "_pos"))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, x / (float) getWidth()));
    }

    void GrainView::mouseDown (const juce::MouseEvent& e)
    {
        // grab the nearest playhead (by its handle or line), else move the selected one here
        int best = -1; float bestD = 12.0f;
        for (int h = 0; h < bash::grainHeads; ++h)
        {
            const float x = proc.granular().headPosition (h) * getWidth();
            const float d = std::abs (x - e.position.x);
            if (d < bestD) { bestD = d; best = h; }
        }
        dragging = best >= 0 ? best : selected;
        if (dragging != selected && onSelectHead) onSelectHead (dragging);
        if (auto* p = proc.apvts.getParameter ("g_h" + juce::String (dragging + 1) + "_pos")) p->beginChangeGesture();
        setPosition (dragging, e.position.x);
        repaint();
    }

    void GrainView::mouseDrag (const juce::MouseEvent& e) { if (dragging >= 0) { setPosition (dragging, e.position.x); repaint(); } }
    void GrainView::mouseUp (const juce::MouseEvent&)
    {
        if (dragging >= 0)
            if (auto* p = proc.apvts.getParameter ("g_h" + juce::String (dragging + 1) + "_pos")) p->endChangeGesture();
        dragging = -1;
    }

    // ---- the panel -------------------------------------------------------------------
    Knob& GranularPanel::knob (const juce::String& id, const juce::String& name, std::vector<std::unique_ptr<Knob>>& into)
    {
        auto k = std::make_unique<Knob>();
        styleKnob (*k, name);
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        k->slider.setName (id);
        into.push_back (std::move (k));
        return *into.back();
    }

    GranularPanel::GranularPanel (BashProcessor& p)
        : proc (p), view (p), die (p),
          takeChip ("TAKE", [this] { return proc.exportGranularTake(); }),
          mixChip ("MIX", [this] { return proc.exportAudio (-1); })
    {
        auto& apvts = proc.apvts;
        addAndMakeVisible (view);
        view.onSelectHead = [this] (int h) { selectHead (h); };

        // top bar
        onButton.setTooltip ("Switch the whole granular section on or off (everything before it is unaffected)");
        freezeButton.setTooltip ("Hold the buffer: play with what's in it while the music carries on");
        for (auto* b : { &onButton, &freezeButton, &loopButton }) addAndMakeVisible (*b);
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "g_on", onButton));
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "g_freeze", freezeButton));
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "g_loop", loopButton));
        mix.setSliderStyle (juce::Slider::LinearBar);
        mix.setColour (juce::Slider::trackColourId, colour::accentDim);
        mix.setColour (juce::Slider::backgroundColourId, colour::raised);
        mix.setColour (juce::Slider::textBoxTextColourId, colour::ink);
        mix.setColour (juce::Slider::textBoxOutlineColourId, colour::line);
        addAndMakeVisible (mix);
        sliderAtts.push_back (std::make_unique<SliderAttachment> (apvts, "g_mix", mix));
        styleComboLabel (mixLabel, "MIX");
        addAndMakeVisible (mixLabel);
        status.setFont (juce::FontOptions (12.0f));
        status.setColour (juce::Label::textColourId, colour::quiet);
        addAndMakeVisible (status);

        die.setTooltip ("Roll the granular section (right-click: undo)");
        die.rollAction = [this] { proc.rollGranularDice(); showMessage ("Rolled the granular section"); };
        die.menuAction = [this]
        {
            juce::PopupMenu m;
            m.addSectionHeader ("Granular die");
            m.addItem (1, "Undo last roll (" + juce::String (proc.granularUndoCount()) + " stored)", proc.granularUndoCount() > 0);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&die), [this] (int r) { if (r == 1) proc.undoGranularDice(); });
        };
        addAndMakeVisible (die);

        recButton.setClickingTogglesState (false);
        recButton.setTooltip ("Record what this section plays into a take (up to 60 s)");
        recButton.onClick = [this]
        {
            const bool now = ! proc.granularRecording();
            if (now && proc.apvts.getRawParameterValue ("g_on")->load() < 0.5f)
                if (auto* p = proc.apvts.getParameter ("g_on")) p->setValueNotifyingHost (1.0f);
            proc.setGranularRecording (now);
            showMessage (now ? "Recording..." : "Take: " + juce::String (proc.granularTakeSeconds(), 1) + " s. Drag TAKE out, loop it, or send it on from the Take menu.");
        };
        takeButton.onClick = [this] { showTakeMenu(); };
        for (auto* b : { &recButton, &takeButton }) addAndMakeVisible (*b);
        takeChip.setTooltip ("Drag the recorded take out as a WAV (click to save it)");
        mixChip.setTooltip ("Drag the whole mix, through this section, out as a WAV");
        for (auto* c : { &takeChip, &mixChip })
        {
            c->onMessage = [this] (const juce::String& m) { showMessage (m); };
            addAndMakeVisible (*c);
        }

        // heads
        for (int h = 0; h < bash::grainHeads; ++h)
        {
            auto& t = headTabs[(size_t) h];
            t.setButtonText ("HEAD " + juce::String (h + 1));
            t.setClickingTogglesState (false);
            t.onClick = [this, h] { selectHead (h); };
            addAndMakeVisible (t);
        }
        addAndMakeVisible (headOn);
        addAndMakeVisible (keyLock);
        keyLock.setTooltip ("Grain pitches snap to the session key and scale");
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "g_keylock", keyLock));
        fillChoices (windowBox, apvts, "g_window");
        addAndMakeVisible (windowBox);
        comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, "g_window", windowBox));
        styleComboLabel (windowLabel, "GRAIN SHAPE");
        addAndMakeVisible (windowLabel);

        const std::pair<const char*, const char*> headParams[] = {
            { "pos", "POSITION" }, { "size", "SIZE" }, { "dens", "DENSITY" }, { "pitch", "PITCH" }, { "pspray", "PITCH SPRAY" },
            { "spray", "POS SPRAY" }, { "scan", "SCAN" }, { "rev", "REVERSE" }, { "drift", "DRIFT" }, { "level", "LEVEL" }, { "pan", "PAN" } };
        for (auto [id, name] : headParams) { knob (id, name, headKnobs); headKnobIds.push_back (id); }

        // global knobs (attached once)
        auto attach = [this, &apvts] (const char* id, const char* name)
        {
            auto& k = knob (id, name, knobs);
            sliderAtts.push_back (std::make_unique<SliderAttachment> (apvts, id, k.slider));
        };
        attach ("g_spread", "SPREAD");
        attach ("g_cutoff", "CUTOFF"); attach ("g_reso", "RESO");
        attach ("g_resmix", "MIX"); attach ("g_resdecay", "DECAY"); attach ("g_restone", "TONE");
        attach ("g_dlymix", "DELAY"); attach ("g_dlyfb", "FEEDBACK");
        attach ("g_revmix", "REVERB"); attach ("g_revsize", "SIZE"); attach ("g_shimmer", "SHIMMER");
        attach ("g_glitch", "CHANCE");
        attach ("g_looplvl", "LOOP LVL"); attach ("g_out", "OUTPUT");
        for (auto& k : knobs) if (k->slider.getName() == "g_resdecay") k->slider.textFromValueFunction = [] (double v) { return v >= 0.99 ? juce::String ("infinite") : juce::String (v, 2); };

        auto combo = [this, &apvts] (juce::ComboBox& box, juce::Label& label, const char* id, const char* name)
        {
            fillChoices (box, apvts, id);
            addAndMakeVisible (box);
            comboAtts.push_back (std::make_unique<ComboAttachment> (apvts, id, box));
            styleComboLabel (label, name);
            addAndMakeVisible (label);
        };
        combo (ftypeBox, ftypeLabel, "g_ftype", "TYPE");
        combo (chordBox, chordLabel, "g_reschord", "CHORD");
        combo (octBox, octLabel, "g_resoct", "OCTAVE");
        combo (dlyDivBox, dlyDivLabel, "g_dlydiv", "TIME");
        combo (gDivBox, gDivLabel, "g_gdiv", "EVERY");

        revFreeze.setTooltip ("Infinite reverb: hold the wash forever");
        addAndMakeVisible (revFreeze);
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, "g_revfreeze", revFreeze));
        const std::pair<const char*, const char*> modes[] = { { "g_grep", "Repeat" }, { "g_grev", "Reverse" }, { "g_gtape", "Tape stop" }, { "g_gcrush", "Crush" }, { "g_gchop", "Chop" } };
        for (int i = 0; i < 5; ++i)
        {
            glitchModes[(size_t) i].setButtonText (modes[i].second);
            addAndMakeVisible (glitchModes[(size_t) i]);
            buttonAtts.push_back (std::make_unique<ButtonAttachment> (apvts, modes[i].first, glitchModes[(size_t) i]));
        }
        stutter.setTooltip ("Hold to repeat the last division");
        stutter.onHold = [this] (bool down)
        {
            if (auto* p = proc.apvts.getParameter ("g_stutter")) p->setValueNotifyingHost (down ? 1.0f : 0.0f);
        };
        addAndMakeVisible (stutter);

        selectHead (0);
    }

    GranularPanel::~GranularPanel() = default;

    void GranularPanel::selectHead (int h)
    {
        head = juce::jlimit (0, bash::grainHeads - 1, h);
        view.selected = head;
        headAtts.clear();
        headOnAtt.reset();
        const auto pre = "g_h" + juce::String (head + 1) + "_";
        for (size_t i = 0; i < headKnobs.size(); ++i)
            headAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, pre + headKnobIds[i], headKnobs[i]->slider));
        headOnAtt = std::make_unique<ButtonAttachment> (proc.apvts, pre + "on", headOn);
        for (int i = 0; i < bash::grainHeads; ++i) headTabs[(size_t) i].setToggleState (i == head, juce::dontSendNotification);
        repaint();
    }

    void GranularPanel::showMessage (const juce::String& m)
    {
        message = m;
        messageTime = juce::Time::getMillisecondCounter();
        refresh();
    }

    void GranularPanel::showTakeMenu()
    {
        juce::PopupMenu m, slicer;
        const bool has = proc.granularTakeSeconds() > 0.0 && ! proc.granularRecording();
        m.addSectionHeader (has ? "Take: " + juce::String (proc.granularTakeSeconds(), 1) + " s" : "No take yet: press Record");
        m.addItem (1, "Feed it back into the grains (freezes the buffer)", has);
        for (int t = 0; t < bash::numTracks; ++t) slicer.addItem (10 + t, "Track " + juce::String (t + 1));
        m.addSubMenu ("Send to a slice sampler", slicer, has);
        m.addItem (2, "Save as WAV and show it", has);
        m.addSeparator();
        m.addItem (3, "Clear the take", has);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&takeButton), [this] (int r)
        {
            if (r == 1) { proc.granularTakeToGrains(); showMessage ("The take is in the grain buffer (frozen)."); }
            else if (r == 2) { const auto f = proc.exportGranularTake(); if (f.existsAsFile()) f.revealToUser(); }
            else if (r == 3) { proc.granular().clearTake(); showMessage ("Take cleared"); }
            else if (r >= 10)
            {
                const auto err = proc.granularTakeToSlicer (r - 10);
                showMessage (err.isNotEmpty() ? err : "The take is in track " + juce::String (r - 9) + "'s slice sampler.");
            }
        });
    }

    void GranularPanel::refresh()
    {
        view.repaint();
        auto& gr = proc.granular();
        const bool rec = proc.granularRecording();
        recButton.setButtonText (rec ? "Stop " + juce::String (proc.granularTakeSeconds(), 1) + " s" : "Record");
        recButton.setToggleState (rec, juce::dontSendNotification);
        juce::String text;
        if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 5000) text = message;
        else if (proc.apvts.getRawParameterValue ("g_on")->load() < 0.5f) text = "Off: switch GRANULAR on to play with the sound of everything you've made.";
        else
        {
            text = juce::String (gr.activeGrains()) + " grains";
            const juce::String gl (gr.lastGlitch());
            if (gl.isNotEmpty() && proc.apvts.getRawParameterValue ("g_glitch")->load() > 0.0f) text << "   |   last glitch: " << gl;
            if (proc.granularTakeSeconds() > 0.0 && ! rec) text << "   |   take " << juce::String (proc.granularTakeSeconds(), 1) << " s";
        }
        status.setText (text, juce::dontSendNotification);
    }

    void GranularPanel::paint (juce::Graphics& g)
    {
        auto box = [&] (juce::Rectangle<int> r, const juce::String& title)
        {
            g.setColour (colour::panel);
            g.fillRoundedRectangle (r.toFloat(), 14.0f);
            g.setColour (colour::line);
            g.drawRoundedRectangle (r.toFloat(), 14.0f, 1.0f);
            if (title.isNotEmpty())
            {
                g.setColour (colour::ink);
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawText (title, r.getX() + 16, r.getY() + 10, 300, 16, juce::Justification::centredLeft);
            }
        };
        box (barBox, {});
        box (headBox, "PLAYHEADS");
        box (filterBox, "FILTER");
        box (resBox, "RESONATOR  (in key)");
        box (delayBox, "DELAY");
        box (reverbBox, "REVERB");
        box (glitchBox, "GLITCH");
        box (outBox, "LOOPER / OUT");
    }

    void GranularPanel::resized()
    {
        auto area = getLocalBounds();
        constexpr int knobW = 74, knobH = 104;

        // top bar
        barBox = area.removeFromTop (48);
        {
            auto r = barBox.reduced (12, 8);
            onButton.setBounds (r.removeFromLeft (120)); r.removeFromLeft (8);
            mixLabel.setBounds (r.removeFromLeft (30)); mix.setBounds (r.removeFromLeft (90)); r.removeFromLeft (10);
            freezeButton.setBounds (r.removeFromLeft (92)); r.removeFromLeft (8);
            die.setBounds (r.removeFromLeft (32).withSizeKeepingCentre (32, 32)); r.removeFromLeft (12);
            mixChip.setBounds (r.removeFromRight (70)); r.removeFromRight (6);
            takeChip.setBounds (r.removeFromRight (74)); r.removeFromRight (6);
            takeButton.setBounds (r.removeFromRight (64)); r.removeFromRight (6);
            loopButton.setBounds (r.removeFromRight (110)); r.removeFromRight (6);
            recButton.setBounds (r.removeFromRight (96)); r.removeFromRight (10);
            status.setBounds (r);
        }
        area.removeFromTop (10);

        // the bottom rows first, the waveform gets what's left
        auto bottom = area.removeFromBottom (knobH + 36);
        area.removeFromBottom (10);
        auto middle = area.removeFromBottom (knobH + 36);
        area.removeFromBottom (10);
        headBox = area.removeFromBottom (knobH + 44);
        area.removeFromBottom (10);
        view.setBounds (area);

        auto knobsIn = [&] (juce::Rectangle<int> r, std::initializer_list<Knob*> ks)
        {
            for (auto* k : ks) { placeKnob (*k, r.removeFromLeft (knobW).withHeight (knobH)); r.removeFromLeft (2); }
            return r;
        };
        auto find = [this] (const char* id) -> Knob*
        {
            for (auto& k : knobs) if (k->slider.getName() == id) return k.get();
            return nullptr;
        };
        auto comboIn = [] (juce::Rectangle<int>& r, juce::ComboBox& box, juce::Label& label, int w)
        {
            auto c = r.removeFromLeft (w).withHeight (60).withY (r.getY() + 12);
            label.setBounds (c.removeFromTop (18));
            box.setBounds (c.removeFromTop (30));
            r.removeFromLeft (8);
        };

        // playheads
        {
            auto r = headBox.reduced (14, 0);
            auto top = r.removeFromTop (36).withTrimmedTop (6);
            top.removeFromLeft (100);
            for (auto& t : headTabs) { t.setBounds (top.removeFromLeft (72)); top.removeFromLeft (4); }
            top.removeFromLeft (8);
            headOn.setBounds (top.removeFromLeft (96));
            keyLock.setBounds (top.removeFromRight (100)); top.removeFromRight (8);
            windowBox.setBounds (top.removeFromRight (100)); top.removeFromRight (4);
            windowLabel.setBounds (top.removeFromRight (86));
            r.removeFromTop (4);
            for (auto& k : headKnobs) { placeKnob (*k, r.removeFromLeft (knobW).withHeight (knobH)); r.removeFromLeft (2); }
            if (auto* k = find ("g_spread")) placeKnob (*k, r.removeFromLeft (knobW).withHeight (knobH));
        }

        auto column = [] (juce::Rectangle<int>& r, int w, std::initializer_list<std::pair<juce::Label*, juce::Component*>> items)
        {
            auto c = r.removeFromLeft (w);
            c.removeFromTop (2);
            for (auto [label, comp] : items)
            {
                if (label) label->setBounds (c.removeFromTop (16));
                comp->setBounds (c.removeFromTop (28));
                c.removeFromTop (6);
            }
            r.removeFromLeft (8);
        };

        // filter | resonator | delay
        {
            filterBox = middle.removeFromLeft (262);
            middle.removeFromLeft (10);
            resBox = middle.removeFromLeft (360);
            middle.removeFromLeft (10);
            delayBox = middle;

            auto f = filterBox.reduced (12, 0).withTrimmedTop (30);
            column (f, 92, { { &ftypeLabel, &ftypeBox } });
            knobsIn (f, { find ("g_cutoff"), find ("g_reso") });

            auto rr = resBox.reduced (12, 0).withTrimmedTop (30);
            rr = knobsIn (rr, { find ("g_resmix"), find ("g_resdecay"), find ("g_restone") });
            rr.removeFromLeft (4);
            column (rr, 96, { { &chordLabel, &chordBox }, { &octLabel, &octBox } });

            auto d = delayBox.reduced (12, 0).withTrimmedTop (30);
            d = knobsIn (d, { find ("g_dlymix"), find ("g_dlyfb") });
            column (d, 80, { { &dlyDivLabel, &dlyDivBox } });
        }

        // glitch | reverb | looper / out
        {
            outBox = bottom.removeFromRight (178);
            bottom.removeFromRight (10);
            reverbBox = bottom.removeFromRight (330);
            bottom.removeFromRight (10);
            glitchBox = bottom;

            auto gl = glitchBox.reduced (12, 0).withTrimmedTop (30);
            gl = knobsIn (gl, { find ("g_glitch") });
            column (gl, 76, { { &gDivLabel, &gDivBox } });
            auto grid = gl.withTrimmedTop (8);
            const int bw = std::min (96, (grid.getWidth() - 6) / 3);
            auto row1 = grid.removeFromTop (30), row2 = grid.removeFromTop (36).withTrimmedTop (6);
            for (int i = 0; i < 3; ++i) { glitchModes[(size_t) i].setBounds (row1.removeFromLeft (bw)); row1.removeFromLeft (3); }
            for (int i = 3; i < 5; ++i) { glitchModes[(size_t) i].setBounds (row2.removeFromLeft (bw)); row2.removeFromLeft (3); }
            stutter.setBounds (row2.removeFromLeft (bw));

            auto rv = reverbBox.reduced (12, 0).withTrimmedTop (30);
            rv = knobsIn (rv, { find ("g_revmix"), find ("g_revsize"), find ("g_shimmer") });
            revFreeze.setBounds (rv.removeFromLeft (84).withHeight (28).withY (rv.getY() + 30));

            auto o = outBox.reduced (12, 0).withTrimmedTop (30);
            knobsIn (o, { find ("g_looplvl"), find ("g_out") });
        }
    }
}
