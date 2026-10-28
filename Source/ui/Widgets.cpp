// Paula's Revenge - black & white bevelled widgets
#include "Widgets.h"

namespace prv::ui
{
float Theme::px = 3.0f;
Theme::Skin Theme::skin;

void Theme::setSkin (bool dark)
{
    Skin k;
    k.dark = dark;
    if (dark)
    {
        k.bg = juce::Colour (0xff141a24);       k.text = juce::Colour (0xfff4ecd6);
        k.dim = juce::Colour (0xff566074);      k.stripe = juce::Colour (0xff1c2432);
        k.band = juce::Colour (0xff334058);     k.cursor = juce::Colour (0xff5a6a88);
        k.cursorEdge = juce::Colour (0xfff4ecd6); k.rule = juce::Colour (0xff3a4458);
        k.hot = juce::Colour (0xffff5a4a);      k.specBar = juce::Colour (0xfff4ecd6);
        const juce::uint32 t[8] = { 0xff3fd0e6, 0xffe864c8, 0xfff2b23a, 0xff8ad84a, 0xff6aa4ff, 0xffff8466, 0xffe6d84c, 0xff4ee0a6 };
        for (int i = 0; i < 8; ++i) k.track[i] = juce::Colour (t[i]);
    }
    else
    {
        k.bg = juce::Colour (0xffffffff);       k.text = juce::Colour (0xff000000);
        k.dim = juce::Colour (0xff8c8c8c);      k.stripe = juce::Colour (0xffefefef);
        k.band = juce::Colour (0xffc4c4c4);     k.cursor = juce::Colour (0xffb0b0b0);
        k.cursorEdge = juce::Colour (0xff000000); k.rule = juce::Colour (0xff000000);
        k.hot = juce::Colour (0xffd01818);      k.specBar = juce::Colour (0xff000000);
        const juce::uint32 t[8] = { 0xff00869c, 0xff9c1f94, 0xffb85e00, 0xff2f8010, 0xff1f56c0, 0xffc0301f, 0xff7a6c00, 0xff0a8058 };
        for (int i = 0; i < 8; ++i) k.track[i] = juce::Colour (t[i]);
    }
    skin = k;
}

void Theme::bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t) { bevel (g, r, raised, t, lightPalette()); }
void Theme::panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title) { panelBox (g, r, title, lightPalette()); }

const Theme::Palette& Theme::lightPalette()
{
    static const Palette p { paper, panel, light, shadow, ink, pressed, faint, juce::Colour (0xfff4f4f4), ink };
    return p;
}

const Theme::Palette& Theme::darkPalette()
{
    // dark blue-black like the dark skin's pattern, cream text: high contrast, nothing white
    static const Palette p { juce::Colour (0xff0b0f17), juce::Colour (0xff161c28), juce::Colour (0xff3c465c), juce::Colour (0xff020304),
                             juce::Colour (0xfff4ecd6), juce::Colour (0xff33415e), juce::Colour (0xff7d8597), juce::Colour (0xff222a3a),
                             juce::Colour (0xff6a7590) };
    return p;
}

const Theme::Palette& Theme::palFor (const juce::Component& c)
{
    if (! skin.dark) return lightPalette();
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (p->getProperties().contains ("prvDarkPage")) return darkPalette();
    return lightPalette();
}

void Theme::bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t, const Palette& P)
{
    const auto tl = raised ? P.light : P.shadow;
    const auto br = raised ? P.shadow : P.light;
    g.setColour (tl);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), t);
    g.fillRect (r.getX(), r.getY(), t, r.getHeight());
    g.setColour (br);
    g.fillRect (r.getX(), r.getBottom() - t, r.getWidth(), t);
    g.fillRect (r.getRight() - t, r.getY(), t, r.getHeight());
    g.setColour (P.edge);
    g.drawRect (r.expanded (1.0f), 1.0f);
}

void Theme::panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const Palette& P)
{
    g.setColour (P.panel);
    g.fillRect (r);
    bevel (g, r, true, std::max (1.0f, px * 0.5f), P);
    if (title.isNotEmpty())
    {
        const float h = PixelFont::textHeight (px) + px * 2;
        auto bar = r.removeFromTop (h).reduced (px, px * 0.5f);
        // title strip: thin horizontal rules either side of the title (Amiga-era feel, drawn fresh)
        const float tw = PixelFont::textWidth (title, px) + px * 2;
        g.setColour (P.ink);
        for (float yy = bar.getY() + px; yy < bar.getBottom() - px * 0.5f; yy += px * 2)
        {
            g.fillRect (bar.getX(), yy, bar.getWidth() * 0.5f - tw * 0.5f - px, std::max (1.0f, px * 0.5f));
            g.fillRect (bar.getCentreX() + tw * 0.5f + px, yy, bar.getWidth() * 0.5f - tw * 0.5f - px, std::max (1.0f, px * 0.5f));
        }
        PixelFont::drawCentred (g, title, bar, px, P.ink);
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
    const auto& P = Theme::palFor (*this);
    g.setColour (on ? P.pressed : (over ? P.hover : P.panel));
    g.fillRect (r);
    Theme::bevel (g, r, ! on, std::max (1.0f, Theme::px * 0.5f), P);
    const float px = Theme::px * textScale;
    auto label = getButtonText();
    float usePx = px;
    while (usePx > 1.0f && PixelFont::textWidth (label, usePx) > r.getWidth() - 4) usePx -= 0.5f;
    PixelFont::drawCentred (g, label, r.translated (on ? 1.0f : 0.0f, on ? 1.0f : 0.0f), usePx, isEnabled() ? P.ink : P.faint);
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
    const auto& P = Theme::palFor (*this);
    PixelFont::drawLeft (g, label, r.withWidth (px * PixelFont::kCellW * labelChars), px, P.ink);
    g.setColour (P.paper);
    g.fillRect (valueArea);
    Theme::bevel (g, valueArea, false, std::max (1.0f, px * 0.5f), P);
    PixelFont::drawCentred (g, format (value), valueArea, px, P.ink);
}

void NumberBox::mouseDown (const juce::MouseEvent& e) { dragStart = value; juce::ignoreUnused (e); }

void NumberBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! valueArea.contains (e.mouseDownPosition)) return;
    // calm: one step per ~4 font pixels of movement; Shift = finer still and single steps
    const bool fine = e.mods.isShiftDown();
    const int pixelsPerStep = std::max (3, juce::roundToInt (Theme::px * (fine ? 8.0f : 4.0f)));
    const int steps = -e.getDistanceFromDragStartY() / pixelsPerStep;
    setValue (dragStart + steps * (! fine && hi - lo > 300 ? 4 : 1), true);
}

void NumberBox::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! allowTyping || ! valueArea.contains (e.position)) return;
    auto* w = new juce::AlertWindow (label.trim(), "Type a value (" + juce::String (lo) + " to " + juce::String (hi) + ")", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("v", juce::String (value));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<NumberBox> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (r != 1 || safe == nullptr) return;
        const auto t = w->getTextEditorContents ("v").trim();
        if (t.containsAnyOf ("0123456789")) safe->setValue (t.getIntValue(), true);
    }), true);
}

void NumberBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    WheelSteps steps { wheelAccum, 6.0f };
    const int n = steps (w);
    wheelAccum = steps.accum;
    if (n != 0) setValue (value - n, true);   // wheel up = higher
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
    r.removeFromRight (px * PixelFont::kCellW * valueChars);
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
    const auto& P = Theme::palFor (*this);
    PixelFont::drawLeft (g, label, r, px, P.ink);
    auto t = trough();
    g.setColour (P.paper);
    g.fillRect (t);
    Theme::bevel (g, t, false, std::max (1.0f, px * 0.5f), P);
    auto fill = t.reduced (px).withWidth ((t.getWidth() - px * 2) * value);
    g.setColour (P.ink);
    g.fillRect (fill);
    PixelFont::drawLeft (g, format (value), r.withLeft (t.getRight() + px * 2), px, P.ink);
}

void PixSlider::setFromX (float x)
{
    auto t = trough().reduced (Theme::px);
    setValue ((x - t.getX()) / std::max (1.0f, t.getWidth()), true);
}

void PixSlider::mouseDown (const juce::MouseEvent& e)
{
    if (onDragStart) onDragStart();
    // a click in the trough jumps there; dragging then moves relative to that, calmly
    if (trough().contains (e.position) && ! e.mods.isShiftDown()) setFromX (e.position.x);
    dragStartValue = value;
}

void PixSlider::mouseDrag (const juce::MouseEvent& e)
{
    auto t = trough().reduced (Theme::px);
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;   // Shift: fine
    setValue (dragStartValue + e.getDistanceFromDragStartX() / std::max (1.0f, t.getWidth()) * fine, true);
}

void PixSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick) onDoubleClick();
    else setValue (defaultValue, true);
}

void PixSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    WheelSteps steps { wheelAccum, 10.0f };
    const int n = steps (w);
    wheelAccum = steps.accum;
    if (n == 0) return;
    if (onDragStart) onDragStart();
    setValue (value - n * (e.mods.isShiftDown() ? 0.002f : 0.01f), true);
}

// ---------------------------------------------------------------------------
void PixLabel::paint (juce::Graphics& g)
{
    float px = Theme::px * textScale;
    while (px > 1.5f && PixelFont::textWidth (text, px) > (float) getWidth()) px -= 0.5f;
    const auto c = colour == Theme::ink ? Theme::palFor (*this).ink : colour;   // (the page's text colour)
    if (centre) PixelFont::drawCentred (g, text, getLocalBounds().toFloat(), px, c);
    else        PixelFont::drawLeft (g, text, getLocalBounds().toFloat(), px, c);
}
} // namespace prv::ui
