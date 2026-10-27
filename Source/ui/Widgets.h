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
};

class PixButton : public juce::Button
{
public:
    explicit PixButton (const juce::String& buttonText, bool toggle = false);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    float textScale = 1.0f;
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

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int preferredWidth (float px) const;

private:
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

    void setValue (float v, bool notify = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    float defaultValue = 0.5f;

private:
    juce::String label;
    juce::Rectangle<float> trough() const;
    void setFromX (float x);
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
