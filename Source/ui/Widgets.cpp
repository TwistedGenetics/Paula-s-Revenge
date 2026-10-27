// Paula's Revenge - black & white bevelled widgets
#include "Widgets.h"

namespace prv::ui
{
float Theme::px = 3.0f;

void Theme::bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t)
{
    const auto tl = raised ? light : shadow;
    const auto br = raised ? shadow : light;
    g.setColour (tl);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), t);
    g.fillRect (r.getX(), r.getY(), t, r.getHeight());
    g.setColour (br);
    g.fillRect (r.getX(), r.getBottom() - t, r.getWidth(), t);
    g.fillRect (r.getRight() - t, r.getY(), t, r.getHeight());
    g.setColour (ink);
    g.drawRect (r.expanded (1.0f), 1.0f);
}

void Theme::panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (panel);
    g.fillRect (r);
    bevel (g, r, true, std::max (1.0f, px * 0.5f));
    if (title.isNotEmpty())
    {
        const float h = PixelFont::textHeight (px) + px * 2;
        auto bar = r.removeFromTop (h).reduced (px, px * 0.5f);
        // title strip: thin horizontal rules either side of the title (Amiga-era feel, drawn fresh)
        const float tw = PixelFont::textWidth (title, px) + px * 2;
        g.setColour (ink);
        for (float yy = bar.getY() + px; yy < bar.getBottom() - px * 0.5f; yy += px * 2)
        {
            g.fillRect (bar.getX(), yy, bar.getWidth() * 0.5f - tw * 0.5f - px, std::max (1.0f, px * 0.5f));
            g.fillRect (bar.getCentreX() + tw * 0.5f + px, yy, bar.getWidth() * 0.5f - tw * 0.5f - px, std::max (1.0f, px * 0.5f));
        }
        PixelFont::drawCentred (g, title, bar, px, ink);
    }
}

// ---------------------------------------------------------------------------
PixButton::PixButton (const juce::String& buttonText, bool toggle) : juce::Button (buttonText)
{
    setClickingTogglesState (toggle);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void PixButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState() || down;
    g.setColour (on ? Theme::pressed : (over ? juce::Colour (0xfff4f4f4) : Theme::panel));
    g.fillRect (r);
    Theme::bevel (g, r, ! on, std::max (1.0f, Theme::px * 0.5f));
    const float px = Theme::px * textScale;
    auto label = getButtonText();
    float usePx = px;
    while (usePx > 1.0f && PixelFont::textWidth (label, usePx) > r.getWidth() - 4) usePx -= 0.5f;
    PixelFont::drawCentred (g, label, r.translated (on ? 1.0f : 0.0f, on ? 1.0f : 0.0f), usePx, isEnabled() ? Theme::ink : Theme::faint);
}

// ---------------------------------------------------------------------------
NumberBox::NumberBox (const juce::String& l, int minV, int maxV, int v) : label (l), lo (minV), hi (maxV), value (v)
{
    addAndMakeVisible (minus);
    addAndMakeVisible (plus);
    minus.setRepeatSpeed (350, 60);
    plus.setRepeatSpeed (350, 60);
    minus.onClick = [this] { setValue (value - 1, true); };
    plus.onClick  = [this] { setValue (value + 1, true); };
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void NumberBox::setValue (int v, bool notify)
{
    v = juce::jlimit (lo, hi, v);
    if (v == value) { return; }
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

int NumberBox::preferredWidth (float px) const
{
    return (int) std::ceil (px * PixelFont::kCellW * (labelChars + valueChars + 1) + 2 * (px * 9) + px * 4);
}

void NumberBox::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (px * PixelFont::kCellW * labelChars);
    const float bw = std::min (r.getHeight(), px * 9);
    minus.setBounds (r.removeFromLeft (bw).toNearestInt());
    plus.setBounds (r.removeFromRight (bw).toNearestInt());
    valueArea = r.reduced (px * 0.5f, 0);
}

void NumberBox::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    PixelFont::drawLeft (g, label, r.withWidth (px * PixelFont::kCellW * labelChars), px, Theme::ink);
    g.setColour (Theme::paper);
    g.fillRect (valueArea);
    Theme::bevel (g, valueArea, false, std::max (1.0f, px * 0.5f));
    PixelFont::drawCentred (g, format (value), valueArea, px, Theme::ink);
}

void NumberBox::mouseDown (const juce::MouseEvent& e) { dragStart = value; juce::ignoreUnused (e); }

void NumberBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! valueArea.contains (e.mouseDownPosition)) return;
    const int steps = -e.getDistanceFromDragStartY() / std::max (2, juce::roundToInt (Theme::px * 2));
    setValue (dragStart + steps * (hi - lo > 300 ? 4 : 1), true);
}

void NumberBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAccum += w.deltaY * 8.0f;
    while (wheelAccum >= 1.0f)  { setValue (value + 1, true); wheelAccum -= 1.0f; }
    while (wheelAccum <= -1.0f) { setValue (value - 1, true); wheelAccum += 1.0f; }
}

// ---------------------------------------------------------------------------
PixSlider::PixSlider (const juce::String& l) : label (l)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::Rectangle<float> PixSlider::trough() const
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (px * PixelFont::kCellW * labelChars);
    r.removeFromRight (px * PixelFont::kCellW * 4.5f);
    return r.reduced (px, std::max (px, r.getHeight() * 0.2f));
}

void PixSlider::setValue (float v, bool notify)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (std::abs (v - value) < 1e-6f) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void PixSlider::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    PixelFont::drawLeft (g, label, r, px, Theme::ink);
    auto t = trough();
    g.setColour (Theme::paper);
    g.fillRect (t);
    Theme::bevel (g, t, false, std::max (1.0f, px * 0.5f));
    auto fill = t.reduced (px).withWidth ((t.getWidth() - px * 2) * value);
    g.setColour (Theme::ink);
    g.fillRect (fill);
    PixelFont::drawLeft (g, format (value), r.withLeft (t.getRight() + px * 2), px, Theme::ink);
}

void PixSlider::setFromX (float x)
{
    auto t = trough().reduced (Theme::px);
    setValue ((x - t.getX()) / std::max (1.0f, t.getWidth()), true);
}

void PixSlider::mouseDown (const juce::MouseEvent& e) { setFromX (e.position.x); }
void PixSlider::mouseDrag (const juce::MouseEvent& e) { setFromX (e.position.x); }
void PixSlider::mouseDoubleClick (const juce::MouseEvent&) { setValue (defaultValue, true); }

// ---------------------------------------------------------------------------
void PixLabel::paint (juce::Graphics& g)
{
    float px = Theme::px * textScale;
    while (px > 1.5f && PixelFont::textWidth (text, px) > (float) getWidth()) px -= 0.5f;
    if (centre) PixelFont::drawCentred (g, text, getLocalBounds().toFloat(), px, colour);
    else        PixelFont::drawLeft (g, text, getLocalBounds().toFloat(), px, colour);
}
} // namespace prv::ui
