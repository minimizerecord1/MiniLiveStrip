#include "PluginEditor.h"
#include <cmath>

namespace
{
    const juce::Colour kNotchColour     (0xffff5a4a);   // red: notches / Q curve
    const juce::Colour kEqColour        (0xff7cf29a);   // green: EQ curve
    const juce::Colour kAccent          (0xffffb020);
    const juce::Colour kPostColour      (0xffe0a2ff);   // violet: output RTA (post)
    const juce::Colour kGeqColour       (0xfff06292);   // pink: graphic EQ

    const juce::Colour kBgColour    (0xff0e0f11);
    const juce::Colour kPanelColour (0xff1c1f23);
    const juce::Colour kHeaderColour(0xff141619);
    const juce::Colour kLineColour  (0xff3a3e44);
    const juce::Colour kLabelColour (0xff9aa0a6);

    const juce::Colour kBandColours[MiniLiveStripProcessor::kNumEqBands] =
    {
        juce::Colour (0xff4dd0e1), juce::Colour (0xffaed581), juce::Colour (0xffffb74d),
        juce::Colour (0xffba68c8), juce::Colour (0xff81d4fa), juce::Colour (0xfff48fb1),
        juce::Colour (0xffffee58), juce::Colour (0xff80cbc4), juce::Colour (0xffff8a65),
        juce::Colour (0xffb39ddb)
    };

    constexpr float kRotStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kRotEnd   = juce::MathConstants<float>::pi * 2.75f;
    constexpr int   kTextBoxH = 12;

    // RTA display scale
    constexpr float kFMin      = 20.0f;
    constexpr float kFMax      = 20000.0f;
    constexpr float kDbTop     = 0.0f;
    constexpr float kRespRange = 30.0f;      // filter overlay: +/- dB around the middle line
    constexpr float kNodeR     = 7.0f;       // EQ point radius
    constexpr float kNodeHit   = 12.0f;      // EQ point hit radius

    juce::Font fkFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::String freqText (double v)
    {
        if (v >= 1000.0)
            return juce::String (v / 1000.0, v >= 10000.0 ? 0 : 1) + "kHz";
        return juce::String ((int) std::lround (v)) + "Hz";
    }

    double parseFreq (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        double v = s.getDoubleValue();
        if (s.contains ("k"))
            v *= 1000.0;
        return v;
    }

    juce::String gainText (double v)
    {
        return (v > 0.0 ? juce::String ("+") : juce::String()) + juce::String (v, 1) + "dB";
    }
}

//==============================================================================
// Look and feel
MiniLiveStripLookAndFeel::MiniLiveStripLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       juce::Colour (0xffd7dade));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId,            juce::Colour (0xffd7dade));
    setColour (juce::PopupMenu::backgroundColourId,     juce::Colour (0xff14161a));
    setColour (juce::PopupMenu::textColourId,           juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3b6ea5));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

juce::Label* MiniLiveStripLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (fkFont (10.0f));
    l->setBorderSize (juce::BorderSize<int> (0));
    l->setJustificationType (juce::Justification::centred);
    return l;
}

juce::Font MiniLiveStripLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fkFont (10.5f);
}

void MiniLiveStripLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                                  float sliderPos, float startAngle, float endAngle,
                                                  juce::Slider& s)
{
    const float R  = 0.5f * (float) juce::jmin (w, h) - 3.0f;
    const float cx = (float) x + 0.5f * (float) w;
    const float cy = (float) y + 0.5f * (float) h;
    const float a  = startAngle + (endAngle - startAngle) * sliderPos;
    const bool  hot = s.isMouseOverOrDragging();

    // Track + value arc
    juce::Path track, value;
    track.addCentredArc (cx, cy, R, R, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (cx, cy, R, R, 0.0f, startAngle, a, true);

    g.setColour (kLineColour);
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (hot ? kAccent.brighter (0.2f) : kAccent);
    g.strokePath (value, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Flat body + pointer
    const float ri = R - 4.0f;
    g.setColour (juce::Colour (0xff25282c));
    g.fillEllipse (cx - ri, cy - ri, 2.0f * ri, 2.0f * ri);

    juce::Path p;
    p.startNewSubPath (cx + ri * 0.35f * std::sin (a), cy - ri * 0.35f * std::cos (a));
    p.lineTo          (cx + ri * 0.95f * std::sin (a), cy - ri * 0.95f * std::cos (a));
    g.setColour (juce::Colours::white);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MiniLiveStripLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool,
                                              int, int, int, int, juce::ComboBox&)
{
    const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff111316));
    g.fillRoundedRectangle (b, 2.5f);
    g.setColour (kLineColour);
    g.drawRoundedRectangle (b, 2.5f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 9.0f, ay = 0.5f * (float) h;
    arrow.addTriangle (ax - 3.0f, ay - 1.5f, ax + 3.0f, ay - 1.5f, ax, ay + 2.5f);
    g.setColour (juce::Colour (0xffb0b4b9));
    g.fillPath (arrow);
}

//==============================================================================
// LED button
LedButton::LedButton (const juce::String& name) : juce::Button (name) {}

void LedButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool lit = getToggleState() || down;

    g.setColour (over ? juce::Colour (0xff262a2f) : juce::Colour (0xff1c1f23));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (kLineColour);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ b.getX() + 10.0f, b.getCentreY() });
    if (lit)
    {
        g.setColour (colourOn.withAlpha (0.25f));
        g.fillEllipse (dot.expanded (2.5f));
    }
    g.setColour (lit ? colourOn : kLineColour);
    g.fillEllipse (dot);

    g.setFont (fkFont (10.5f, true));
    g.setColour (lit ? juce::Colours::white : kLabelColour);
    g.drawText ((lit || labelOff.isEmpty()) ? label : labelOff,
                b.withTrimmedLeft (20.0f), juce::Justification::centredLeft);
}

//==============================================================================
// Level meter
namespace { constexpr float kMeterFloorDb = -60.0f; }

void LevelMeter::setLevel (float linearPeak)
{
    const float db = juce::Decibels::gainToDecibels (linearPeak, kMeterFloorDb);

    if (db >= holdDb)          { holdDb = db; holdFrames = 30; }          // hold ~1 s
    else if (holdFrames > 0)   { --holdFrames; }
    else                       { holdDb = juce::jmax (db, holdDb - 1.5f); }

    levelDb = db;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff111316));
    g.fillRoundedRectangle (b, 2.0f);

    const auto in = b.reduced (1.5f);
    auto yFor = [&] (float db)
    {
        return in.getBottom() - in.getHeight() * juce::jlimit (0.0f, 1.0f, (db - kMeterFloorDb) / -kMeterFloorDb);
    };

    const float top = yFor (levelDb);
    auto seg = [&] (float dbLo, float dbHi, juce::Colour c)
    {
        const float yLo = yFor (dbLo);
        const float yHi = juce::jmax (yFor (dbHi), top);
        if (yHi < yLo)
        {
            g.setColour (c);
            g.fillRect (in.getX(), yHi, in.getWidth(), yLo - yHi);
        }
    };
    seg (kMeterFloorDb, -12.0f, juce::Colour (0xff4caf50));
    seg (-12.0f, -3.0f,         juce::Colour (0xffffd54a));
    seg (-3.0f, 0.0f,           juce::Colour (0xffff5a4a));

    if (holdDb > kMeterFloorDb + 1.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (in.getX(), yFor (holdDb) - 0.5f, in.getWidth(), 1.0f);
    }

    g.setColour (kLineColour);
    g.drawRoundedRectangle (b.reduced (0.5f), 2.0f, 1.0f);
}

//==============================================================================
// Shared helpers of the sections below
namespace
{
    const juce::Colour kGateColour (0xff40c4ff);

    const juce::StringArray& typeNames()
    {
        static const juce::StringArray n { "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Bell", "Tilt", "Notch" };
        return n;
    }

    const juce::StringArray& shapeNames()
    {
        static const juce::StringArray n { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct",
                                           "30 dB/oct", "36 dB/oct", "42 dB/oct" };
        return n;
    }

    juce::String msText (double v)
    {
        if (v >= 1000.0) return juce::String (v / 1000.0, 2) + " s";
        if (v >= 10.0)   return juce::String ((int) std::lround (v)) + " ms";
        return juce::String (v, 1) + " ms";
    }

    double msFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const double v = s.getDoubleValue();
        if (s.endsWith ("ms")) return v;
        return s.endsWith ("s") ? v * 1000.0 : v;
    }
}

//==============================================================================
// Gain-reduction meter (hangs from the top)
void ReductionMeter::setFallRate (float dbPerSecond)
{
    // The timer runs at 30 Hz. The processor already releases with the release time, so the
    // meter may fall at most as fast as a full-scale reduction would release.
    fallPerFrame = juce::jlimit (0.3f, juce::jmax (1.0f, range), dbPerSecond / 30.0f);
}

void ReductionMeter::setValue (float reductionDb)
{
    const float db = juce::jmax (0.0f, reductionDb);

    if (db >= valueDb) valueDb = db;                                   // attack: immediate
    else               valueDb = juce::jmax (db, valueDb - fallPerFrame);   // release: follows the knob

    if (db >= holdDb)          { holdDb = db; holdFrames = 30; }
    else if (holdFrames > 0)   { --holdFrames; }
    else                       { holdDb = juce::jmax (db, holdDb - juce::jmax (0.8f, fallPerFrame * 0.25f)); }

    repaint();
}

juce::Rectangle<float> ReductionMeter::barRect() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) kGutter).reduced (1.5f);
}

float ReductionMeter::yForDepth (float db) const
{
    const auto in = barRect();
    return in.getY() + in.getHeight() * juce::jlimit (0.0f, 1.0f, db / juce::jmax (1.0f, range));
}

void ReductionMeter::mouseMove (const juce::MouseEvent& e)
{
    const bool onMarker = markerDraggable && markDb >= 0.0f
                          && std::abs (e.position.y - yForDepth (markDb)) <= 6.0f;
    setMouseCursor (onMarker ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void ReductionMeter::mouseDown (const juce::MouseEvent& e)
{
    if (! markerDraggable || markDb < 0.0f || std::abs (e.position.y - yForDepth (markDb)) > 6.0f)
        return;

    draggingMarker = true;
    if (onMarkerDragState) onMarkerDragState (true);
    mouseDrag (e);
}

void ReductionMeter::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingMarker || ! onMarkerDrag)
        return;

    const auto in = barRect();
    const float depth = (e.position.y - in.getY()) / juce::jmax (1.0f, in.getHeight()) * range;
    onMarkerDrag (juce::jlimit (0.0f, range, depth));
}

void ReductionMeter::mouseUp (const juce::MouseEvent&)
{
    if (! draggingMarker)
        return;

    draggingMarker = false;
    if (onMarkerDragState) onMarkerDragState (false);
}

void ReductionMeter::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto b = full.withTrimmedLeft ((float) kGutter);             // the bar itself
    g.setColour (juce::Colour (0xff111316));
    g.fillRoundedRectangle (b, 2.0f);

    const auto in = b.reduced (1.5f);
    auto yFor = [&] (float db)
    {
        return in.getY() + in.getHeight() * juce::jlimit (0.0f, 1.0f, db / juce::jmax (1.0f, range));
    };

    // bar
    g.setColour (colour.withAlpha (0.9f));
    g.fillRect (in.getX(), in.getY(), in.getWidth(), yFor (valueDb) - in.getY());

    // shaded range (0 .. span) on the right edge of the bar
    if (spanDb > 0.05f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (in.getRight() - 2.5f, in.getY(), 2.5f, yFor (spanDb) - in.getY());
    }

    // dB lines + numbers
    g.setFont (fkFont (7.5f));
    g.setColour (kLabelColour.withAlpha (0.9f));
    g.drawText ("0", full.getX(), in.getY() - 1.0f, (float) kGutter - 3.0f, 10.0f, juce::Justification::centredRight);

    for (float db = step; db <= range + 0.01f; db += step)
    {
        const float y = yFor (db);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (in.getX(), y - 0.5f, in.getWidth(), 1.0f);

        g.setColour (kLabelColour.withAlpha (0.9f));
        const float ty = juce::jmin (y - 5.0f, in.getBottom() - 9.0f);
        g.drawText ("-" + juce::String ((int) std::lround (db)), full.getX(), ty,
                    (float) kGutter - 3.0f, 10.0f, juce::Justification::centredRight);
    }

    // marker (depth / ratio): dashed line over the bar + label in the gutter
    if (markDb >= 0.0f && markDb <= range + 0.01f)
    {
        const float y = yFor (markDb);
        const float dashes[] = { 3.0f, 2.0f };
        juce::Path line;
        line.startNewSubPath (in.getX(), y);
        line.lineTo (in.getRight(), y);
        juce::Path dashed;
        juce::PathStrokeType (1.4f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (juce::Colours::white);
        g.fillPath (dashed);

        if (markerDraggable)                                 // fader handle: a solid bar you can grab
            g.fillRoundedRectangle (in.getX(), y - 1.5f, in.getWidth(), 3.0f, 1.0f);

        const auto pill = juce::Rectangle<float> (full.getX(), y - 5.5f, (float) kGutter - 2.0f, 11.0f);
        g.setColour (juce::Colour (0xff0b0d0f));
        g.fillRoundedRectangle (pill, 2.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (pill, 2.0f, 1.0f);
        g.setFont (fkFont (7.0f, true));
        g.drawText (markText, pill, juce::Justification::centred);
    }

    if (holdDb > 0.5f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (in.getX(), yFor (holdDb) - 0.5f, in.getWidth(), 1.0f);
    }

    g.setColour (kLineColour);
    g.drawRoundedRectangle (b.reduced (0.5f), 2.0f, 1.0f);
}

//==============================================================================
// Knob popup of the selected EQ band
void BandPopup::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xf0141619));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (colour.withAlpha (0.8f));
    g.drawRoundedRectangle (b, 6.0f, 1.2f);

    g.setColour (colour);
    g.fillEllipse (9.0f, 6.0f, 7.0f, 7.0f);
    g.setColour (juce::Colour (0xffd7dade));
    g.setFont (fkFont (10.5f, true));
    g.drawText (title, 22, 1, getWidth() - 28 - 80, 17, juce::Justification::centredLeft);

    g.setFont (fkFont (9.0f, true));
    static const char* names[3] = { "FREQ", "Q", "GAIN" };
    for (int i = 0; i < 3; ++i)
    {
        const auto s = slot (i);
        g.setColour ((i == 2 && ! usesGain) ? kLabelColour.withAlpha (0.4f) : kLabelColour);
        g.drawText (names[i], s.getX(), s.getY() - 11, s.getWidth(), 11, juce::Justification::centred);
    }
}

//==============================================================================
// Small module button
ModuleButton::ModuleButton (Icon i, const juce::String& t, juce::Colour c)
    : icon (i), text (t), accent (c)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModuleButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool hot = over || dragging;

    const juce::Colour bg = dragging ? juce::Colour (0xff2c3138)
                          : hot      ? juce::Colour (0xff262a2f)
                                     : juce::Colour (0xff1c1f23);
    g.setColour (bg.withAlpha (0.72f));                      // a bit transparent: the RTA shows through
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour ((open ? accent : kLineColour).withAlpha (0.9f));
    g.drawRoundedRectangle (b, 6.0f, open ? 1.6f : 1.0f);

    // Lamp (module active) top left, position in the chain top right
    const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withPosition (b.getX() + 6.0f, b.getY() + 6.0f);
    if (lit)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillEllipse (dot.expanded (2.5f));
    }
    g.setColour (lit ? accent : kLineColour);
    g.fillEllipse (dot);

    g.setFont (fkFont (8.5f, true));
    g.setColour (kLabelColour);
    g.drawText (juce::String (slotNumber),
                juce::Rectangle<float> (b.getRight() - 20.0f, b.getY() + 3.0f, 16.0f, 11.0f),
                juce::Justification::centredRight);

    // Icon
    const auto ia = juce::Rectangle<float> (30.0f, 20.0f).withCentre ({ b.getCentreX(), b.getY() + 26.0f });
    const float x = ia.getX(), y = ia.getY(), w = ia.getWidth(), h = ia.getHeight();
    const auto ink = (lit || open) ? accent : kLabelColour;
    g.setColour (ink);

    juce::Path p;
    const juce::PathStrokeType stroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    switch (icon)
    {
        case IconFeedback:      // flat spectrum with a narrow notch
            p.startNewSubPath (x, y + h * 0.30f);
            p.lineTo (x + w * 0.38f, y + h * 0.30f);
            p.lineTo (x + w * 0.46f, y + h * 0.95f);
            p.lineTo (x + w * 0.54f, y + h * 0.95f);
            p.lineTo (x + w * 0.62f, y + h * 0.30f);
            p.lineTo (x + w, y + h * 0.30f);
            g.strokePath (p, stroke);
            break;

        case IconEq:            // three faders
        {
            const float ky[3] = { 0.62f, 0.28f, 0.72f };
            for (int k = 0; k < 3; ++k)
            {
                const float fx = x + w * (0.17f + 0.33f * (float) k);
                g.drawLine (fx, y, fx, y + h, 1.3f);
                g.fillRoundedRectangle (fx - 4.0f, y + h * ky[k] - 2.5f, 8.0f, 5.0f, 1.5f);
            }
            break;
        }

        case IconDynamics:      // transfer curve that bends flat above the threshold
            p.startNewSubPath (x, y + h);
            p.lineTo (x + w * 0.50f, y + h * 0.45f);
            p.lineTo (x + w, y + h * 0.15f);
            g.strokePath (p, stroke);
            g.drawLine (x, y + h, x + w, y + h, 0.8f);
            break;

        case IconGeq:           // five little faders of different height
        {
            const float ky[5] = { 0.55f, 0.25f, 0.70f, 0.40f, 0.60f };
            for (int k = 0; k < 5; ++k)
            {
                const float fx = x + w * (0.10f + 0.20f * (float) k);
                g.drawLine (fx, y, fx, y + h, 1.0f);
                g.fillRoundedRectangle (fx - 2.5f, y + h * ky[k] - 2.0f, 5.0f, 4.0f, 1.0f);
            }
            break;
        }

        case IconGate:          // quiet signal that is cut, then a loud one passes
        {
            const int n = 14;
            for (int k = 0; k <= n; ++k)
            {
                const float t = (float) k / (float) n;
                const float amp = t < 0.45f ? 0.06f : 0.46f;
                const float yy = y + h * 0.5f + ((k & 1) ? 1.0f : -1.0f) * amp * h;
                if (k == 0) p.startNewSubPath (x, yy);
                else        p.lineTo (x + w * t, yy);
            }
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
    }

    // Label
    g.setFont (fkFont (8.5f, true));
    g.setColour ((lit || open) ? juce::Colours::white : kLabelColour);
    g.drawText (text, b.withTrimmedTop (b.getHeight() - 18.0f).withTrimmedBottom (3.0f), juce::Justification::centred);
}

void ModuleButton::mouseDown (const juce::MouseEvent& e)
{
    grabX = e.getPosition().x;
    dragging = false;
}

void ModuleButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 6)
    {
        dragging = true;
        toFront (false);
        if (onDragStart)
            onDragStart();
        repaint();
    }

    if (dragging && onDragMove)
        onDragMove (e.getEventRelativeTo (getParentComponent()).getPosition().x - grabX);
}

void ModuleButton::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDragging = dragging;
    dragging = false;
    repaint();

    if (wasDragging)
    {
        if (onDragEnd)
            onDragEnd();
    }
    else if (e.mouseWasClicked() && contains (e.getPosition()) && ! e.mods.isPopupMenu() && onClick)
    {
        onClick();
    }
}

//==============================================================================
// Knob panel (limiter / compressor, gate)
DynamicsPanel::DynamicsPanel (MiniLiveStripProcessor& p, Kind k) : proc (p), kind (k)
{
    auto& apvts = proc.apvts;
    const bool lim = (kind == Limiter);
    const juce::Colour accent = lim ? kAccent : kGateColour;
    pMode = apvts.getRawParameterValue ("limMode");

    auto addKnob = [&] (const char* id, const char* caption, int mode,
                        std::function<juce::String (double)> toText,
                        std::function<double (const juce::String&)> fromText = {})
    {
        auto s = std::make_unique<juce::Slider>();
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, kTextBoxH);
        s->setRotaryParameters (kRotStart, kRotEnd, true);
        addAndMakeVisible (*s);

        auto* param = apvts.getParameter (id);
        sliderAtt.push_back (std::make_unique<juce::SliderParameterAttachment> (*param, *s, nullptr));

        s->textFromValueFunction = toText;
        if (fromText)
            s->valueFromTextFunction = fromText;
        s->setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
        s->updateText();

        Knob kb;
        kb.slider = std::move (s);
        kb.caption = caption;
        kb.mode = mode;
        knobs.push_back (std::move (kb));
    };

    auto dbText = [] (double v) { return juce::String (v, 1) + " dB"; };
    auto negText = [] (double v) { return "-" + juce::String (v, 1) + " dB"; };
    auto absFromText = [] (const juce::String& t) { return std::abs (t.getDoubleValue()); };

    if (lim)
    {
        // limiter
        addKnob ("limThresh",  "THRESHOLD", 0, dbText);
        addKnob ("limCeiling", "CEILING",   0, dbText);
        addKnob ("limAttack",  "ATTACK",    0, msText, msFromText);
        addKnob ("limRelease", "RELEASE",   0, msText, msFromText);

        // compressor
        addKnob ("compThresh",  "THRESHOLD", 1, dbText);
        addKnob ("compRatio",   "RATIO",     1, [] (double v) { return juce::String (v, 1) + ":1"; });
        addKnob ("compKnee",    "KNEE",      1, dbText);
        addKnob ("compAttack",  "ATTACK",    1, msText, msFromText);
        addKnob ("compRelease", "RELEASE",   1, msText, msFromText);
        addKnob ("compMakeup",  "MAKEUP",    1, [] (double v) { return "+" + juce::String (v, 1) + " dB"; });
    }
    else
    {
        addKnob ("gateThresh",  "THRESHOLD", 0, dbText);
        addKnob ("gateDepth",   "DEPTH",     0, negText, absFromText);
        addKnob ("gateKnee",    "KNEE",      0, dbText);
        addKnob ("gateAttack",  "ATTACK",    0, msText, msFromText);
        addKnob ("gateHold",    "HOLD",      0, msText, msFromText);
        addKnob ("gateRelease", "RELEASE",   0, msText, msFromText);
    }

    onBtn.setClickingTogglesState (true);
    onBtn.setLabel ("ON");
    onBtn.setLabelOff ("OFF");
    onBtn.setColourOn (accent);
    addAndMakeVisible (onBtn);
    onAtt = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (lim ? "limOn" : "gateOn"),
                                                              onBtn, nullptr);

    closeBtn.setLabel ("CLOSE");
    closeBtn.setColourOn (accent);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);

    // Limiter (left) | Compressor (right)
    if (lim)
    {
        limModeBtn.setLabel ("LIMITER");
        limModeBtn.setColourOn (kAccent);
        limModeBtn.onClick = [this] { setMode (false); };
        addAndMakeVisible (limModeBtn);

        compModeBtn.setLabel ("COMPRESSOR");
        compModeBtn.setColourOn (kAccent);
        compModeBtn.onClick = [this] { setMode (true); };
        addAndMakeVisible (compModeBtn);
    }

    syncMode();
}

bool DynamicsPanel::compMode() const
{
    return kind == Limiter && pMode != nullptr && pMode->load() > 0.5f;
}

void DynamicsPanel::setMode (bool comp)
{
    if (comp == compMode())
        return;

    if (auto* p = proc.apvts.getParameter ("limMode"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (comp ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
    syncMode();
}

// Shows the knobs of the active mode and resizes the panel to fit them.
void DynamicsPanel::syncMode()
{
    lastComp = compMode();
    limModeBtn.setToggleState (! lastComp, juce::dontSendNotification);
    compModeBtn.setToggleState (lastComp, juce::dontSendNotification);

    const int m = lastComp ? 1 : 0;
    for (auto& kb : knobs)
        kb.slider->setVisible (kb.mode == m);

    if (getWidth() != getPreferredSize().x && onSizeChanged)
        onSizeChanged();                       // the editor sets new bounds -> resized()
    resized();                                 // always lay out the knobs of the active mode

    repaint();
}

int DynamicsPanel::activeKnobCount() const
{
    const int m = compMode() ? 1 : 0;
    int n = 0;
    for (const auto& kb : knobs)
        if (kb.mode == m)
            ++n;
    return n;
}

juce::Point<int> DynamicsPanel::getPreferredSize() const
{
    const int n = activeKnobCount();
    return { juce::jmax (372, 10 + n * 78 + 10), 8 + 25 + 6 + 83 + 8 };
}

void DynamicsPanel::tick()
{
    if (compMode() != lastComp)                // changed by undo / automation / preset load
        syncMode();
}

void DynamicsPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    auto title = r.removeFromTop (25);
    closeBtn.setBounds (title.removeFromRight (63));
    title.removeFromRight (6);
    onBtn.setBounds (title.removeFromRight (63));

    if (kind == Limiter)
    {
        limModeBtn.setBounds (title.removeFromLeft (85));
        title.removeFromLeft (4);
        compModeBtn.setBounds (title.removeFromLeft (112));
    }

    r.removeFromTop (6);

    const int m = compMode() ? 1 : 0;
    std::vector<juce::Slider*> active;
    for (auto& kb : knobs)
        if (kb.mode == m)
            active.push_back (kb.slider.get());

    const int n = (int) active.size();
    if (n == 0)
        return;

    const int cw = juce::jmax (48, juce::jmin (78, r.getWidth() / n));   // never narrower than 48 px
    auto row = r.withSizeKeepingCentre (cw * n, 83);
    for (auto* s : active)
    {
        auto cell = row.removeFromLeft (cw);
        s->setBounds (cell.withTrimmedTop (11).withSizeKeepingCentre (juce::jmin (56, cw), 68));
    }
}

void DynamicsPanel::paint (juce::Graphics& g)
{
    const bool lim = (kind == Limiter);
    const bool comp = compMode();
    const auto b = getLocalBounds().toFloat().reduced (0.5f);


    g.setColour (juce::Colour (0xe0141619));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (kLineColour);
    g.drawRoundedRectangle (b, 6.0f, 1.0f);

    if (! lim)      // the limiter panel has the Limiter | Compressor switch instead of a title
    {
        g.setColour (kGateColour);
        g.fillEllipse (14.0f, 17.0f, 7.0f, 7.0f);
        g.setColour (juce::Colour (0xffd7dade));
        g.setFont (fkFont (11.5f, true));
        g.drawText ("GATE", 28, 8, 120, 25, juce::Justification::centredLeft);
    }

    const int m = comp ? 1 : 0;
    g.setColour (kLabelColour);
    g.setFont (fkFont (9.0f, true));
    for (const auto& kb : knobs)
    {
        if (kb.mode != m)
            continue;
        const auto& s = *kb.slider;
        g.drawText (kb.caption, s.getX() - 10, s.getY() - 12, s.getWidth() + 20, 12,
                    juce::Justification::centred);
    }
}

//==============================================================================
//==============================================================================
// 31-band graphic EQ panel
namespace
{
    juce::String geqFreqLabel (float f)
    {
        if (f >= 1000.0f)
        {
            const float v = f / 1000.0f;
            if (std::abs (v - std::round (v)) < 0.001f)
                return juce::String ((int) std::lround (v)) + "k";
            auto s = juce::String (v, 2);
            if (s.endsWithChar ('0'))
                s = s.dropLastCharacters (1);
            return s + "k";
        }
        return std::abs (f - std::round (f)) > 0.01f ? juce::String (f, 1) : juce::String ((int) std::lround (f));
    }
}

GeqPanel::GeqPanel (MiniLiveStripProcessor& p) : proc (p)
{
    for (int i = 0; i < kN; ++i)
    {
        params[(size_t) i] = proc.apvts.getParameter ("geq" + juce::String (i + 1));
        shown[(size_t) i]  = getGain (i);
    }
    pOn = proc.apvts.getRawParameterValue ("geqOn");
    lastOn = pOn->load() > 0.5f;

    onBtn.setClickingTogglesState (true);
    onBtn.setLabel ("ON");
    onBtn.setLabelOff ("OFF");
    onBtn.setColourOn (kGeqColour);
    addAndMakeVisible (onBtn);
    onAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("geqOn"), onBtn, nullptr);

    resetBtn.setLabel ("RESET");
    resetBtn.setColourOn (kGeqColour);
    resetBtn.onClick = [this] { resetAll(); };
    addAndMakeVisible (resetBtn);

    closeBtn.setLabel ("CLOSE");
    closeBtn.setColourOn (kGeqColour);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);

    setInterceptsMouseClicks (false, false);       // not selected: the mouse passes through to the RTA
    setAlpha (kIdleAlpha);
}

void GeqPanel::setOpen (bool o)
{
    if (open == o)
        return;

    open = o;
    setInterceptsMouseClicks (o, o);
    if (! o)
    {
        endTouches();
        hover = active = -1;
    }
    repaint();
}

void GeqPanel::tick()
{
    bool changed = false;
    for (int i = 0; i < kN; ++i)
    {
        const float v = getGain (i);
        if (v != shown[(size_t) i]) { shown[(size_t) i] = v; changed = true; }
    }

    const bool on = pOn->load() > 0.5f;
    if (on != lastOn) { lastOn = on; changed = true; }

    const float target = open ? 1.0f : kIdleAlpha;
    if (alphaNow != target)
    {
        alphaNow = std::abs (alphaNow - target) < 0.02f ? target : alphaNow + (target - alphaNow) * 0.35f;
        setAlpha (alphaNow);
    }

    if (changed)
        repaint();
}

void GeqPanel::resized()
{
    auto head = getLocalBounds().reduced (8, 6).removeFromTop (24);
    closeBtn.setBounds (head.removeFromRight (63));
    head.removeFromRight (6);
    resetBtn.setBounds (head.removeFromRight (63));
    head.removeFromRight (6);
    onBtn.setBounds (head.removeFromRight (63));
}

juce::Rectangle<float> GeqPanel::faderArea() const
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
    r.removeFromTop (32.0f);                       // header
    r.removeFromBottom (16.0f);                    // frequency labels
    r.removeFromLeft (26.0f);                      // dB scale
    return r;
}

float GeqPanel::colX (int i) const
{
    const auto a = faderArea();
    return a.getX() + ((float) i + 0.5f) * a.getWidth() / (float) kN;
}

float GeqPanel::yForDb (float db) const
{
    const auto a = faderArea();
    const float top = a.getY() + 5.0f, bot = a.getBottom() - 5.0f;
    const float t = (juce::jlimit (-kRange, kRange, db) + kRange) / (2.0f * kRange);
    return bot - t * (bot - top);
}

float GeqPanel::dbForY (float y) const
{
    const auto a = faderArea();
    const float top = a.getY() + 5.0f, bot = a.getBottom() - 5.0f;
    const float t = juce::jlimit (0.0f, 1.0f, (bot - y) / juce::jmax (1.0f, bot - top));
    return -kRange + t * 2.0f * kRange;
}

int GeqPanel::columnAt (float x) const
{
    const auto a = faderArea();
    if (x < a.getX() - 2.0f || x > a.getRight() + 2.0f)
        return -1;
    return juce::jlimit (0, kN - 1, (int) ((x - a.getX()) / juce::jmax (1.0f, a.getWidth()) * (float) kN));
}

float GeqPanel::getGain (int i) const
{
    auto* p = params[(size_t) i];
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

void GeqPanel::setGain (int i, float db)
{
    if (auto* p = params[(size_t) i])
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (db)));
}

void GeqPanel::beginTouch (int i)
{
    if (std::find (touched.begin(), touched.end(), i) != touched.end())
        return;
    touched.push_back (i);
    if (auto* p = params[(size_t) i])
        p->beginChangeGesture();
}

void GeqPanel::endTouches()
{
    for (int i : touched)
        if (auto* p = params[(size_t) i])
            p->endChangeGesture();
    touched.clear();
}

void GeqPanel::resetAll()
{
    for (auto* p : params)
        if (p != nullptr) p->beginChangeGesture();
    for (auto* p : params)
        if (p != nullptr) p->setValueNotifyingHost (p->getDefaultValue());
    for (auto* p : params)
        if (p != nullptr) p->endChangeGesture();
}

void GeqPanel::mouseMove (const juce::MouseEvent& e)
{
    const int h = columnAt (e.position.x);
    if (h != hover) { hover = h; repaint(); }
}

void GeqPanel::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void GeqPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Reset all faders");
        juce::Component::SafePointer<GeqPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                .withTargetScreenArea (juce::Rectangle<int> (e.getScreenX(), e.getScreenY(), 1, 1)),
                            [safe] (int r) { if (safe != nullptr && r == 1) safe->resetAll(); });
        return;
    }

    const auto a = faderArea();
    const int i = columnAt (e.position.x);
    if (i < 0 || e.position.y < a.getY() - 4.0f || e.position.y > a.getBottom() + 4.0f)
        return;

    active = i;
    beginTouch (i);

    // Grab the cap where it is (no jump); a click elsewhere on the track moves it there.
    const float capY = yForDb (getGain (i));
    grabOffset = std::abs (e.position.y - capY) <= 8.0f ? capY - e.position.y : 0.0f;
    setGain (i, snapDb (dbForY (e.position.y + grabOffset), e.mods.isCtrlDown()));
    repaint();
}

void GeqPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (active < 0)
        return;

    if (e.mods.isShiftDown())                      // sweep: every fader the mouse passes follows it
    {
        const int c = columnAt (e.position.x);
        if (c >= 0)
        {
            active = c;
            beginTouch (c);
            setGain (c, snapDb (dbForY (e.position.y), false));
        }
    }
    else
    {
        setGain (active, snapDb (dbForY (e.position.y + grabOffset), e.mods.isCtrlDown()));
    }
    repaint();
}

void GeqPanel::mouseUp (const juce::MouseEvent&)
{
    endTouches();                                  // the whole drag = one undo step
    active = -1;
    repaint();
}

void GeqPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = columnAt (e.position.x);
    if (i < 0)
        return;

    beginTouch (i);
    setGain (i, 0.0f);
    endTouches();
    repaint();
}

void GeqPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int i = columnAt (e.position.x);
    if (i < 0 || wheel.deltaY == 0.0f)
        return;

    const float step = e.mods.isCtrlDown() ? 0.1f : 0.5f;
    beginTouch (i);
    setGain (i, getGain (i) + (wheel.deltaY > 0.0f ? step : -step));
    endTouches();
    repaint();
}

void GeqPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xe0141619));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (open ? kGeqColour.withAlpha (0.7f) : kLineColour);
    g.drawRoundedRectangle (b, 6.0f, 1.0f);

    // Title + read-out of the fader under the mouse
    g.setColour (kGeqColour);
    g.fillEllipse (14.0f, 16.0f, 7.0f, 7.0f);
    g.setColour (juce::Colour (0xffd7dade));
    g.setFont (fkFont (11.5f, true));
    g.drawText ("GEQ  31 BAND", 28, 8, 130, 22, juce::Justification::centredLeft);

    const int ri = active >= 0 ? active : hover;
    if (ri >= 0)
    {
        g.setColour (kGeqColour);
        g.setFont (fkFont (10.5f, true));
        g.drawText (geqFreqLabel (MiniLiveStripProcessor::kGeqFreqs[(size_t) ri]) + "Hz   " + gainText ((double) getGain (ri)),
                    160, 8, 200, 22, juce::Justification::centredLeft);
    }

    const bool on = pOn->load() > 0.5f;
    const float al = on ? 1.0f : 0.45f;
    const auto a = faderArea();

    // dB scale
    g.setFont (fkFont (8.0f));
    for (float db : { 30.0f, 15.0f, 0.0f, -15.0f, -30.0f })
    {
        const float y = yForDb (db);
        g.setColour (juce::Colours::white.withAlpha (db == 0.0f ? 0.35f : 0.12f));
        g.fillRect (a.getX(), y - 0.5f, a.getWidth(), 1.0f);
        g.setColour (kLabelColour);
        g.drawText ((db > 0.0f ? juce::String ("+") : juce::String()) + juce::String ((int) db),
                    (int) b.getX() + 4, (int) y - 6, 22, 12, juce::Justification::centredRight, false);
    }

    // Faders
    const float colW = a.getWidth() / (float) kN;
    const float capW = juce::jlimit (8.0f, 18.0f, colW - 5.0f), capH = 10.0f;
    const float zeroY = yForDb (0.0f), topY = yForDb (kRange), botY = yForDb (-kRange);

    for (int i = 0; i < kN; ++i)
    {
        const float x = colX (i);
        const float gain = shown[(size_t) i];
        const float y = yForDb (gain);
        const bool hot = (i == hover || i == active);

        g.setColour (juce::Colour (0xff0b0d0f));                       // slot
        g.fillRoundedRectangle (x - 1.5f, topY, 3.0f, botY - topY, 1.5f);

        g.setColour (kGeqColour.withAlpha (al * (hot ? 1.0f : 0.75f))); // fill from 0 dB to the cap
        g.fillRect (x - 1.5f, juce::jmin (y, zeroY), 3.0f, std::abs (y - zeroY));

        const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ x, y });
        g.setColour ((hot ? juce::Colours::white : juce::Colour (0xffd7dade)).withAlpha (al));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (0xff1c1f23));
        g.fillRect (cap.getX() + 2.0f, y - 0.5f, capW - 4.0f, 1.0f);

        g.setFont (fkFont (7.5f));
        g.setColour (hot ? juce::Colours::white : kLabelColour);
        g.drawText (geqFreqLabel (MiniLiveStripProcessor::kGeqFreqs[(size_t) i]),
                    (int) (x - colW * 0.5f - 2.0f), (int) a.getBottom() + 3, (int) colW + 4, 11,
                    juce::Justification::centred, false);
    }
}

//==============================================================================
MiniLiveStripEditor::MiniLiveStripEditor (MiniLiveStripProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& apvts = proc.apvts;

    addAndMakeVisible (geqPanel);                 // added first: sits below every other child
    geqPanel.onClose = [this] { if (openModule == 4) toggleModule (4); };
    rtaDepthVal = apvts.getRawParameterValue ("rtaDepth");

    auto knob = [&] (juce::Slider& s, const juce::String& id,
                     std::function<juce::String (double)> toText,
                     std::function<double (const juce::String&)> fromText = {})
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, kTextBoxH);
        s.setRotaryParameters (kRotStart, kRotEnd, true);
        addAndMakeVisible (s);

        auto* param = apvts.getParameter (id);
        sliderAtt.push_back (std::make_unique<juce::SliderParameterAttachment> (*param, s, nullptr));

        // Set after the attachment (it installs its own text functions).
        s.textFromValueFunction = toText;
        if (fromText)
            s.valueFromTextFunction = fromText;
        s.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
        s.updateText();
    };

    auto secText = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + "s"
                                                      : juce::String ((int) std::lround (v)) + "ms"; };
    auto secFromText = [] (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const double v = s.getDoubleValue();
        if (s.endsWith ("ms")) return v;
        return s.endsWith ("s") ? v * 1000.0 : v;
    };

    // ---- Suppressor knobs
    knob (sensitivity, "sensitivity", [] (double v) { return juce::String (v, 1); });
    knob (notchQ,      "q",           [] (double v) { return juce::String (v, 1); });
    knob (depth,       "maxDepth",    [] (double v) { return "-" + juce::String (v, 1) + "dB"; },
                                      [] (const juce::String& t) { return std::abs (t.getDoubleValue()); });
    knob (growthTime,  "growthTime",  secText, secFromText);

    // ---- Input gain (left of the module buttons), RTA depth and RTA time (bottom right of the analyser)
    knob (outGain, "outGain", gainText);                 // GAIN IN: in front of all four modules

    knob (rtaTime, "rtaTime", secText, secFromText);
    rtaTime.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, kTextBoxH);
    rtaTime.updateText();

    knob (rtaDepth, "rtaDepth",
          [] (double v) { return "-" + juce::String ((int) std::lround (v)) + "dB"; },
          [] (const juce::String& t) { return std::abs (t.getDoubleValue()); });
    rtaDepth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, kTextBoxH);
    rtaDepth.updateText();

    // ---- Suppressor buttons

    scanBtn.setClickingTogglesState (true);
    scanBtn.setLabel ("SCAN");
    scanBtn.setColourOn (kAccent);
    addAndMakeVisible (scanBtn);
    buttonAtt.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter ("scan"), scanBtn, nullptr));

    growthBtn.setClickingTogglesState (true);
    growthBtn.setLabel ("GROWTH");
    growthBtn.setColourOn (kAccent);
    addAndMakeVisible (growthBtn);
    buttonAtt.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter ("growth"), growthBtn, nullptr));

    // Reset is momentary: set the parameter, the timer clears it once the audio thread handled it.
    resetBtn.setLabel ("RESET");
    resetBtn.setColourOn (kAccent);
    resetBtn.onClick = [this]
    {
        if (auto* r = proc.apvts.getParameter ("reset"))
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (1.0f);
            r->endChangeGesture();
        }
    };
    addAndMakeVisible (resetBtn);

    // Feedback suppressor on/off
    fbBtn.setClickingTogglesState (true);
    fbBtn.setLabel ("ON");
    fbBtn.setLabelOff ("OFF");
    fbBtn.setColourOn (kAccent);
    addAndMakeVisible (fbBtn);
    buttonAtt.push_back (std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter ("fbOn"), fbBtn, nullptr));

    // Undo / redo (momentary), top left of the analyser
    undoBtn.setLabel ("UNDO");
    undoBtn.setColourOn (kAccent);
    undoBtn.onClick = [this] { proc.history.undo(); };
    addAndMakeVisible (undoBtn);

    redoBtn.setLabel ("REDO");
    redoBtn.setColourOn (kAccent);
    redoBtn.onClick = [this] { proc.history.redo(); };
    addAndMakeVisible (redoBtn);

    // ---- EQ bands: only the knobs live here, inside the popup above the selected point.
    pEqVisible = apvts.getParameter ("eqVisible");
    pEqOn      = apvts.getParameter ("eqOn");

    for (int b = 0; b < kBands; ++b)
    {
        auto& ui = bands[(size_t) b];
        const juce::String id = "eq" + juce::String (b + 1);

        knob (ui.freq, id + "Freq", freqText, parseFreq);
        knob (ui.q,    id + "Q",    [] (double v) { return juce::String (v, 2); });
        knob (ui.gain, id + "Gain", gainText);

        for (auto* s : { &ui.freq, &ui.q, &ui.gain })
        {
            popup.addChildComponent (*s);            // moves the slider from the editor into the popup
            s->setVisible (false);
        }

        ui.pFreq  = apvts.getParameter (id + "Freq");
        ui.pQ     = apvts.getParameter (id + "Q");
        ui.pGain  = apvts.getParameter (id + "Gain");
        ui.pType  = apvts.getParameter (id + "Type");
        ui.pShape = apvts.getParameter (id + "Shape");
        ui.pOn    = apvts.getParameter (id + "On");
        ui.pSolo  = apvts.getParameter (id + "Solo");
    }

    bandSoloBtn.setBounds (BandPopup::kW - 72, 3, 32, 17);   // popup-local coordinates
    popup.addAndMakeVisible (bandSoloBtn);
    // One click toggles solo for the selected band. Only one band can solo at a time.
    bandSoloBtn.onSoloToggled = [this] (bool on)
    {
        const bool valid = on && selectedBand >= 0 && selectedBand < visibleBands();

        for (int b = 0; b < kBands; ++b)
        {
            auto* p = bands[(size_t) b].pSolo;
            const bool want = valid && b == selectedBand;
            if ((plainValue (p) > 0.5f) != want)
            {
                gesture (p, true);
                setPlain (p, want ? 1.0f : 0.0f);
                gesture (p, false);
            }
        }
    };

    // Drag the SOLO button: horizontal = frequency (Ctrl = Q), vertical = solo level.
    // Gesture-wrapped per drag so a single Undo brings back the state before the drag.
    auto bandParam = [this] (int b, const char* suffix) { return proc.apvts.getParameter ("eq" + juce::String (b + 1) + suffix); };
    auto soloLevelParam = [this] { return proc.apvts.getParameter ("soloLevel"); };

    bandSoloBtn.onDragBegin = [this, bandParam, soloLevelParam]
    {
        if (selectedBand < 0 || selectedBand >= visibleBands())
            return;

        soloDragBand   = selectedBand;
        soloDragFreq0  = plainValue (bandParam (soloDragBand, "Freq"));
        soloDragQ0     = plainValue (bandParam (soloDragBand, "Q"));
        soloDragLevel0 = plainValue (soloLevelParam());

        gesture (bandParam (soloDragBand, "Freq"), true);
        gesture (bandParam (soloDragBand, "Q"), true);
        gesture (soloLevelParam(), true);
    };

    bandSoloBtn.onDragMove = [this, bandParam, soloLevelParam] (float dx, float dy, bool ctrl)
    {
        if (soloDragBand < 0)
            return;

        const int  b       = soloDragBand;
        const bool usesGain = bandUsesGain (b);
        const float octaves = dx / 60.0f;                       // 60 px per octave

        // Gain bands: plain drag = frequency, Ctrl + drag = Q. Other types: both move together.
        const bool moveFreq = ! usesGain || ! ctrl;
        const bool moveQ    = ! usesGain || ctrl;

        if (auto* pf = bandParam (b, "Freq"); pf != nullptr && moveFreq)
        {
            const auto r = pf->getNormalisableRange();
            setPlain (pf, juce::jlimit (r.start, r.end, soloDragFreq0 * std::pow (2.0f, octaves)));
        }
        if (auto* pq = bandParam (b, "Q"); pq != nullptr && moveQ)
        {
            const auto r = pq->getNormalisableRange();
            setPlain (pq, juce::jlimit (r.start, r.end, soloDragQ0 * std::pow (2.0f, octaves)));
        }
        if (auto* pl = soloLevelParam())
        {
            const auto r = pl->getNormalisableRange();
            setPlain (pl, juce::jlimit (r.start, r.end, soloDragLevel0 - dy * 0.1f));   // up = louder
        }
    };

    bandSoloBtn.onDragEnd = [this, bandParam, soloLevelParam]
    {
        if (soloDragBand >= 0)
        {
            gesture (bandParam (soloDragBand, "Freq"), false);
            gesture (bandParam (soloDragBand, "Q"), false);
        }
        gesture (soloLevelParam(), false);
        soloDragBand = -1;
    };

    // ---- The four module buttons (click = panel, drag = change the processing order)
    pChainOrder = apvts.getParameter ("chainOrder");
    {
        const char* names[kMods] = { "FEEDBACK", "EQ", "LIMI / COMP", "GATE", "GEQ" };
        const ModuleButton::Icon icons[kMods] = { ModuleButton::IconFeedback, ModuleButton::IconEq,
                                                  ModuleButton::IconDynamics, ModuleButton::IconGate,
                                                  ModuleButton::IconGeq };
        const juce::Colour cols[kMods] = { kNotchColour, kEqColour, kAccent, kGateColour, kGeqColour };

        for (int m = 0; m < kMods; ++m)
        {
            auto bt = std::make_unique<ModuleButton> (icons[m], names[m], cols[m]);
            bt->onClick     = [this, m] { toggleModule (m); };
            bt->onDragStart = [this, m] { moduleDragStart (m); };
            bt->onDragMove  = [this, m] (int x) { moduleDragMove (m, x); };
            bt->onDragEnd   = [this, m] { moduleDragEnd (m); };
            addAndMakeVisible (*bt);
            modBtn[(size_t) m] = std::move (bt);
        }
    }
    moduleOrder = MiniLiveStripProcessor::orderFromIndex ((int) std::lround (plainValue (pChainOrder)));

    // Close button of the feedback panel
    subCloseBtn.setLabel ("CLOSE");
    subCloseBtn.setColourOn (kAccent);
    subCloseBtn.onClick = [this] { if (openModule == 0) toggleModule (0); };
    addChildComponent (subCloseBtn);

    limPanel.onClose  = [this] { if (openModule == 2) toggleModule (2); };
    limPanel.onSizeChanged = [this] { layoutSub(); };      // limiter <-> compressor changes the knob count
    gatePanel.onClose = [this] { if (openModule == 3) toggleModule (3); };
    addChildComponent (limPanel);
    addChildComponent (gatePanel);

    // ---- Thin meters on the right (always visible, independent of the module switches)
    grMeter.setScale (30.0f, 5.0f);
    grMeter.setBarColour (kAccent);
    gateMeter.setScale (80.0f, 10.0f);
    gateMeter.setBarColour (kGateColour);
    // GR meter fader: drag the marker line to set the limiter / compressor threshold.
    grMeter.markerDraggable = true;
    grMeter.onMarkerDragState = [this] (bool begin)
    {
        grThrComp  = proc.apvts.getRawParameterValue ("limMode")->load() > 0.5f;
        grThrParam = proc.apvts.getParameter (grThrComp ? "compThresh" : "limThresh");
        gesture (grThrParam, begin);
    };
    grMeter.onMarkerDrag = [this] (float depth)
    {
        if (grThrParam == nullptr)
            return;

        float thr = -depth;                                  // limiter: reduction at 0 dBFS = -threshold
        if (grThrComp)                                        // compressor: reduction = -thr * (1 - 1/ratio)
        {
            const float ratio = proc.apvts.getRawParameterValue ("compRatio")->load();
            const float k = 1.0f - 1.0f / juce::jmax (1.0f, ratio);
            if (k < 0.05f)
                return;                                       // ratio ~1: nothing to move
            thr = -depth / k;
        }
        setPlain (grThrParam, thr);                           // snapped / clamped to the range
    };
    addAndMakeVisible (grMeter);
    addAndMakeVisible (gateMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (outMeterR);

    addChildComponent (popup);

    setWantsKeyboardFocus (true);
    setSize (950, 500);                 // smallest size as default
    setResizable (true, true);
    setResizeLimits (950, 500, 2000, 1200);
    updateModuleVisibility();
    startTimerHz (30);
}

MiniLiveStripEditor::~MiniLiveStripEditor()
{
    stopTimer();
    endWheelGesture();
    setLookAndFeel (nullptr);
}

void MiniLiveStripEditor::timerCallback()
{
    // "Reset Notches" behaves like a button: once the audio thread has cleared
    // the notches, flip the switch back off.
    if (proc.resetDone.exchange (false))
    {
        if (auto* reset = proc.apvts.getParameter ("reset"))
        {
            if (reset->getValue() > 0.5f)
            {
                reset->beginChangeGesture();
                reset->setValueNotifyingHost (0.0f);
                reset->endChangeGesture();
            }
        }
    }

    // Show the Q preview curve for ~2.5 s after Q or Depth was touched.
    const float q = proc.getQ();
    const float d = proc.getMaxDepth();
    if (lastQ >= 0.0f && (q != lastQ || d != lastDepth))
        qPreviewFrames = 75;
    lastQ = q;
    lastDepth = d;
    if (qPreviewFrames > 0)
        --qPreviewFrames;

    // Close the Q wheel gesture shortly after the last wheel tick (= one undo step).
    if (wheelBand >= 0 && juce::Time::getMillisecondCounter() - lastWheelMs > 400)
        endWheelGesture();

    // Undo / redo availability, Growth time only matters while Growth Check is on.
    const bool cu = proc.history.canUndo(), cr = proc.history.canRedo();
    undoBtn.setEnabled (cu);  undoBtn.setAlpha (cu ? 1.0f : 0.4f);
    redoBtn.setEnabled (cr);  redoBtn.setAlpha (cr ? 1.0f : 0.4f);
    growthTime.setAlpha (growthBtn.getToggleState() ? 1.0f : 0.5f);

    // Meters on the right: always running, whatever is switched on or off.
    {
        auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
        auto ratioText = [] (float r)
        {
            return std::abs (r - std::round (r)) < 0.05f ? juce::String ((int) std::lround (r)) + ":1"
                                                         : juce::String (r, 1) + ":1";
        };

        // ---- Gain reduction (limiter / compressor)
        const bool  comp   = raw ("limMode") > 0.5f;
        const float grThr  = raw (comp ? "compThresh" : "limThresh");
        const float ratio  = raw ("compRatio");
        // Reduction a 0 dBFS signal gets: compressor = (-thr) * (1 - 1/ratio), limiter = the threshold gain.
        const float grMax  = comp ? juce::jmax (0.0f, -grThr * (1.0f - 1.0f / juce::jmax (1.0f, ratio)))
                                  : juce::jmax (0.0f, -grThr);
        grMeter.setFallRate (30.0f / (0.001f * juce::jmax (1.0f, raw (comp ? "compRelease" : "limRelease"))));
        grMeter.setSpan (grMax);
        grMeter.setMarker (grMax, comp ? ratioText (ratio) : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e:1")));

        // ---- Gate
        const float gDepth = raw ("gateDepth");
        gateMeter.setFallRate (80.0f / (0.001f * juce::jmax (1.0f, raw ("gateRelease"))));
        gateMeter.setSpan (gDepth);
        gateMeter.setMarker (gDepth, juce::String ((int) std::lround (gDepth)));

        // ---- Threshold read-outs under the meters
        const auto c1 = "THR " + juce::String (grThr, 1);
        const auto c2 = "THR " + juce::String (raw ("gateThresh"), 1);
        if (c1 != grCaption2 || c2 != gateCaption2)
        {
            grCaption2 = c1;
            gateCaption2 = c2;
            repaint (meterArea);
        }
    }

    // GR values keep the deepest reduction since the last look (exchange = read and reset).
    grMeter.setValue   (proc.limGrDisplay.exchange  (0.0f, std::memory_order_relaxed));
    gateMeter.setValue (proc.gateGrDisplay.exchange (0.0f, std::memory_order_relaxed));
    outMeter.setLevel  (proc.outLevelDisplay.load (std::memory_order_relaxed));
    outMeterR.setLevel (proc.outLevelDisplayR.load (std::memory_order_relaxed));
    if (proc.outIsStereo.load (std::memory_order_relaxed) != outStereoShown)
    {
        resized();                                 // mono <-> stereo changed: re-split the output block
        repaint();
    }

    // Module lamps follow their on/off parameters.
    auto paramOn = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; };

    modBtn[0]->setLit (paramOn ("fbOn"));
    modBtn[1]->setLit (paramOn ("eqOn"));
    modBtn[2]->setLit (paramOn ("limOn"));
    modBtn[3]->setLit (paramOn ("gateOn"));
    modBtn[4]->setLit (paramOn ("geqOn"));
    geqPanel.tick();

    syncModuleOrder();                                  // undo / redo / preset load of the chain order

    updatePopupHover();                                 // hover / fade of the band popup

    if (selectedBand >= 0)
        layoutPopup();                                  // follows the point, hides it if the band is gone

    if (limPanel.isVisible())  limPanel.tick();
    if (gatePanel.isVisible()) gatePanel.tick();

    repaint (displayArea);
}

void MiniLiveStripEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    // Thin meters at the right edge, from the top to the bottom of the GUI.
    meterArea = r.removeFromRight (2 * kRedW + kOutW + 2 * kMeterGap);
    r.removeFromRight (8);
    displayArea = r;                                   // the RTA gets everything that is left

    auto bars = meterArea;
    bars.removeFromBottom (26);                        // captions (two lines)
    grMeter.setBounds   (meterArea.getX(),                                  bars.getY(), kRedW,   bars.getHeight());
    gateMeter.setBounds (meterArea.getX() + kRedW + kMeterGap,              bars.getY(), kRedW,   bars.getHeight());
    outStereoShown = proc.outIsStereo.load (std::memory_order_relaxed);
    const int ox = meterArea.getX() + 2 * (kRedW + kMeterGap);
    if (outStereoShown)     // stereo: the output block is split into left and right bars
    {
        outMeter.setBounds  (ox,                       bars.getY(), kMeterW, bars.getHeight());
        outMeterR.setBounds (ox + kMeterW + 2,         bars.getY(), kMeterW, bars.getHeight());
    }
    else
    {
        outMeter.setBounds  (ox, bars.getY(), kMeterW, bars.getHeight());
    }
    outMeterR.setVisible (outStereoShown);

    // Undo / redo: top left inside the analyser.
    undoBtn.setBounds (displayArea.getX() + 8, displayArea.getY() + 8, 56, 20);
    redoBtn.setBounds (undoBtn.getRight() + 4, undoBtn.getY(), 56, 20);

    // Module buttons: bottom left inside the analyser, just above the frequency labels.
    // GAIN IN knob first (signal enters here), then the four modules.
    const int barY = displayArea.getBottom() - 44 - kModBtn;
    outGain.setBounds (displayArea.getX() + 8, barY, kGainBlkW, kModBtn);
    barArea = { outGain.getRight() + kChevGap, barY, kMods * kModBtn + (kMods - 1) * kChevGap, kModBtn };
    layoutModuleBar (false);

    // Bottom right strip of the analyser:  RTA DEPTH | RTA TIME
    rtaTime.setBounds (displayArea.getRight() - 8 - 92, displayArea.getBottom() - 36, 92, 34);
    rtaDepth.setBounds (rtaTime.getX() - 70 - 92, rtaTime.getY(), 92, 34);

    layoutSub();
    layoutPopup();
}

//==============================================================================
// Module buttons + panels
juce::Rectangle<int> MiniLiveStripEditor::slotRect (int slot) const
{
    return { barArea.getX() + slot * (kModBtn + kChevGap), barArea.getY(), kModBtn, kModBtn };
}

void MiniLiveStripEditor::layoutModuleBar (bool animate)
{
    for (int s = 0; s < kMods; ++s)
    {
        const int m = moduleOrder[(size_t) s];
        auto& b = *modBtn[(size_t) m];
        b.setPosition (s + 1);

        if (animate && b.isShowing())
        {
            animator.animateComponent (&b, slotRect (s), 1.0f, 150, false, 1.0, 1.0);
        }
        else
        {
            animator.cancelAnimation (&b, false);
            b.setBounds (slotRect (s));
        }
    }
}

// Places the panel of the open module in the bottom right corner of the analyser.
void MiniLiveStripEditor::layoutSub()
{
    subArea = {};

    // GEQ panel: over the RTA, between the Undo / Redo row and the module buttons.
    {
        const int gy = displayArea.getY() + 34;
        geqRect = { displayArea.getX() + 8, gy, displayArea.getWidth() - 16,
                    juce::jmax (80, barArea.getY() - 14 - gy) };
        geqPanel.setBounds (geqRect);
    }

    if (openModule == 0 || openModule == 2 || openModule == 3)
    {
        juce::Point<int> size { kFbPanelW, kFbPanelH };
        if (openModule == 2) size = limPanel.getPreferredSize();
        if (openModule == 3) size = gatePanel.getPreferredSize();

        // never run into the module buttons on the left
        size.x = juce::jmax (juce::jmin (size.x, displayArea.getRight() - 8 - (barArea.getRight() + 8)), 212);
        subArea = { displayArea.getRight() - 8 - size.x, displayArea.getBottom() - 44 - size.y, size.x, size.y };
    }

    if (openModule == 0)                    // ---- FEEDBACK: on/off, scan, growth, reset + 4 knobs
    {
        auto r = subArea.reduced (10, 8);
        auto title = r.removeFromTop (25);
        subCloseBtn.setBounds (title.removeFromRight (63));
        title.removeFromRight (6);
        fbBtn.setBounds (title.removeFromRight (63));
        r.removeFromTop (8);
        r.removeFromBottom (16);            // hint line

        constexpr int bh = 26;
        auto col = r.removeFromLeft (130);
        scanBtn.setBounds (col.removeFromTop (bh));
        col.removeFromTop (4);
        growthBtn.setBounds (col.removeFromTop (bh));
        col.removeFromTop (4);
        resetBtn.setBounds (col.removeFromTop (bh));
        r.removeFromLeft (14);

        constexpr int knobW = 65, knobH = 61;                       // 10 % smaller than before
        const int cell = juce::jmin (86, r.getWidth() / 4);
        juce::Slider* ks[4] = { &sensitivity, &notchQ, &depth, &growthTime };
        for (auto* s : ks)
        {
            auto cl = r.removeFromLeft (cell);
            cl.removeFromTop (14);                                  // caption
            s->setBounds (cl.withSizeKeepingCentre (knobW, knobH));
        }
    }
    else if (openModule == 2)
    {
        limPanel.setBounds (subArea);
    }
    else if (openModule == 3)
    {
        gatePanel.setBounds (subArea);
    }
}

void MiniLiveStripEditor::updateModuleVisibility()
{
    const bool fb = (openModule == 0);

    for (auto* c : std::initializer_list<juce::Component*> { &fbBtn, &scanBtn, &growthBtn, &resetBtn,
                                                              &sensitivity, &notchQ, &depth, &growthTime,
                                                              &subCloseBtn })
        c->setVisible (fb);

    limPanel.setVisible (openModule == 2);
    gatePanel.setVisible (openModule == 3);
    geqPanel.setOpen (openModule == 4);

    for (int m = 0; m < kMods; ++m)
        modBtn[(size_t) m]->setOpen (m == openModule);
}

void MiniLiveStripEditor::toggleModule (int module)
{
    if (module == 1)                        // EQ: only an on / off switch, no panel
    {
        gesture (pEqOn, true);
        setPlain (pEqOn, plainValue (pEqOn) > 0.5f ? 0.0f : 1.0f);
        gesture (pEqOn, false);
        return;
    }

    openModule = (openModule == module) ? -1 : module;
    updateModuleVisibility();
    layoutSub();

    if (openModule == 2)      limPanel.tick();      // picks up the current mode / size
    else if (openModule == 3) gatePanel.tick();

    repaint();
}

void MiniLiveStripEditor::syncModuleOrder()
{
    if (dragModule >= 0 || pChainOrder == nullptr)
        return;

    const auto o = MiniLiveStripProcessor::orderFromIndex ((int) std::lround (plainValue (pChainOrder)));
    if (o != moduleOrder)
    {
        moduleOrder = o;
        layoutModuleBar (true);
        repaint (barArea);
    }
}

// Dragging a module button: it follows the mouse, the other buttons slide aside. On release the
// new order is written to the "chainOrder" parameter (one Undo step) and the audio follows.
void MiniLiveStripEditor::moduleDragStart (int m)
{
    dragModule = m;
    dragOrder = moduleOrder;
    animator.cancelAnimation (modBtn[(size_t) m].get(), false);
}

void MiniLiveStripEditor::moduleDragMove (int m, int wantedX)
{
    const int x0 = slotRect (0).getX(), x3 = slotRect (kMods - 1).getX();
    const int x = juce::jlimit (x0, x3, wantedX);
    modBtn[(size_t) m]->setTopLeftPosition (x, barArea.getY());

    const int stride = kModBtn + kChevGap;
    const int target = juce::jlimit (0, kMods - 1, (int) std::lround ((double) (x - x0) / (double) stride));

    MiniLiveStripProcessor::ModuleOrder rest {};
    int n = 0;
    for (int s = 0; s < kMods; ++s)
        if (moduleOrder[(size_t) s] != m)
            rest[(size_t) n++] = moduleOrder[(size_t) s];

    MiniLiveStripProcessor::ModuleOrder next {};
    int k = 0;
    for (int s = 0; s < kMods; ++s)
        next[(size_t) s] = (s == target) ? m : rest[(size_t) k++];

    if (next == dragOrder)
        return;

    dragOrder = next;
    for (int s = 0; s < kMods; ++s)
    {
        const int mod = dragOrder[(size_t) s];
        modBtn[(size_t) mod]->setPosition (s + 1);
        if (mod != m)
            animator.animateComponent (modBtn[(size_t) mod].get(), slotRect (s), 1.0f, 120, false, 1.0, 1.0);
    }
    repaint (barArea);
}

void MiniLiveStripEditor::moduleDragEnd (int m)
{
    int slot = 0;
    for (int s = 0; s < kMods; ++s)
        if (dragOrder[(size_t) s] == m)
            slot = s;

    animator.animateComponent (modBtn[(size_t) m].get(), slotRect (slot), 1.0f, 120, false, 1.0, 1.0);
    dragModule = -1;

    if (dragOrder != moduleOrder)
    {
        moduleOrder = dragOrder;
        gesture (pChainOrder, true);
        setPlain (pChainOrder, (float) MiniLiveStripProcessor::indexFromOrder (moduleOrder));
        gesture (pChainOrder, false);
    }
    repaint (barArea);
}

//==============================================================================
// EQ bands
int MiniLiveStripEditor::visibleBands() const
{
    return juce::jlimit (MiniLiveStripProcessor::kMinEqBands, kBands,
                         (int) std::lround (plainValue (pEqVisible)));
}

bool MiniLiveStripEditor::overlayHit (juce::Point<int> p) const
{
    return (openModule == 0 && subArea.contains (p)) || (openModule == 4 && geqRect.contains (p));
}

void MiniLiveStripEditor::selectBand (int band)
{
    if (band != selectedBand)
    {
        selectedBand = band;
        if (band >= 0)
            popup.toFront (false);
    }

    layoutPopup();
    repaint (displayArea);
}

// The band popup opens while the mouse is on the selected band (or on the popup itself) and
// fades out over kPopupFadeMs once the mouse has left. The selection itself is kept.
void MiniLiveStripEditor::updatePopupHover()
{
    // Only the selected band may solo: selecting another band ends the solo of the previous one.
    for (int b = 0; b < kBands; ++b)
    {
        auto* p = bands[(size_t) b].pSolo;
        if (b != selectedBand && plainValue (p) > 0.5f)
        {
            gesture (p, true);
            setPlain (p, 0.0f);
            gesture (p, false);
        }
    }

    if (selectedBand < 0 || selectedBand >= visibleBands())
    {
        popupAlpha = 0.0f;
    }
    else
    {
        const bool onBand = hoverBand == selectedBand || dragBand == selectedBand
                            || popup.isMouseOverOrDragging (true);
        const auto now = juce::Time::getMillisecondCounter();

        if (onBand)
        {
            popupLeaveMs = now;
            popupAlpha   = 1.0f;
        }
        else if (popupAlpha > 0.0f)
        {
            const float t = (float) (now - popupLeaveMs) / (float) kPopupFadeMs;
            popupAlpha = juce::jlimit (0.0f, 1.0f, 1.0f - t);
        }
    }

    if (openModule >= 0)
        popupAlpha = 0.0f;                  // a module sub-display takes the same spot

    popup.setAlpha (popupAlpha);
    popup.setVisible (popupAlpha > 0.0f);

}

// Knob popup (Freq / Q / Gain) above the selected point; below it when there is no room above.
void MiniLiveStripEditor::layoutPopup()
{
    if (selectedBand >= visibleBands())
        selectedBand = -1;

    for (int b = 0; b < kBands; ++b)
    {
        const bool sel = (b == selectedBand);
        auto& ui = bands[(size_t) b];
        ui.freq.setVisible (sel);
        ui.q.setVisible (sel);
        ui.gain.setVisible (sel);
    }

    popup.setVisible (selectedBand >= 0 && popupAlpha > 0.0f);
    if (selectedBand < 0)
    {
        popupRect = {};
        return;
    }

    // While one of the three knobs is being dragged the popup must stay where it is: changing
    // FREQ / GAIN moves the EQ point, and a popup that follows it would slide away under the mouse,
    // which makes the knob (it measures the drag in its own coordinates) jump around.
    {
        auto& sel = bands[(size_t) selectedBand];
        if (popup.getWidth() > 0 && (sel.freq.isMouseButtonDown() || sel.q.isMouseButtonDown()
                                      || sel.gain.isMouseButtonDown() || bandSoloBtn.isDown()))
        {
            const int type  = juce::jlimit (0, typeNames().size() - 1,  (int) std::lround (plainValue (sel.pType)));
            const int shape = juce::jlimit (0, shapeNames().size() - 1, (int) std::lround (plainValue (sel.pShape)));
            popup.setContent ("Band " + juce::String (selectedBand + 1) + "   " + typeNames()[type] + "   " + shapeNames()[shape],
                              kBandColours[selectedBand], bandUsesGain (selectedBand));
            return;
        }
    }

    // Docked like the module sub-displays: bottom-right of the analyser, never on top of the EQ points.
    popupRect = { displayArea.getRight() - 8 - BandPopup::kW, displayArea.getBottom() - 44 - BandPopup::kH,
                  BandPopup::kW, BandPopup::kH };
    popup.setBounds (popupRect);

    auto& ui = bands[(size_t) selectedBand];
    ui.freq.setBounds (BandPopup::slot (0));
    ui.q.setBounds    (BandPopup::slot (1));
    ui.gain.setBounds (BandPopup::slot (2));
    ui.gain.setAlpha (bandUsesGain (selectedBand) ? 1.0f : 0.35f);

    const int type  = juce::jlimit (0, typeNames().size() - 1,  (int) std::lround (plainValue (ui.pType)));
    const int shape = juce::jlimit (0, shapeNames().size() - 1, (int) std::lround (plainValue (ui.pShape)));
    popup.setContent ("Band " + juce::String (selectedBand + 1) + "   " + typeNames()[type] + "   " + shapeNames()[shape],
                      kBandColours[selectedBand], bandUsesGain (selectedBand));
    bandSoloBtn.setColourOn (kBandColours[selectedBand]);
    bandSoloBtn.setToggleState (plainValue (ui.pSolo) > 0.5f, juce::dontSendNotification);
}

// Right-click on an EQ point: slope, shape, band on/off, add band, delete band.
void MiniLiveStripEditor::showBandMenu (int band, juce::Point<int> screenPos)
{
    const int n = visibleBands();
    auto& ui = bands[(size_t) band];
    const int curType  = (int) std::lround (plainValue (ui.pType));
    const int curShape = (int) std::lround (plainValue (ui.pShape));

    juce::PopupMenu typeMenu, shapeMenu, menu;
    for (int i = 0; i < typeNames().size(); ++i)
        typeMenu.addItem (100 + i, typeNames()[i], true, i == curType);
    for (int i = 0; i < shapeNames().size(); ++i)
        shapeMenu.addItem (200 + i, shapeNames()[i], true, i == curShape);

    menu.addSubMenu ("Slope", typeMenu);
    menu.addSubMenu ("Shape", shapeMenu);
    menu.addItem (300, "Band On", true, plainValue (ui.pOn) > 0.5f);
    menu.addSeparator();
    menu.addItem (400, "Add band EQ", n < kBands);
    menu.addItem (500, "Delete band", n > MiniLiveStripProcessor::kMinEqBands);

    juce::Component::SafePointer<MiniLiveStripEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)),
                        [safe, band] (int result)
                        {
                            if (safe != nullptr && result != 0)
                                safe->handleBandMenu (band, result);
                        });
}

void MiniLiveStripEditor::handleBandMenu (int band, int id)
{
    if (band < 0 || band >= visibleBands())
        return;

    auto& ui = bands[(size_t) band];

    if (id >= 100 && id < 200)
    {
        gesture (ui.pType, true);
        setPlain (ui.pType, (float) (id - 100));
        gesture (ui.pType, false);
    }
    else if (id >= 200 && id < 300)
    {
        gesture (ui.pShape, true);
        setPlain (ui.pShape, (float) (id - 200));
        gesture (ui.pShape, false);
    }
    else if (id == 300)
    {
        gesture (ui.pOn, true);
        setPlain (ui.pOn, plainValue (ui.pOn) > 0.5f ? 0.0f : 1.0f);
        gesture (ui.pOn, false);
    }
    else if (id == 400)
    {
        addBand (-1.0f, 0.0f);
    }
    else if (id == 500)
    {
        deleteBand (band);
    }

    layoutPopup();
    repaint (displayArea);
}

// Right-click on empty space of the analyser: add a band right there, or reset the EQ.
void MiniLiveStripEditor::showEmptyMenu (const juce::MouseEvent& e)
{
    const auto g = makeGeom();
    const float f    = g.freqForX (e.position.x);
    const float gain = g.respForY (e.position.y);

    juce::PopupMenu menu;
    menu.addItem (1, "Add band EQ here", visibleBands() < kBands);
    menu.addItem (2, "Reset EQ");

    const auto sp = e.getScreenPosition();
    juce::Component::SafePointer<MiniLiveStripEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (juce::Rectangle<int> (sp.x, sp.y, 1, 1)),
                        [safe, f, gain] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            if (result == 1)      safe->addBand (f, gain);
                            else if (result == 2) safe->resetAllBands();
                        });
}

// Adds the next band (one undo step). freq > 0 places it at that frequency / gain.
void MiniLiveStripEditor::addBand (float freq, float gain)
{
    const int n = visibleBands();
    if (n >= kBands)
        return;

    auto& ui = bands[(size_t) n];
    const bool place = freq > 0.0f;

    gesture (pEqVisible, true);
    if (place) { gesture (ui.pFreq, true); gesture (ui.pGain, true); }

    setPlain (pEqVisible, (float) (n + 1));
    if (place)
    {
        setPlain (ui.pFreq, freq);
        setPlain (ui.pGain, gain);
    }

    if (place) { gesture (ui.pGain, false); gesture (ui.pFreq, false); }
    gesture (pEqVisible, false);

    selectBand (n);
}

// Deletes one band. The bands behind it move up one place (all their values are copied),
// the freed last band goes back to its defaults and the shown count drops by one.
// Everything happens in one gesture, so a single Undo brings the band back.
void MiniLiveStripEditor::deleteBand (int band)
{
    const int n = visibleBands();
    if (n <= MiniLiveStripProcessor::kMinEqBands || band < 0 || band >= n)
        return;

    endWheelGesture();
    hoverBand = -1;
    dragBand = -1;

    static const char* const suffix[] = { "Freq", "Q", "Gain", "Type", "Shape", "On" };
    auto& apvts = proc.apvts;
    auto idOf = [] (int b, const char* s) { return "eq" + juce::String (b + 1) + s; };

    std::vector<juce::RangedAudioParameter*> touched;
    for (int b = band; b < n; ++b)
        for (auto* s : suffix)
            touched.push_back (apvts.getParameter (idOf (b, s)));
    touched.push_back (pEqVisible);

    for (auto* p : touched)
        gesture (p, true);

    for (int b = band; b < n - 1; ++b)
        for (auto* s : suffix)
            apvts.getParameter (idOf (b, s))->setValueNotifyingHost (apvts.getParameter (idOf (b + 1, s))->getValue());

    for (auto* s : suffix)
        if (auto* p = apvts.getParameter (idOf (n - 1, s)))
            p->setValueNotifyingHost (p->getDefaultValue());

    setPlain (pEqVisible, (float) (n - 1));

    for (auto* p : touched)
        gesture (p, false);

    selectBand (-1);
}

// Reset EQ: every parameter of all bands back to its default and the shown band count back to 4.
// One gesture, so a single Undo brings everything back.
void MiniLiveStripEditor::resetAllBands()
{
    endWheelGesture();
    hoverBand = -1;
    dragBand = -1;

    static const char* const suffix[] = { "Freq", "Q", "Gain", "Type", "Shape", "On" };
    auto& apvts = proc.apvts;

    std::vector<juce::RangedAudioParameter*> touched;
    for (int b = 0; b < kBands; ++b)
        for (auto* s : suffix)
            if (auto* p = apvts.getParameter ("eq" + juce::String (b + 1) + s))
                touched.push_back (p);
    touched.push_back (pEqVisible);

    for (auto* p : touched)
        gesture (p, true);

    for (auto* p : touched)
        p->setValueNotifyingHost (p->getDefaultValue());

    for (auto* p : touched)
        gesture (p, false);

    selectBand (-1);
}

//==============================================================================
// EQ points on the RTA display
MiniLiveStripEditor::PlotGeom MiniLiveStripEditor::makeGeom() const
{
    PlotGeom g;
    g.area       = displayArea.toFloat();
    g.plotTop    = g.area.getY() + 4.0f;
    g.plotBottom = g.area.getBottom() - 38.0f;
    g.dbBottom   = -juce::jlimit (30.0f, 120.0f, rtaDepthVal != nullptr ? rtaDepthVal->load() : 100.0f);
    g.midY       = 0.5f * (g.plotTop + g.plotBottom);
    g.halfH      = 0.5f * (g.plotBottom - g.plotTop);
    return g;
}

float MiniLiveStripEditor::PlotGeom::xForFreq (float f) const
{
    const float t = std::log (juce::jlimit (kFMin, kFMax, f) / kFMin) / std::log (kFMax / kFMin);
    return area.getX() + area.getWidth() * t;
}

float MiniLiveStripEditor::PlotGeom::freqForX (float x) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / juce::jmax (1.0f, area.getWidth()));
    return kFMin * std::pow (kFMax / kFMin, t);
}

float MiniLiveStripEditor::PlotGeom::yForDb (float db) const
{
    const float t = (juce::jlimit (dbBottom, kDbTop, db) - dbBottom) / (kDbTop - dbBottom);
    return plotBottom - t * (plotBottom - plotTop);
}

float MiniLiveStripEditor::PlotGeom::yForResp (double db) const
{
    const float t = juce::jlimit (-kRespRange, kRespRange, (float) db) / kRespRange;
    return midY - t * halfH;
}

float MiniLiveStripEditor::PlotGeom::respForY (float y) const
{
    return juce::jlimit (-1.0f, 1.0f, (midY - y) / juce::jmax (1.0f, halfH)) * kRespRange;
}

float MiniLiveStripEditor::plainValue (juce::RangedAudioParameter* p)
{
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

void MiniLiveStripEditor::setPlain (juce::RangedAudioParameter* p, float plain)
{
    if (p == nullptr)
        return;

    const auto& range = p->getNormalisableRange();
    p->setValueNotifyingHost (p->convertTo0to1 (range.snapToLegalValue (plain)));
}

void MiniLiveStripEditor::gesture (juce::RangedAudioParameter* p, bool begin)
{
    if (p == nullptr)
        return;

    if (begin) p->beginChangeGesture();
    else       p->endChangeGesture();
}

bool MiniLiveStripEditor::bandUsesGain (int band) const
{
    const int type = (int) std::lround (plainValue (bands[(size_t) band].pType));
    return MiniLiveStripProcessor::typeUsesGain (type);
}

juce::Point<float> MiniLiveStripEditor::nodePos (int band, const PlotGeom& g) const
{
    const auto& ui = bands[(size_t) band];
    const float x = g.xForFreq (plainValue (ui.pFreq));
    const float y = bandUsesGain (band) ? g.yForResp ((double) plainValue (ui.pGain)) : g.midY;
    return { x, y };
}

int MiniLiveStripEditor::hitTestNode (juce::Point<float> p) const
{
    if (overlayHit (p.toInt()))
        return -1;

    const auto g = makeGeom();
    if (! g.area.contains (p))
        return -1;

    int best = -1;
    float bestD = kNodeHit;
    for (int b = 0; b < visibleBands(); ++b)
    {
        const float d = nodePos (b, g).getDistanceFrom (p);
        if (d <= bestD)
        {
            bestD = d;
            best = b;
        }
    }
    return best;
}

void MiniLiveStripEditor::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h != hoverBand)
    {
        hoverBand = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
}

void MiniLiveStripEditor::mouseExit (const juce::MouseEvent&)
{
    hoverBand = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MiniLiveStripEditor::mouseDown (const juce::MouseEvent& e)
{
    dragBand = -1;
    if (overlayHit (e.getPosition()))
        return;

    const int h = hitTestNode (e.position);

    if (e.mods.isPopupMenu())
    {
        if (h >= 0)
        {
            selectBand (h);
            showBandMenu (h, e.getScreenPosition());
        }
        else if (displayArea.contains (e.getPosition()))
        {
            showEmptyMenu (e);
        }
        return;
    }

    selectBand (h);                         // click on empty space (-1) closes the knob popup
    if (h < 0)
        return;

    if (e.mods.isShiftDown())               // Shift + hold on a point: solo that band while held
    {
        auto* p = bands[(size_t) h].pSolo;
        gesture (p, true);
        setPlain (p, 1.0f);
        gesture (p, false);
        shiftSoloBand = h;
    }

    auto& ui = bands[(size_t) h];
    dragBand = h;
    dragGain = bandUsesGain (h);
    dragCtrl = e.mods.isCtrlDown();

    if (dragCtrl)                           // Ctrl + drag: horizontal = Q, vertical = gain, freq stays
    {
        ctrlQ0    = plainValue (ui.pQ);
        ctrlGain0 = plainValue (ui.pGain);
        gesture (ui.pQ, true);
        if (dragGain)
            gesture (ui.pGain, true);
        return;
    }

    gesture (ui.pFreq, true);
    if (dragGain)
        gesture (ui.pGain, true);
}

void MiniLiveStripEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    auto& ui = bands[(size_t) dragBand];

    if (dragCtrl)
    {
        const auto rq = ui.pQ->getNormalisableRange();
        setPlain (ui.pQ, juce::jlimit (rq.start, rq.end, ctrlQ0 * std::exp (0.01f * (float) e.getDistanceFromDragStartX())));

        if (dragGain)
        {
            const auto rg = ui.pGain->getNormalisableRange();
            setPlain (ui.pGain, juce::jlimit (rg.start, rg.end, ctrlGain0 - 0.1f * (float) e.getDistanceFromDragStartY()));
        }

        layoutPopup();
        return;
    }

    const auto g = makeGeom();
    setPlain (ui.pFreq, g.freqForX (e.position.x));
    if (dragGain)
        setPlain (ui.pGain, g.respForY (e.position.y));

    layoutPopup();                          // the knob popup follows the point
}

void MiniLiveStripEditor::mouseUp (const juce::MouseEvent&)
{
    if (shiftSoloBand >= 0)                 // release the Shift + hold solo
    {
        auto* p = bands[(size_t) shiftSoloBand].pSolo;
        gesture (p, true);
        setPlain (p, 0.0f);
        gesture (p, false);
        shiftSoloBand = -1;
    }

    if (dragBand < 0)
        return;

    auto& ui = bands[(size_t) dragBand];
    gesture (dragCtrl ? ui.pQ : ui.pFreq, false);
    if (dragGain)
        gesture (ui.pGain, false);

    dragCtrl = false;
    dragBand = -1;
}

void MiniLiveStripEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h < 0)
    {
        // Double-click on empty analyser space: add a band right there (same as "Add band EQ here").
        if (overlayHit (e.getPosition()) || ! displayArea.contains (e.getPosition()))
            return;

        const auto g = makeGeom();
        if (! g.area.contains (e.position))
            return;

        addBand (g.freqForX (e.position.x), g.respForY (e.position.y));
        return;
    }

    // Double-click on a point: gain and Q back to their defaults.
    // (both parameters in one gesture = one undo step)
    auto& ui = bands[(size_t) h];
    for (auto* p : { ui.pGain, ui.pQ })
        gesture (p, true);
    for (auto* p : { ui.pGain, ui.pQ })
        p->setValueNotifyingHost (p->getDefaultValue());
    for (auto* p : { ui.pGain, ui.pQ })
        gesture (p, false);
}

void MiniLiveStripEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int h = hitTestNode (e.position);
    if (h < 0)
    {
        juce::AudioProcessorEditor::mouseWheelMove (e, wheel);
        return;
    }

    auto& ui = bands[(size_t) h];

    // Shift + scroll = slope (filter type), Ctrl + scroll = shape. One notch = one step.
    if (e.mods.isShiftDown() || e.mods.isCtrlDown())
    {
        endWheelGesture();

        const float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        choiceWheelAccum += delta;
        if (std::abs (choiceWheelAccum) < 0.25f)
            return;

        const int dir = choiceWheelAccum > 0.0f ? 1 : -1;
        choiceWheelAccum = 0.0f;

        const bool slope = e.mods.isShiftDown();
        auto* p = slope ? ui.pType : ui.pShape;
        const int maxIdx = slope ? (int) MiniLiveStripProcessor::NumEqTypes - 1
                                 : MiniLiveStripProcessor::kNumShapes - 1;
        const int cur  = (int) std::lround (plainValue (p));
        const int next = juce::jlimit (0, maxIdx, cur + dir);

        if (next != cur)
        {
            gesture (p, true);
            setPlain (p, (float) next);
            gesture (p, false);
        }
        return;
    }

    // Plain scroll = Q: scroll up = higher Q (narrower), scroll down = lower Q (wider). Exponential steps.
    const float step = juce::jlimit (-0.2f, 0.2f, wheel.deltaY * 0.5f);
    const float q = plainValue (ui.pQ) * std::exp (step);

    // The gesture stays open while the wheel keeps turning, so one scroll = one undo step.
    if (wheelBand != h)
    {
        endWheelGesture();
        gesture (ui.pQ, true);
        wheelBand = h;
    }

    setPlain (ui.pQ, q);
    lastWheelMs = juce::Time::getMillisecondCounter();
}

void MiniLiveStripEditor::endWheelGesture()
{
    if (wheelBand >= 0)
    {
        gesture (bands[(size_t) wheelBand].pQ, false);
        wheelBand = -1;
    }
}

bool MiniLiveStripEditor::keyPressed (const juce::KeyPress& k)
{
    if (! k.getModifiers().isCommandDown())
        return false;

    const int code = k.getKeyCode();

    if (code == 'Z' || code == 'z')
    {
        if (k.getModifiers().isShiftDown()) proc.history.redo();
        else                                proc.history.undo();
        return true;
    }

    if (code == 'Y' || code == 'y')
    {
        proc.history.redo();
        return true;
    }

    return false;
}

//==============================================================================
void MiniLiveStripEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBgColour);

    g.setColour (juce::Colour (0xff2a2d31));
    g.drawRoundedRectangle (displayArea.toFloat().expanded (1.0f), 6.0f, 1.0f);

    drawDisplay (g);
    drawBar (g);
    drawSub (g);

    // Captions of the thin meters
    g.setColour (kLabelColour);
    g.setFont (fkFont (8.5f, true));
    auto caption = [&] (const juce::Component& m, const juce::String& t)
    {
        g.drawText (t, m.getX() - 6, m.getBottom() + 1, m.getWidth() + 12, 12, juce::Justification::centred);
    };
    caption (grMeter, "GR");
    caption (gateMeter, "GATE");
    if (outStereoShown)
    {
        caption (outMeter, "L");
        caption (outMeterR, "R");
    }
    else
    {
        caption (outMeter, "OUT");
    }

    // Threshold read-outs under the GR and GATE meters
    g.setFont (fkFont (8.0f));
    g.drawText (grCaption2,   grMeter.getX(),   grMeter.getBottom() + 12,   grMeter.getWidth(),   12, juce::Justification::centred);
    g.drawText (gateCaption2, gateMeter.getX(), gateMeter.getBottom() + 12, gateMeter.getWidth(), 12, juce::Justification::centred);
}

// Arrows between the module buttons (signal flow) + a short usage hint next to Undo / Redo.
void MiniLiveStripEditor::drawBar (juce::Graphics& g)
{
    g.setColour (kLabelColour);
    g.setFont (fkFont (9.0f, true));
    g.drawText ("GAIN IN", outGain.getX() - 6, outGain.getY() - 10, outGain.getWidth() + 12, 11,
                juce::Justification::centred);

    g.setColour (kLabelColour.withAlpha (0.75f));
    for (int s = -1; s < kMods - 1; ++s)        // s = -1: arrow from GAIN IN into the first module
    {
        const float cx = s < 0 ? (float) barArea.getX() - 0.5f * (float) kChevGap
                               : (float) slotRect (s).getRight() + 0.5f * (float) kChevGap;
        const float cy = (float) barArea.getCentreY();
        juce::Path p;
        p.startNewSubPath (cx - 3.0f, cy - 5.0f);
        p.lineTo (cx + 3.0f, cy);
        p.lineTo (cx - 3.0f, cy + 5.0f);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const int hx = redoBtn.getRight() + 14;
    const int hw = displayArea.getRight() - 70 - hx;
    if (hw > 260)
    {
        g.setFont (fkFont (9.5f));
        g.setColour (kLabelColour.withAlpha (0.7f));
        g.drawText ("EQ point: drag = freq/gain   ctrl+drag = Q/gain   scroll = Q   shift+scroll = slope   ctrl+scroll = shape",
                    hx, undoBtn.getY(), hw, undoBtn.getHeight(), juce::Justification::centredLeft);
    }
}

// Panel of the feedback module (the dynamics panels paint themselves).
void MiniLiveStripEditor::drawSub (juce::Graphics& g)
{
    if (openModule != 0)
        return;

    const auto b = subArea.toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xe0141619));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (kLineColour);
    g.drawRoundedRectangle (b, 6.0f, 1.0f);

    g.setColour (kNotchColour);
    g.fillEllipse ((float) subArea.getX() + 14.0f, (float) subArea.getY() + 17.0f, 7.0f, 7.0f);
    g.setColour (juce::Colour (0xffd7dade));
    g.setFont (fkFont (11.5f, true));
    g.drawText ("FEEDBACK", subArea.getX() + 28, subArea.getY() + 8, 140, 25, juce::Justification::centredLeft);

    g.setColour (kLabelColour);
    g.setFont (fkFont (10.0f, true));

    struct T { const juce::Slider* s; const char* name; };
    for (const auto& t : { T { &sensitivity, "SENSITIVITY" }, T { &notchQ, "NOTCH Q" },
                           T { &depth, "DEPTH" }, T { &growthTime, "GROWTH TIME" } })
        g.drawText (t.name, t.s->getX() - 10, t.s->getY() - 12, t.s->getWidth() + 20, 12,
                    juce::Justification::centred);

    g.setFont (fkFont (9.0f));
    g.setColour (kLabelColour.withAlpha (0.8f));
    g.drawText ("Scan: places notches   Growth: a peak must keep growing   Reset feedback: clears all notches",
                subArea.getX() + 12, subArea.getBottom() - 22, subArea.getWidth() - 24, 14,
                juce::Justification::centredLeft);
}

//==============================================================================
void MiniLiveStripEditor::drawDisplay (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (displayArea);

    const auto G = makeGeom();
    const auto area = G.area;
    g.setColour (juce::Colour (0xff25272a));
    g.fillRoundedRectangle (area, 6.0f);

    constexpr float fMin = kFMin;
    constexpr float fMax = kFMax;
    const float dbBottom = G.dbBottom;
    constexpr float respRange = kRespRange;

    const float plotTop    = G.plotTop;
    const float plotBottom = G.plotBottom;
    const float midY       = G.midY;

    auto xForFreq = [&] (float f)   { return G.xForFreq (f); };
    auto yForDb   = [&] (float db)  { return G.yForDb (db); };
    auto yForResp = [&] (double db) { return G.yForResp (db); };

    // ---- Grid: dB lines
    g.setFont (11.0f);
    const float gridStep = G.dbBottom <= -60.0f ? 20.0f : 10.0f;
    for (float db = -gridStep; db > G.dbBottom + 5.0f; db -= gridStep)
    {
        const float y = yForDb (db);
        g.setColour (juce::Colour (0x22ffffff));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        g.setColour (juce::Colours::grey);
        g.drawText (juce::String ((int) db), (int) area.getX() + 3, (int) y - 12, 30, 12,
                    juce::Justification::left);
    }

    // ---- Grid: frequency lines
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);

        g.setColour (juce::Colour (0x22ffffff));
        g.drawVerticalLine ((int) x, plotTop, plotBottom);

        const auto label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k"
                                        : juce::String ((int) f);
        g.setColour (juce::Colours::grey);
        g.drawText (label, (int) x + 2, (int) plotBottom + 3, 36, 12, juce::Justification::left);
    }

    // ---- RTA (spectrum of the input, before the notches) + detection threshold line
    constexpr int nb = MiniLiveStripProcessor::kFftSize / 2;
    const float sr = proc.displaySampleRate.load();
    const float binHz = sr / (float) MiniLiveStripProcessor::kFftSize;

    auto spec = [&] (int i)
    {
        return proc.spectrumDisplay[(size_t) juce::jlimit (0, nb, i)].load (std::memory_order_relaxed);
    };


    const int columns = juce::jmax (1, (int) (area.getWidth() / 2.0f));   // 2 px per column
    if ((int) smoothed.size() != columns)
        smoothed.assign ((size_t) columns, dbBottom);

    // Display value of one RTA column for a given spectrum source (input or output).
    auto columnDb = [&] (int c, auto&& specAt)
    {
        const float f0 = fMin * std::pow (fMax / fMin, (float) c / (float) columns);
        const float f1 = fMin * std::pow (fMax / fMin, (float) (c + 1) / (float) columns);
        const float b0 = f0 / binHz;
        const float b1 = f1 / binHz;

        float v = dbBottom;
        if (b0 < (float) nb)
        {
            if (b1 - b0 < 1.0f)
            {
                // Less than one FFT bin per column (low frequencies): interpolate.
                const float bc = 0.5f * (b0 + b1);
                const int i0 = (int) bc;
                const float frac = bc - (float) i0;
                v = specAt (i0) + (specAt (i0 + 1) - specAt (i0)) * frac;
            }
            else
            {
                // Several bins per column (high frequencies): show the strongest one.
                const int i0 = (int) std::ceil (b0);
                const int i1 = (int) std::floor (b1);
                v = -200.0f;
                for (int i = i0; i <= i1; ++i)
                    v = juce::jmax (v, specAt (i));
            }
        }
        return v;
    };

    // RTA fall speed (knob). Time-based, so it doesn't depend on how often we repaint.
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float dtMs = lastRtaMs > 0.0 ? (float) juce::jlimit (0.0, 250.0, nowMs - lastRtaMs) : 33.0f;
    lastRtaMs = nowMs;
    const float fallCoef = 1.0f - std::exp (-dtMs / juce::jmax (1.0f, (float) rtaTime.getValue()));

    juce::Path line;
    float firstX = 0.0f, lastX = 0.0f;

    for (int c = 0; c < columns; ++c)
    {
        const float f0 = fMin * std::pow (fMax / fMin, (float) c / (float) columns);
        const float f1 = fMin * std::pow (fMax / fMin, (float) (c + 1) / (float) columns);
        const float db = columnDb (c, spec);

        // Fast attack, slower release so the curve is readable.
        auto& s = smoothed[(size_t) c];
        s = db > s ? db : s + (db - s) * fallCoef;

        const float x = area.getX() + ((float) c + 0.5f) * area.getWidth() / (float) columns;
        const float y = yForDb (s);

        if (c == 0) { line.startNewSubPath (x, y); firstX = x; }
        else        { line.lineTo (x, y); }
        lastX = x;

    }

    juce::Path fill (line);
    fill.lineTo (lastX, plotBottom);
    fill.lineTo (firstX, plotBottom);
    fill.closeSubPath();

    g.setColour (juce::Colour (0x3340c4ff));
    g.fillPath (fill);
    g.setColour (juce::Colour (0xff40c4ff));
    g.strokePath (line, juce::PathStrokeType (1.2f));

    // ---- RTA of the OUTPUT (after notches + EQ + output gain), violet
    {
        auto specPost = [&] (int i)
        {
            return proc.spectrumPostDisplay[(size_t) juce::jlimit (0, nb, i)].load (std::memory_order_relaxed);
        };

        if ((int) smoothedPost.size() != columns)
            smoothedPost.assign ((size_t) columns, dbBottom);

        juce::Path postLine;
        float pFirstX = 0.0f, pLastX = 0.0f;

        for (int c = 0; c < columns; ++c)
        {
            const float db = columnDb (c, specPost);
            auto& s = smoothedPost[(size_t) c];
            s = db > s ? db : s + (db - s) * fallCoef;

            const float x = area.getX() + ((float) c + 0.5f) * area.getWidth() / (float) columns;
            const float y = yForDb (s);

            if (c == 0) { postLine.startNewSubPath (x, y); pFirstX = x; }
            else        { postLine.lineTo (x, y); }
            pLastX = x;
        }

        juce::Path postFill (postLine);
        postFill.lineTo (pLastX, plotBottom);
        postFill.lineTo (pFirstX, plotBottom);
        postFill.closeSubPath();

        g.setColour (kPostColour.withAlpha (0.10f));
        g.fillPath (postFill);
        g.setColour (kPostColour);
        g.strokePath (postLine, juce::PathStrokeType (1.2f));
    }

    // ---- Active notches: translucent red bar, thin marker = depth of the cut
    constexpr float maxDepthShown = 60.0f;
    std::vector<MiniLiveStripProcessor::Coeffs> notchCoeffs;
    const float qNow = proc.getQ();
    const double srD = (double) sr;
    const bool  fbOn = proc.getFeedbackOn();
    const float dimA = fbOn ? 1.0f : 0.3f;     // notches are drawn faint while Feedback is off

    g.setFont (10.0f);
    for (int i = 0; i < MiniLiveStripProcessor::kMaxNotches; ++i)
    {
        const float f = proc.notchFreqDisplay[(size_t) i].load();
        if (f <= 0.0f)
            continue;

        const float cutDb = proc.notchDepthDisplay[(size_t) i].load();
        if (fbOn)
            notchCoeffs.push_back (MiniLiveStripProcessor::makePeaking (srD, (double) f,
                                                                         -(double) cutDb, (double) qNow));

        const float x = xForFreq (f);
        const float h = (plotBottom - plotTop - 40.0f) * juce::jlimit (0.0f, 1.0f, cutDb / maxDepthShown);

        g.setColour (kNotchColour.withAlpha (0.55f * dimA));
        g.fillRect (x - 2.5f, plotTop, 5.0f, plotBottom - plotTop);
        g.setColour (kNotchColour.withAlpha (dimA));
        g.fillRect (x - 1.0f, plotTop, 2.0f, h);

        // Stagger the labels over 3 rows so neighbouring notches don't overlap.
        g.setColour (juce::Colours::white);
        g.drawText (juce::String ((int) f) + " Hz  -" + juce::String ((int) cutDb) + " dB",
                    (int) x - 45, (int) (plotTop + h + 2.0f) + (i % 3) * 11, 90, 11,
                    juce::Justification::centred);
    }

    // ---- Filter response overlays (own scale: +/- respRange dB around the middle line)
    g.setColour (juce::Colour (0x33ffffff));
    g.drawHorizontalLine ((int) midY, area.getX(), area.getRight());

    auto makeCurve = [&] (auto&& dbAt)
    {
        juce::Path p;
        for (int c = 0; c < columns; ++c)
        {
            const float f = fMin * std::pow (fMax / fMin, ((float) c + 0.5f) / (float) columns);
            const float x = area.getX() + ((float) c + 0.5f) * area.getWidth() / (float) columns;
            const float y = yForResp (dbAt ((double) f));
            if (c == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    };

    // Q preview: a single notch at 1 kHz with the current Q and Depth,
    // shown briefly while the Notch Q / Depth knobs are being moved.
    if (qPreviewFrames > 0)
    {
        const auto pc = MiniLiveStripProcessor::makePeaking (srD, 1000.0, -(double) proc.getMaxDepth(),
                                                              (double) qNow);
        const auto path = makeCurve ([&] (double f) { return MiniLiveStripProcessor::responseDb (pc, srD, f); });

        const float dashes[] = { 4.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, path, dashes, 2);
        g.setColour (kNotchColour.withAlpha (0.9f));
        g.fillPath (dashed);

        g.setFont (11.0f);
        g.drawText ("Q " + juce::String (qNow, 1) + "  |  -" + juce::String (proc.getMaxDepth(), 1) + " dB",
                    (int) xForFreq (1000.0f) + 8, (int) midY - 14, 120, 12, juce::Justification::left);
    }

    // Combined curve of all active notches (their real widths, from the current Q).
    if (! notchCoeffs.empty())
    {
        const auto path = makeCurve ([&] (double f)
        {
            double total = 0.0;
            for (const auto& c : notchCoeffs)
                total += MiniLiveStripProcessor::responseDb (c, srD, f);
            return total;
        });

        g.setColour (kNotchColour);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    // EQ curve (sum of all 6 bands).
    {
        std::vector<MiniLiveStripProcessor::Coeffs> eq;
        for (int b = 0; b < kBands; ++b)
        {
            const auto s = proc.getEqSections (b);
            for (int k = 0; k < s.count; ++k)
                eq.push_back (s.c[(size_t) k]);
        }

        const auto path = makeCurve ([&] (double f)
        {
            double total = 0.0;
            for (const auto& c : eq)
                total += MiniLiveStripProcessor::responseDb (c, srD, f);
            return total;
        });

        const bool eqLive = plainValue (pEqOn) > 0.5f;
        g.setColour (juce::Colours::white.withAlpha (eqLive ? 0.25f : 0.07f));   // soft glow under the line
        g.strokePath (path, juce::PathStrokeType (6.0f));
        g.setColour (juce::Colours::white.withAlpha (eqLive ? 1.0f : 0.35f));
        g.strokePath (path, juce::PathStrokeType (2.4f));
    }

    // ---- EQ points (drag = freq/gain, scroll = Q)
    {
        const juce::Colour ink (0xff0e1a2c);

        int soloingBand = -1;                              // band currently held in SOLO
        for (int b = 0; b < kBands; ++b)
            if (plainValue (bands[(size_t) b].pSolo) > 0.5f)
                soloingBand = b;
        g.setFont (fkFont (9.0f, true));

        for (int b = 0; b < visibleBands(); ++b)
        {
            const auto p = nodePos (b, G);
            const bool active = (b == hoverBand || b == dragBand || b == selectedBand);
            const auto col = kBandColours[b];

            if (active)
            {
                g.setColour (col.withAlpha (0.25f));
                g.fillEllipse (p.x - kNodeR - 4.0f, p.y - kNodeR - 4.0f, 2.0f * (kNodeR + 4.0f), 2.0f * (kNodeR + 4.0f));
            }

            g.setColour (col.withAlpha ((active ? 1.0f : 0.85f) * (plainValue (bands[(size_t) b].pOn) > 0.5f ? 1.0f : 0.35f)
                                          * (plainValue (pEqOn) > 0.5f ? 1.0f : 0.5f) * (soloingBand >= 0 && b != soloingBand ? 0.35f : 1.0f)));
            g.fillEllipse (p.x - kNodeR, p.y - kNodeR, 2.0f * kNodeR, 2.0f * kNodeR);
            g.setColour (active ? juce::Colours::white : ink);
            g.drawEllipse (p.x - kNodeR, p.y - kNodeR, 2.0f * kNodeR, 2.0f * kNodeR, 1.5f);

            g.setColour (ink);
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (2.0f * kNodeR, 2.0f * kNodeR).withCentre (p),
                        juce::Justification::centred);
        }

        // Value read-out for the point under the mouse / being dragged
        const int ab = dragBand >= 0 ? dragBand : hoverBand;
        if (ab >= 0)
        {
            const auto& ui = bands[(size_t) ab];
            const auto p = nodePos (ab, G);

            auto text = "B" + juce::String (ab + 1) + "  " + freqText ((double) plainValue (ui.pFreq));
            if (bandUsesGain (ab))
                text += "  " + gainText ((double) plainValue (ui.pGain));
            text += "  Q " + juce::String (plainValue (ui.pQ), 2);

            constexpr float lw = 150.0f, lh = 16.0f;
            float lx = p.x + kNodeR + 6.0f;
            if (lx + lw > area.getRight() - 4.0f)
                lx = p.x - kNodeR - 6.0f - lw;
            const float ly = juce::jlimit (area.getY() + 2.0f, area.getBottom() - 40.0f - lh, p.y - 0.5f * lh);

            g.setColour (juce::Colour (0xdd000000));
            g.fillRoundedRectangle (lx, ly, lw, lh, 3.0f);
            g.setColour (kBandColours[ab]);
            g.setFont (fkFont (10.5f, true));
            g.drawText (text, juce::Rectangle<float> (lx, ly, lw, lh), juce::Justification::centred);
        }
    }

    // Scale of the filter overlay (right side)
    g.setFont (10.0f);
    g.setColour (kEqColour.withAlpha (0.8f));
    const auto rr = juce::String ((int) respRange);
    g.drawText ("+" + rr + " dB", (int) area.getRight() - 50, (int) plotTop, 46, 11, juce::Justification::right);
    g.drawText ("0 dB",           (int) area.getRight() - 50, (int) midY - 11, 46, 11, juce::Justification::right);
    g.drawText ("-" + rr + " dB", (int) area.getRight() - 50, (int) plotBottom - 11, 46, 11, juce::Justification::right);

    // ---- Legend
    g.setFont (11.0f);
    const int ly = (int) area.getBottom() - 15;
    int lx = (int) area.getX() + 8;

    g.setColour (kNotchColour);
    g.drawText ("Notch / Q " + juce::String (qNow, 1), lx, ly, 110, 12, juce::Justification::left);
    lx += 115;

    g.setColour (juce::Colours::white);
    g.drawText ("EQ curve", lx, ly, 70, 12, juce::Justification::left);
    lx += 75;

    g.setColour (juce::Colour (0xff40c4ff));
    g.drawText ("RTA in", lx, ly, 50, 12, juce::Justification::left);
    lx += 55;

    g.setColour (kPostColour);
    g.drawText ("RTA out", lx, ly, 55, 12, juce::Justification::left);
    lx += 60;


    // Caption of the RTA time knob
    g.setColour (kLabelColour);
    g.setFont (fkFont (10.0f, true));
    g.drawText ("RTA TIME", rtaTime.getX() - 58, rtaTime.getY(), 56, rtaTime.getHeight(),
                juce::Justification::centredRight);
    g.drawText ("RTA DEPTH", rtaDepth.getX() - 62, rtaDepth.getY(), 60, rtaDepth.getHeight(),
                juce::Justification::centredRight);
}
