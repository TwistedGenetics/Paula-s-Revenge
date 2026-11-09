// Paula's Revenge - original 5x7 pixel font
#include "PixelFont.h"
#include <PrvFonts.h>
#include <unordered_map>
#include <map>

namespace prv::ui
{
namespace
{
    struct GlyphDef { char c; const char* rows[7]; };

    // Hand-drawn for this project. '#' = pixel on.
    const GlyphDef kGlyphs[] = {
        { 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
        { 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
        { 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
        { 'D', { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
        { 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
        { 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
        { 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" } },
        { 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
        { 'I', { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." } },
        { 'J', { "..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.." } },
        { 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
        { 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
        { 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
        { 'N', { "#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#" } },
        { 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
        { 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
        { 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
        { 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
        { 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
        { 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
        { 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
        { 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
        { 'W', { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#." } },
        { 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
        { 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
        { 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
        { '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
        { '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
        { '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
        { '3', { "####.", "....#", "....#", ".###.", "....#", "....#", "####." } },
        { '4', { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
        { '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
        { '6', { ".###.", "#....", "#....", "####.", "#...#", "#...#", ".###." } },
        { '7', { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." } },
        { '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
        { '9', { ".###.", "#...#", "#...#", ".####", "....#", "....#", ".###." } },
        { '-', { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
        { '#', { ".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#." } },
        { '.', { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." } },
        { ':', { ".....", ".##..", ".##..", ".....", ".##..", ".##..", "....." } },
        { '/', { "....#", "....#", "...#.", "..#..", ".#...", "#....", "#...." } },
        { '\'',{ "..#..", "..#..", ".#...", ".....", ".....", ".....", "....." } },
        { '"', { ".#.#.", ".#.#.", ".....", ".....", ".....", ".....", "....." } },
        { '!', { "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.." } },
        { '?', { ".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.." } },
        { '(', { "...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#." } },
        { ')', { ".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..." } },
        { '+', { ".....", "..#..", "..#..", "#####", "..#..", "..#..", "....." } },
        { ',', { ".....", ".....", ".....", ".....", ".##..", "..#..", ".#..." } },
        { '_', { ".....", ".....", ".....", ".....", ".....", ".....", "#####" } },
        { '=', { ".....", ".....", "#####", ".....", "#####", ".....", "....." } },
        { '<', { "...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#." } },
        { '>', { ".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..." } },
        { '%', { "##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##" } },
        { '*', { ".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....." } },
        { '[', { ".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###." } },
        { ']', { ".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###." } },
        { '|', { "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
        { '&', { ".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#" } },
        { '^', { "..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....." } },
        { '\x01', { "#....", "##...", "###..", "####.", "###..", "##...", "#...." } }, // play
        { '\x02', { ".....", "#####", "#####", "#####", "#####", "#####", "....." } }, // stop
        { '\x03', { "..#..", ".###.", "#####", ".....", ".....", ".....", "....." } }, // up arrow
        { '\x04', { ".....", ".....", ".....", ".....", "#####", ".###.", "..#.." } }, // down arrow
        { '\x05', { ".....", ".###.", "#####", "#####", "#####", ".###.", "....." } }, // record dot
        { '\x06', { "..###", "..#.#", "..#..", "..#..", "###..", "###..", "....." } }, // note (plug-in instrument)
        { '\x07', { "#....", "#....", "#.##.", "##..#", "#...#", "#..#.", "###.." } }, // flat sign (chord names)
        { '\x08', { "....#", "...##", "..###", ".####", "..###", "...##", "....#" } }, // play backwards (left arrow)
    };

    struct Glyph { std::vector<juce::Rectangle<int>> runs; };

    struct Table
    {
        Glyph glyphs[128];
        Table()
        {
            for (auto& def : kGlyphs)
            {
                auto& gl = glyphs[(unsigned char) def.c];
                for (int y = 0; y < 7; ++y)
                {
                    int x = 0;
                    while (x < 5)
                    {
                        if (def.rows[y][x] == '#')
                        {
                            int x2 = x;
                            while (x2 < 5 && def.rows[y][x2] == '#') ++x2;
                            gl.runs.push_back ({ x, y, x2 - x, 1 });
                            x = x2;
                        }
                        else ++x;
                    }
                }
            }
            // unknown characters: hollow box
            Glyph box;
            box.runs = { { 0, 0, 5, 1 }, { 0, 6, 5, 1 }, { 0, 1, 1, 5 }, { 4, 1, 1, 5 } };
            for (int i = 33; i < 128; ++i)
                if (glyphs[i].runs.empty() && ! (i >= 'a' && i <= 'z')) glyphs[i] = box;
        }
    };

    const Table& table() { static Table t; return t; }

    // ---- SMOOTH TYPE: glyph outlines straight from the font (no shaping, so no coding ligatures
    // joining "---" or "=="), cached per character
    struct Face
    {
        juce::Typeface::Ptr tf;
        float cap = 0.7f;              // capital height, per unit of font height
        struct G { int index = 0; float adv = 0; };
        std::unordered_map<juce::juce_wchar, G> cache;
        static constexpr auto kind = juce::TypefaceMetricsKind::portable;

        Face (const char* data, int size)
        {
            tf = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            if (tf != nullptr)
                if (auto gH = tf->getNominalGlyphForCodepoint ('H'))
                    cap = std::max (0.2f, tf->getGlyphBounds (kind, (int) *gH).getHeight());
        }
        const G* glyph (juce::juce_wchar c)
        {
            if (tf == nullptr) return nullptr;
            if (auto it = cache.find (c); it != cache.end()) return it->second.adv > 0 ? &it->second : nullptr;
            G g;
            if (auto gi = tf->getNominalGlyphForCodepoint (c); gi && *gi != 0)
            {
                g.index = (int) *gi;
                juce::Array<int> glyphs; juce::Array<float> xs;
                tf->getGlyphPositions (kind, juce::String::charToString (c), glyphs, xs);
                g.adv = xs.size() > 1 ? xs[xs.size() - 1] : 0.0f;
            }
            auto& slot = cache[c] = g;
            return slot.adv > 0 ? &slot : nullptr;
        }
        // a font at this height and squeeze (kept: JUCE caches the glyphs it draws per font)
        std::map<std::pair<int, int>, juce::Font> fonts;
        const juce::Font& font (float height, float hScale)
        {
            const auto key = std::make_pair (juce::roundToInt (height * 64), juce::roundToInt (hScale * 256));
            if (auto it = fonts.find (key); it != fonts.end()) return it->second;
            if (fonts.size() > 64) fonts.clear();
            return fonts.emplace (key, juce::Font (juce::FontOptions (tf).withMetricsKind (kind).withHeight (key.first / 64.0f)
                                                                       .withHorizontalScale (key.second / 256.0f))).first->second;
        }
    };
    Face& monoFace() { static Face f (PrvFonts::JetBrainsMonoBold_ttf, PrvFonts::JetBrainsMonoBold_ttfSize); return f; }
    Face& sansFace()
    {
        static Face inter (PrvFonts::InterSemiBold_ttf, PrvFonts::InterSemiBold_ttfSize);
        static Face space (PrvFonts::SpaceGroteskBold_ttf, PrvFonts::SpaceGroteskBold_ttfSize);
        static Face barlow (PrvFonts::BarlowCondensedSemiBold_ttf, PrvFonts::BarlowCondensedSemiBold_ttfSize);
        static Face nunito (PrvFonts::NunitoExtraBold_ttf, PrvFonts::NunitoExtraBold_ttfSize);
        switch (PixelFont::sans)
        {
            case PixelFont::SpaceGrotesk:    return space;
            case PixelFont::BarlowCondensed: return barlow;
            case PixelFont::Nunito:          return nunito;
            default:                         return inter;
        }
    }

    float snapTo (float v) { return std::round (v * PixelFont::snapScale) / PixelFont::snapScale; }

    // one pixel-font glyph (the play / record / arrow symbols the typefaces don't have), into a path
    void addPixelGlyph (juce::Path& p, juce::juce_wchar c, float x, float y, float px)
    {
        if (c <= 0 || c >= 128) return;
        for (auto& r : table().glyphs[c].runs)
            p.addRectangle (x + (float) r.getX() * px, y + (float) r.getY() * px, (float) r.getWidth() * px, px);
    }
    bool pixelOnly (juce::juce_wchar c) { return c < 32 || c >= 128; }

    // the cap height a smooth character gets (the pixel font's capitals are 7 font pixels)
    float capFor (float px) { return 7.0f * px * 0.94f; }
}

bool& PixelFont::ctrlKeyNames()
{
   #if JUCE_WINDOWS
    static bool on = true;
   #else
    static bool on = false;
   #endif
    return on;
}

juce::String PixelFont::forScreen (const juce::String& text)
{
    if (! ctrlKeyNames() || ! text.containsIgnoreCase ("cmd")) return text;
    // the word CMD (any case) on its own - CMD+Z, CMD-CLICK, (CMD) - not inside other words
    juce::String out;
    const int n = text.length();
    auto letter = [] (juce::juce_wchar c) { return juce::CharacterFunctions::isLetter (c); };
    for (int i = 0; i < n;)
    {
        if (i + 3 <= n && text.substring (i, i + 3).equalsIgnoreCase ("cmd")
            && (i == 0 || ! letter (text[i - 1])) && (i + 3 == n || ! letter (text[i + 3])))
        {
            const bool lower = text[i + 1] == 'm';
            out << (lower ? "Ctrl" : "CTRL");
            i += 3;
        }
        else out << juce::String::charToString (text[i++]);
    }
    return out;
}

void PixelFont::addText (juce::RectangleList<float>& list, const juce::String& rawText, float x, float y, float px)
{
    const auto& t = table();
    const auto text = forScreen (rawText);
    x = std::round (x * snapScale) / snapScale;
    y = std::round (y * snapScale) / snapScale;
    for (auto ch : text)
    {
        juce::juce_wchar c = ch;
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c > 0 && c < 128)
            for (auto& r : t.glyphs[c].runs)
            {
                // each edge on a whole screen pixel: crisp even when a font pixel is 2.5 of them
                const float x0 = std::round ((x + (float) r.getX() * px) * snapScale) / snapScale;
                const float x1 = std::round ((x + (float) r.getRight() * px) * snapScale) / snapScale;
                const float y0 = std::round ((y + (float) r.getY() * px) * snapScale) / snapScale;
                const float y1 = std::round ((y + (float) (r.getY() + 1) * px) * snapScale) / snapScale;
                list.addWithoutMerging ({ x0, y0, x1 - x0, y1 - y0 });
            }
        x += kCellW * px;
    }
}

namespace
{
    // monospaced: each character centred on its pixel cell, never wider than it
    void addMono (juce::GlyphArrangement& ga, juce::Path& pix, const juce::String& text, float x, float y, float px)
    {
        auto& F = monoFace();
        const float cell = PixelFont::kCellW * px;
        float s = capFor (px) / F.cap;
        if (auto* zero = F.glyph ('0')) s = std::min (s, cell * 0.98f / zero->adv);
        x = snapTo (x); y = snapTo (y);
        const float base = snapTo (y + 7.0f * px);
        const auto* fnt = F.tf != nullptr ? &F.font (s, 1.0f) : nullptr;
        int i = 0;
        for (auto ch : text)
        {
            const float cx = x + (float) i * cell;
            if (ch != ' ')
            {
                const auto* G = (pixelOnly (ch) || fnt == nullptr) ? nullptr : F.glyph (ch);
                if (G != nullptr)
                    ga.addGlyph (juce::PositionedGlyph (*fnt, ch, G->index, snapTo (cx + 2.5f * px - G->adv * s * 0.5f), base, G->adv * s, false));
                else addPixelGlyph (pix, ch, cx, y, px);
            }
            ++i;
        }
    }
    void drawMono (juce::Graphics& g, const juce::String& text, float x, float y, float px, juce::Colour c)
    {
        juce::GlyphArrangement ga; juce::Path pix;
        addMono (ga, pix, text, x, y, px);
        g.setColour (c);
        ga.draw (g);
        if (! pix.isEmpty()) g.fillPath (pix);
    }

    // proportional (labels): the natural widths, squeezed only if it would be wider than the pixel text
    void drawSans (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px, juce::Colour c, bool centred)
    {
        auto& F = sansFace();
        const float s = capFor (px) / F.cap;
        const float cell = PixelFont::kCellW * px;
        float w = 0;
        for (auto ch : text)
        {
            if (pixelOnly (ch)) { w += cell; continue; }
            if (auto* G = F.glyph (ch)) w += G->adv * s;
            else w += ch == ' ' ? cell * 0.55f : cell;
        }
        const float room = PixelFont::textWidth (text, px) - px;
        const float sx = (w > room && w > 0) ? room / w : 1.0f;
        w *= sx;
        const float h = 7.0f * px;
        float x = snapTo (centred ? area.getCentreX() - w * 0.5f : area.getX());
        const float y = snapTo (area.getCentreY() - h * 0.5f);
        const float base = snapTo (y + h);
        juce::GlyphArrangement ga; juce::Path p;
        const auto* fnt = F.tf != nullptr ? &F.font (s, sx) : nullptr;
        for (auto ch : text)
        {
            if (pixelOnly (ch)) { addPixelGlyph (p, ch, x + (cell * sx - 5 * px) * 0.5f, y, px); x += cell * sx; continue; }
            if (auto* G = fnt != nullptr ? F.glyph (ch) : nullptr)
            {
                if (ch != ' ') ga.addGlyph (juce::PositionedGlyph (*fnt, ch, G->index, snapTo (x), base, G->adv * s * sx, false));
                x += G->adv * s * sx;
            }
            else
            {
                if (ch != ' ') addPixelGlyph (p, ch, x, y, px);
                x += (ch == ' ' ? cell * 0.55f : cell) * sx;
            }
        }
        g.setColour (c);
        ga.draw (g);
        if (! p.isEmpty()) g.fillPath (p);
    }
}

void PixelFont::addText (TextList& list, const juce::String& text, float x, float y, float px)
{
    if (smooth) addMono (list.glyphs, list.path, forScreen (text), x, y, px);
    else addText (list.rects, text, x, y, px);
}

void PixelFont::draw (juce::Graphics& g, const juce::String& text, float x, float y, float px, juce::Colour c)
{
    if (smooth) { drawMono (g, forScreen (text), x, y, px, c); return; }
    juce::RectangleList<float> list;
    addText (list, text, x, y, px);
    g.setColour (c);
    g.fillRectList (list);
}

void PixelFont::drawCentred (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px, juce::Colour c)
{
    if (smooth) { drawSans (g, forScreen (text), area, px, c, true); return; }
    const float w = textWidth (text, px) - px; // drop trailing spacing column
    const float h = 7 * px;
    draw (g, text, std::round (area.getCentreX() - w * 0.5f), std::round (area.getCentreY() - h * 0.5f), px, c);
}

void PixelFont::drawLeft (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px, juce::Colour c)
{
    if (smooth) { drawSans (g, forScreen (text), area, px, c, false); return; }
    const float h = 7 * px;
    draw (g, text, std::round (area.getX()), std::round (area.getCentreY() - h * 0.5f), px, c);
}

float PixelFont::fitPx (int chars, float width, float maxPx)
{
    float px = width / (float) (std::max (1, chars) * kCellW);
    px = std::floor (px * 2.0f) / 2.0f; // half-pixel steps stay crisp on Retina screens
    return juce::jlimit (1.0f, maxPx, px);
}
} // namespace prv::ui
