// Paula's Revenge - how a spectrum is drawn everywhere (the strip under the pattern, PARAMS' filter, the EQ
// pages): the analyser look - a smooth line through the levels, a fill under it in the spectrum's colour
// fading down to the background, and a faint peak-hold line above. The colour can change across (RAINBOW).
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"

namespace prv::ui
{
// level01 / peak01: 0..1 from the bottom of the area to the top, spread evenly across it (left = low)
inline void drawAnalyser (juce::Graphics& g, juce::Rectangle<float> area, const std::vector<float>& level01,
                          const std::vector<float>& peak01, const std::function<juce::Colour (float x01)>& colourAt)
{
    const size_t n = level01.size();
    if (n < 2 || area.getWidth() < 4 || area.getHeight() < 4) return;
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    auto pt = [&] (const std::vector<float>& v, size_t i)
    {
        return juce::Point<float> (area.getX() + area.getWidth() * (float) i / (float) (n - 1),
                                   area.getBottom() - juce::jlimit (0.0f, 1.0f, v[i]) * area.getHeight());
    };
    // a smooth line through the points (Catmull-Rom as Bezier curves): no corners, no steps
    auto smooth = [&] (const std::vector<float>& v)
    {
        juce::Path p;
        p.startNewSubPath (pt (v, 0));
        for (size_t i = 0; i + 1 < n; ++i)
        {
            const auto p0 = pt (v, i > 0 ? i - 1 : 0), p1 = pt (v, i), p2 = pt (v, i + 1), p3 = pt (v, std::min (n - 1, i + 2));
            p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
        }
        return p;
    };
    const auto edge = smooth (level01);
    auto fill = edge;
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();

    // the colour across: one stop per eighth (one colour, or the track colours as RAINBOW)
    juce::ColourGradient across (colourAt (0.0f), area.getX(), 0.0f, colourAt (1.0f), area.getRight(), 0.0f, false);
    for (int k = 1; k < 8; ++k) across.addColour (k / 8.0, colourAt ((float) k / 8.0f));
    auto withAlpha = [&] (juce::ColourGradient cg, float a)
    {
        for (int k = 0; k < cg.getNumColours(); ++k) cg.setColour (k, cg.getColour (k).withAlpha (a));
        return cg;
    };

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    // the fill: the colour, then the background rising over it towards the foot (a soft fade, as on a studio analyser)
    g.setGradientFill (withAlpha (across, sk.dark ? 0.62f : 0.42f));
    g.fillPath (fill);
    g.setGradientFill (juce::ColourGradient (sk.bg.withAlpha (0.0f), 0.0f, area.getY() + area.getHeight() * 0.15f,
                                             sk.bg.withAlpha (sk.dark ? 0.88f : 0.80f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    // the peak hold: faint, above
    if (peak01.size() == n)
    {
        g.setGradientFill (withAlpha (across, 0.38f));
        g.strokePath (smooth (peak01), juce::PathStrokeType (std::max (1.0f, px * 0.35f)));
    }
    // the line: bright, a touch lighter than the fill
    auto bright = across;
    for (int k = 0; k < bright.getNumColours(); ++k) bright.setColour (k, bright.getColour (k).brighter (sk.dark ? 0.35f : 0.0f));
    g.setGradientFill (bright);
    g.strokePath (edge, juce::PathStrokeType (std::max (1.5f, px * 0.6f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();
}
} // namespace prv::ui
