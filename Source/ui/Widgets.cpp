// Paula's Revenge - black & white bevelled widgets
#include "Widgets.h"

namespace prv::ui
{
float Theme::px = 3.0f;
Theme::Skin Theme::skin;

namespace
{
    Theme::Palette chromePal, pagePal;
    juce::Colour C (juce::uint32 v) { return juce::Colour (v); }
}

juce::String Theme::skinName (SkinId id)
{
    switch (id)
    {
        case SkinId::Workbench:    return "WORKBENCH";
        case SkinId::Dark:         return "DARK";
        case SkinId::Kickstart:    return "KICKSTART 1.3";
        case SkinId::HighContrast: return "HIGH CONTRAST";
        case SkinId::Glass:        return "GLASS";
        case SkinId::Modern:       return "MODERN";
        case SkinId::Studio:       return "STUDIO";
        default:                   return {};
    }
}

void Theme::setSkin (bool dark) { setSkinId (dark ? SkinId::Dark : SkinId::Workbench); }

void Theme::setSkinId (SkinId id)
{
    Skin k;
    k.id = id;
    k.dark = skinIsDark ((int) id);
    k.style = id == SkinId::Glass ? Style::Glass : (id == SkinId::Modern || id == SkinId::Studio) ? Style::Flat : Style::Bevel;
    k.smoothType = id == SkinId::Studio;
    //                  paper         panel         light         shadow        ink           pressed       faint         hover         edge          pressedInk
    const Palette workbench { C (0xffffffff), C (0xffe6e6e6), C (0xffffffff), C (0xff7a7a7a), C (0xff000000), C (0xffa9bcd9), C (0xff7c7c7c), C (0xffffffff), C (0xff000000), C (0xff000000) };
    // dark blue-black like the dark skin's pattern, cream text: high contrast, nothing white
    const Palette darkPage  { C (0xff0b0f17), C (0xff161c28), C (0xff3c465c), C (0xff020304), C (0xfff4ecd6), C (0xff33415e), C (0xff7d8597), C (0xff222a3a), C (0xff6a7590), C (0xfff4ecd6) };
    // Workbench 1.3's blue, white, black and orange (orange with black on it: chosen, as it was)
    const Palette kick      { C (0xff0055aa), C (0xff0055aa), C (0xffffffff), C (0xff000822), C (0xffffffff), C (0xffff8800), C (0xffa8c8ee), C (0xff1666bb), C (0xff000822), C (0xff000822) };
    // pure black and white; chosen = yellow with black on it
    const Palette hc        { C (0xff000000), C (0xff000000), C (0xffffffff), C (0xff8a8a8a), C (0xffffffff), C (0xffffe000), C (0xffc8c8c8), C (0xff262626), C (0xffffffff), C (0xff000000) };
    // GLASS: deep blue panes with a gloss; pressed glows cyan
    const Palette glass     { C (0xff0c1626), C (0xff1a2b45), C (0xff9cc4f0), C (0xff03070e), C (0xffeaf4ff), C (0xff1f7fc8), C (0xff8aa0bf), C (0xff26406a), C (0xff4f74a8), C (0xffffffff) };
    // MODERN: white and soft grey, thin borders, blue for chosen
    const Palette modern    { C (0xffffffff), C (0xfff2f4f8), C (0xffffffff), C (0xffc9ced8), C (0xff1b2130), C (0xff2f66f0), C (0xff7e8799), C (0xffe6ebf3), C (0xffb8bfcc), C (0xffffffff) };
    // STUDIO: flat greys like a modern DAW, orange for chosen (dark text on it), smooth type
    const Palette studio    { C (0xff1e1e1e), C (0xff2b2b2b), C (0xff3c3c3c), C (0xff0e0e0e), C (0xffededed), C (0xffffa53d), C (0xffa3a3a3), C (0xff4a4a4a), C (0xff111111), C (0xff141414) };
    switch (id)
    {
        case SkinId::Studio:
            chromePal = studio; pagePal = studio;
            k.bg = C (0xff1a1a1a); k.text = C (0xffededed); k.dim = C (0xff747474); k.stripe = C (0xff222222);
            k.band = C (0xff3a3a3a); k.cursor = C (0xff6b4a22); k.cursorEdge = C (0xffffa53d); k.rule = C (0xff3a3a3a);
            k.hot = C (0xffff5a4a); k.specBar = C (0xffffa53d);
            k.accentSel = C (0xffffa53d); k.accentMod = C (0xff5ec8ff); k.accentRec = C (0xffff4d4d);
            break;
        case SkinId::Glass:
            chromePal = glass; pagePal = glass;
            k.bg = C (0xff08111f); k.text = C (0xffdcecff); k.dim = C (0xff5c7496); k.stripe = C (0xff0e1a2d);
            k.band = C (0xff1d3558); k.cursor = C (0xff2a8fd8); k.cursorEdge = C (0xffeaf4ff); k.rule = C (0xff3a5a86);
            k.hot = C (0xffff5c74); k.specBar = C (0xff7fd4ff);
            k.accentSel = C (0xff4fd2ff); k.accentMod = C (0xffffb347); k.accentRec = C (0xffff5c74);
            break;
        case SkinId::Modern:
            chromePal = modern; pagePal = modern;
            k.bg = C (0xffffffff); k.text = C (0xff1b2130); k.dim = C (0xff9aa3b4); k.stripe = C (0xfff6f8fb);
            k.band = C (0xffdde6f7); k.cursor = C (0xffb9cdf7); k.cursorEdge = C (0xff1747c8); k.rule = C (0xffc9ced8);
            k.hot = C (0xffe11d48); k.specBar = C (0xff1747c8);
            k.accentSel = C (0xff1f5bff); k.accentMod = C (0xffe57a00); k.accentRec = C (0xffe11d48);
            break;
        case SkinId::Dark:
            chromePal = workbench; pagePal = darkPage;
            k.bg = C (0xff141a24); k.text = C (0xfff4ecd6); k.dim = C (0xff566074); k.stripe = C (0xff1c2432);
            k.band = C (0xff334058); k.cursor = C (0xff5a6a88); k.cursorEdge = C (0xfff4ecd6); k.rule = C (0xff3a4458);
            k.hot = C (0xffff5a4a); k.specBar = C (0xfff4ecd6);
            k.accentSel = C (0xff6aa4ff); k.accentMod = C (0xffffa030); k.accentRec = C (0xffff4a3a);
            break;
        case SkinId::Kickstart:
            chromePal = kick; pagePal = kick;
            k.bg = C (0xff0055aa); k.text = C (0xffffffff); k.dim = C (0xffa8c8ee); k.stripe = C (0xff004c99);
            k.band = C (0xff1f6bbf); k.cursor = C (0xffff8800); k.cursorEdge = C (0xffffffff); k.rule = C (0xffffffff);
            k.hot = C (0xffff8800); k.specBar = C (0xffffffff);
            k.accentSel = C (0xffffffff); k.accentMod = C (0xffff8800); k.accentRec = C (0xffff4a3a);
            break;
        case SkinId::HighContrast:
            chromePal = hc; pagePal = hc;
            k.bg = C (0xff000000); k.text = C (0xffffffff); k.dim = C (0xffb0b0b0); k.stripe = C (0xff141414);
            k.band = C (0xff3a3a3a); k.cursor = C (0xffffe000); k.cursorEdge = C (0xffffffff); k.rule = C (0xffffffff);
            k.hot = C (0xffff4040); k.specBar = C (0xffffffff);
            k.accentSel = C (0xffffe000); k.accentMod = C (0xffffa030); k.accentRec = C (0xffff4040);
            break;
        default:
            chromePal = workbench; pagePal = workbench;
            k.bg = C (0xffffffff); k.text = C (0xff000000); k.dim = C (0xff8c8c8c); k.stripe = C (0xffefefef);
            k.band = C (0xffc4c4c4); k.cursor = C (0xffb0b0b0); k.cursorEdge = C (0xff000000); k.rule = C (0xff000000);
            k.hot = C (0xffd01818); k.specBar = C (0xff000000);
            k.accentSel = C (0xff2f5fb0); k.accentMod = C (0xffd86a00); k.accentRec = C (0xffd01818);
            break;
    }
    if (id == SkinId::Workbench || id == SkinId::Modern)
    {
        const juce::uint32 t[8] = { 0xff00869c, 0xff9c1f94, 0xffb85e00, 0xff2f8010, 0xff1f56c0, 0xffc0301f, 0xff7a6c00, 0xff0a8058 };
        for (int i = 0; i < 8; ++i) k.track[i] = C (t[i]);
    }
    else
    {
        const juce::uint32 t[8] = { 0xff3fd0e6, 0xffe864c8, 0xfff2b23a, 0xff8ad84a, 0xff6aa4ff, 0xffff8466, 0xffe6d84c, 0xff4ee0a6 };
        for (int i = 0; i < 8; ++i) k.track[i] = C (t[i]);
        if (id == SkinId::Kickstart) { k.track[4] = C (0xffc8e0ff); k.track[0] = C (0xff6af0ff); }   // (no blue on blue)
    }
    skin = k;
    PixelFont::smooth = k.smoothType;
    // the chrome colours everything else draws with
    paper = chromePal.paper; panel = chromePal.panel; light = chromePal.light; shadow = chromePal.shadow; ink = chromePal.ink;
    pressed = chromePal.pressed; faint = chromePal.faint;
    stripe = id == SkinId::Workbench || id == SkinId::Dark ? C (0xffefefef) : k.stripe;
    band = id == SkinId::Workbench || id == SkinId::Dark ? C (0xffc4c4c4) : k.band;
}

void Theme::bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t) { bevel (g, r, raised, t, lightPalette()); }
void Theme::panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title) { panelBox (g, r, title, lightPalette()); }

const Theme::Palette& Theme::lightPalette() { if (chromePal.ink.isTransparent()) setSkinId (SkinId::Workbench); return chromePal; }
const Theme::Palette& Theme::darkPalette()  { if (pagePal.ink.isTransparent()) setSkinId (SkinId::Workbench); return pagePal; }

const Theme::Palette& Theme::palFor (const juce::Component& c)
{
    if (! skin.dark) return lightPalette();
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (p->getProperties().contains ("prvDarkPage")) return darkPalette();
    return lightPalette();
}

void Theme::bevel (juce::Graphics& g, juce::Rectangle<float> r, bool raised, float t, const Palette& P)
{
    // (whole screen pixels: crisp at any size)
    t = std::max (1.0f / deviceScale, snap (t));
    r = juce::Rectangle<float> (snap (r.getX()), snap (r.getY()), snap (r.getWidth()), snap (r.getHeight()));
    const float hair = 1.0f / deviceScale;
    if (skin.smoothType)
    {
        // STUDIO: raised things are just their face; sunken ones get a dark hairline
        if (! raised) { g.setColour (P.shadow); g.drawRect (r, hair); }
        return;
    }
    if (skin.style == Style::Flat)
    {
        // MODERN: no bevels - a thin border, a touch darker under raised things (a soft drop)
        g.setColour (P.edge);
        g.drawRect (r, hair);
        if (raised)
        {
            g.setColour (P.shadow.withAlpha (0.55f));
            g.fillRect (r.getX() + hair, r.getBottom(), r.getWidth() - hair, hair);
        }
        return;
    }
    if (skin.style == Style::Glass)
    {
        // GLASS: a gloss over the top (strong on small things like buttons, a soft sheen on big panes),
        // a bright rim along the top, and sunken things lit from inside with a shadow under their top edge
        const float h = r.getHeight();
        const bool small = h < px * 24;
        if (raised)
        {
            const float gh = small ? h * 0.5f : std::min (h * 0.5f, px * 40);
            juce::ColourGradient gloss (juce::Colours::white.withAlpha (small ? 0.40f : 0.16f), 0, r.getY(),
                                        juce::Colours::white.withAlpha (small ? 0.10f : 0.02f), 0, r.getY() + gh, false);
            g.setGradientFill (gloss);
            g.fillRect (r.withHeight (gh));
            juce::ColourGradient low (juce::Colours::transparentBlack, 0, r.getY() + h * 0.55f, juce::Colours::black.withAlpha (small ? 0.22f : 0.12f), 0, r.getBottom(), false);
            g.setGradientFill (low);
            g.fillRect (r.withTrimmedTop (h * 0.55f));
            g.setColour (P.light.withAlpha (0.85f));
            g.fillRect (r.getX() + t, r.getY(), r.getWidth() - t * 2, t);           // (the rim)
            g.setColour (P.light.withAlpha (0.25f));
            g.fillRect (r.getX(), r.getY(), t, r.getHeight());
        }
        else
        {
            const float sh = std::min (h * 0.5f, px * 4);
            juce::ColourGradient in (juce::Colours::black.withAlpha (0.45f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + sh, false);
            g.setGradientFill (in);
            g.fillRect (r.withHeight (sh));
            g.setColour (P.light.withAlpha (0.35f));
            g.fillRect (r.getX(), r.getBottom() - hair, r.getWidth(), hair);         // (light caught on the bottom lip)
        }
        g.setColour (P.edge.withAlpha (0.9f));
        g.drawRect (r.expanded (hair), hair);
        return;
    }
    const auto tl = raised ? P.light : P.shadow;
    const auto br = raised ? P.shadow : P.light;
    g.setColour (tl);
    g.fillRect (r.getX(), r.getY(), r.getWidth(), t);
    g.fillRect (r.getX(), r.getY(), t, r.getHeight());
    g.setColour (br);
    g.fillRect (r.getX(), r.getBottom() - t, r.getWidth(), t);
    g.fillRect (r.getRight() - t, r.getY(), t, r.getHeight());
    g.setColour (P.edge);
    g.drawRect (r.expanded (1.0f / deviceScale), 1.0f / deviceScale);
}

void Theme::panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const Palette& P)
{
    g.setColour (P.panel);
    g.fillRect (r);
    bevel (g, r, true, std::max (1.0f, px * 0.5f), P);
    if (title.isNotEmpty() && skin.style != Style::Bevel)
    {
        // GLASS / MODERN: the title on its own, left, over an accent line (no Amiga rules)
        const float h = PixelFont::textHeight (px) + px * 2;
        auto bar = r.removeFromTop (h).reduced (px * 2, px * 0.5f);
        PixelFont::drawLeft (g, title, bar, px, P.ink);
        g.setColour (skin.accentSel.withAlpha (skin.style == Style::Glass ? 0.8f : 1.0f));
        g.fillRect (bar.getX(), Theme::snap (bar.getBottom()), std::min (bar.getWidth(), PixelFont::textWidth (title, px) + px * 2), std::max (1.0f / deviceScale, Theme::snap (px * 0.5f)));
    }
    else if (title.isNotEmpty())
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

// ---- the pixel icons: 7 x 7, like a character
namespace
{
    const char* const kIcons[][7] = {
        { "", "", "", "", "", "", "" },
        { ".......", "###....", "#..####", "#.....#", "#.....#", "#.....#", "#######" },   // folder
        { "######.", "#.##..#", "#.##..#", "#.....#", "#.###.#", "#.###.#", "#######" },   // disk
        { "####...", "#..##..", "#...##.", "#....#.", "#....#.", "#....#.", "######." },   // page
        { ".#####.", "##...##", "....##.", "...##..", "...##..", ".......", "...##.." },   // ?
        { "..#....", ".##....", "######.", ".##...#", "..#...#", "......#", "..####." },   // undo
        { "....#..", "....##.", ".######", "#...##.", "#...#..", "#......", ".####.." },   // redo
        { ".#...#.", "###..#.", ".#...#.", ".#..###", ".#...#.", ".#...#.", ".#...#." },   // faders
        { "......#", "....#.#", "....#.#", "..#.#.#", "..#.#.#", "#.#.#.#", "#.#.#.#" },   // spectrum
        { "..#....", ".#.#...", ".#..#..", "#....#.", "#.....#", ".......", "#######" },   // envelope
        { "#.....#", ".#...#.", "..#.#..", "...#...", "...#...", "..###..", ".#####." },   // antenna
        { "###....", ".......", "..####.", ".......", "####...", ".......", "...####" },   // arrange
        { "..###..", ".#...#.", "#..#..#", "#..#..#", "#.....#", ".#...#.", "..###.." },   // knob
        { ".###...", "#...#..", "#...#..", "#...#..", ".###...", "....##.", ".....##" },   // search
    };
}

void PixButton::drawIcon (juce::Graphics& g, Icon icon, float x, float y, float px, juce::Colour c)
{
    const auto& rows = kIcons[(int) icon];
    juce::RectangleList<float> cells;
    for (int r = 0; r < 7; ++r)
        for (int k = 0; rows[r][k] != 0; ++k)
            if (rows[r][k] == '#')
            {
                const float x0 = Theme::snap (x + k * px), x1 = Theme::snap (x + (k + 1) * px);
                const float y0 = Theme::snap (y + r * px), y1 = Theme::snap (y + (r + 1) * px);
                cells.addWithoutMerging ({ x0, y0, x1 - x0, y1 - y0 });
            }
    g.setColour (c);
    g.fillRectList (cells);
}

namespace { float pressShift() { return std::max (1.0f, Theme::snap (Theme::px * 0.34f)); } }

void PixButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState() || down;
    const auto& P = Theme::palFor (*this);
    auto label = getButtonText();
    // colour that means something: recording is red, learning / modulating is the mod colour, the rest (pages, views) the selection
    auto accent = onAccent;
    if (accent.isTransparent())
    {
        const auto u = label.toUpperCase();
        accent = (u.contains ("REC") || u.contains ("EDIT") || u.contains ("ARM")) ? Theme::skin.accentRec
               : (u.contains ("LEARN") || u.contains ("MODUL") || u.startsWith ("MOD")) ? Theme::skin.accentMod
               : Theme::skin.accentSel;
    }
    const bool studio = Theme::skin.smoothType;
    if (studio)
    {
        // STUDIO: flat, slightly rounded faces; on = filled with what it means (dark text on it)
        const auto face = on ? (getToggleState() && ! accent.isTransparent() ? accent : P.pressed) : (over ? P.hover : P.light);
        g.setColour (face);
        g.fillRoundedRectangle (r, std::max (1.0f, Theme::px * 0.6f));
    }
    else
    {
        g.setColour (on ? P.pressed : (over ? P.hover : P.panel));
        g.fillRect (r);
        Theme::bevel (g, r, ! on, std::max (1.0f, Theme::px * 0.5f), P);
    }
    const float px = Theme::px * textScale;
    if (label.containsChar ('\n'))
    {
        // lines stacked (a tall, narrow button)
        juce::StringArray lines; lines.addLines (label);
        const float lh = px * (PixelFont::kCellH + 3);
        const float sh = on && ! studio ? pressShift() : 0.0f;
        auto area = r.translated (sh, sh);
        float y = area.getCentreY() - lh * (float) lines.size() * 0.5f;
        for (const auto& l : lines)
        {
            PixelFont::drawCentred (g, l, juce::Rectangle<float> (area.getX(), y, area.getWidth(), lh), px, isEnabled() ? (on ? P.pressedInk : P.ink) : P.faint);
            y += lh;
        }
        return;
    }
    if (decorate) decorate (g, r, P);
    float usePx = px;
    // (the text's own width: the last character's spacing column doesn't count)
    while (usePx > 1.0f && PixelFont::textWidth (label, usePx) - usePx > r.getWidth() - 4) usePx -= 0.5f;
    const auto inkNow = isEnabled() ? (on ? P.pressedInk : P.ink) : P.faint;
    const float shift = on && ! studio ? pressShift() : 0.0f;
    const float textW = PixelFont::textWidth (label, usePx) - usePx;
    const float iconW = usePx * 9;   // (7 dots and a gap of 2)
    iconShown = icon != Icon::None && usePx == px && textW + iconW <= r.getWidth() - px * 3;
    if (iconShown)
    {
        const float x0 = std::round (r.getCentreX() - (textW + iconW) * 0.5f) + shift;
        const float y0 = std::round (r.getCentreY() - 3.5f * usePx) + shift;
        drawIcon (g, icon, x0, y0, usePx, inkNow);
        PixelFont::drawCentred (g, label, juce::Rectangle<float> (x0 + iconW, r.getY() + shift, textW + usePx, r.getHeight()), usePx, inkNow);
    }
    else PixelFont::drawCentred (g, label, r.translated (shift, shift), usePx, inkNow);
    if (studio) return;   // (the face is the accent)
    if (onAccent.isTransparent() && Theme::skin.id == Theme::SkinId::Dark && &P == &Theme::lightPalette())
    {
        // DARK's chrome is the light Workbench chrome: its accents too
        const auto u = label.toUpperCase();
        accent = (u.contains ("REC") || u.contains ("EDIT") || u.contains ("ARM")) ? juce::Colour (0xffd01818)
               : (u.contains ("LEARN") || u.contains ("MODUL") || u.startsWith ("MOD")) ? juce::Colour (0xffd86a00)
               : juce::Colour (0xff2f5fb0);
    }
    if (std::abs (accent.getPerceivedBrightness() - P.pressed.getPerceivedBrightness()) < 0.2f)
        accent = P.pressedInk;                                   // (too close to the pressed face to see)
    if (getToggleState() && ! accent.isTransparent())
    {
        // the accent: a stripe along the foot
        const float h = std::max (2.0f / Theme::deviceScale, Theme::snap (Theme::px * 0.6f));
        g.setColour (accent);
        g.fillRect (r.getX() + Theme::px, r.getBottom() - h - Theme::px * 0.5f, r.getWidth() - Theme::px * 2, h);
    }
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

float NumberBox::labelWidth (float px) const
{
    return compact ? PixelFont::textWidth (label.trim(), px) + px : px * PixelFont::kCellW * labelChars;
}

int NumberBox::preferredWidth (float px) const
{
    if (compact)
        return (int) std::ceil (labelWidth (px) + px * PixelFont::kCellW * valueChars + px * 3 + 2 * (px * 5 + 7) + px);
    return (int) std::ceil (px * PixelFont::kCellW * (labelChars + valueChars + 1) + 2 * (px * 9) + px * 4);
}

void NumberBox::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (labelWidth (px));
    const float bw = std::min (r.getHeight(), compact ? px * 5 + 7 : px * 9);   // (compact: just room for the - / + at full size)
    minus.setBounds (r.removeFromLeft (bw).toNearestInt());
    plus.setBounds (r.removeFromRight (bw).toNearestInt());
    valueArea = r.reduced (px * 0.5f, 0);
}

void NumberBox::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    const auto& P = Theme::palFor (*this);
    PixelFont::drawLeft (g, compact ? label.trim() : label, r.withWidth (labelWidth (px)), px, P.ink);
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
    if (modLo >= 0.0f)
    {
        // modulated: an orange band for its range along the bottom, and a line where it is now
        const juce::Colour accent = Theme::skin.accentMod;
        auto in = t.reduced (px);
        const float x0 = in.getX() + in.getWidth() * modLo, x1 = in.getX() + in.getWidth() * modHi;
        g.setColour (accent);
        g.fillRect (juce::Rectangle<float> (x0, in.getBottom() - px * 2.5f, std::max (px, x1 - x0), px * 2.5f));
        const float xn = in.getX() + in.getWidth() * modNow;
        g.setColour (juce::Colours::black);
        g.fillRect (juce::Rectangle<float> (xn - px * 1.5f, in.getY(), px * 3.0f, in.getHeight()));
        g.setColour (accent);
        g.fillRect (juce::Rectangle<float> (xn - px * 0.75f, in.getY(), px * 1.5f, in.getHeight()));
    }
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
    DragReadout::show (*this, label + " " + format (value));
}

void PixSlider::mouseDrag (const juce::MouseEvent& e)
{
    auto t = trough().reduced (Theme::px);
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;   // Shift: fine
    setValue (dragStartValue + e.getDistanceFromDragStartX() / std::max (1.0f, t.getWidth()) * fine, true);
    DragReadout::show (*this, label + " " + format (value));
}

void PixSlider::mouseUp (const juce::MouseEvent&) { DragReadout::hide(); }

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
    const auto c = (colour.isTransparent() || colour == Theme::ink) ? Theme::palFor (*this).ink : colour;   // (the page's text colour)
    if (centre) PixelFont::drawCentred (g, text, getLocalBounds().toFloat(), px, c);
    else        PixelFont::drawLeft (g, text, getLocalBounds().toFloat(), px, c);
}
} // namespace prv::ui

namespace prv::ui
{
// ---------------------------------------------------------------- the drag readout
void DragReadout::show (juce::Component& from, const juce::String& t)
{
    auto* r = instance;
    auto* parent = r != nullptr ? r->getParentComponent() : nullptr;
    if (parent == nullptr || ! parent->isParentOf (&from)) return;
    r->text = t.trim().toUpperCase();
    const float px = Theme::px;
    const int w = (int) std::ceil (PixelFont::textWidth (r->text, px) + px * 4);
    const int h = (int) std::ceil (px * (PixelFont::kCellH + 5));
    const auto area = parent->getLocalArea (&from, from.getLocalBounds());
    int x = area.getCentreX() - w / 2;
    int y = area.getY() - h - (int) px;
    if (y < 0) y = area.getBottom() + (int) px;                    // (no room above: below it)
    x = juce::jlimit (0, std::max (0, parent->getWidth() - w), x);
    y = juce::jlimit (0, std::max (0, parent->getHeight() - h), y);
    r->setBounds (x, y, w, h);
    r->setVisible (true);
    r->toFront (false);
    r->repaint();
}

void DragReadout::hide()
{
    if (instance != nullptr) instance->setVisible (false);
}

void DragReadout::paint (juce::Graphics& g)
{
    const auto& P = Theme::lightPalette();
    auto r = getLocalBounds().toFloat();
    g.setColour (P.ink);
    g.fillRect (r);                                               // (a hard edge, like a Workbench requester)
    r = r.reduced (std::max (1.0f, Theme::snap (Theme::px * 0.5f)));
    g.setColour (P.paper);
    g.fillRect (r);
    PixelFont::drawCentred (g, text, r, Theme::px, P.ink);
}
} // namespace prv::ui
