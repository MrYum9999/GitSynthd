#include "PluginEditor.h"

namespace
{
    constexpr int kWidth = 1120, kHeight = 640, kCheek = 34;

    juce::String noteName (float hz, float& centsOut)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int   n    = (int) std::lround (midi);
        centsOut = (midi - (float) n) * 100.0f;
        const int pc = ((n % 12) + 12) % 12;
        return juce::String (names[pc]) + juce::String (n / 12 - 1);
    }

    void drawWood (juce::Graphics& g, juce::Rectangle<float> r, uint32_t seed)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6b3d22), r.getX(), 0,
                                                 juce::Colour (0xff3b1f10), r.getRight(), 0, false));
        g.fillRect (r);
        gs::Rng rng (seed);
        for (int i = 0; i < 46; ++i)
        {
            juce::Path p;
            float x = r.getX() + rng.uni() * r.getWidth();
            p.startNewSubPath (x, r.getY());
            for (float y = r.getY(); y <= r.getBottom(); y += 20.0f)
            {
                x += (rng.uni() - 0.5f) * 1.6f;
                p.lineTo (juce::jlimit (r.getX(), r.getRight(), x), y);
            }
            g.setColour (juce::Colour (rng.uni() < 0.5f ? 0x22000000u : 0x14ffd9a0u));
            g.strokePath (p, juce::PathStrokeType (0.6f + rng.uni() * 1.4f));
        }
        // bevel
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRect (r, 1.5f);
        g.setColour (juce::Colour (0x30ffffff));
        g.drawLine (r.getX() + 2, r.getY() + 2, r.getX() + 2, r.getBottom() - 2, 1.0f);
    }
}

juce::Font Retro::font (float h, bool bold, bool italic)
{
    juce::String style = bold ? (italic ? "Bold Italic" : "Bold") : (italic ? "Italic" : "Regular");
    return juce::Font (juce::FontOptions (h).withStyle (style));
}

//==============================================================================
RetroLookAndFeel::RetroLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Retro::amber);
    setColour (juce::AlertWindow::backgroundColourId, Retro::panelLight);
    setColour (juce::AlertWindow::textColourId, Retro::silk);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0c120f));
    setColour (juce::TextEditor::textColourId, Retro::vfd);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3c3936));
    setColour (juce::TextButton::textColourOffId, Retro::silk);
}

void RetroLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float a0, float a1, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto c = bounds.getCentre();

    // Silkscreened scale: 11 ticks, 0..10
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        const float r1 = radius * 0.86f, r2 = radius * (i % 5 == 0 ? 1.0f : 0.95f);
        g.setColour (Retro::silk.withAlpha (i % 5 == 0 ? 0.95f : 0.6f));
        g.drawLine (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a),
                    c.x + r2 * std::sin (a), c.y - r2 * std::cos (a), i % 5 == 0 ? 1.6f : 1.0f);
    }

    // Knurled black skirt
    const float rb = radius * 0.78f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - rb + 1.5f, c.y - rb + 2.5f, rb * 2, rb * 2);   // drop shadow
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3836), c.x, c.y - rb,
                                             juce::Colour (0xff0d0d0d), c.x, c.y + rb, false));
    g.fillEllipse (c.x - rb, c.y - rb, rb * 2, rb * 2);
    g.setColour (juce::Colour (0x22ffffff));
    for (int i = 0; i < 36; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f + pos * 2.0f;
        g.drawLine (c.x + rb * 0.84f * std::sin (a), c.y - rb * 0.84f * std::cos (a),
                    c.x + rb * 0.99f * std::sin (a), c.y - rb * 0.99f * std::cos (a), 0.8f);
    }

    // Cream cap
    const float rc = rb * 0.66f;
    g.setGradientFill (juce::ColourGradient (Retro::cream.brighter (0.25f), c.x - rc * 0.4f, c.y - rc * 0.6f,
                                             Retro::cream.darker (0.45f), c.x + rc * 0.5f, c.y + rc, true));
    g.fillEllipse (c.x - rc, c.y - rc, rc * 2, rc * 2);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (c.x - rc, c.y - rc, rc * 2, rc * 2, 1.0f);

    // Pointer line across skirt and cap
    const float a = a0 + pos * (a1 - a0);
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.6f, -rb * 0.98f, 3.2f, rb * 0.62f, 1.2f);
    g.setColour (juce::Colour (0xff181818));
    g.fillPath (ptr, juce::AffineTransform::rotation (a).translated (c.x, c.y));
    juce::Path tip;
    tip.addRoundedRectangle (-1.0f, -rb * 0.98f, 2.0f, rb * 0.26f, 1.0f);
    g.setColour (Retro::silk);
    g.fillPath (tip, juce::AffineTransform::rotation (a).translated (c.x, c.y));
}

//==============================================================================
LedButton::LedButton (const juce::String& buttonText, juce::Colour led)
    : juce::Button (buttonText), ledColour (led)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LedButton::setShowLed (bool shouldShow) { showLed = shouldShow; repaint(); }

void LedButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = getToggleState();
    const float ledD = 8.0f;

    if (showLed)
    {
    // LED
    const juce::Point<float> lc (r.getCentreX(), r.getY() + ledD * 0.5f + 1.0f);
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (ledColour.withAlpha (0.55f), lc.x, lc.y,
                                                 ledColour.withAlpha (0.0f), lc.x + 11.0f, lc.y, true));
        g.fillEllipse (lc.x - 11, lc.y - 11, 22, 22);
    }
    g.setColour (on ? ledColour.brighter (0.3f) : ledColour.darker (2.6f));
    g.fillEllipse (lc.x - ledD * 0.5f, lc.y - ledD * 0.5f, ledD, ledD);
    g.setColour (on ? juce::Colours::white.withAlpha (0.7f) : juce::Colour (0x30ffffff));
    g.fillEllipse (lc.x - 1.8f, lc.y - 2.6f, 2.4f, 2.0f);
    }

    // Rubber key
    auto key = r.withTrimmedTop (ledD + 5.0f);   // same key size with or without the LED
    if (down) key = key.translated (0.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (key.translated (0.0f, 2.0f), 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (down ? 0xff3c3a37 : 0xff57534e), key.getX(), key.getY(),
                                             juce::Colour (0xff2a2826), key.getX(), key.getBottom(), false));
    g.fillRoundedRectangle (key, 3.0f);
    g.setColour (juce::Colour (over ? 0x50ffffff : 0x28ffffff));
    g.drawLine (key.getX() + 3, key.getY() + 1, key.getRight() - 3, key.getY() + 1, 1.0f);

    g.setColour (Retro::silk);
    g.setFont (Retro::font (std::min (12.0f, key.getHeight() * 0.55f)));
    g.drawFittedText (getButtonText(), key.toNearestInt().reduced (2, 0), juce::Justification::centred, 1);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& text) : label (text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);

    if (auto* p = s.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);
    r.removeFromBottom (16);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (Retro::silk);
    g.setFont (Retro::font (11.5f));
    g.drawText (label, getLocalBounds().removeFromTop (15), juce::Justification::centred);

    auto vr = getLocalBounds().removeFromBottom (15).withSizeKeepingCentre (std::min (getWidth() - 6, 78), 14);
    g.setColour (juce::Colour (0xff0d0a07));
    g.fillRoundedRectangle (vr.toFloat(), 2.0f);
    g.setColour (Retro::amber);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold)));
    g.drawText (slider.getTextFromValue (slider.getValue()), vr, juce::Justification::centred);
}

//==============================================================================
ParamToggle::ParamToggle (juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& buttonText, juce::Colour led)
    : LedButton (buttonText, led)
{
    setClickingTogglesState (true);
    param = s.getParameter (id);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, *this);
}

//==============================================================================
ChoiceButtons::ChoiceButtons (juce::AudioProcessorValueTreeState& s, const char* id, const juce::StringArray& labels)
{
    param = s.getParameter (id);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new LedButton (labels[i]));
        b->onClick = [this, i] { select (i); };
        addAndMakeVisible (b);
    }
    att = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
    {
        const int idx = juce::roundToInt (v);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    });
    att->sendInitialUpdate();
}

void ChoiceButtons::select (int index) { att->setValueAsCompleteGesture ((float) index); }

void ChoiceButtons::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / std::max (1, buttons.size());
    for (auto* b : buttons) b->setBounds (r.removeFromLeft (w).reduced (2, 0));
}

//==============================================================================
RackModulePanel::RackModulePanel (GitSynthdEditor& e, int id) : moduleId (id), editor (e)
{
    auto& s = e.proc.apvts;
    switch (id)
    {
        case Rack::Bitcrush:
            onButton = std::make_unique<ParamToggle> (s, ParamID::crushOn, "ON");
            knobA = std::make_unique<Knob> (s, ParamID::crushRate, "CRUSH");
            knobB = std::make_unique<Knob> (s, ParamID::crushBits, "RESOLUTION");
            break;
        case Rack::RingMod:
            onButton = std::make_unique<ParamToggle> (s, ParamID::ringOn, "ON");
            knobA = std::make_unique<Knob> (s, ParamID::ringFreq, "FREQ");
            knobB = std::make_unique<Knob> (s, ParamID::ringMix, "MIX");
            break;
        default:
            onButton = std::make_unique<ParamToggle> (s, ParamID::verbOn, "ON");
            knobA = std::make_unique<Knob> (s, ParamID::verbTime, "TIME");
            knobB = std::make_unique<Knob> (s, ParamID::verbMix, "MIX");
            break;
    }
    addAndMakeVisible (*onButton);
    addAndMakeVisible (*knobA);
    addAndMakeVisible (*knobB);
}

void RackModulePanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    header = getLocalBounds().removeFromTop (30);
    r.removeFromTop (30);
    onButton->setBounds (r.removeFromTop (46).withSizeKeepingCentre (64, 44));
    r.removeFromTop (6);
    auto row = r.removeFromTop (118);
    knobA->setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4, 0));
    knobB->setBounds (row.reduced (4, 0));
}

void RackModulePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (2, 3), 5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff302d2a), 0, 0, juce::Colour (0xff201e1c), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.0f);

    // Coloured header strip = drag handle
    auto h = header.toFloat().reduced (3.0f);
    const juce::Colour stripe = moduleId == Rack::Bitcrush ? Retro::yellow : moduleId == Rack::RingMod ? Retro::orange : Retro::red;
    g.setColour (stripe);
    g.fillRoundedRectangle (h, 3.0f);

    g.setColour (juce::Colour (0xff111111));
    g.setFont (Retro::font (13.0f, true, true));
    g.drawText (juce::String (slotNumber + 1) + "  " + Rack::name (moduleId), h.reduced (8, 0).toNearestInt(),
                juce::Justification::centredLeft);
    // grip
    for (int i = 0; i < 3; ++i)
        g.fillRect (h.getRight() - 26.0f, h.getY() + 7.0f + (float) i * 5.0f, 16.0f, 2.0f);

    // screws
    for (auto pt : { juce::Point<float> (8, r.getBottom() - 8), juce::Point<float> (r.getRight() - 8, r.getBottom() - 8) })
    {
        g.setColour (juce::Colour (0xff8d8a85));
        g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7, 7);
        g.setColour (juce::Colour (0xff3a3835));
        g.drawLine (pt.x - 2.5f, pt.y, pt.x + 2.5f, pt.y, 1.0f);
    }
}

void RackModulePanel::paintOverChildren (juce::Graphics& g)
{
    if (hover)
    {
        g.setColour (Retro::orange.withAlpha (0.9f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f, 3.0f);
    }
    if (dragging)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    }
}

void RackModulePanel::mouseDown (const juce::MouseEvent& e)
{
    setMouseCursor (header.contains (e.getPosition()) ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void RackModulePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! header.contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 5) return;
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        repaint();
        c->startDragging (juce::var (moduleId), this);
    }
}

void RackModulePanel::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

bool RackModulePanel::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.isInt() && (int) d.description != moduleId;
}

void RackModulePanel::itemDropped (const SourceDetails& d)
{
    hover = false;
    repaint();
    editor.moveModule ((int) d.description, moduleId);
}

//==============================================================================
GitSynthdEditor::GitSynthdEditor (GitSynthdProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      inputKnob     (p.apvts, ParamID::inputGain,  "INPUT"),
      synthMixKnob  (p.apvts, ParamID::synthMix,   "SYNTH MIX"),
      int1Knob      (p.apvts, ParamID::interval1,  "INTERVAL 1"),
      int2Knob      (p.apvts, ParamID::interval2,  "INTERVAL 2"),
      modDepthKnob  (p.apvts, ParamID::modDepth,   "DEPTH"),
      modMixKnob    (p.apvts, ParamID::modMix,     "MIX"),
      distGainKnob  (p.apvts, ParamID::distGain,   "GAIN"),
      globalMixKnob (p.apvts, ParamID::globalMix,  "GLOBAL MIX"),
      outputKnob    (p.apvts, ParamID::outputGain, "OUTPUT"),
      synthOn  (p.apvts, ParamID::synthOn,     "ON"),
      int2On   (p.apvts, ParamID::interval2On, "INT 2"),
      modOn    (p.apvts, ParamID::modOn,       "ON"),
      distOn   (p.apvts, ParamID::distOn,      "ON"),
      distPre  (p.apvts, ParamID::distPre,     "PRE"),
      distPost (p.apvts, ParamID::distPost,    "POST"),
      latency  (p.apvts, ParamID::latencyMode, "ALIGN", Retro::amber),
      engineButtons   (p.apvts, ParamID::synthMode, { "DEEP", "VINTAGE", "SOLO", "STRINGS", "HARMONY" }),
      distModeButtons (p.apvts, ParamID::distMode,  { "SOFT", "HARD", "RICH", "FOLD" })
{
    setLookAndFeel (&lnf);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &inputKnob, &synthMixKnob, &int1Knob, &int2Knob, &modDepthKnob, &modMixKnob, &distGainKnob,
             &globalMixKnob, &outputKnob, &synthOn, &int2On, &modOn, &distOn, &distPre, &distPost, &latency,
             &engineButtons, &distModeButtons, &prevPreset, &nextPreset, &savePreset })
        addAndMakeVisible (c);

    latency.setTooltip ("Off: zero latency (synth answers as fast as it can track). "
                        "On: 12 ms lookahead so the dry signal lines up with the synth.");

    distPre.onClick  = [this] { enforceDistortionStage (true); };
    distPost.onClick = [this] { enforceDistortionStage (false); };

    for (auto* b : { &prevPreset, &nextPreset, &savePreset }) b->setShowLed (false);

    prevPreset.onClick = [this] { proc.presets.step (-1); repaint (rVfd); };
    nextPreset.onClick = [this] { proc.presets.step (+1); repaint (rVfd); };
    savePreset.onClick = [this] { savePresetDialog(); };

    for (int m = 0; m < 3; ++m)
    {
        rack[(size_t) m] = std::make_unique<RackModulePanel> (*this, m);
        addAndMakeVisible (*rack[(size_t) m]);
    }

    setSize (kWidth, kHeight);
    startTimerHz (30);
}

GitSynthdEditor::~GitSynthdEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GitSynthdEditor::enforceDistortionStage (bool preWasClicked)
{
    // At least one of Pre / Post must stay active.
    if (! distPre.getToggleState() && ! distPost.getToggleState())
    {
        auto* other = preWasClicked ? distPost.param : distPre.param;
        other->beginChangeGesture();
        other->setValueNotifyingHost (1.0f);
        other->endChangeGesture();
    }
}

void GitSynthdEditor::moveModule (int moduleId, int ontoModuleId)
{
    auto* param = proc.apvts.getParameter (ParamID::rackOrder);
    const int current = juce::jlimit (0, 5, (int) proc.apvts.getRawParameterValue (ParamID::rackOrder)->load());
    auto o = Rack::orders[(size_t) current];

    std::vector<int> v (o.begin(), o.end());
    const int dst = (int) (std::find (v.begin(), v.end(), ontoModuleId) - v.begin());
    v.erase (std::find (v.begin(), v.end(), moduleId));
    v.insert (v.begin() + dst, moduleId);

    const int idx = Rack::indexOf ({ v[0], v[1], v[2] });
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 ((float) idx));
    param->endChangeGesture();
}

void GitSynthdEditor::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("SAVE PRESET", "Name this preset:", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&lnf);
    saveWindow->addTextEditor ("name", proc.presets.getCurrentName());
    saveWindow->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
            proc.presets.save (saveWindow->getTextEditorContents ("name"));
        saveWindow.reset();
        repaint (rVfd);
    }), false);
}

//==============================================================================
void GitSynthdEditor::timerCallback()
{
    const float hz  = proc.uiHz.load();
    const float lvl = proc.uiInputLevel.load();
    const bool lock = proc.uiLock.load(), gate = proc.uiGate.load();

    if (std::abs (hz - dispHz) > 0.05f || lock != dispLock || gate != dispGate)
    {
        dispHz = hz; dispLock = lock; dispGate = gate;
        repaint (rTuner);
    }
    const float newLvl = std::max (lvl, dispLevel * 0.85f);   // meter fall-back
    if (std::abs (newLvl - dispLevel) > 0.002f) { dispLevel = newLvl; repaint (rInput); }

    const int order = (int) proc.apvts.getRawParameterValue (ParamID::rackOrder)->load();
    if (order != shownOrder) layoutRack (shownOrder >= 0);

    if (proc.presets.getCurrentName() != lastName) { lastName = proc.presets.getCurrentName(); repaint (rVfd); }
}

void GitSynthdEditor::layoutRack (bool animate)
{
    shownOrder = juce::jlimit (0, 5, (int) proc.apvts.getRawParameterValue (ParamID::rackOrder)->load());
    const auto& o = Rack::orders[(size_t) shownOrder];

    auto area = rRackArea;
    const int gap = 30;
    const int w = (area.getWidth() - 2 * gap) / 3;

    for (int slot = 0; slot < 3; ++slot)
    {
        auto& panel = *rack[(size_t) o[(size_t) slot]];
        panel.slotNumber = slot;
        const juce::Rectangle<int> target (area.getX() + slot * (w + gap), area.getY(), w, area.getHeight());
        if (animate) juce::Desktop::getInstance().getAnimator().animateComponent (&panel, target, 1.0f, 160, false, 1.0, 1.0);
        else         panel.setBounds (target);
        panel.repaint();
    }
}

//==============================================================================
void GitSynthdEditor::resized()
{
    const int x0 = kCheek + 6, x1 = kWidth - kCheek - 6;
    rHeader = { x0, 0, x1 - x0, 86 };
    rTuner  = { 420, 14, 250, 60 };
    rVfd    = { 690, 18, 230, 50 };

    prevPreset.setBounds (928, 18, 40, 52);
    nextPreset.setBounds (968, 18, 40, 52);
    savePreset.setBounds (1010, 18, 66, 52);

    // Row 1
    const int y1 = 94, h1 = 244, gap = 8;
    int x = x0;
    rInput = { x, y1, 132, h1 }; x += 132 + gap;
    rSynth = { x, y1, 408, h1 }; x += 408 + gap;
    rMod   = { x, y1, 196, h1 }; x += 196 + gap;
    rDist  = { x, y1, x1 - x, h1 };

    // Row 2
    const int y2 = y1 + h1 + gap, h2 = kHeight - y2 - 10;
    rRack = { x0, y2, 812, h2 };
    rOut  = { x0 + 812 + gap, y2, x1 - (x0 + 812 + gap), h2 };

    // INPUT
    {
        auto r = rInput.reduced (8).withTrimmedTop (28);
        inputKnob.setBounds (r.removeFromTop (124).withTrimmedRight (26));
        r.removeFromTop (8);
        latency.setBounds (r.removeFromTop (50).withSizeKeepingCentre (70, 48));
    }
    // SYNTH ENGINE
    {
        auto r = rSynth.reduced (8).withTrimmedTop (28);
        auto top = r.removeFromTop (52);
        synthOn.setBounds (top.removeFromLeft (62).reduced (2, 0));
        top.removeFromLeft (10);
        engineButtons.setBounds (top);
        r.removeFromTop (14);
        auto row = r.removeFromTop (128);
        const int kw = row.getWidth() / 4;
        synthMixKnob.setBounds (row.removeFromLeft (kw));
        int1Knob.setBounds (row.removeFromLeft (kw));
        int2Knob.setBounds (row.removeFromLeft (kw));
        int2On.setBounds (row.withSizeKeepingCentre (66, 48));
    }
    // MODULATION
    {
        auto r = rMod.reduced (8).withTrimmedTop (28);
        modOn.setBounds (r.removeFromTop (52).withSizeKeepingCentre (62, 50));
        r.removeFromTop (14);
        auto row = r.removeFromTop (128);
        modDepthKnob.setBounds (row.removeFromLeft (row.getWidth() / 2));
        modMixKnob.setBounds (row);
    }
    // DISTORTION
    {
        auto r = rDist.reduced (8).withTrimmedTop (28);
        auto top = r.removeFromTop (52);
        distOn.setBounds (top.removeFromLeft (58).reduced (2, 0));
        top.removeFromLeft (12);
        distPre.setBounds (top.removeFromLeft (58).reduced (2, 0));
        distPost.setBounds (top.removeFromLeft (58).reduced (2, 0));
        r.removeFromTop (14);
        auto row = r.removeFromTop (128);
        distGainKnob.setBounds (row.removeFromRight (100));
        distModeButtons.setBounds (row.withSizeKeepingCentre (row.getWidth(), 50));
    }
    background = {};   // re-render the static panel

    // RACK
    rRackArea = rRack.reduced (14).withTrimmedTop (36);
    layoutRack (false);

    // OUTPUT
    {
        auto r = rOut.reduced (10).withTrimmedTop (34);
        r.removeFromTop (20);
        auto row = r.removeFromTop (128);
        globalMixKnob.setBounds (row.removeFromLeft (row.getWidth() / 2));
        outputKnob.setBounds (row);
    }
}

//==============================================================================
void GitSynthdEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour stripe) const
{
    auto rf = r.toFloat();
    g.setColour (juce::Colour (0x18ffffff));
    g.drawRoundedRectangle (rf.reduced (0.5f), 4.0f, 1.0f);

    // Silkscreened title bar: coloured block with black lettering, then thin stripes
    auto bar = rf.removeFromTop (22.0f).reduced (4.0f, 3.0f);
    g.setColour (stripe);
    g.fillRect (bar.removeFromLeft (std::min (bar.getWidth(), 18.0f + 8.4f * (float) title.length())));
    g.setColour (juce::Colour (0xff121110));
    g.setFont (Retro::font (12.5f, true, true));
    g.drawText (title, r.getX() + 9, r.getY() + 3, r.getWidth(), 16, juce::Justification::centredLeft);
    g.setColour (stripe.withAlpha (0.8f));
    g.fillRect (bar.getX() + 4, bar.getY() + 4.0f, bar.getWidth() - 4, 2.0f);
    g.fillRect (bar.getX() + 4, bar.getY() + 9.0f, bar.getWidth() - 4, 2.0f);
}

void GitSynthdEditor::paint (juce::Graphics& g)
{
    if (background.isNull())
    {
        background = juce::Image (juce::Image::RGB, kWidth, kHeight, true);
        juce::Graphics bg (background);
        paintStatic (bg);
    }
    g.drawImageAt (background, 0, 0);
    paintMeter (g);
    drawTuner (g, rTuner);
    drawVfd (g, rVfd);
}

void GitSynthdEditor::paintStatic (juce::Graphics& g)
{
    // Panel with faint brushed texture
    g.fillAll (Retro::panel);
    {
        gs::Rng rng (42);
        for (int y = 0; y < kHeight; y += 2)
        {
            g.setColour (juce::Colours::white.withAlpha (0.008f + 0.012f * rng.uni()));
            g.drawHorizontalLine (y, (float) kCheek, (float) (kWidth - kCheek));
        }
    }

    drawWood (g, { 0.0f, 0.0f, (float) kCheek, (float) kHeight }, 7);
    drawWood (g, { (float) (kWidth - kCheek), 0.0f, (float) kCheek, (float) kHeight }, 11);

    // Header: logo and retro stripes
    {
        const int lx = kCheek + 18;
        const float stripeY = 26.0f;
        const juce::Colour stripes[] = { Retro::yellow, Retro::orange, Retro::red };
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (stripes[i]);
            g.fillRect ((float) lx + 248.0f, stripeY + (float) i * 9.0f, 140.0f, 5.0f);
        }
        g.setFont (Retro::font (42.0f, true, true).withHorizontalScale (1.05f));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawText ("GitSynth'd!", lx + 2, 10, 300, 52, juce::Justification::centredLeft);
        g.setColour (Retro::silk);
        g.drawText ("GitSynth'd!", lx, 8, 300, 52, juce::Justification::centredLeft);

        g.setFont (Retro::font (10.5f));
        g.setColour (Retro::silkDim);
        g.drawText ("MONOPHONIC AUDIO-TO-SYNTH RESYNTHESIZER   MODEL GS-84", lx + 2, 60, 380, 14, juce::Justification::centredLeft);

        g.setColour (juce::Colour (0x30ffffff));
        g.drawHorizontalLine (86, (float) kCheek, (float) (kWidth - kCheek));
    }

    drawSection (g, rInput, "INPUT",        Retro::yellow);
    drawSection (g, rSynth, "SYNTH ENGINE", Retro::orange);
    drawSection (g, rMod,   "MODULATION",   Retro::yellow);
    drawSection (g, rDist,  "DISTORTION",   Retro::red);
    drawSection (g, rRack,  "EFFECTS RACK", Retro::orange);
    drawSection (g, rOut,   "OUTPUT",       Retro::yellow);

    g.setFont (Retro::font (10.0f, false));
    g.setColour (Retro::silkDim);
    g.drawText ("DRAG A MODULE'S COLOURED HEADER TO REORDER THE CHAIN", rRack.getX() + 14, rRack.getY() + 26, 400, 14, juce::Justification::centredLeft);
    g.drawText ("PRE  >  RACK  >  POST", rDist.getX() + 74, rDist.getY() + 96, 128, 12, juce::Justification::centred);
    g.drawText ("MIX 100% = ALL WET", rOut.getX(), rOut.getY() + 34, rOut.getWidth(), 14, juce::Justification::centred);

    // Signal-flow arrows between rack slots
    {
        const int gap = 30;
        const int w = (rRackArea.getWidth() - 2 * gap) / 3;
        g.setColour (Retro::silk.withAlpha (0.8f));
        for (int s = 0; s < 2; ++s)
        {
            const float ax = (float) (rRackArea.getX() + (s + 1) * w + s * gap) + gap * 0.5f;
            const float ay = (float) rRackArea.getCentreY();
            juce::Path a;
            a.addTriangle (ax - 7, ay - 8, ax - 7, ay + 8, ax + 7, ay);
            g.fillPath (a);
        }
    }

}

void GitSynthdEditor::paintMeter (juce::Graphics& g)
{
    // Input level LED column (beside the input knob)
    {
        const int mx = rInput.getRight() - 28, my = rInput.getY() + 46;
        const float db = juce::Decibels::gainToDecibels (dispLevel, -60.0f);
        for (int i = 0; i < 10; ++i)
        {
            const float thresh = -48.0f + (float) i * 5.0f;     // -48 .. -3 dBFS
            const juce::Colour c = i >= 9 ? Retro::ledRed : i >= 7 ? Retro::yellow : juce::Colour (0xff4cff5a);
            const bool lit = db >= thresh;
            const auto led = juce::Rectangle<float> ((float) mx, (float) (my + (9 - i) * 10), 14.0f, 6.0f);
            g.setColour (lit ? c : c.darker (3.0f));
            g.fillRoundedRectangle (led, 1.5f);
        }
        g.setFont (Retro::font (9.0f));
        g.setColour (Retro::silkDim);
        g.drawText ("LVL", mx - 4, my + 100, 22, 10, juce::Justification::centred);
    }
}

void GitSynthdEditor::drawTuner (juce::Graphics& g, juce::Rectangle<int> area) const
{
    auto r = area.toFloat();
    g.setColour (juce::Colour (0xff050303));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (0x40ffffff));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    // red filter glass
    g.setColour (juce::Colour (0x22ff2010));
    g.fillRoundedRectangle (r.reduced (3.0f), 3.0f);

    const auto mono = [] (float h) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold)); };

    juce::String note = "--", cents = "", hzText = "--- HZ";
    float c = 0.0f;
    if (dispLock && dispHz > 0.0f)
    {
        note = noteName (dispHz, c);
        cents = (c >= 0 ? "+" : "") + juce::String (juce::roundToInt (c)) + " CT";
        hzText = juce::String (dispHz, 1) + " HZ";
    }

    auto lit = Retro::ledRed;
    g.setFont (mono (30.0f));
    g.setColour (lit);
    g.drawText (note, r.withTrimmedLeft (10).withWidth (90).toNearestInt(), juce::Justification::centredLeft);

    g.setFont (mono (13.0f));
    g.drawText (cents,  (int) r.getX() + 100, (int) r.getY() + 8,  80, 16, juce::Justification::centredLeft);
    g.drawText (hzText, (int) r.getX() + 100, (int) r.getY() + 26, 90, 16, juce::Justification::centredLeft);

    const float latMs = (float) proc.uiLatencySamples.load() * 1000.0f / (float) std::max (1.0, proc.getSampleRate());
    g.setFont (mono (10.0f));
    g.setColour (Retro::amber.withAlpha (0.9f));
    g.drawText ("LAT " + juce::String (latMs, 1) + "MS", (int) r.getX() + 100, (int) r.getY() + 43, 90, 12, juce::Justification::centredLeft);

    // cents needle bar
    auto bar = juce::Rectangle<float> (r.getX() + 10.0f, r.getBottom() - 10.0f, 84.0f, 4.0f);
    g.setColour (lit.withAlpha (0.2f));
    g.fillRect (bar);
    if (dispLock)
    {
        g.setColour (lit);
        const float px = bar.getCentreX() + juce::jlimit (-50.0f, 50.0f, c) / 50.0f * bar.getWidth() * 0.5f;
        g.fillRect (px - 2.0f, bar.getY() - 2.0f, 4.0f, 8.0f);
    }

    // status LEDs
    auto led = [&] (float y, bool on, const char* text)
    {
        const float lx = r.getRight() - 52.0f;
        g.setColour (on ? Retro::ledRed : Retro::ledRed.darker (2.6f));
        g.fillEllipse (lx, y, 7.0f, 7.0f);
        g.setColour (Retro::silkDim);
        g.setFont (Retro::font (9.5f));
        g.drawText (text, (int) lx + 10, (int) y - 3, 40, 13, juce::Justification::centredLeft);
    };
    led (r.getY() + 14.0f, dispGate, "GATE");
    led (r.getY() + 34.0f, dispLock, "LOCK");
}

void GitSynthdEditor::drawVfd (juce::Graphics& g, juce::Rectangle<int> area) const
{
    auto r = area.toFloat();
    g.setColour (juce::Colour (0xff04100c));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (0x40ffffff));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    // VFD grid texture
    g.setColour (Retro::vfd.withAlpha (0.04f));
    for (float x = r.getX() + 4; x < r.getRight() - 4; x += 3.0f) g.drawVerticalLine ((int) x, r.getY() + 4, r.getBottom() - 4);

    g.setFont (Retro::font (9.5f));
    g.setColour (Retro::vfd.withAlpha (0.6f));
    g.drawText ("PRESET", r.reduced (10, 5).toNearestInt(), juce::Justification::topLeft);

    const auto name = proc.presets.getCurrentName().toUpperCase();
    const auto f = juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold));
    g.setFont (f);
    auto tr = r.reduced (10, 4).withTrimmedTop (14).toNearestInt();
    g.setColour (Retro::vfd.withAlpha (0.25f));
    g.drawFittedText (name, tr.translated (0, 1), juce::Justification::centredLeft, 1);   // glow
    g.setColour (Retro::vfd);
    g.drawFittedText (name, tr, juce::Justification::centredLeft, 1);
}
