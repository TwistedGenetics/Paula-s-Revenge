#pragma once
/*
    Skin — the S950 front panel: warm light-grey casing, blue backlit LCD with
    near-white glyphs, square grey mode buttons with red LEDs, blue-grey keypad
    keys, amber FX pads. Text is big and high contrast throughout.
*/
#include <JuceHeader.h>

namespace skin
{
    namespace col
    {
        inline const juce::Colour panelTop  { 0xffe4e1da };
        inline const juce::Colour panelBot  { 0xffd3cfc6 };
        inline const juce::Colour ear       { 0xffdcd9d1 };
        inline const juce::Colour line      { 0xffa29e95 };
        inline const juce::Colour ink       { 0xff1a1a1a };
        inline const juce::Colour label     { 0xff2a2a2a };
        inline const juce::Colour red       { 0xffc0141c };
        inline const juce::Colour ledOn     { 0xffff2a2a };
        inline const juce::Colour ledOff    { 0xff3a2a2a };
        inline const juce::Colour bezel     { 0xff141414 };
        inline const juce::Colour lcd       { 0xff3d4cf2 };
        inline const juce::Colour lcdInk    { 0xfff4f6ff };
        inline const juce::Colour lcdDim    { 0xffc9d0ff };
        inline const juce::Colour lcdFaint  { 0xff7782fa };
        inline const juce::Colour lcdInv    { 0xff2633c8 };   // text on an inverted (lit) cell
        inline const juce::Colour lcdShade  { 0xff141a80 };
        inline const juce::Colour yellow    { 0xffffe14d };
        inline const juce::Colour keyTop    { 0xfff4f2ec };
        inline const juce::Colour keyBot    { 0xffd6d2c8 };
        inline const juce::Colour keyEdge   { 0xff8e8a80 };
        inline const juce::Colour padBlueT  { 0xff6f8ba6 };
        inline const juce::Colour padBlueB  { 0xff4e6984 };
        inline const juce::Colour padAmber  { 0xfff2b33d };
        inline const juce::Colour padAmberE { 0xffb57d17 };
        inline const juce::Colour padRed    { 0xffc0141c };
        inline const juce::Colour panelHead { 0xff9c988e };
    }

    /** The LCD's colours: S950 blue by default; GREEN / AMBER / WHITE are dark text on a light screen. */
    struct LcdTheme { juce::Colour bg, ink, dim, faint, inv, shade, mark; };
    inline std::atomic<int>& lcdThemeIndex() { static std::atomic<int> i { 0 }; return i; }
    inline const LcdTheme& lcdc()
    {
        static const LcdTheme t[4] = {
            { juce::Colour (0xff3d4cf2), juce::Colour (0xfff4f6ff), juce::Colour (0xffc9d0ff), juce::Colour (0xff7782fa), juce::Colour (0xff2633c8), juce::Colour (0xff141a80), juce::Colour (0xffffe14d) },
            { juce::Colour (0xffc7d6a2), juce::Colour (0xff142008), juce::Colour (0xff3f4d26), juce::Colour (0xff8fa06a), juce::Colour (0xffc7d6a2), juce::Colour (0xff9aab76), juce::Colour (0xffb3360a) },
            { juce::Colour (0xfff2a93b), juce::Colour (0xff241200), juce::Colour (0xff5e3a0c), juce::Colour (0xffb07a2a), juce::Colour (0xfff2a93b), juce::Colour (0xffc98a2c), juce::Colour (0xff8a0b0b) },
            { juce::Colour (0xffeef0ea), juce::Colour (0xff15171a), juce::Colour (0xff4a4e55), juce::Colour (0xffa3a7ad), juce::Colour (0xffeef0ea), juce::Colour (0xffc9ccd1), juce::Colour (0xffc0141c) } };
        return t[juce::jlimit (0, 3, lcdThemeIndex().load())];
    }

    inline juce::Font ui (float size, bool bold = true)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font lcd (float size)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold));
    }

    inline juce::Font logo (float size)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), size, juce::Font::bold | juce::Font::italic));
    }

    /** Raised square key (S950 mode button / keypad key). */
    inline void drawKey (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bot,
                         juce::Colour edge, bool down)
    {
        auto face = r.withTrimmedBottom (down ? 1.0f : 4.0f).translated (0, down ? 3.0f : 0.0f);
        if (! down)
        {
            g.setColour (edge.darker (0.4f));
            g.fillRoundedRectangle (r.withTrimmedTop (4.0f), 5.0f);
        }
        g.setGradientFill (juce::ColourGradient (top, face.getX(), face.getY(), bot, face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 5.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (face.reduced (1.0f), 5.0f, 2.0f);
    }

    /** The blue backlit LCD inside its black bezel. Returns the glass area. */
    inline juce::Rectangle<float> drawLcd (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (col::bezel);
        g.fillRoundedRectangle (r, 7.0f);
        auto glass = r.reduced (11.0f);
        g.setColour (lcdc().bg);
        g.fillRoundedRectangle (glass, 3.0f);
        // backlight falloff at the edges
        juce::ColourGradient vg (juce::Colours::transparentBlack, glass.getCentreX(), glass.getCentreY(),
                                 juce::Colour (0x40000028), glass.getX(), glass.getY(), true);
        g.setGradientFill (vg);
        g.fillRoundedRectangle (glass, 3.0f);
        return glass;
    }

    inline void drawScrew (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f2), c.x - r * 0.4f, c.y - r * 0.4f,
                                                 juce::Colour (0xff8f8f8f), c.x + r, c.y + r, true));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colour (0xff6a6a6a));
        g.drawLine (c.x - r * 0.6f, c.y, c.x + r * 0.6f, c.y, 2.0f);
    }
}
