// Paula's Revenge - HELP: the README, built into the app
#include "HelpPage.h"
#include "ChoiceBox.h"
#include <BinaryData.h>

namespace prv::ui
{
juce::String helpText (const juce::String& in)
{
    juce::String s = in;
    // links: keep the words
    while (s.contains ("]("))
    {
        const int a = s.indexOf ("](");
        const int open = s.substring (0, a).lastIndexOfChar ('[');
        const int close = s.indexOfChar (a, ')');
        if (open < 0 || close < 0) break;
        s = s.substring (0, open) + s.substring (open + 1, a) + s.substring (close + 1);
    }
    s = s.replace ("**", "").replace ("`", "").replace ("<br>", " ");
    struct Swap { const char* from; const char* to; };
    static const Swap swaps[] = {
        { "\xe2\x80\x94", "-" }, { "\xe2\x80\x93", "-" }, { "\xe2\x88\x92", "-" },          // dashes, minus
        { "\xe2\x86\x92", "->" }, { "\xe2\x86\x90", "<-" },                                   // arrows
        { "\xe2\x86\x91", "\x03" }, { "\xe2\x86\x93", "\x04" },
        { "\xe2\x96\xb2", "\x03" }, { "\xe2\x96\xbc", "\x04" }, { "\xe2\x96\xb6", "\x01" },    // triangles
        { "\xe2\x97\x8f", "\x05" }, { "\xc3\x97", "X" }, { "\xc2\xbd", "1/2" }, { "\xc2\xb1", "+/-" },
        { "\xe2\x80\xa6", "..." }, { "\xe2\x80\x98", "'" }, { "\xe2\x80\x99", "'" }, { "\xe2\x80\x9c", "\"" }, { "\xe2\x80\x9d", "\"" },
        { "\xe2\x99\xad", "\x07" },                                                        // flat sign
        { "\xe2\x82\x82", "2" }, { "\xe2\x89\x88", "ABOUT " }, { "\xe2\x8c\xab", "" }, { "\xc2\xb7", "-" },
        { ";", "," }, { "~", "-" }, { "\\", "/" }, { "{", "(" }, { "}", ")" }, { "@", "AT" }, { "$", "S" },
    };
    for (auto& w : swaps) s = s.replace (juce::String::fromUTF8 (w.from), juce::String::fromUTF8 (w.to));
    // anything else the font doesn't have goes
    juce::String out;
    for (auto c : s)
        if ((c >= 32 && c < 127) || (c >= 1 && c <= 7)) out << juce::String::charToString (c);
    return out.toUpperCase();
}

std::vector<HelpBlock> parseHelp (const juce::String& md)
{
    std::vector<HelpBlock> out;
    auto lines = juce::StringArray::fromLines (md);
    bool skipping = false, inCode = false;
    juce::String para;
    auto flush = [&] { if (para.trim().isNotEmpty()) out.push_back ({ HelpBlock::Kind::Text, helpText (para.trim()) }); para.clear(); };
    for (auto raw : lines)
    {
        const auto line = raw.trimEnd();
        if (line.startsWith ("```")) { inCode = ! inCode; continue; }
        if (line.startsWith ("## "))
        {
            flush();
            const auto name = line.substring (3).trim();
            skipping = name.startsWithIgnoreCase ("Install") || name.startsWithIgnoreCase ("Building");
            if (! skipping) out.push_back ({ HelpBlock::Kind::Chapter, helpText (name) });
            continue;
        }
        if (skipping || inCode) continue;
        if (line.startsWith ("# ") || line.startsWith ("![") || line == "---" || line.startsWith ("**Manual:**")) continue;
        if (line.startsWith ("### ")) { flush(); out.push_back ({ HelpBlock::Kind::Heading, helpText (line.substring (4)) }); continue; }
        if (line.trim().isEmpty()) { flush(); if (! out.empty() && out.back().kind != HelpBlock::Kind::Gap) out.push_back ({ HelpBlock::Kind::Gap }); continue; }
        const auto t = line.trimStart();
        const int indent = (line.length() - t.length()) / 2;
        if (t.startsWith ("* ") || t.startsWith ("- ") || (t.length() > 2 && juce::CharacterFunctions::isDigit (t[0]) && t.substring (1, 3) == ". "))
        {
            flush();
            const bool numbered = juce::CharacterFunctions::isDigit (t[0]);
            out.push_back ({ HelpBlock::Kind::Bullet, (numbered ? t.upToFirstOccurrenceOf (".", true, false) + " " : juce::String()) + helpText (t.fromFirstOccurrenceOf (" ", false, false)), {}, indent });
            continue;
        }
        if (t.startsWith ("|"))
        {
            flush();
            juce::StringArray cells;
            cells.addTokens (t.substring (1, t.endsWithChar ('|') ? t.length() - 1 : t.length()), "|", "`");
            for (auto& c : cells) c = c.trim();
            if (cells.size() > 0 && cells[0].containsOnly ("-: ")) continue;   // the |---| line
            const bool header = out.empty() || (out.back().kind != HelpBlock::Kind::Row && out.back().kind != HelpBlock::Kind::HeaderRow);
            juce::StringArray rest;
            for (int i = 1; i < cells.size(); ++i) rest.add (helpText (cells[i]));
            out.push_back ({ header ? HelpBlock::Kind::HeaderRow : HelpBlock::Kind::Row, helpText (cells[0]), rest.joinIntoString (" - ") });
            continue;
        }
        // bullet continued on the next line, or a paragraph
        if (indent > 0 && ! out.empty() && out.back().kind == HelpBlock::Kind::Bullet && para.isEmpty())
        {
            out.back().text << " " << helpText (t);
            continue;
        }
        para << (para.isEmpty() ? "" : " ") << t;
    }
    flush();
    return out;
}

// ---------------------------------------------------------------------------
HelpPage::HelpPage()
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    blocks = parseHelp (juce::String::fromUTF8 (PrvDocs::README_md, PrvDocs::README_mdSize));
    for (int i = 0; i < (int) blocks.size(); ++i)
        if (blocks[(size_t) i].kind == HelpBlock::Kind::Chapter) chapters.push_back ({ blocks[(size_t) i].text, i, 0 });

    for (int i = 0; i < (int) chapters.size(); ++i) listChapters.push_back (i);
    list.getNumRows = [this] { return (int) listChapters.size(); };
    list.getRowText = [this] (int lr)
    {
        // short enough to read whole: no (brackets), then whole words off the end
        const int r = listChapters[(size_t) lr];
        juce::String n = chapters[(size_t) r].name;
        if (n.containsChar ('(')) n = n.upToFirstOccurrenceOf ("(", false, false).trim();
        const int room = std::max (8, (int) ((list.getWidth() - Theme::px * 16) / (Theme::px * PixelFont::kCellW)) - 3);
        while (n.length() > room && n.containsChar (' ')) n = n.upToLastOccurrenceOf (" ", false, false).trim();
        return juce::String (r).paddedLeft ('0', 2) + " " + n;
    };
    list.onClick = [this] (int lr, const juce::MouseEvent&)
    {
        const int r = listChapters[(size_t) lr];
        // searching: the chapter's first match, else its start
        for (int m = 0; m < (int) matchRows.size(); ++m)
            if (rows[(size_t) matchRows[(size_t) m]].chapter == r) { matchIndex = m; scrollTo (matchRows[(size_t) m] - 3); searchStatus(); grabKeyboardFocus(); return; }
        showChapter (r);
        grabKeyboardFocus();
    };
    addAndMakeVisible (list);
    pdfBtn.onClick = [this] { openPdf(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (pdfBtn);
    addAndMakeVisible (closeBtn);
}

void HelpPage::opened()
{
    laidOutChars = -1;
    layout();
    grabKeyboardFocus();
    repaint();
}

float HelpPage::rowH() const { return Theme::px * 11.0f; }
int HelpPage::visibleRows() const { return std::max (1, (int) (textArea.getHeight() / rowH())); }

void HelpPage::layout()
{
    const float px = Theme::px;
    const int chars = std::max (20, (int) ((textArea.getWidth() - px * 8) / (px * PixelFont::kCellW)));
    if (chars == laidOutChars) return;
    laidOutChars = chars;
    rows.clear();
    auto wrap = [&] (const juce::String& text, int x, int width, HelpBlock::Kind kind, int chapter, int hangX)
    {
        juce::StringArray words;
        words.addTokens (text, " ", "");
        juce::String cur;
        int curX = x;
        bool first = true;
        auto push = [&]
        {
            rows.push_back ({ cur, curX, kind, chapter, first });
            first = false; cur.clear(); curX = hangX; width = chars - hangX;
        };
        for (auto w : words)
        {
            if (w.isEmpty()) continue;
            while (w.length() > width) { if (cur.isNotEmpty()) push(); cur = w.substring (0, width); w = w.substring (width); push(); }
            if (cur.isEmpty()) cur = w;
            else if (cur.length() + 1 + w.length() <= width) cur << " " << w;
            else { push(); cur = w; }
        }
        if (cur.isNotEmpty() || first) push();
    };
    int chapter = -1;
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        const auto& b = blocks[(size_t) i];
        switch (b.kind)
        {
            case HelpBlock::Kind::Chapter:
                ++chapter;
                if (! rows.empty()) rows.push_back ({ {}, 0, HelpBlock::Kind::Gap, chapter });
                chapters[(size_t) chapter].row = (int) rows.size();
                rows.push_back ({ b.text, 0, HelpBlock::Kind::Chapter, chapter, true });
                rows.push_back ({ {}, 0, HelpBlock::Kind::Gap, chapter });
                break;
            case HelpBlock::Kind::Heading:
                rows.push_back ({ b.text, 0, HelpBlock::Kind::Heading, chapter, true });
                break;
            case HelpBlock::Kind::Text:
                wrap (b.text, 0, chars, HelpBlock::Kind::Text, chapter, 0);
                break;
            case HelpBlock::Kind::Bullet:
            {
                const int x = 2 + b.indent * 2;
                const bool numbered = juce::CharacterFunctions::isDigit (b.text[0]);
                wrap ((numbered ? juce::String() : juce::String::charToString ((juce::juce_wchar) 5) + " ") + b.text, x - 2 + (numbered ? 0 : 0), chars - x + 2, HelpBlock::Kind::Bullet, chapter, x + (numbered ? 1 : 0));
                break;
            }
            case HelpBlock::Kind::Row:
            case HelpBlock::Kind::HeaderRow:
            {
                // the first column on its own line when it's long, else beside the rest
                const int col = std::min (16, chars / 3);
                if (b.text.length() <= col - 1)
                {
                    const int start = (int) rows.size();
                    wrap (b.text2, col, chars - col, b.kind, chapter, col);
                    rows[(size_t) start].text = rows[(size_t) start].text;   // (the first column is drawn separately)
                    rows[(size_t) start].x = col;
                    rows.insert (rows.begin() + start, { b.text, 0, b.kind, chapter, true });
                    // the first column shares the first row: mark it
                    rows[(size_t) start].first = true;
                    rows[(size_t) start + 1].first = false;
                    // (drawn on the same line: see paint)
                    rows[(size_t) start].x = -1 - col;   // negative x = "draw with the next row"
                }
                else
                {
                    rows.push_back ({ b.text, 0, b.kind, chapter, true });
                    wrap (b.text2, 2, chars - 2, b.kind, chapter, 2);
                }
                break;
            }
            case HelpBlock::Kind::Gap:
                if (! rows.empty() && rows.back().kind != HelpBlock::Kind::Gap) rows.push_back ({ {}, 0, HelpBlock::Kind::Gap, chapter });
                break;
        }
    }
    top = juce::jlimit (0, std::max (0, (int) rows.size() - 1), top);
}

void HelpPage::scrollTo (int row)
{
    top = juce::jlimit (0, std::max (0, (int) rows.size() - visibleRows()), row);
    list.selected = 0;
    for (int i = 0; i < (int) listChapters.size(); ++i) if (listChapters[(size_t) i] <= currentChapter()) list.selected = i;
    list.ensureVisible (list.selected);
    list.repaint();
    repaint();
}

void HelpPage::showChapter (int i)
{
    if (i < 0 || i >= (int) chapters.size()) return;
    layout();
    scrollTo (chapters[(size_t) i].row);
}

int HelpPage::currentChapter() const
{
    int c = 0;
    for (int i = 0; i < (int) chapters.size(); ++i) if (chapters[(size_t) i].row <= top + 1) c = i;
    return c;
}

juce::File HelpPage::openPdf()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Paulas Revenge Manual.pdf");
    f.replaceWithData (PrvDocs::Paulas_Revenge_Manual_pdf, (size_t) PrvDocs::Paulas_Revenge_Manual_pdfSize);
    if (launchPdf) f.startAsProcess();
    if (onStatus) onStatus ("OPENING THE PDF MANUAL");
    return f;
}

juce::String HelpPage::copyText (int chapter)
{
    juce::StringArray out;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& r = rows[(size_t) i];
        if (chapter >= 0 && r.chapter != chapter) continue;
        if (r.x < 0 && i + 1 < (int) rows.size()) { out.add (r.text + "  " + rows[(size_t) i + 1].text); ++i; continue; }
        out.add (juce::String::repeatedString (" ", std::max (0, r.x)) + r.text);
    }
    const auto text = out.joinIntoString ("\n").replaceCharacter ('\x05', '*').replaceCharacter ('\x07', 'b').trim();
    juce::SystemClipboard::copyTextToClipboard (text);
    if (onStatus) onStatus (chapter >= 0 ? "CHAPTER COPIED - PASTE IT ANYWHERE" : "THE WHOLE MANUAL COPIED - PASTE IT ANYWHERE");
    return text;
}

void HelpPage::findMatches()
{
    matchRows.clear();
    listChapters.clear();
    if (query.length() >= 2)
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].text.containsIgnoreCase (query))
            {
                matchRows.push_back (i);
                const int c = rows[(size_t) i].chapter;
                if (c >= 0 && (listChapters.empty() || listChapters.back() != c)) listChapters.push_back (c);
            }
    }
    else for (int i = 0; i < (int) chapters.size(); ++i) listChapters.push_back (i);
    list.repaint();
}

void HelpPage::setSearch (const juce::String& q)
{
    query = q.toUpperCase();
    layout();
    findMatches();
    matchIndex = -1;
    if (! matchRows.empty())
    {
        // the first match from where the page is
        matchIndex = 0;
        for (int m = 0; m < (int) matchRows.size(); ++m) if (matchRows[(size_t) m] >= top) { matchIndex = m; break; }
        scrollTo (matchRows[(size_t) matchIndex] - 3);
    }
    else scrollTo (top);
    searchStatus();
    repaint();
}

void HelpPage::nextMatch (int dir)
{
    if (matchRows.empty()) return;
    const int n = (int) matchRows.size();
    matchIndex = ((matchIndex < 0 ? 0 : matchIndex + dir) % n + n) % n;
    scrollTo (matchRows[(size_t) matchIndex] - 3);
    searchStatus();
}

void HelpPage::searchStatus()
{
    if (! onStatus) return;
    if (query.length() < 2) onStatus (query.isEmpty() ? juce::String ("HELP: TYPE TO SEARCH THE MANUAL") : juce::String ("KEEP TYPING..."));
    else if (matchRows.empty()) onStatus ("NOT FOUND: " + query);
    else onStatus (juce::String (matchIndex + 1) + " OF " + juce::String ((int) matchRows.size()) + " - ENTER: NEXT, SHIFT+ENTER: BACK, ESC: CLEAR");
}

void HelpPage::resized()
{
    const float px = Theme::px;
    const int rowHt = (int) (px * 11), gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int c) { return (int) (px * 6 * c + px * 4); };
    auto r = getLocalBounds().reduced (gap);
    auto head = r.removeFromTop (rowHt);
    closeBtn.setBounds (head.removeFromRight (charsW (5))); head.removeFromRight (gap);
    pdfBtn.setBounds (head.removeFromRight (charsW (15))); head.removeFromRight (gap * 3);
    // the search box: after the title when there's room, else the whole of the rest
    const int titleW = charsW (22) + gap * 2;
    searchArea = head.getWidth() - titleW >= charsW (24) ? head.withTrimmedLeft (titleW) : head;
    r.removeFromTop (gap);
    list.setBounds (r.removeFromLeft (std::min (charsW (30), r.getWidth() * 2 / 5)));
    r.removeFromLeft (gap * 2);
    scrollBar = r.removeFromRight ((int) (px * 6));
    r.removeFromRight (gap);
    textArea = r;
    laidOutChars = -1;
    layout();
    scrollTo (top);
}

void HelpPage::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    g.fillAll (Theme::panel);
    const auto headRow = getLocalBounds().reduced ((int) std::max (2.0f, px * 1.5f)).removeFromTop ((int) (px * 11)).toFloat();
    if (searchArea.getX() > (int) headRow.getX() + 2) PixelFont::drawLeft (g, "HELP - PAULA'S REVENGE", headRow, px, Theme::ink);
    {
        // the search box
        g.setColour (Theme::paper);
        g.fillRect (searchArea);
        Theme::bevel (g, searchArea.toFloat(), false, std::max (1.0f, px * 0.5f));
        juce::String t = query.isEmpty() ? juce::String ("TYPE TO SEARCH")
                                         : "SEARCH: " + query + "_" + (query.length() >= 2 ? "  " + (matchRows.empty() ? juce::String ("NONE") : juce::String (matchIndex + 1) + "/" + juce::String ((int) matchRows.size())) : juce::String());
        const int fit = (int) ((searchArea.getWidth() - px * 6) / (px * PixelFont::kCellW));
        if (t.length() > fit) t = t.substring (0, std::max (0, fit));
        PixelFont::drawLeft (g, t, searchArea.toFloat().reduced (px * 3, 0), px, query.isEmpty() ? Theme::faint : Theme::ink);
    }

    // the text: black on white, like a page
    g.setColour (Theme::paper);
    g.fillRect (textArea);
    Theme::bevel (g, textArea.toFloat(), false, std::max (1.0f, px * 0.5f));
    const float rh = rowH();
    const float x0 = (float) textArea.getX() + px * 4;
    const float cw = px * PixelFont::kCellW;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (textArea.reduced (2));
    float y = (float) textArea.getY() + px * 2;
    for (int i = top; i < (int) rows.size() && y < (float) textArea.getBottom(); ++i)
    {
        const auto& row = rows[(size_t) i];
        const auto line = juce::Rectangle<float> (x0, y, (float) textArea.getWidth() - px * 8, rh);
        // search matches: the words themselves where the row's text starts at a known place, else the line
        if (query.length() >= 2)
        {
            auto mark = [&] (const Row& rr, int rowIndex, float startX)
            {
                if (! rr.text.containsIgnoreCase (query)) return;
                const bool current = rowIndex == currentMatchRow();
                g.setColour (current ? juce::Colour (0xffffb000) : juce::Colour (0xffffe58a));
                int from = 0;
                for (int at = rr.text.indexOfIgnoreCase (query); at >= 0; at = rr.text.indexOfIgnoreCase (from, query))
                {
                    g.fillRect (juce::Rectangle<float> (startX + cw * (float) at - px * 0.5f, line.getY(), cw * (float) query.length() + px, rh));
                    from = at + query.length();
                }
            };
            if (row.kind == HelpBlock::Kind::Chapter)
            {
                if (row.text.containsIgnoreCase (query)) { g.setColour (i == currentMatchRow() ? juce::Colour (0xffffb000) : juce::Colour (0xffffe58a)); g.fillRect (line); }
            }
            else if (row.x < 0)
            {
                mark (row, i, line.getX());
                if (i + 1 < (int) rows.size()) mark (rows[(size_t) i + 1], i + 1, line.getX() + cw * (float) rows[(size_t) i + 1].x);
            }
            else mark (row, i, line.getX() + cw * (float) row.x);
        }
        switch (row.kind)
        {
            case HelpBlock::Kind::Chapter:
            {
                // a striped title strip, like the panels' titles
                const float tw = PixelFont::textWidth (row.text, px) + px * 6;
                const auto mid = line.withSizeKeepingCentre (tw, rh);
                g.setColour (Theme::ink);
                for (float sy = line.getY() + px * 2; sy < line.getBottom() - px * 2; sy += px * 2)
                {
                    g.fillRect (line.getX(), sy, mid.getX() - line.getX(), std::max (1.0f, px * 0.5f));
                    g.fillRect (mid.getRight(), sy, line.getRight() - mid.getRight(), std::max (1.0f, px * 0.5f));
                }
                PixelFont::drawCentred (g, row.text, mid, px, Theme::ink);
                break;
            }
            case HelpBlock::Kind::Heading:
                g.setColour (Theme::ink);
                g.fillRect (line.getX(), line.getBottom() - px * 1.5f, PixelFont::textWidth (row.text, px), std::max (1.0f, px * 0.5f));
                PixelFont::drawLeft (g, row.text, line, px, Theme::ink);
                break;
            case HelpBlock::Kind::HeaderRow:
            case HelpBlock::Kind::Row:
            {
                if (row.kind == HelpBlock::Kind::HeaderRow) { g.setColour (Theme::panel); g.fillRect (line.withTrimmedLeft (-px * 2)); }
                if (row.x < 0)
                {
                    // the first column, with the rest of the row beside it on the same line
                    PixelFont::drawLeft (g, row.text, line, px, Theme::ink);
                    if (i + 1 < (int) rows.size())
                    {
                        const auto& next = rows[(size_t) i + 1];
                        PixelFont::drawLeft (g, next.text, line.withTrimmedLeft (cw * (float) next.x), px, Theme::ink);
                        ++i;
                    }
                }
                else PixelFont::drawLeft (g, row.text, line.withTrimmedLeft (cw * (float) row.x), px, row.first ? Theme::ink : Theme::ink);
                g.setColour (Theme::band);
                g.fillRect (line.getX(), line.getBottom() - 1.0f, line.getWidth(), 1.0f);
                break;
            }
            case HelpBlock::Kind::Gap:
                break;
            default:
                PixelFont::drawLeft (g, row.text, line.withTrimmedLeft (cw * (float) row.x), px, Theme::ink);
                break;
        }
        y += rh;
    }

    // the scroll bar
    g.setColour (Theme::paper);
    g.fillRect (scrollBar);
    Theme::bevel (g, scrollBar.toFloat(), false, 1.0f);
    const int total = std::max (1, (int) rows.size());
    const float h = (float) scrollBar.getHeight();
    const float thumbH = std::max (px * 8, h * (float) visibleRows() / (float) total);
    const float thumbY = (float) scrollBar.getY() + (h - thumbH) * (float) top / (float) std::max (1, total - visibleRows());
    g.setColour (Theme::pressed);
    g.fillRect (juce::Rectangle<float> ((float) scrollBar.getX() + 1, thumbY, (float) scrollBar.getWidth() - 2, thumbH));
    Theme::bevel (g, { (float) scrollBar.getX() + 1, thumbY, (float) scrollBar.getWidth() - 2, thumbH }, true, 1.0f);
}

bool HelpPage::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    const auto mods = k.getModifiers();
    if (code == juce::KeyPress::escapeKey)
    {
        if (query.isNotEmpty()) setSearch ({});
        else if (onClose) onClose();
        return true;
    }
    if (code == juce::KeyPress::returnKey) { nextMatch (mods.isShiftDown() ? -1 : 1); return true; }
    if (mods.isCommandDown() && (code == 'G' || code == 'g')) { nextMatch (mods.isShiftDown() ? -1 : 1); return true; }
    if (code == juce::KeyPress::backspaceKey) { if (query.isNotEmpty()) setSearch (query.dropLastCharacters (1)); return true; }
    {
        const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
        const bool typed = ! mods.isCommandDown() && ! mods.isCtrlDown()
                        && ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || juce::String ("#+-./:'%&()").containsChar (ch) || (ch == ' ' && query.isNotEmpty()));
        if (typed) { setSearch (query + juce::String::charToString (ch)); return true; }
    }
    if (code == juce::KeyPress::upKey)       { scrollTo (top - 1); return true; }
    if (code == juce::KeyPress::downKey)     { scrollTo (top + 1); return true; }
    if (code == juce::KeyPress::pageUpKey)   { scrollTo (top - visibleRows() + 1); return true; }
    if (code == juce::KeyPress::pageDownKey || code == juce::KeyPress::spaceKey) { scrollTo (top + visibleRows() - 1); return true; }
    if (code == juce::KeyPress::homeKey)     { scrollTo (0); return true; }
    if (code == juce::KeyPress::endKey)      { scrollTo ((int) rows.size()); return true; }
    if (code == juce::KeyPress::leftKey)     { showChapter (std::max (0, currentChapter() - (chapters[(size_t) currentChapter()].row < top ? 0 : 1))); return true; }
    if (code == juce::KeyPress::rightKey)    { showChapter (currentChapter() + 1); return true; }
    return false;
}

void HelpPage::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int steps = wheel (w);
    if (steps != 0) scrollTo (top + steps * 3);
}

void HelpPage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.mods.isPopupMenu() && textArea.contains (e.getPosition()))
    {
        // right-click on the text: copy it (to paste into notes, a message...)
        juce::Component::SafePointer<HelpPage> safe (this);
        showMenu ("HELP", {
            { "COPY THIS CHAPTER", [safe] { if (safe != nullptr) safe->copyText (safe->currentChapter()); } },
            { "COPY ALL OF IT", [safe] { if (safe != nullptr) safe->copyText (-1); } },
        });
        return;
    }
    dragFrom = -1;
    if (scrollBar.contains (e.getPosition()))
    {
        // jump the thumb there, then drag
        const float f = (float) (e.y - scrollBar.getY()) / (float) std::max (1, scrollBar.getHeight());
        scrollTo ((int) (f * (float) rows.size()) - visibleRows() / 2);
        dragFrom = (float) e.y; dragTop = top;
    }
}

void HelpPage::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    const float perPx = (float) rows.size() / (float) std::max (1, scrollBar.getHeight());
    scrollTo (dragTop + (int) std::lround (((float) e.y - dragFrom) * perPx));
}
} // namespace prv::ui
