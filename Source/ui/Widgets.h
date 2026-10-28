// Paula's Revenge - black & white bevelled widgets drawn with the pixel font
#pragma once

#include <JuceHeader.h>
#include "PixelFont.h"
#include <functional>

namespace prv::ui
{
struct Theme
{
    static inline const juce::Colour paper  { 0xffffffff };   // main background
    static inline const juce::Colour panel  { 0xffe6e6e6 };   // panel fill
    static inline const juce::Colour light  { 0xffffffff };   // bevel highlight
    static inline const juce::Colour shadow { 0xff7a7a7a };   // bevel shadow
    static inline const juce::Colour ink    { 0xff000000 };   // text and lines
    static inline const juce::Colour stripe { 0xffefefef };   // every-4th-line band
    static inline const juce::Colour band   { 0xffc4c4c4 };   // cursor line
    static inline const juce::Colour pressed{ 0xffb0b0b0 };   // pressed/on buttons
    static inline const juce::Colour faint  { 0xff8c8c8c };   // dimmed text (still dark on light)

    // size of one font pixel for the UI, set by the editor on resize
    static float px;

    // Colours for the pattern area and spectrum strip. Two variants:
    // light (black text on white) and dark (cream text on deep blue-black).
    struct Skin
    {
        bool dark = false;
        juce::Colour bg, text, dim, stripe, band, cursor, cursorEdge, rule, hot, specBar;
        juce::Colour track[8];
    };
    static Skin skin;
    static void setSkin (bool dark);

    static void bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t);
    static void panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title);

    // The page colours. Most of the window keeps the light grey chrome in both skins; a page
    // marked dark (the instrument, MIDI-out and mixer / effect pages) goes dark with light
    // text in the dark skin. Widgets look theirs up from the page they're on.
    struct Palette { juce::Colour paper, panel, light, shadow, ink, pressed, faint, hover, edge; };
    static const Palette& lightPalette();
    static const Palette& darkPalette();
    static const Palette& palFor (const juce::Component& c);
    static void markDarkPage (juce::Component& c) { c.getProperties().set ("prvDarkPage", true); }
    static void bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t, const Palette&);
    static void panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const Palette&);
};

class PixButton : public juce::Button
{
public:
    explicit PixButton (const juce::String& buttonText, bool toggle = false);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    float textScale = 1.0f;
    std::function<void()> onRightClick;   // set: a right-click opens this instead of clicking the button
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onRightClick && e.mods.isPopupMenu()) { rightDown = true; onRightClick(); return; }
        rightDown = false;
        juce::Button::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (rightDown) { rightDown = false; return; } juce::Button::mouseUp (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! rightDown) juce::Button::mouseDrag (e); }
private:
    bool rightDown = false;
};

// Value box: "LABEL  [-] 123 [+]" with drag-to-change on the number
class NumberBox : public juce::Component
{
public:
    NumberBox (const juce::String& label, int minV, int maxV, int value);

    void setValue (int v, bool notify = false);
    int  getValue() const { return value; }
    void setRange (int minV, int maxV) { lo = minV; hi = maxV; setValue (value); }

    std::function<void (int)> onChange;
    std::function<juce::String (int)> format = [] (int v) { return juce::String (v); };
    int labelChars = 6;     // width reserved for the label
    int valueChars = 3;
    bool compact = false;   // the transport row: no spare room around the label and the number (text full size)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool allowTyping = true;   // double-click the number to type one in

    int preferredWidth (float px) const;

private:
    float labelWidth (float px) const;
    juce::String label;
    int lo, hi, value;
    int dragStart = 0;
    float wheelAccum = 0;
    PixButton minus { "-" }, plus { "+" };
    juce::Rectangle<float> valueArea;
};

// Horizontal slider: bevelled trough with a black fill bar
class PixSlider : public juce::Component
{
public:
    explicit PixSlider (const juce::String& label);
    float value = 0.5f;
    std::function<void (float)> onChange;
    std::function<juce::String (float)> format = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
    int labelChars = 5;
    float valueChars = 4.5f;

    void setValue (float v, bool notify = false);
    void setLabel (const juce::String& l) { if (l != label) { label = l; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    float defaultValue = 0.5f;
    std::function<void()> onDoubleClick;   // replaces the reset-to-default when set
    std::function<void()> onDragStart;     // e.g. to mark an undo step

private:
    juce::String label;
    float dragStartValue = 0;
    float wheelAccum = 0;
    juce::Rectangle<float> trough() const;
    void setFromX (float x);
};

// Turns mouse-wheel / trackpad scrolling into whole steps at a calm rate:
// one step per wheel notch, and a gentle trackpad swipe moves a few steps.
struct WheelSteps
{
    float accum = 0;
    float stepsPerUnit = 10.0f;   // trackpad: steps per unit of scroll
    int operator() (const juce::MouseWheelDetails& w)
    {
        const float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        if (! w.isSmooth)
        {
            // a mouse wheel: one step per notch, whatever the acceleration
            accum = 0;
            return d > 0 ? -1 : (d < 0 ? 1 : 0);
        }
        accum += -d * stepsPerUnit;
        const int steps = (int) accum;
        accum -= (float) steps;
        return steps;
    }
};

// Scrolls at a steady rate while a drag is held past an edge (timer driven).
class EdgeScroller : private juce::Timer
{
public:
    std::function<void (int dir)> onStep;   // move one step (-1 up/left, +1 down/right), then redo the drag
    // overshoot: how far (in pixels) the pointer is past the edge; negative = before the start
    void update (float overshoot, float unit)
    {
        dir = overshoot < 0 ? -1 : (overshoot > 0 ? 1 : 0);
        if (dir == 0) { stopTimer(); return; }
        // about 12 steps a second, up to ~30 further out
        const float far = juce::jlimit (0.0f, 1.0f, std::abs (overshoot) / std::max (1.0f, unit * 6.0f));
        const int ms = juce::roundToInt (80.0f - 47.0f * far);
        if (! isTimerRunning()) { startTimer (ms); }
        else if (std::abs (getTimerInterval() - ms) > 8) startTimer (ms);
    }
    void stop() { dir = 0; stopTimer(); }
private:
    int dir = 0;
    void timerCallback() override { if (dir != 0 && onStep) onStep (dir); }
};

// Static pixel-font text
class PixLabel : public juce::Component
{
public:
    PixLabel (const juce::String& t = {}, bool centred = false) : text (t), centre (centred) { setInterceptsMouseClicks (false, false); }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    const juce::String& getText() const { return text; }
    void paint (juce::Graphics& g) override;
    float textScale = 1.0f;
    juce::Colour colour = Theme::ink;
private:
    juce::String text;
    bool centre;
};
} // namespace prv::ui
