#include "ChopUI/Widgets.h"

namespace ui
{
    using namespace skin;

    void drawLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float size, juce::Justification j)
    {
        g.setColour (col::label);
        g.setFont (skin::ui (size));
        g.drawFittedText (t, r, j, 2, 0.8f);
    }

    void drawSection (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r)
    {
        g.setColour (col::panelHead);
        g.fillRect (r);
        g.setColour (juce::Colours::white);
        g.setFont (skin::ui (17.0f));
        g.drawText (t, r.reduced (10, 0), juce::Justification::centredLeft);
    }

    // ---- Knob -------------------------------------------------------------------------------
    Knob::Knob (juce::Colour pointer) : pointerColour (pointer)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        setMouseDragSensitivity (220);
        setVelocityBasedMode (false);
    }

    void Knob::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const float d = juce::jmin (r.getWidth(), r.getHeight()) - 6.0f;
        auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        // skirt shadow
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (c.translated (0, 3));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a8a8a), c.getX() + d * 0.35f, c.getY() + d * 0.3f,
                                                 juce::Colour (0xff3c3c3c), c.getRight(), c.getBottom(), true));
        g.fillEllipse (c);
        g.setColour (juce::Colour (0xff222222));
        g.drawEllipse (c.reduced (1.0f), 1.5f);

        const double norm = valueToProportionOfLength (getValue());
        const auto p = getRotaryParameters();
        const float angle = p.startAngleRadians + (float) norm * (p.endAngleRadians - p.startAngleRadians);
        juce::Path ptr;
        const float w = juce::jmax (4.0f, d * 0.1f);
        ptr.addRoundedRectangle (-w * 0.5f, -d * 0.46f, w, d * 0.36f, 2.0f);
        g.setColour (pointerColour);
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.getCentreX(), c.getCentreY()));
    }

    // ---- Fader ------------------------------------------------------------------------------
    Fader::Fader()
    {
        setSliderStyle (juce::Slider::LinearVertical);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    }

    void Fader::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const float capH = 34.0f;
        auto track = juce::Rectangle<float> (8.0f, r.getHeight() - capH).withCentre ({ r.getCentreX() - 10.0f, r.getCentreY() });
        g.setColour (col::ink);
        g.fillRoundedRectangle (track, 3.0f);
        // tick marks
        g.setColour (col::label);
        for (int i = 0; i <= 8; ++i)
        {
            const float y = track.getY() + track.getHeight() * (float) i / 8.0f;
            g.fillRect (r.getRight() - 22.0f, y - 1.0f, 18.0f, 2.0f);
        }
        const float norm = (float) valueToProportionOfLength (getValue());
        const float y = track.getBottom() - norm * track.getHeight();
        auto cap = juce::Rectangle<float> (44.0f, capH).withCentre ({ track.getCentreX(), y });
        drawKey (g, cap, col::keyTop, juce::Colour (0xffb9b5ac), juce::Colour (0xff6e6a62), false);
        g.setColour (col::ink);
        g.fillRect (cap.getX() + 6.0f, cap.getCentreY() - 2.0f, cap.getWidth() - 12.0f, 2.0f);
    }

    // ---- ModeButton -------------------------------------------------------------------------
    void ModeButton::paintButton (juce::Graphics& g, bool, bool isDown)
    {
        auto r = getLocalBounds();
        auto labelArea = r.removeFromTop (24);
        drawLabel (g, getButtonText(), labelArea, 19.0f);
        auto led = r.removeFromTop (14).withSizeKeepingCentre (26, 8).toFloat();
        g.setColour (lit ? col::ledOn : col::ledOff);
        g.fillRoundedRectangle (led, 2.0f);
        if (lit) { g.setColour (col::ledOn.withAlpha (0.35f)); g.fillRoundedRectangle (led.expanded (3.0f), 4.0f); }
        auto key = r.reduced (4, 2).toFloat();
        drawKey (g, key, col::keyTop, col::keyBot, col::keyEdge, isDown);
    }

    // ---- DiceButton -------------------------------------------------------------------------
    void DiceButton::paintButton (juce::Graphics& g, bool, bool isDown)
    {
        auto r = getLocalBounds().toFloat();
        drawKey (g, r, col::keyTop, col::keyBot, juce::Colour (0xff6e6a62), isDown);
        auto face = r.withTrimmedBottom (4.0f).translated (0, isDown ? 3.0f : 0.0f);
        const float s = juce::jmin (40.0f, face.getHeight() - 22.0f);
        auto die = juce::Rectangle<float> (s, s).withPosition (face.getX() + 18.0f, face.getCentreY() - s * 0.5f);
        g.setColour (col::ink);
        g.fillRoundedRectangle (die, 6.0f);
        g.setColour (col::keyTop);
        const float pr = s * 0.09f;
        for (auto pt : { juce::Point<float> (0.28f, 0.28f), { 0.72f, 0.28f }, { 0.5f, 0.5f }, { 0.28f, 0.72f }, { 0.72f, 0.72f } })
            g.fillEllipse (die.getX() + pt.x * s - pr, die.getY() + pt.y * s - pr, pr * 2, pr * 2);
        g.setColour (col::ink);
        g.setFont (skin::ui (32.0f));
        g.drawText (getButtonText(), face.withTrimmedLeft (s + 34.0f).withTrimmedRight (44.0f), juce::Justification::centredLeft);
        auto led = juce::Rectangle<float> (26.0f, 9.0f).withCentre ({ face.getRight() - 28.0f, face.getCentreY() });
        g.setColour (flash ? col::ledOn : col::ledOff);
        g.fillRoundedRectangle (led, 2.0f);
    }

    // ---- LcdCell -------------------------------------------------------------------------------
    void LcdCell::paint (juce::Graphics& g)
    {
        const bool on = isOn != nullptr && isOn();
        auto r = getLocalBounds().toFloat();
        if (on) { g.setColour (lcdc().ink); g.fillRect (r); }
        else if (boxed) { g.setColour (lcdc().ink); g.drawRect (r, 2.0f); }
        g.setColour (on ? lcdc().inv : lcdc().ink);
        g.setFont (lcd (fontSize));
        const auto t = text != nullptr ? text() : juce::String();
        g.drawFittedText (t, getLocalBounds().reduced (6, 0), just, 1, 0.7f);
        if (underline && ! on)
        {
            g.setColour (lcdc().dim);
            g.fillRect (r.removeFromBottom (2.0f).reduced (4.0f, 0.0f));
        }
    }

    void LcdCell::mouseDown (const juce::MouseEvent& e) { dragAccum = 0.0f; lastY = e.y; dragged = false; }

    void LcdCell::mouseDrag (const juce::MouseEvent& e)
    {
        if (onDragOut != nullptr)
        {
            if (! dragged && e.getDistanceFromDragStart() > 4) { dragged = true; onDragOut(); }
            return;
        }
        if (onDrag == nullptr) return;
        dragAccum += (float) (lastY - e.y) / 6.0f;
        lastY = e.y;
        const float whole = std::trunc (dragAccum);
        if (std::abs (whole) >= 1.0f)
        {
            dragged = true;
            onDrag (whole, e.mods);
            dragAccum -= whole;
            repaint();
        }
    }

    void LcdCell::mouseUp (const juce::MouseEvent& e)
    {
        if (! dragged && onClick != nullptr && getLocalBounds().contains (e.getPosition())) onClick (e);
        repaint();
    }

    void LcdCell::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (onDrag == nullptr) return;
        const float d = std::abs (w.deltaY) > 0.0f ? w.deltaY : w.deltaX;
        if (std::abs (d) > 0.0f) { onDrag (d > 0 ? 1.0f : -1.0f, e.mods); repaint(); }
    }

    // ---- Pad --------------------------------------------------------------------------------------
    void Pad::paint (juce::Graphics& g)
    {
        const bool lit = down || (isLit != nullptr && isLit());
        auto r = getLocalBounds().toFloat();
        const auto top = lit ? juce::Colour (0xffd9343b) : col::padBlueT;
        const auto bot = lit ? juce::Colour (0xffa50f16) : col::padBlueB;
        const auto edge = lit ? juce::Colour (0xff6e0a0f) : juce::Colour (0xff31475c);
        drawKey (g, r, top, bot, edge, lit);
        auto face = r.withTrimmedBottom (4.0f).translated (0, lit ? 3.0f : 0.0f).reduced (8.0f, 8.0f);
        g.setColour (juce::Colours::white);
        g.setFont (skin::ui (22.0f));
        g.drawFittedText (name, face.removeFromTop (face.getHeight() * 0.48f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
        auto mini = face.withSizeKeepingCentre (juce::jmin (face.getWidth(), 120.0f), juce::jmin (face.getHeight(), 32.0f));
        g.setColour (col::bezel);
        g.fillRect (mini);
        g.setColour (lcdc().bg);
        g.fillRect (mini.reduced (2.0f));
        g.setColour (lcdc().ink);
        g.setFont (lcd (21.0f));
        g.drawFittedText (setting != nullptr ? setting() : juce::String(), mini.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    void Pad::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu()) return;
        down = true;
        if (onPress) onPress (true);
        repaint();
    }

    void Pad::mouseUp (const juce::MouseEvent&)
    {
        if (! down) return;
        down = false;
        if (onPress) onPress (false);
        repaint();
    }

    void Pad::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (onSettingDrag != nullptr && std::abs (w.deltaY) > 0.0f) { onSettingDrag (w.deltaY > 0 ? 1.0f : -1.0f); repaint(); }
    }

    // ---- LevelMeter -------------------------------------------------------------------------------
    void LevelMeter::setLevel (float peak)
    {
        const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
        const float target = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 51.0f);
        level = target > level ? target : level * 0.85f + target * 0.15f;
        repaint();
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bezel);
        g.fillRoundedRectangle (r, 4.0f);
        auto in = r.reduced (6.0f);
        const int segs = 22;
        const float h = in.getHeight() / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            const float pos = (float) (i + 1) / (float) segs;
            auto seg = juce::Rectangle<float> (in.getX(), in.getBottom() - (float) (i + 1) * h + 1.5f, in.getWidth(), h - 3.0f);
            const auto c = i >= segs - 2 ? juce::Colour (0xffff3b30) : i >= segs - 6 ? juce::Colour (0xfff2b33d) : juce::Colour (0xff4cc36b);
            g.setColour (pos <= level ? c : c.withAlpha (0.13f));
            g.fillRect (seg);
        }
    }
}
