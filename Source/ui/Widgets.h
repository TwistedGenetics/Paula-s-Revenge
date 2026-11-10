// Paula's Revenge - black & white bevelled widgets drawn with the pixel font
#pragma once

#include <JuceHeader.h>
#include "PixelFont.h"
#include <functional>

namespace prv::ui
{
struct Theme
{
    // the window's own colours (the chrome): set by the skin (WORKBENCH by default)
    static inline juce::Colour paper  { 0xffffffff };   // main background
    static inline juce::Colour panel  { 0xffe6e6e6 };   // panel fill
    static inline juce::Colour light  { 0xffffffff };   // bevel highlight
    static inline juce::Colour shadow { 0xff7a7a7a };   // bevel shadow
    static inline juce::Colour ink    { 0xff000000 };   // text and lines
    static inline juce::Colour stripe { 0xffefefef };   // every-4th-line band
    static inline juce::Colour band   { 0xffc4c4c4 };   // cursor line
    static inline juce::Colour pressed{ 0xffb0b0b0 };   // pressed/on buttons
    static inline juce::Colour faint  { 0xff8c8c8c };   // dimmed text (still dark on light)

    // size of one font pixel for the UI, set by the editor on resize; and how many screen
    // pixels make one of ours (2 on a Retina screen) - text and lines snap to them
    static float px;
    static inline float deviceScale = 1.0f;
    static float snap (float v) { return std::round (v * deviceScale) / deviceScale; }

    // SKINS: WORKBENCH (light), DARK (the default), KICKSTART 1.3 (blue, white, orange), HIGH CONTRAST,
    // GLASS (deep blue, glossy), MODERN (light and flat), STUDIO (flat greys and orange, smooth type),
    // NEON (navy, rounded, glowing cyan), HARDWARE (matte black, LED buttons, amber pattern), PRISM (slate, colour, note bars),
    // BLUEWAVE (a wavetable synth's charcoal and blue), PAPER (light and clean), VIOLET (dark purple, glowing lilac),
    // EMBER (warm charcoal and orange), FOREST (deep green and lime), PRISM II (PRISM without the bars: coloured notes and effects),
    // AURORA (near black washed purple to navy, smoky rounded panes, pill buttons, pastel mint / yellow / coral)
    enum class SkinId { Workbench = 0, Dark, Kickstart, HighContrast, Glass, Modern, Studio, Neon, Hardware, Prism,
                        BlueWave, Paper, Violet, Ember, Forest, Prism2, Aurora, Boombox, DarkII, GlassII, DarkIII, Count };
    static constexpr int kSkins = (int) SkinId::Count;
    static juce::String skinName (SkinId);
    static bool skinIsDark (int id) { return id != (int) SkinId::Workbench && id != (int) SkinId::Modern && id != (int) SkinId::Paper; }
    // how raised and sunken things are drawn: Amiga bevels, glass (a gloss on top, a soft inner shadow), or flat (thin borders)
    enum class Style { Bevel, Glass, Flat };
    // Colours for the pattern area and spectrum strip (and the accents everywhere):
    // SELECTED, MODULATED (orange), RECORDING (red), one colour per track.
    struct Skin
    {
        SkinId id = SkinId::Workbench;
        Style style = Style::Bevel;
        bool dark = false;
        bool smoothType = false;   // real typefaces instead of the pixel font (STUDIO, NEON, HARDWARE, PRISM)
        int sans = 0;              // the labels' typeface (PixelFont::Sans)
        float round = 0.6f;        // corner radius, in font pixels
        bool gradient = false;     // faces lit from above (NEON)
        bool glow = false;         // on = glowing (NEON)
        bool leds = false;         // on = an LED lit along the top, the face stays (HARDWARE)
        bool noteBars = false;     // the pattern: notes as bars in their track's colour, held until the next (PRISM)
        bool colourNotes = false;  // the pattern: notes in their track's colour, effects a lighter shade of it (PRISM II)
        bool gloss = false;   // (GLASS II: a glassy shine over buttons and panes)
        bool wash = false;         // the window behind everything: a soft wash from washA (top left) to washB (AURORA)
        juce::Colour washA, washB;
        juce::Colour bg, text, dim, stripe, band, cursor, cursorEdge, rule, hot, specBar;
        juce::Colour accentSel, accentMod, accentRec;
        juce::Colour track[8];
    };
    static Skin skin;
    static void setSkin (bool dark);         // WORKBENCH or DARK
    static void setSkinId (SkinId);
    static inline bool crt = false;          // CRT: scanlines over the window

    static void bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t);
    // the window's background: the paper colour, or (a skin with a wash) the wash - drawn once per size and kept
    static void paintBackground (juce::Graphics&, juce::Rectangle<int> area);
    static void panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title);

    // The page colours. The chrome palette is the window's; a page marked dark (the instrument,
    // MIDI-out and mixer / effect pages) uses the page palette (dark in the dark skin; the
    // same as the chrome in the others). Widgets look theirs up from the page they're on.
    struct Palette { juce::Colour paper, panel, light, shadow, ink, pressed, faint, hover, edge, pressedInk; };
    static const Palette& lightPalette();    // the chrome
    static const Palette& darkPalette();     // a marked page's
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
    // lit with a colour when on: a stripe along its foot (Theme::skin.accentRec for recording,
    // accentMod for learning / modulating, accentSel for the page you're on); transparent = none
    juce::Colour onAccent { juce::Colours::transparentBlack };
    std::function<void()> onRightClick;   // set: a right-click opens this instead of clicking the button
    // PICTURES: a small pixel icon left of the label - only when it fits beside the label at full size
    // (never instead of it, never shrinking it); and anything drawn behind the label (an EQ's curve...)
    enum class Icon { None, Folder, Disk, Page, Help, Undo, Redo, Mixer, Spectrum, Curve, Radio, Arrange, Knob, Search };
    Icon icon = Icon::None;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, const Theme::Palette&)> decorate;
    bool iconShown = false;               // (the last paint had room for it: tests)
    static void drawIcon (juce::Graphics&, Icon, float x, float y, float px, juce::Colour);
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
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    float defaultValue = 0.5f;
    std::function<void()> onDoubleClick;   // replaces the reset-to-default when set
    std::function<void()> onDragStart;     // e.g. to mark an undo step
    std::function<void()> onRightClick;    // set: a right-click opens this instead of moving it
    // MODULATION: the range it's moved in and where it is now (-1 = not modulated)
    float modLo = -1, modHi = -1, modNow = -1;
    void setMod (float lo, float hi, float now)
    {
        if (std::abs (lo - modLo) > 0.002f || std::abs (hi - modHi) > 0.002f || std::abs (now - modNow) > 0.002f) { modLo = lo; modHi = hi; modNow = now; repaint(); }
    }

private:
    juce::String label;
    float dragStartValue = 0;
    float wheelAccum = 0;
    bool rightDown = false;
    juce::Rectangle<float> trough() const;
    void setFromX (float x);
};

// SMARTER LAYOUT: a folded sidebar panel - just its title bar (click it to open the panel)
class FoldBar : public juce::Component
{
public:
    explicit FoldBar (const juce::String& t) : title (t) { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
    juce::String title;
    std::function<void()> onClick, onRightClick;
    void paint (juce::Graphics& g) override
    {
        const auto& P = Theme::palFor (*this);
        Theme::panelBox (g, getLocalBounds().toFloat(), juce::String::charToString ((juce::juce_wchar) 4) + " " + title, P);
        if (isMouseOver()) { g.setColour (P.ink.withAlpha (0.08f)); g.fillRect (getLocalBounds()); }
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
        else if (onClick) onClick();
    }
};

// LIVE FEEDBACK: the value, big, beside whatever is being dragged (sliders, faders, mini bars).
// The editor owns the one bubble; controls call show() while dragging and hide() on release.
class DragReadout : public juce::Component
{
public:
    DragReadout() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); }
    static inline DragReadout* instance = nullptr;
    static void show (juce::Component& from, const juce::String& text);
    static void hide();
    static juce::String current() { return instance != nullptr && instance->isVisible() ? instance->text : juce::String(); }
    void paint (juce::Graphics&) override;
private:
    juce::String text;
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
    juce::Colour colour { juce::Colours::transparentBlack };   // (none: the page's text colour)
private:
    juce::String text;
    bool centre;
};
// CRT: scanlines along the font's pixel rows and darker corners, over everything; it never takes a
// click (shown when Theme::crt is on)
class CrtOverlay : public juce::Component
{
public:
    CrtOverlay() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (false); }
    void paint (juce::Graphics& g) override
    {
        const float px = Theme::px;
        const auto clip = g.getClipBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        const float line = std::max (1.0f / Theme::deviceScale, Theme::snap (px * 0.34f));
        for (float y = std::floor (clip.getY() / px) * px; y < clip.getBottom(); y += px)
            g.fillRect (clip.getX(), Theme::snap (y + px - line), clip.getWidth(), line);
        // the tube's corners
        const auto b = getLocalBounds().toFloat();
        juce::ColourGradient vg (juce::Colours::transparentBlack, b.getCentre(), juce::Colours::black.withAlpha (0.28f),
                                 b.getTopLeft(), true);
        vg.addColour (0.75, juce::Colours::transparentBlack);
        g.setGradientFill (vg);
        g.fillRect (clip);
    }
};
} // namespace prv::ui
