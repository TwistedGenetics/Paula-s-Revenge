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

    // Adds the text's pixels to a rectangle list (fast for big blocks of text)
    static void addText (juce::RectangleList<float>& list, const juce::String& text, float x, float y, float px);

    static void draw (juce::Graphics& g, const juce::String& text, float x, float y, float px, juce::Colour c);
    static void drawCentred (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px, juce::Colour c);
    static void drawLeft (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px, juce::Colour c);

    // Largest whole-ish pixel size that fits `chars` characters into `width`
    static float fitPx (int chars, float width, float maxPx);
};
} // namespace prv::ui
