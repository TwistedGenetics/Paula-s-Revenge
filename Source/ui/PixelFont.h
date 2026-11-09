// Paula's Revenge - original 5x7 pixel font, drawn as crisp rectangles at any size
#pragma once

#include <JuceHeader.h>

namespace prv::ui
{
class PixelFont
{
public:
    // Each glyph sits in a 6x8 cell (5x7 glyph + 1 px spacing). `px` is the
    // size of one font pixel in screen units.
    static constexpr int kCellW = 6;
    static constexpr int kCellH = 8;

    static float textWidth (const juce::String& text, float px) { return (float) forScreen (text).length() * kCellW * px; }

    // On a PC the Cmd key is Ctrl: the word CMD is shown as CTRL (on by default on Windows;
    // tests can switch it on elsewhere)
    static bool& ctrlKeyNames();
    static juce::String forScreen (const juce::String& text);
    static float textHeight (float px) { return kCellH * px; }
    // screen pixels per unit (2 on Retina): text starts on a whole screen pixel, so it stays crisp
    static inline float snapScale = 1.0f;

    // SMOOTH TYPE (the STUDIO skin): the same cells, drawn with real typefaces - numbers and
    // anything placed by position in a monospaced face, labels (centred / left in an area) in a sans.
    // Every layout stays as it is: a character never takes more room than its pixel cell.
    static inline bool smooth = false;
    enum Sans { Inter = 0, SpaceGrotesk, BarlowCondensed, Nunito };
    static inline int sans = Inter;   // the labels' typeface

    // Adds the text's pixels to a rectangle list (fast for big blocks of text)
    static void addText (juce::RectangleList<float>& list, const juce::String& text, float x, float y, float px);
    // a block of text in one colour, filled at once: the pixels, or (smooth type) the glyph outlines
    struct TextList
    {
        juce::RectangleList<float> rects;
        juce::GlyphArrangement glyphs;   // (smooth type)
        juce::Path path;                 // (smooth type: the symbols only the pixel font has)
        bool isEmpty() const { return rects.isEmpty() && glyphs.getNumGlyphs() == 0 && path.isEmpty(); }
        void clear() { rects.clear(); glyphs.clear(); path.clear(); }
        void fill (juce::Graphics& g) const
        {
            if (! rects.isEmpty()) g.fillRectList (rects);
            if (glyphs.getNumGlyphs() > 0) glyphs.draw (g);
            if (! path.isEmpty()) g.fillPath (path);
        }
    };
    static void addText (TextList& list, const juce::String& text, float x, float y, float px);

    static void draw (juce::Graphics& g, const juce::String& text, float x, float y, float px, juce::Colour c);
    static void drawCentred (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px, juce::Colour c);
    static void drawLeft (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px, juce::Colour c);

    // Largest whole-ish pixel size that fits `chars` characters into `width`
    static float fitPx (int chars, float width, float maxPx);
};
} // namespace prv::ui
