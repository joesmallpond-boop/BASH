#include "ModPanel.h"

using namespace bashui;

// ============================================================================
void LfoView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (colour::raised.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour::line);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const auto shape = (bash::LfoShape) (int) *proc.apvts.getRawParameterValue ("lfo" + juce::String (lfo + 1) + "shape");
    juce::Path p;
    const int steps = 200;
    for (int i = 0; i <= steps; ++i)
    {
        const double ph = (double) i / steps;
        float held = std::sin ((float) (std::floor (ph * 4.0) * 2.1)), prev = std::sin ((float) ((std::floor (ph * 4.0) - 1.0) * 2.1));
        float v = bash::Lfo::shapeAt (shape, ph, held, prev);
        if (shape == bash::LfoShape::SampleHold || shape == bash::LfoShape::Smooth)
            v = bash::Lfo::shapeAt (shape, std::fmod (ph * 4.0, 1.0), held, prev);
        const float x = r.getX() + r.getWidth() * (float) ph;
        const float y = r.getCentreY() - v * r.getHeight() * 0.4f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colour::accentDim.darker (0.2f));
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const double ph = proc.lfoPhase (lfo);
    const float v = proc.modSourceValue (0, lfo == 0 ? bash::ModSource::Lfo1 : bash::ModSource::Lfo2);
    const float x = r.getX() + r.getWidth() * (float) ph;
    const float y = r.getCentreY() - v * r.getHeight() * 0.4f;
    g.setColour (colour::accent);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, y }));
}

// ============================================================================
juce::Rectangle<float> MatrixGrid::cellBounds (int d, int s) const
{
    const float w = (float) (getWidth() - labelW) / (float) bash::numModSources;
    const float h = (float) (getHeight() - headerH) / (float) bash::numModDests;
    return { (float) labelW + (float) s * w, (float) headerH + (float) d * h, w, h };
}

bool MatrixGrid::cellAt (juce::Point<float> p, int& d, int& s) const
{
    for (d = 0; d < bash::numModDests; ++d)
        for (s = 0; s < bash::numModSources; ++s)
            if (cellBounds (d, s).contains (p)) return true;
    return false;
}

void MatrixGrid::paint (juce::Graphics& g)
{
    auto& m = proc.matrix (track);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (int s = 0; s < bash::numModSources; ++s)
    {
        auto top = cellBounds (0, s).withY (0.0f).withHeight ((float) headerH);
        g.setColour (colour::ink);
        g.drawText (bash::modSourceName ((bash::ModSource) s), top.removeFromTop (22.0f), juce::Justification::centred);
        // live source value
        const float v = proc.modSourceValue (track, (bash::ModSource) s);
        auto bar = top.reduced (10.0f, 4.0f);
        g.setColour (colour::line);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colour::accent);
        const float cx = bar.getCentreX(), half = bar.getWidth() * 0.5f;
        g.fillRect (juce::Rectangle<float> (std::min (cx, cx + v * half), bar.getY(), std::abs (v * half), bar.getHeight()));
    }

    for (int d = 0; d < bash::numModDests; ++d)
    {
        const auto row = cellBounds (d, 0);
        g.setColour (d % 2 ? colour::panel : colour::background);
        g.fillRect (juce::Rectangle<float> (0.0f, row.getY(), (float) getWidth(), row.getHeight()));
        g.setColour (colour::quiet);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (bash::modDestName ((bash::ModDest) d), juce::Rectangle<float> (8.0f, row.getY(), (float) labelW - 8.0f, row.getHeight()),
                    juce::Justification::centredLeft);

        for (int s = 0; s < bash::numModSources; ++s)
        {
            auto c = cellBounds (d, s).reduced (4.0f, 4.0f);
            const float a = m.get ((bash::ModDest) d, (bash::ModSource) s);
            g.setColour (colour::line);
            g.drawVerticalLine ((int) c.getCentreX(), c.getY(), c.getBottom());
            if (std::abs (a) > 0.0f)
            {
                const float cx = c.getCentreX(), half = c.getWidth() * 0.5f;
                g.setColour (a > 0.0f ? colour::accentDim : colour::accent.withAlpha (0.8f));
                g.fillRoundedRectangle (juce::Rectangle<float> (std::min (cx, cx + a * half), c.getY(), std::abs (a * half), c.getHeight()), 3.0f);
                g.setColour (colour::ink);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText ((a > 0 ? "+" : "") + juce::String (a, 2), c, juce::Justification::centred);
            }
        }
    }
}

void MatrixGrid::mouseDown (const juce::MouseEvent& e)
{
    if (! cellAt (e.position, dragDest, dragSrc)) { dragDest = dragSrc = -1; return; }
    dragStart = proc.matrix (track).get ((bash::ModDest) dragDest, (bash::ModSource) dragSrc);
}

void MatrixGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDest < 0) return;
    const float sens = e.mods.isShiftDown() ? 0.001f : 0.006f;
    const float v = juce::jlimit (-1.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() * sens);
    proc.matrix (track).set ((bash::ModDest) dragDest, (bash::ModSource) dragSrc, std::abs (v) < 0.01f ? 0.0f : v);
    repaint();
}

void MatrixGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    int d, s;
    if (cellAt (e.position, d, s)) { proc.matrix (track).set ((bash::ModDest) d, (bash::ModSource) s, 0.0f); repaint(); }
}

// ============================================================================
ModPanel::ModPanel (BashProcessor& p) : proc (p), grid (p)
{
    auto& apvts = proc.apvts;
    for (int i = 0; i < 2; ++i)
    {
        auto& L = lfos[(size_t) i];
        const juce::String pre = "lfo" + juce::String (i + 1);
        fillChoices (L.shape, apvts, pre + "shape");
        fillChoices (L.division, apvts, pre + "div");
        L.shapeAtt = std::make_unique<ComboAttachment> (apvts, pre + "shape", L.shape);
        L.divAtt   = std::make_unique<ComboAttachment> (apvts, pre + "div", L.division);
        L.syncAtt  = std::make_unique<ButtonAttachment> (apvts, pre + "sync", L.sync);
        styleKnob (L.rate, "RATE");
        L.rateAtt  = std::make_unique<SliderAttachment> (apvts, pre + "rate", L.rate.slider);
        L.view = std::make_unique<LfoView> (proc, i);
        for (juce::Component* c : { (juce::Component*) &L.shape, (juce::Component*) &L.division, (juce::Component*) &L.sync,
                                    (juce::Component*) &L.rate.slider, (juce::Component*) &L.rate.label, (juce::Component*) L.view.get() })
            addAndMakeVisible (*c);
    }
    styleKnob (lifeRate, "LIFE RATE");
    addAndMakeVisible (lifeRate.slider);
    addAndMakeVisible (lifeRate.label);
    addAndMakeVisible (grid);
    lifeTrace.assign (240, 0.0f);
}

void ModPanel::setTrack (int t)
{
    track = t;
    lifeAtt.reset();
    lifeAtt = std::make_unique<SliderAttachment> (proc.apvts, BashProcessor::pid (t, "liferate"), lifeRate.slider);
    grid.setTrack (t);
    std::fill (lifeTrace.begin(), lifeTrace.end(), 0.0f);
    repaint();
}

void ModPanel::refresh()
{
    for (auto& L : lfos)
    {
        const bool synced = L.sync.getToggleState();
        L.division.setVisible (synced);
        L.rate.slider.setEnabled (! synced);
        L.view->repaint();
    }
    lifeTrace.erase (lifeTrace.begin());
    lifeTrace.push_back (proc.modSourceValue (track, bash::ModSource::Life));
    repaint (lifeTraceArea);
    grid.repaint();
}

void ModPanel::paint (juce::Graphics& g)
{
    auto box = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (colour::panel);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (colour::line);
        g.drawRoundedRectangle (r.toFloat(), 14.0f, 1.0f);
        g.setColour (colour::ink);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 16, r.getY() + 10, 300, 16, juce::Justification::centredLeft);
    };
    box (lfoBoxes[0], "LFO 1");
    box (lfoBoxes[1], "LFO 2");
    box (lifeBox, "LIFE  (track " + juce::String (track + 1) + ")");
    box (matrixBox, "MOD MATRIX  (track " + juce::String (track + 1) + ")");
    g.setColour (colour::quiet);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("Drag a cell up or down to set its amount (shift = fine). Double-click to clear.",
                matrixBox.getX() + 16, matrixBox.getBottom() - 24, matrixBox.getWidth() - 32, 16, juce::Justification::centredLeft);

    // Life trace
    auto r = lifeTraceArea.toFloat();
    g.setColour (colour::raised.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 8.0f);
    juce::Path p;
    for (size_t i = 0; i < lifeTrace.size(); ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) (lifeTrace.size() - 1);
        const float y = r.getCentreY() - lifeTrace[i] * r.getHeight() * 0.42f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colour::accentDim.darker (0.2f));
    g.strokePath (p, juce::PathStrokeType (2.0f));
}

void ModPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft (300);
    area.removeFromLeft (16);

    for (int i = 0; i < 2; ++i)
    {
        lfoBoxes[i] = left.removeFromTop (230);
        left.removeFromTop (12);
        auto& L = lfos[(size_t) i];
        auto in = lfoBoxes[i].reduced (16).withTrimmedTop (22);
        auto row = in.removeFromTop (30);
        L.shape.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (8);
        L.sync.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (8);
        L.division.setBounds (row);
        in.removeFromTop (8);
        placeKnob (L.rate, in.removeFromLeft (80));
        in.removeFromLeft (8);
        L.view->setBounds (in);
    }
    lifeBox = left.removeFromTop (std::min (left.getHeight(), 200));
    auto in = lifeBox.reduced (16).withTrimmedTop (22);
    placeKnob (lifeRate, in.removeFromLeft (80));
    in.removeFromLeft (8);
    lifeTraceArea = in;

    matrixBox = area;
    grid.setBounds (area.reduced (16).withTrimmedTop (24).withTrimmedBottom (20));
}

// ============================================================================
void DiceButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    if (pressed) r = r.reduced (2.0f);
    g.setColour (colour::panel);
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (colour::accent);
    g.drawRoundedRectangle (r, 9.0f, 2.0f);
    const float d = r.getWidth() * 0.16f;
    auto pip = [&] (float fx, float fy) { g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ r.getX() + r.getWidth() * fx, r.getY() + r.getHeight() * fy })); };
    g.setColour (colour::accent);
    switch (face)
    {
        case 1: pip (0.5f, 0.5f); break;
        case 2: pip (0.28f, 0.28f); pip (0.72f, 0.72f); break;
        case 3: pip (0.28f, 0.28f); pip (0.5f, 0.5f); pip (0.72f, 0.72f); break;
        case 4: pip (0.28f, 0.28f); pip (0.72f, 0.28f); pip (0.28f, 0.72f); pip (0.72f, 0.72f); break;
        case 6: pip (0.28f, 0.25f); pip (0.72f, 0.25f); pip (0.28f, 0.5f); pip (0.72f, 0.5f); pip (0.28f, 0.75f); pip (0.72f, 0.75f); break;
        default: pip (0.28f, 0.28f); pip (0.72f, 0.28f); pip (0.5f, 0.5f); pip (0.28f, 0.72f); pip (0.72f, 0.72f); break;
    }
}

void DiceButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    pressed = true;
    repaint();
}

void DiceButton::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed) return;
    pressed = false;
    if (getLocalBounds().contains (e.getPosition()))
    {
        proc.rollDice (currentTrack ? currentTrack() : 0);
        face = 1 + juce::Random::getSystemRandom().nextInt (6);
        if (onRolled) onRolled();
    }
    repaint();
}

void DiceButton::showMenu()
{
    auto& d = proc.dice;
    juce::PopupMenu m, scope, chaos;
    for (int i = 0; i < proc.diceScopeNames().size(); ++i)
        scope.addItem (100 + i, proc.diceScopeNames()[i], true, d.scope == i);
    const float amounts[] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (int i = 0; i < 5; ++i)
        chaos.addItem (200 + i, juce::String ((int) (amounts[i] * 100)) + "%" + (i == 4 ? "  (anything goes)" : ""), true, std::abs (d.chaos - amounts[i]) < 0.01f);
    m.addSectionHeader ("Roll the Dice");
    m.addSubMenu ("Scope", scope);
    m.addSubMenu ("Chaos", chaos);
    m.addSeparator();
    m.addItem (300, "Also roll key and scale", true, d.includeKey);
    m.addItem (301, "Also roll tempo", true, d.includeTempo);
    m.addItem (302, "Also roll levels and pan", true, d.includeLevels);
    m.addItem (303, "New patterns (locks kept)", true, d.includePatterns);
    m.addSeparator();
    m.addItem (400, "Undo last roll (" + juce::String (proc.diceUndoCount()) + " stored)", proc.diceUndoCount() > 0);
    m.addItem (401, "Clear protected knobs (" + juce::String (proc.diceLocked.size()) + ")", proc.diceLocked.size() > 0);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, amounts] (int r)
    {
        auto& dd = proc.dice;
        if (r >= 100 && r < 200) dd.scope = r - 100;
        else if (r >= 200 && r < 300) dd.chaos = amounts[r - 200];
        else if (r == 300) dd.includeKey = ! dd.includeKey;
        else if (r == 301) dd.includeTempo = ! dd.includeTempo;
        else if (r == 302) dd.includeLevels = ! dd.includeLevels;
        else if (r == 303) dd.includePatterns = ! dd.includePatterns;
        else if (r == 400) { proc.undoDice(); if (onRolled) onRolled(); }
        else if (r == 401) proc.diceLocked.clear();
    });
}

// ============================================================================
struct DiceLocks::Listener : public juce::MouseListener
{
    Listener (BashProcessor& p, juce::Slider& s, juce::Label& l, const juce::String& i) : proc (p), slider (s), label (l), id (i)
    {
        slider.addMouseListener (this, false);
        update();
    }
    ~Listener() override { slider.removeMouseListener (this); label.setColour (juce::Label::textColourId, colour::quiet); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        const bool locked = proc.diceLocked.contains (id);
        m.addItem (1, "Protect from Roll the Dice", true, locked);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [this, locked] (int r)
        {
            if (r != 1) return;
            if (locked) proc.diceLocked.removeString (id);
            else        proc.diceLocked.addIfNotAlreadyThere (id);
            update();
        });
    }
    void update() { label.setColour (juce::Label::textColourId, proc.diceLocked.contains (id) ? colour::accent : colour::quiet); }

    BashProcessor& proc;
    juce::Slider& slider;
    juce::Label& label;
    juce::String id;
};

DiceLocks::DiceLocks (BashProcessor& p) : proc (p) {}
DiceLocks::~DiceLocks() { clear(); }

void DiceLocks::bind (juce::Slider& s, juce::Label& l, const juce::String& id)
{
    listeners.push_back (std::make_unique<Listener> (proc, s, l, id));
}

void DiceLocks::clear() { listeners.clear(); }

void DiceLocks::refresh() { for (auto& l : listeners) l->update(); }
