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
        case SkinId::Neon:         return "NEON";
        case SkinId::Hardware:     return "HARDWARE";
        case SkinId::Prism:        return "PRISM";
        case SkinId::BlueWave:     return "BLUEWAVE";
        case SkinId::Paper:        return "PAPER";
        case SkinId::Violet:       return "VIOLET";
        case SkinId::Ember:        return "EMBER";
        case SkinId::Forest:       return "FOREST";
        case SkinId::Prism2:       return "PRISM II";
        case SkinId::Aurora:       return "AURORA";
        default:                   return {};
    }
}

void Theme::setSkin (bool dark) { setSkinId (dark ? SkinId::Dark : SkinId::Workbench); }

void Theme::setSkinId (SkinId id)
{
    Skin k;
    k.id = id;
    k.dark = skinIsDark ((int) id);
    const bool smoothSkin = (int) id >= (int) SkinId::Studio;   // (STUDIO and every skin after it)
    k.style = id == SkinId::Glass ? Style::Glass : (id == SkinId::Modern || smoothSkin) ? Style::Flat : Style::Bevel;
    k.smoothType = smoothSkin;
    k.sans = (id == SkinId::Neon || id == SkinId::Ember) ? PixelFont::SpaceGrotesk
           : (id == SkinId::Hardware || id == SkinId::Forest) ? PixelFont::BarlowCondensed
           : (id == SkinId::Prism || id == SkinId::Prism2 || id == SkinId::Violet) ? PixelFont::Nunito : PixelFont::Inter;
    k.round = id == SkinId::Aurora ? 6.0f   // (pills: corners as round as the button is tall)
            : (id == SkinId::Neon || id == SkinId::Violet) ? 2.0f : (id == SkinId::Prism || id == SkinId::Prism2) ? 1.6f
            : (id == SkinId::BlueWave || id == SkinId::Paper) ? 1.2f : (id == SkinId::Hardware || id == SkinId::Forest) ? 0.8f : 0.6f;
    k.gradient = id == SkinId::Neon || id == SkinId::BlueWave || id == SkinId::Forest || id == SkinId::Aurora;
    k.glow = id == SkinId::Neon || id == SkinId::Violet;
    k.leds = id == SkinId::Hardware;
    k.noteBars = id == SkinId::Prism;
    k.colourNotes = id == SkinId::Prism2;
    k.wash = id == SkinId::Aurora;
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
    // NEON: deep navy, rounded, lit from above; chosen glows cyan
    const Palette neon      { C (0xff0a0f1e), C (0xff131b33), C (0xff1e2a4c), C (0xff04070f), C (0xffeaf6ff), C (0xff35d6ff), C (0xff93a7cc), C (0xff2a3a68), C (0xff2c3d6b), C (0xff03131d) };
    // HARDWARE: matte black and rubber grey; chosen lights an LED (the face stays)
    const Palette hardware  { C (0xff0d0d0d), C (0xff181818), C (0xff2a2a2a), C (0xff000000), C (0xfff2ede4), C (0xff5c2216), C (0xffa39d93), C (0xff343434), C (0xff050505), C (0xfff2ede4) };
    // PRISM: slate, soft and rounded; chosen is cyan with dark text
    const Palette prism     { C (0xff1e2025), C (0xff26282e), C (0xff353840), C (0xff121317), C (0xffeef0f4), C (0xff3fd6f0), C (0xffaab0bd), C (0xff41454f), C (0xff16171b), C (0xff14161a) };
    // BLUEWAVE: a wavetable synth's charcoal and slate, bright blue for chosen, orange for modulation
    const Palette bluewave  { C (0xff1b1f24), C (0xff262b33), C (0xff343b46), C (0xff0c0e11), C (0xffe6edf5), C (0xff3fa9f5), C (0xff8c97a6), C (0xff3e4654), C (0xff11141a), C (0xff07131e) };
    // PAPER: light and clean - white panes on soft grey, blue for chosen (white text on it)
    const Palette paperPal  { C (0xfff2f4f7), C (0xffffffff), C (0xffe6eaf0), C (0xffc3c8d1), C (0xff16191f), C (0xff1d4ed8), C (0xff5f6876), C (0xffd9e0ea), C (0xffcfd5de), C (0xffffffff) };
    // VIOLET: dark purple, rounded; chosen glows lilac
    const Palette violet    { C (0xff15121d), C (0xff201b2b), C (0xff2e2740), C (0xff08060c), C (0xfff1ecff), C (0xffa87bff), C (0xff9a90b5), C (0xff3a3152), C (0xff2a2338), C (0xff120c1f) };
    // EMBER: warm charcoal, orange for chosen
    const Palette ember     { C (0xff1c1a18), C (0xff282522), C (0xff37332f), C (0xff0b0a09), C (0xfff3ece4), C (0xffff8a2b), C (0xffa39a90), C (0xff443f3a), C (0xff12100f), C (0xff1a0f05) };
    // FOREST: deep green, lime for chosen
    const Palette forest    { C (0xff111813), C (0xff1a231d), C (0xff263229), C (0xff060907), C (0xffe6f5e9), C (0xff7ee06a), C (0xff8aa391), C (0xff304036), C (0xff0d130f), C (0xff0b1a08) };
    // AURORA: near black, smoky grey panes, pastel mint for chosen (dark text on it)
    const Palette aurora    { C (0xff0f0e14), C (0xff25242c), C (0xff34333d), C (0xff050508), C (0xfff4f2f8), C (0xff7dfc9b), C (0xffa19fb0), C (0xff42414d), C (0xff3a3944), C (0xff0d1a10) };
    switch (id)
    {
        case SkinId::Aurora:
            chromePal = aurora; pagePal = aurora;
            k.bg = C (0xff16151c); k.text = C (0xfff2f0f7); k.dim = C (0xff57556a); k.stripe = C (0xff1b1a22);
            k.band = C (0xff2e2c3a); k.cursor = C (0xff2f5a3a); k.cursorEdge = C (0xff7dfc9b); k.rule = C (0xff2c2b35);
            k.hot = C (0xffff8a80); k.specBar = C (0xff7dfc9b);
            k.accentSel = C (0xff7dfc9b); k.accentMod = C (0xfffff27a); k.accentRec = C (0xffff8a80);
            k.washA = C (0xff24102c); k.washB = C (0xff0c1428);
            break;
        case SkinId::BlueWave:
            chromePal = bluewave; pagePal = bluewave;
            k.bg = C (0xff14171b); k.text = C (0xffdbe6f2); k.dim = C (0xff4f5a68); k.stripe = C (0xff191d22);
            k.band = C (0xff2a3340); k.cursor = C (0xff16486b); k.cursorEdge = C (0xff3fa9f5); k.rule = C (0xff2c333d);
            k.hot = C (0xffff6b4a); k.specBar = C (0xff3fa9f5);
            k.accentSel = C (0xff3fa9f5); k.accentMod = C (0xfff5a623); k.accentRec = C (0xffff5468);
            break;
        case SkinId::Paper:
            chromePal = paperPal; pagePal = paperPal;
            k.bg = C (0xffffffff); k.text = C (0xff16191f); k.dim = C (0xff9aa3b0); k.stripe = C (0xfff3f5f8);
            k.band = C (0xffdbe5fb); k.cursor = C (0xffbcd0fa); k.cursorEdge = C (0xff1d4ed8); k.rule = C (0xffd5dae2);
            k.hot = C (0xffdc2626); k.specBar = C (0xff1d4ed8);
            k.accentSel = C (0xff1d4ed8); k.accentMod = C (0xffc2410c); k.accentRec = C (0xffdc2626);
            break;
        case SkinId::Violet:
            chromePal = violet; pagePal = violet;
            k.bg = C (0xff120f19); k.text = C (0xffece6ff); k.dim = C (0xff5b5175); k.stripe = C (0xff18141f);
            k.band = C (0xff2c2440); k.cursor = C (0xff46307a); k.cursorEdge = C (0xffa87bff); k.rule = C (0xff2c2540);
            k.hot = C (0xffff5fa2); k.specBar = C (0xffa87bff);
            k.accentSel = C (0xffa87bff); k.accentMod = C (0xff4fe0c0); k.accentRec = C (0xffff5f7e);
            break;
        case SkinId::Ember:
            chromePal = ember; pagePal = ember;
            k.bg = C (0xff171513); k.text = C (0xfff0e7dc); k.dim = C (0xff6d655c); k.stripe = C (0xff1d1b18);
            k.band = C (0xff3a332b); k.cursor = C (0xff6a3a14); k.cursorEdge = C (0xffff8a2b); k.rule = C (0xff36312c);
            k.hot = C (0xffff5a3c); k.specBar = C (0xffff8a2b);
            k.accentSel = C (0xffff8a2b); k.accentMod = C (0xffffd04a); k.accentRec = C (0xffff4d3d);
            break;
        case SkinId::Forest:
            chromePal = forest; pagePal = forest;
            k.bg = C (0xff0e140f); k.text = C (0xffdcf2df); k.dim = C (0xff4c6352); k.stripe = C (0xff131b15);
            k.band = C (0xff233328); k.cursor = C (0xff2e5a26); k.cursorEdge = C (0xff7ee06a); k.rule = C (0xff223026);
            k.hot = C (0xffff6b5a); k.specBar = C (0xff7ee06a);
            k.accentSel = C (0xff7ee06a); k.accentMod = C (0xffffcf4a); k.accentRec = C (0xffff5a5a);
            break;
        case SkinId::Neon:
            chromePal = neon; pagePal = neon;
            k.bg = C (0xff080d1a); k.text = C (0xffeaf6ff); k.dim = C (0xff56688f); k.stripe = C (0xff0e1527);
            k.band = C (0xff1b2748); k.cursor = C (0xff0f4660); k.cursorEdge = C (0xff35d6ff); k.rule = C (0xff26345e);
            k.hot = C (0xffff4fa3); k.specBar = C (0xff35d6ff);
            k.accentSel = C (0xff35d6ff); k.accentMod = C (0xffb48cff); k.accentRec = C (0xffff4f7a);
            break;
        case SkinId::Hardware:
            chromePal = hardware; pagePal = hardware;
            // the pattern is an amber display
            k.bg = C (0xff0a0908); k.text = C (0xffffb84d); k.dim = C (0xff7a6240); k.stripe = C (0xff131110);
            k.band = C (0xff2c2418); k.cursor = C (0xff5a2814); k.cursorEdge = C (0xffff5a36); k.rule = C (0xff2c2c2c);
            k.hot = C (0xffff5a36); k.specBar = C (0xffff5a36);
            k.accentSel = C (0xffff5a36); k.accentMod = C (0xffffb84d); k.accentRec = C (0xffff2d2d);
            break;
        case SkinId::Prism:
        case SkinId::Prism2:
            chromePal = prism; pagePal = prism;
            k.bg = C (0xff26282e); k.text = C (0xffeef0f4); k.dim = C (0xff6a7080); k.stripe = C (0xff2b2d34);
            k.band = C (0xff3a3d46); k.cursor = C (0xff1d5560); k.cursorEdge = C (0xff3fd6f0); k.rule = C (0xff16171b);
            k.hot = C (0xffff5470); k.specBar = C (0xff3fd6f0);
            k.accentSel = C (0xff3fd6f0); k.accentMod = C (0xffa98bff); k.accentRec = C (0xffff5470);
            break;
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
    if (id == SkinId::Workbench || id == SkinId::Modern || id == SkinId::Paper)
    {
        const juce::uint32 t[8] = { 0xff00869c, 0xff9c1f94, 0xffb85e00, 0xff2f8010, 0xff1f56c0, 0xffc0301f, 0xff7a6c00, 0xff0a8058 };
        for (int i = 0; i < 8; ++i) k.track[i] = C (t[i]);
    }
    else
    {
        const juce::uint32 t[8] = { 0xff3fd0e6, 0xffe864c8, 0xfff2b23a, 0xff8ad84a, 0xff6aa4ff, 0xffff8466, 0xffe6d84c, 0xff4ee0a6 };
        for (int i = 0; i < 8; ++i) k.track[i] = C (t[i]);
        if (id == SkinId::Kickstart) { k.track[4] = C (0xffc8e0ff); k.track[0] = C (0xff6af0ff); }   // (no blue on blue)
        if (id == SkinId::Aurora)
        {
            // pastels, as in the panes' highlights
            const juce::uint32 v[8] = { 0xff7dfc9b, 0xfffff27a, 0xffc9b3f5, 0xffffb38a, 0xff8fd3ff, 0xffff9fc8, 0xffb8f28a, 0xfff5d0a9 };
            for (int i = 0; i < 8; ++i) k.track[i] = C (v[i]);
        }
        if (id == SkinId::Prism || id == SkinId::Prism2)
        {
            const juce::uint32 v[8] = { 0xffff4fd8, 0xff3fd6f0, 0xffffd23f, 0xff7ee06a, 0xffff8a3d, 0xffa98bff, 0xffff5470, 0xff4ee0a6 };
            for (int i = 0; i < 8; ++i) k.track[i] = C (v[i]);
        }
    }
    skin = k;
    PixelFont::smooth = k.smoothType;
    PixelFont::sans = k.sans;
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
        // smooth skins: raised things are just their face; sunken ones get a dark hairline (rounded if the skin is)
        if (! raised)
        {
            g.setColour (skin.glow ? P.edge : P.shadow);
            if (skin.round > 1.0f) g.drawRoundedRectangle (r, std::min (skin.round * px, r.getHeight() * 0.5f), hair);
            else g.drawRect (r, hair);
        }
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

void Theme::paintBackground (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! skin.wash) { g.setColour (paper); g.fillRect (area); return; }
    // a soft wash: deep purple top left, navy to the right, near black below - made once for the size, then just drawn
    static juce::Image img;
    static juce::Rectangle<int> made;
    static juce::uint32 madeFor = 0;
    const float sc = std::max (1.0f, deviceScale);
    const auto key = skin.washA.getARGB() ^ (skin.washB.getARGB() << 1) ^ skin.bg.getARGB();
    if (img.isNull() || made != area || madeFor != key)
    {
        const int w = std::max (1, (int) std::ceil (area.getWidth() * sc)), h = std::max (1, (int) std::ceil (area.getHeight() * sc));
        img = juce::Image (juce::Image::RGB, w, h, false);
        juce::Graphics ig (img);
        ig.setColour (paper);
        ig.fillAll();
        juce::ColourGradient a (skin.washA, 0.0f, 0.0f, skin.washA.withAlpha (0.0f), (float) w * 0.7f, (float) h * 0.9f, true);
        ig.setGradientFill (a); ig.fillAll();
        juce::ColourGradient b (skin.washB, (float) w, 0.0f, skin.washB.withAlpha (0.0f), (float) w * 0.25f, (float) h * 0.8f, true);
        ig.setGradientFill (b); ig.fillAll();
        made = area; madeFor = key;
    }
    // (made at the screen's own size, so it's a straight copy - only the part being repainted)
    if (sc == 1.0f) g.drawImageAt (img, area.getX(), area.getY());
    else g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / sc).translated ((float) area.getX(), (float) area.getY()));
}

void Theme::panelBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const Palette& P)
{
    if (skin.smoothType && skin.round > 1.0f)
    {
        // rounded panes (NEON lit from above, with a fine rim)
        const float rad = std::min ({ skin.round * px * 1.5f, px * 4.5f, r.getHeight() * 0.5f });   // (panes: never so round the title meets the curve)
        // (a flat fill: a gradient over a big pane costs a lot to draw at 60 frames a second)
        g.setColour (P.panel);
        g.fillRoundedRectangle (r, rad);
        if (skin.gradient)
        {
            // NEON: a fine rim, lit along the top
            g.setColour (P.edge);
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f / deviceScale);
            g.setColour (P.ink.withAlpha (0.10f));
            g.fillRect (r.getX() + rad, r.getY() + 1.0f / deviceScale, r.getWidth() - rad * 2, 1.0f / deviceScale);
        }
    }
    else
    {
        g.setColour (P.panel);
        g.fillRect (r);
    }
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
    const auto& K = Theme::skin;
    const bool lit = getToggleState() && ! accent.isTransparent();
    const bool ledOn = studio && K.leds && on;
    if (studio)
    {
        const float rad = std::max (1.0f, Theme::px * K.round);
        if (K.leds)
        {
            // HARDWARE: rubber-grey keys; on lights an LED along the top (pressed darkens the key)
            g.setColour (down ? P.pressed : (over ? P.hover : P.light));
            g.fillRoundedRectangle (r, rad);
            g.setColour (P.shadow.withAlpha (0.6f));
            g.fillRect (r.getX() + rad, r.getBottom() - std::max (1.0f, Theme::px * 0.4f), r.getWidth() - rad * 2, std::max (1.0f, Theme::px * 0.4f));
            if (on)
            {
                const auto led = lit ? accent : K.accentSel;
                const float lh = std::max (2.0f, Theme::px * 0.7f);
                juce::Rectangle<float> strip (r.getX() + Theme::px * 1.5f, r.getY() + Theme::px * 0.8f, r.getWidth() - Theme::px * 3.0f, lh);
                g.setColour (led.withAlpha (0.28f));
                g.fillRoundedRectangle (strip.expanded (Theme::px * 0.6f), lh);
                g.setColour (led);
                g.fillRoundedRectangle (strip, lh * 0.5f);
            }
        }
        else
        {
            // STUDIO / PRISM flat, NEON lit from above and glowing: on = filled with what it means (dark text on it)
            const auto face = on ? (lit ? accent : P.pressed) : (over ? P.hover : P.light);
            auto f = r;
            if (K.glow)
            {
                f = r.reduced (std::max (1.0f, Theme::px * 0.6f));
                if (on)
                    for (int k = 3; k >= 1; --k)
                    {
                        g.setColour (face.withAlpha (0.10f * (float) (4 - k)));
                        g.fillRoundedRectangle (f.expanded ((float) k * std::max (0.5f, Theme::px * 0.25f)), rad + (float) k);
                    }
            }
            g.setColour (K.gradient ? face.darker (on ? 0.06f : 0.14f) : face);
            g.fillRoundedRectangle (f, rad);
            if (K.gradient)
            {
                // lit from above: a lighter top half (two flat fills - a real gradient on every button costs too much)
                g.setColour (face.brighter (on ? 0.22f : 0.16f).withAlpha (0.55f));
                g.fillRoundedRectangle (f.withHeight (f.getHeight() * 0.5f), rad);
            }
            if (K.gradient && ! on)
            {
                // a fine rim catching the light
                g.setColour (P.edge.brighter (0.4f).withAlpha (0.7f));
                g.drawRoundedRectangle (f.reduced (0.5f), rad, 1.0f / Theme::deviceScale);
            }
        }
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
            PixelFont::drawCentred (g, l, juce::Rectangle<float> (area.getX(), y, area.getWidth(), lh), px, isEnabled() ? (on && ! ledOn ? P.pressedInk : P.ink) : P.faint);
            y += lh;
        }
        return;
    }
    if (decorate) decorate (g, r, P);
    float usePx = px;
    // (the text's own width: the last character's spacing column doesn't count)
    while (usePx > 1.0f && PixelFont::textWidth (label, usePx) - usePx > r.getWidth() - 4) usePx -= 0.5f;
    const auto inkNow = isEnabled() ? (on && ! ledOn ? P.pressedInk : P.ink) : P.faint;
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
