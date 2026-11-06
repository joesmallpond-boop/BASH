#include "StepPanel.h"

namespace bashui
{
    namespace
    {
        const char* condShort (bash::TrigCond c)
        {
            switch (c)
            {
                case bash::TrigCond::Fill:     return "F";
                case bash::TrigCond::NotFill:  return "!F";
                case bash::TrigCond::First:    return "1st";
                case bash::TrigCond::NotFirst: return "!1";
                case bash::TrigCond::Always:   return "";
                default:                       return bash::trigCondName (c);
            }
        }

        void styleField (juce::Label& l, juce::Slider& s, const juce::String& name, double lo, double hi, double step)
        {
            l.setText (name, juce::dontSendNotification);
            l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            l.setColour (juce::Label::textColourId, colour::quiet);
            l.setJustificationType (juce::Justification::centredLeft);
            s.setSliderStyle (juce::Slider::LinearBar);
            s.setRange (lo, hi, step);
            s.setColour (juce::Slider::trackColourId, colour::accentDim);
            s.setColour (juce::Slider::backgroundColourId, colour::raised);
            s.setColour (juce::Slider::textBoxTextColourId, colour::ink);
            s.setColour (juce::Slider::textBoxOutlineColourId, colour::line);
            s.setTextBoxIsEditable (true);
        }
    }

    StepPanel::StepPanel (BashProcessor& p) : proc (p)
    {
        playPos.fill (-1);

        // toolbar
        fillButton.setTooltip ("Latch fills: steps set to Fill play, Not fill steps rest");
        addAndMakeVisible (fillButton);
        autoFillLabel.setText ("AUTO FILL", juce::dontSendNotification);
        swingLabel.setText ("SWING", juce::dontSendNotification);
        for (auto* l : { &autoFillLabel, &swingLabel })
        {
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, colour::quiet);
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (*l);
        }
        addAndMakeVisible (autoFillBox);
        swing.setSliderStyle (juce::Slider::LinearBar);
        swing.setColour (juce::Slider::trackColourId, colour::accentDim);
        swing.setColour (juce::Slider::backgroundColourId, colour::raised);
        swing.setColour (juce::Slider::textBoxTextColourId, colour::ink);
        swing.setColour (juce::Slider::textBoxOutlineColourId, colour::line);
        addAndMakeVisible (swing);
        for (int i = 0; i < 4; ++i)
        {
            auto& b = pageButtons[(size_t) i];
            b.setButtonText (juce::String (i * 16 + 1) + "-" + juce::String (i * 16 + 16));
            b.onClick = [this, i] { page = i; updateInspector(); repaint(); };
            addAndMakeVisible (b);
        }
        clearButton.setTooltip ("Clear every step on this track (lane settings stay)");
        clearButton.onClick = [this] { edit ([] (bash::StepPattern& sp) { sp.clear(); }); };
        addAndMakeVisible (clearButton);

        // inspector
        styleField (pulses.label, pulses.slider, "PULSES", 0, 64, 1);
        styleField (rotate.label, rotate.slider, "ROTATE", 0, 63, 1);
        styleField (amount.label, amount.slider, "AMOUNT", 0, 100, 1);
        styleField (evolve.label, evolve.slider, "EVOLVE", 0, 100, 1);
        styleField (vel.label, vel.slider, "VELOCITY", 1, 127, 1);
        styleField (prob.label, prob.slider, "CHANCE", 0, 100, 1);
        styleField (ratchet.label, ratchet.slider, "RATCHET", 1, 8, 1);
        styleField (nudge.label, nudge.slider, "NUDGE", -50, 50, 1);
        amount.slider.setTextValueSuffix ("%");
        evolve.slider.setTextValueSuffix ("%");
        prob.slider.setTextValueSuffix ("%");
        nudge.slider.setTextValueSuffix ("%");
        evolve.slider.setTooltip ("Each time the lane loops, a few hits come and go (always the same way for a given loop)");
        for (auto* f : { &pulses, &rotate, &amount, &evolve, &vel, &prob, &ratchet, &nudge })
        {
            addAndMakeVisible (f->label);
            addAndMakeVisible (f->slider);
        }
        for (int i = 0; i < (int) bash::RatchetShape::count; ++i) shapeBox.addItem (bash::ratchetShapeName ((bash::RatchetShape) i), i + 1);
        for (int i = 0; i < (int) bash::TrigCond::count; ++i)    condBox.addItem (bash::trigCondName ((bash::TrigCond) i), i + 1);
        shapeLabel.setText ("ROLL SHAPE", juce::dontSendNotification);
        condLabel.setText ("CONDITION", juce::dontSendNotification);
        for (auto* l : { &shapeLabel, &condLabel, &laneTitle, &stepTitle })
        {
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, colour::quiet);
            addAndMakeVisible (*l);
        }
        laneTitle.setColour (juce::Label::textColourId, colour::accent);
        stepTitle.setColour (juce::Label::textColourId, colour::accent);
        addAndMakeVisible (shapeBox);
        addAndMakeVisible (condBox);
        generateButton.setTooltip ("Rewrite this lane with its algorithm");
        generateButton.onClick = [this] { proc.generateLane (track, selLane); refresh(); };
        addAndMakeVisible (generateButton);

        auto laneEdit = [this] (std::function<void (bash::Lane&)> fn, bool regenerate)
        {
            if (updatingInspector) return;
            edit ([&] (bash::StepPattern& sp) { fn (sp.lanes[(size_t) selLane]); });
            if (regenerate) proc.generateLane (track, selLane), refresh();
        };
        auto stepEdit = [this] (std::function<void (bash::Step&)> fn)
        {
            if (updatingInspector) return;
            edit ([&] (bash::StepPattern& sp) { fn (sp.lanes[(size_t) selLane].steps[(size_t) selStep]); });
        };
        // Euclid settings rewrite the lane straight away (that's the fun of them); the others wait for Generate
        pulses.slider.onValueChange = [this, laneEdit] { laneEdit ([this] (bash::Lane& l) { l.pulses = (uint8_t) pulses.slider.getValue(); }, true); };
        rotate.slider.onValueChange = [this, laneEdit] { laneEdit ([this] (bash::Lane& l) { l.rotate = (uint8_t) rotate.slider.getValue(); }, true); };
        amount.slider.onValueChange = [this, laneEdit] { laneEdit ([this] (bash::Lane& l) { l.density = (float) amount.slider.getValue() / 100.0f; }, false); };
        evolve.slider.onValueChange = [this, laneEdit] { laneEdit ([this] (bash::Lane& l) { l.evolve = (float) evolve.slider.getValue() / 100.0f; }, false); };
        vel.slider.onValueChange     = [this, stepEdit] { stepEdit ([this] (bash::Step& s) { s.vel = (uint8_t) vel.slider.getValue(); }); };
        prob.slider.onValueChange    = [this, stepEdit] { stepEdit ([this] (bash::Step& s) { s.prob = (uint8_t) prob.slider.getValue(); }); };
        ratchet.slider.onValueChange = [this, stepEdit] { stepEdit ([this] (bash::Step& s) { s.ratchet = (uint8_t) ratchet.slider.getValue(); }); };
        nudge.slider.onValueChange   = [this, stepEdit] { stepEdit ([this] (bash::Step& s) { s.nudge = (int8_t) nudge.slider.getValue(); }); };
        shapeBox.onChange = [this, stepEdit] { stepEdit ([this] (bash::Step& s) { s.shape = (bash::RatchetShape) (shapeBox.getSelectedId() - 1); }); };
        condBox.onChange  = [this, stepEdit] { stepEdit ([this] (bash::Step& s) { s.cond = (bash::TrigCond) (condBox.getSelectedId() - 1); }); };

        setTrack (0);
    }

    StepPanel::~StepPanel() = default;

    void StepPanel::setTrack (int t)
    {
        track = t;
        auto& apvts = proc.apvts;
        fillAtt.reset(); autoFillAtt.reset(); swingAtt.reset();
        fillChoices (autoFillBox, apvts, BashProcessor::pid (t, "autofill"));
        fillAtt     = std::make_unique<ButtonAttachment> (apvts, BashProcessor::pid (t, "fill"), fillButton);
        autoFillAtt = std::make_unique<ComboAttachment>  (apvts, BashProcessor::pid (t, "autofill"), autoFillBox);
        swingAtt    = std::make_unique<SliderAttachment> (apvts, BashProcessor::pid (t, "swing"), swing);
        page = 0; selLane = 0; selStep = 0;
        refresh();
        updateInspector();
    }

    void StepPanel::refresh()
    {
        auto p = proc.stepPattern (track);
        std::array<int, bash::stepLanes> pos {};
        std::array<juce::String, bash::stepLanes> n;
        for (int l = 0; l < bash::stepLanes; ++l) { pos[(size_t) l] = proc.stepLanePosition (track, l); n[(size_t) l] = proc.laneName (track, l); }
        const bool changed = p.serialise() != pattern.serialise();
        if (changed || pos != playPos || n != names)
        {
            pattern = p; playPos = pos; names = n;
            if (changed) updateInspector();
            repaint();
        }
        const int pg = pages();
        for (int i = 0; i < 4; ++i)
        {
            pageButtons[(size_t) i].setToggleState (i == page, juce::dontSendNotification);
            pageButtons[(size_t) i].setAlpha (i < pg ? 1.0f : 0.35f);
        }
    }

    int StepPanel::pages() const
    {
        int longest = 1;
        for (const auto& l : pattern.lanes) longest = std::max (longest, (int) l.length);
        return (longest + 15) / 16;
    }

    void StepPanel::edit (const std::function<void (bash::StepPattern&)>& fn)
    {
        proc.editSteps (track, fn);
        pattern = proc.stepPattern (track);
        repaint();
    }

    void StepPanel::select (int lane, int step)
    {
        selLane = juce::jlimit (0, bash::stepLanes - 1, lane);
        selStep = juce::jlimit (0, bash::maxSteps - 1, step);
        updateInspector();
        repaint();
    }

    void StepPanel::updateInspector()
    {
        const juce::ScopedValueSetter<bool> svs (updatingInspector, true);
        const auto& l = pattern.lanes[(size_t) selLane];
        const auto& s = l.steps[(size_t) selStep];
        laneTitle.setText (names[(size_t) selLane].toUpperCase() + "  " + bash::laneAlgoName (l.algo), juce::dontSendNotification);
        stepTitle.setText ("STEP " + juce::String (selStep + 1) + (s.on ? "" : " (off)"), juce::dontSendNotification);
        pulses.slider.setRange (0, l.length, 1);
        rotate.slider.setRange (0, std::max (1, l.length - 1), 1);
        pulses.slider.setValue (l.pulses, juce::dontSendNotification);
        rotate.slider.setValue (l.rotate, juce::dontSendNotification);
        amount.slider.setValue (std::round (l.density * 100.0f), juce::dontSendNotification);
        evolve.slider.setValue (std::round (l.evolve * 100.0f), juce::dontSendNotification);
        const bool euclid = l.algo == bash::LaneAlgo::Euclid;
        for (auto* c : std::initializer_list<juce::Component*> { &pulses.label, &pulses.slider, &rotate.label, &rotate.slider }) c->setVisible (euclid);
        for (auto* c : std::initializer_list<juce::Component*> { &amount.label, &amount.slider }) c->setVisible (! euclid);
        vel.slider.setValue (s.vel, juce::dontSendNotification);
        prob.slider.setValue (s.prob, juce::dontSendNotification);
        ratchet.slider.setValue (s.ratchet, juce::dontSendNotification);
        nudge.slider.setValue (s.nudge, juce::dontSendNotification);
        shapeBox.setSelectedId ((int) s.shape + 1, juce::dontSendNotification);
        condBox.setSelectedId ((int) s.cond + 1, juce::dontSendNotification);
    }

    // ---- layout ----------------------------------------------------------------------
    void StepPanel::resized()
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (28);
        bar.removeFromLeft (toolbarReserved);
        clearButton.setBounds (bar.removeFromRight (56)); bar.removeFromRight (8);
        for (int i = 3; i >= 0; --i) { pageButtons[(size_t) i].setBounds (bar.removeFromRight (44)); bar.removeFromRight (3); }
        bar.removeFromRight (6);
        swing.setBounds (bar.removeFromRight (80)); bar.removeFromRight (4);
        swingLabel.setBounds (bar.removeFromRight (42)); bar.removeFromRight (6);
        autoFillBox.setBounds (bar.removeFromRight (112)); bar.removeFromRight (4);
        autoFillLabel.setBounds (bar.removeFromRight (60)); bar.removeFromRight (6);
        fillButton.setBounds (bar.removeFromRight (std::min (64, bar.getWidth())));

        r.removeFromTop (6);
        inspectorArea = r.removeFromBottom (44);
        r.removeFromBottom (6);
        gridArea = r;

        // inspector: lane algorithm on the left, the selected step on the right
        auto ins = inspectorArea;
        auto field = [&ins] (juce::Label& l, juce::Component& c, int w)
        {
            auto col = ins.removeFromLeft (w);
            l.setBounds (col.removeFromTop (16));
            c.setBounds (col.removeFromTop (24));
            ins.removeFromLeft (6);
        };
        laneTitle.setBounds (ins.removeFromLeft (128).withTrimmedTop (18).withHeight (24));
        auto algoArea = ins;   // pulses+rotate or amount share the same space
        field (pulses.label, pulses.slider, 62);
        field (rotate.label, rotate.slider, 62);
        {
            auto a = algoArea.removeFromLeft (130);
            amount.label.setBounds (a.removeFromTop (16));
            amount.slider.setBounds (a.removeFromTop (24));
        }
        field (evolve.label, evolve.slider, 70);
        generateButton.setBounds (ins.removeFromLeft (72).withTrimmedTop (16).withHeight (24));
        ins.removeFromLeft (14);
        stepTitle.setBounds (ins.removeFromLeft (62).withTrimmedTop (18).withHeight (24));
        field (vel.label, vel.slider, 68);
        field (prob.label, prob.slider, 62);
        field (ratchet.label, ratchet.slider, 62);
        field (shapeLabel, shapeBox, 74);
        field (condLabel, condBox, 92);
        field (nudge.label, nudge.slider, 62);
    }

    juce::Rectangle<int> StepPanel::laneRow (int lane) const
    {
        const int h = gridArea.getHeight() / bash::stepLanes;
        return { gridArea.getX(), gridArea.getY() + lane * h, gridArea.getWidth(), h - laneGap };
    }

    juce::Rectangle<int> StepPanel::headerPart (int lane, Hit part) const
    {
        auto r = laneRow (lane).removeFromLeft (headerWidth);
        auto name  = r.removeFromLeft (92); r.removeFromLeft (4);
        auto mute  = r.removeFromLeft (20); r.removeFromLeft (4);
        auto len   = r.removeFromLeft (30); r.removeFromLeft (4);
        auto speed = r.removeFromLeft (38); r.removeFromLeft (4);
        auto algo  = r.removeFromLeft (52); r.removeFromLeft (4);
        auto die   = r.removeFromLeft (14);
        switch (part)
        {
            case Hit::Name:   return name;
            case Hit::Mute:   return mute;
            case Hit::Length: return len;
            case Hit::Speed:  return speed;
            case Hit::Algo:   return algo;
            case Hit::Die:    return die;
            default:          return {};
        }
    }

    juce::Rectangle<int> StepPanel::stepCell (int lane, int i) const
    {
        auto row = laneRow (lane);
        row.removeFromLeft (headerWidth + 8);
        const float w = (float) row.getWidth() / 16.0f;
        const int x0 = row.getX() + (int) std::round (w * i) + (i / 4) * 0 ;
        const int x1 = row.getX() + (int) std::round (w * (i + 1));
        return { x0, row.getY(), x1 - x0 - 3, row.getHeight() };
    }

    StepPanel::HitInfo StepPanel::hitAt (juce::Point<int> p) const
    {
        for (int l = 0; l < bash::stepLanes; ++l)
        {
            const auto row = laneRow (l).expanded (0, laneGap / 2 + 1);
            if (! row.contains (p)) continue;
            for (auto part : { Hit::Name, Hit::Mute, Hit::Length, Hit::Speed, Hit::Algo, Hit::Die })
                if (headerPart (l, part).contains (p)) return { part, l, -1 };
            for (int i = 0; i < 16; ++i)
                if (stepCell (l, i).expanded (2, 0).contains (p)) return { Hit::Step, l, page * 16 + i };
            return { Hit::None, l, -1 };
        }
        return {};
    }

    // ---- painting ----------------------------------------------------------------------
    void StepPanel::paint (juce::Graphics& g)
    {
        const auto small = juce::FontOptions (11.0f, juce::Font::bold);
        for (int l = 0; l < bash::stepLanes; ++l)
        {
            const auto& lane = pattern.lanes[(size_t) l];
            const bool selected = l == selLane;
            const float dim = lane.mute ? 0.35f : 1.0f;

            // header
            auto name = headerPart (l, Hit::Name);
            g.setColour (selected ? colour::active : colour::raised);
            g.fillRoundedRectangle (name.toFloat(), 4.0f);
            g.setColour ((selected ? colour::accent : colour::ink).withMultipliedAlpha (dim));
            g.setFont (small);
            g.drawFittedText (names[(size_t) l], name.reduced (6, 0), juce::Justification::centredLeft, 1, 0.8f);

            auto chip = [&] (Hit part, const juce::String& text, bool on)
            {
                const auto r = headerPart (l, part).toFloat();
                g.setColour (on ? colour::accent : colour::raised);
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (on ? colour::background : colour::quiet.withMultipliedAlpha (dim));
                g.drawText (text, r, juce::Justification::centred);
            };
            chip (Hit::Mute, "M", lane.mute);
            chip (Hit::Length, juce::String ((int) lane.length), false);
            chip (Hit::Speed, bash::laneSpeeds()[(size_t) lane.speed].first, lane.speed != bash::defaultLaneSpeed);
            chip (Hit::Algo, bash::laneAlgoName (lane.algo), false);
            {
                // the lane die
                auto r = headerPart (l, Hit::Die).toFloat().withSizeKeepingCentre (13.0f, 13.0f);
                g.setColour (colour::accent.withMultipliedAlpha (dim));
                g.drawRoundedRectangle (r, 3.0f, 1.3f);
                const float d = 2.4f;
                for (auto [fx, fy] : { std::pair { 0.3f, 0.3f }, std::pair { 0.7f, 0.7f }, std::pair { 0.5f, 0.5f } })
                    g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ r.getX() + r.getWidth() * fx, r.getY() + r.getHeight() * fy }));
            }

            // steps
            for (int i = 0; i < 16; ++i)
            {
                const int idx = page * 16 + i;
                const auto cell = stepCell (l, i).toFloat();
                const bool inLength = idx < lane.length;
                const auto& s = lane.steps[(size_t) idx];
                const bool beat = (i % 4) == 0;
                if (! inLength)
                {
                    g.setColour (colour::background);
                    g.fillRoundedRectangle (cell, 3.0f);
                    continue;
                }
                g.setColour (beat ? colour::raised.brighter (0.06f) : colour::raised);
                g.fillRoundedRectangle (cell, 3.0f);
                if (s.on)
                {
                    const float a = (s.prob < 100 ? 0.45f + 0.55f * s.prob / 100.0f : 1.0f) * dim;
                    g.setColour (colour::accentDim.withMultipliedAlpha (a));
                    g.fillRoundedRectangle (cell, 3.0f);
                    const float h = cell.getHeight() * (float) s.vel / 127.0f;
                    g.setColour (colour::accent.withMultipliedAlpha (a));
                    g.fillRoundedRectangle (cell.withTop (cell.getBottom() - h), 3.0f);
                    if (s.ratchet > 1)
                    {
                        g.setColour (colour::background.withAlpha (0.8f));
                        const float seg = cell.getWidth() / s.ratchet;
                        for (int k = 1; k < s.ratchet; ++k)
                            g.fillRect (cell.getX() + seg * k - 0.5f, cell.getY() + 2.0f, 1.2f, cell.getHeight() - 4.0f);
                    }
                    const juce::String c = condShort (s.cond);
                    if (c.isNotEmpty())
                    {
                        g.setColour (colour::background);
                        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                        g.drawText (c, cell.reduced (3.0f, 0.0f), juce::Justification::centredRight);
                    }
                    if (s.nudge != 0)
                    {
                        g.setColour (colour::ink);
                        const float x = cell.getCentreX() + cell.getWidth() * 0.5f * s.nudge / 50.0f;
                        g.fillRect (x - 1.0f, cell.getY(), 2.0f, 3.0f);
                    }
                }
                if (playPos[(size_t) l] == idx)
                {
                    g.setColour (colour::ink.withAlpha (0.85f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.5f);
                }
                if (selected && idx == selStep)
                {
                    g.setColour (colour::accent);
                    g.drawRoundedRectangle (cell.expanded (1.0f), 4.0f, 1.2f);
                }
            }
        }

        // inspector background
        g.setColour (colour::line);
        g.drawHorizontalLine (inspectorArea.getY() - 4, (float) inspectorArea.getX(), (float) inspectorArea.getRight());
    }

    // ---- mouse -------------------------------------------------------------------------
    void StepPanel::mouseDown (const juce::MouseEvent& e)
    {
        const auto h = hitAt (e.getPosition());
        painting = velocityDrag = false;
        if (h.lane < 0) return;
        switch (h.what)
        {
            case Hit::Step:
            {
                if (e.mods.isPopupMenu()) { select (h.lane, h.step); showStepMenu (h.lane, h.step); return; }
                if (h.step >= pattern.lanes[(size_t) h.lane].length) return;
                const bool wasOn = pattern.lanes[(size_t) h.lane].steps[(size_t) h.step].on;
                select (h.lane, h.step);
                if (e.mods.isAltDown() || e.mods.isShiftDown())   // drag velocity without toggling
                {
                    velocityDrag = wasOn;
                    dragStartVel = pattern.lanes[(size_t) h.lane].steps[(size_t) h.step].vel;
                    dragLane = h.lane;
                    return;
                }
                paintOn = ! wasOn;
                edit ([&] (bash::StepPattern& sp) { sp.lanes[(size_t) h.lane].steps[(size_t) h.step].on = paintOn; });
                painting = true; dragLane = h.lane; lastPaintStep = h.step;
                dragStartVel = pattern.lanes[(size_t) h.lane].steps[(size_t) h.step].vel;
                updateInspector();
                return;
            }
            case Hit::Name:
                select (h.lane, selStep);
                if (proc.engineType (track) == bash::EngineType::Drums) proc.auditionCell (h.lane);
                return;
            case Hit::Mute:   edit ([&] (bash::StepPattern& sp) { auto& l = sp.lanes[(size_t) h.lane]; l.mute = ! l.mute; }); return;
            case Hit::Length: select (h.lane, selStep); showLengthMenu (h.lane); return;
            case Hit::Speed:  select (h.lane, selStep); showSpeedMenu (h.lane); return;
            case Hit::Algo:   select (h.lane, selStep); showAlgoMenu (h.lane); return;
            case Hit::Die:
                select (h.lane, selStep);
                proc.rollLane (track, h.lane);
                refresh();
                if (onMessage) onMessage ("Rolled " + names[(size_t) h.lane] + " (" + bash::laneAlgoName (pattern.lanes[(size_t) h.lane].algo) + "). The track die's menu can undo it.");
                return;
            default: return;
        }
    }

    void StepPanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (velocityDrag)
        {
            const int v = juce::jlimit (1, 127, dragStartVel - e.getDistanceFromDragStartY());
            edit ([&] (bash::StepPattern& sp) { sp.lanes[(size_t) selLane].steps[(size_t) selStep].vel = (uint8_t) v; });
            updateInspector();
            return;
        }
        if (! painting) return;
        const auto h = hitAt ({ e.getPosition().x, laneRow (dragLane).getCentreY() });
        if (h.what == Hit::Step && h.lane == dragLane && h.step != lastPaintStep && h.step < pattern.lanes[(size_t) dragLane].length)
        {
            lastPaintStep = h.step;
            edit ([&] (bash::StepPattern& sp) { sp.lanes[(size_t) dragLane].steps[(size_t) h.step].on = paintOn; });
        }
        else if (h.step == lastPaintStep && paintOn && std::abs (e.getDistanceFromDragStartY()) > 4 && e.getDistanceFromDragStartX() == 0)
        {
            // vertical drag on a step just switched on sets its velocity
            const int v = juce::jlimit (1, 127, dragStartVel - e.getDistanceFromDragStartY());
            edit ([&] (bash::StepPattern& sp) { sp.lanes[(size_t) dragLane].steps[(size_t) lastPaintStep].vel = (uint8_t) v; });
        }
    }

    void StepPanel::mouseUp (const juce::MouseEvent&)
    {
        painting = velocityDrag = false;
        updateInspector();
    }

    // ---- menus -------------------------------------------------------------------------
    void StepPanel::showStepMenu (int lane, int step)
    {
        juce::PopupMenu m, rat, cond, chance, vel, nud;
        const auto s = pattern.lanes[(size_t) lane].steps[(size_t) step];
        for (int r : { 1, 2, 3, 4, 6, 8 }) rat.addItem (100 + r, juce::String (r) + (r == 1 ? " (none)" : " hits"), true, s.ratchet == r);
        for (int c = 0; c < (int) bash::TrigCond::count; ++c) cond.addItem (200 + c, bash::trigCondName ((bash::TrigCond) c), true, (int) s.cond == c);
        for (int p : { 100, 75, 50, 25, 10 }) chance.addItem (300 + p, juce::String (p) + "%", true, s.prob == p);
        for (int v : { 127, 110, 90, 70, 45 }) vel.addItem (500 + v, juce::String (v), true, s.vel == v);
        for (int n : { -25, -10, 0, 10, 25 }) nud.addItem (700 + n, (n > 0 ? "+" : "") + juce::String (n) + "%", true, s.nudge == n);
        m.addSectionHeader (names[(size_t) lane] + "  step " + juce::String (step + 1));
        m.addItem (1, s.on ? "Turn off" : "Turn on");
        m.addSubMenu ("Ratchet", rat);
        m.addSubMenu ("Condition", cond);
        m.addSubMenu ("Chance", chance);
        m.addSubMenu ("Velocity", vel);
        m.addSubMenu ("Nudge", nud);
        m.addSeparator();
        m.addItem (2, "Reset this step");
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, lane, step] (int r)
        {
            if (r == 0) return;
            edit ([&] (bash::StepPattern& sp)
            {
                auto& st = sp.lanes[(size_t) lane].steps[(size_t) step];
                if (r == 1) st.on = ! st.on;
                else if (r == 2) st = bash::Step{};
                else if (r < 200) { st.ratchet = (uint8_t) (r - 100); st.on = true; }
                else if (r < 300) { st.cond = (bash::TrigCond) (r - 200); st.on = true; }
                else if (r < 500) st.prob = (uint8_t) (r - 300);
                else if (r < 650) st.vel = (uint8_t) (r - 500);
                else st.nudge = (int8_t) (r - 700);
            });
            updateInspector();
        });
    }

    void StepPanel::showLengthMenu (int lane)
    {
        juce::PopupMenu m;
        const int cur = pattern.lanes[(size_t) lane].length;
        m.addSectionHeader ("Lane length (steps)");
        for (int v : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 24, 28, 32, 40, 48, 56, 64 })
            m.addItem (v, juce::String (v), true, v == cur);
        m.addSeparator();
        m.addItem (1000, "Set every lane to " + juce::String (cur));
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, lane, cur] (int r)
        {
            if (r == 0) return;
            if (r == 1000) edit ([cur] (bash::StepPattern& sp) { for (auto& l : sp.lanes) l.length = (uint8_t) cur; });
            else edit ([lane, r] (bash::StepPattern& sp) { sp.lanes[(size_t) lane].length = (uint8_t) r; });
            refresh();
            updateInspector();
        });
    }

    void StepPanel::showSpeedMenu (int lane)
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Lane speed");
        const auto& sp = bash::laneSpeeds();
        for (int i = 0; i < (int) sp.size(); ++i) m.addItem (i + 1, sp[(size_t) i].first, true, pattern.lanes[(size_t) lane].speed == i);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, lane] (int r)
        {
            if (r > 0) edit ([lane, r] (bash::StepPattern& p) { p.lanes[(size_t) lane].speed = (uint8_t) (r - 1); });
        });
    }

    void StepPanel::showAlgoMenu (int lane)
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Lane algorithm (used by Generate and the lane die)");
        for (int a = 0; a < (int) bash::LaneAlgo::count; ++a)
            m.addItem (a + 1, bash::laneAlgoName ((bash::LaneAlgo) a), true, (int) pattern.lanes[(size_t) lane].algo == a);
        m.addSeparator();
        m.addItem (100, "Generate now");
        m.addItem (101, "Clear lane");
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, lane] (int r)
        {
            if (r == 0) return;
            if (r == 100) { proc.generateLane (track, lane); refresh(); return; }
            if (r == 101) { edit ([lane] (bash::StepPattern& p) { for (auto& s : p.lanes[(size_t) lane].steps) s = bash::Step{}; }); return; }
            edit ([lane, r] (bash::StepPattern& p) { p.lanes[(size_t) lane].algo = (bash::LaneAlgo) (r - 1); });
            updateInspector();
        });
    }
}
