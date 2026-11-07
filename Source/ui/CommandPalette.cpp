#include "CommandPalette.h"

namespace prv::ui
{
CommandPalette::CommandPalette()
{
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);
}

void CommandPalette::open (juce::Component& parent, std::vector<PaletteItem> all)
{
    items = std::move (all);
    // (the same name twice - a button and its menu command - once, keeping the one with a shortcut)
    std::vector<PaletteItem> unique;
    for (auto& it : items)
    {
        auto same = std::find_if (unique.begin(), unique.end(), [&] (const PaletteItem& u) { return u.name == it.name; });
        if (same == unique.end()) unique.push_back (it);
        else if (same->keys.isEmpty() && it.keys.isNotEmpty()) *same = it;
    }
    items = std::move (unique);
    std::stable_sort (items.begin(), items.end(), [] (const PaletteItem& a, const PaletteItem& b) { return a.name < b.name; });
    query.clear();
    if (getParentComponent() != &parent) parent.addChildComponent (this);
    setBounds (parent.getLocalBounds());
    refilter();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void CommandPalette::close()
{
    setVisible (false);
    if (auto* p = getParentComponent()) p->grabKeyboardFocus();
}

int CommandPalette::score (const juce::String& name, const juce::String& q)
{
    if (q.isEmpty()) return 1;
    const auto n = name.toUpperCase(), u = q.toUpperCase().trim();
    if (u.isEmpty()) return 1;
    if (n.startsWith (u)) return 1000 - n.length();
    // a word starting with it
    if (n.contains (" " + u) || n.contains (":" + u) || n.contains ("/" + u)) return 800 - n.length();
    if (n.contains (u)) return 600 - n.length();
    // the letters in order (Cmd+K "sva" finds SAVE AS)
    int at = 0, gaps = 0;
    for (auto c : u)
    {
        if (c == ' ') continue;
        const int f = n.indexOfChar (at, c);
        if (f < 0) return 0;
        gaps += f - at;
        at = f + 1;
    }
    return std::max (1, 300 - gaps * 4 - n.length());
}

void CommandPalette::refilter()
{
    matches.clear();
    std::vector<std::pair<int, int>> scored;
    for (int i = 0; i < (int) items.size(); ++i)
        if (const int s = score (items[(size_t) i].name, query); s > 0) scored.push_back ({ s, i });
    std::stable_sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    for (auto& s : scored) matches.push_back (s.second);
    selected = 0; scroll = 0;
    repaint();
}

void CommandPalette::setQuery (const juce::String& q) { query = q.toUpperCase(); refilter(); }

void CommandPalette::runSelected()
{
    const auto* m = match (selected);
    if (m == nullptr) return;
    auto f = m->action;
    close();
    if (f) f();
}

void CommandPalette::keepInView()
{
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + visibleRows()) scroll = selected - visibleRows() + 1;
    repaint();
}

bool CommandPalette::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { close(); return true; }
    if (code == juce::KeyPress::returnKey) { runSelected(); return true; }
    if (code == juce::KeyPress::downKey) { selected = std::min (numMatches() - 1, selected + 1); keepInView(); return true; }
    if (code == juce::KeyPress::upKey)   { selected = std::max (0, selected - 1); keepInView(); return true; }
    if (code == juce::KeyPress::pageDownKey) { selected = std::min (numMatches() - 1, selected + visibleRows()); keepInView(); return true; }
    if (code == juce::KeyPress::pageUpKey)   { selected = std::max (0, selected - visibleRows()); keepInView(); return true; }
    if (code == juce::KeyPress::backspaceKey) { if (query.isNotEmpty()) setQuery (query.dropLastCharacters (1)); return true; }
    if (k.getModifiers().isCommandDown() && (code == 'K' || code == 'k')) { close(); return true; }   // (Cmd+K again: away)
    const auto c = k.getTextCharacter();
    if (c >= ' ' && c < 127 && ! k.getModifiers().isCommandDown() && ! k.getModifiers().isCtrlDown()) { setQuery (query + juce::String::charToString (c)); return true; }
    return true;   // (nothing else while it's up)
}

int CommandPalette::rowAt (juce::Point<float> p) const
{
    if (! listArea.contains (p)) return -1;
    const int r = scroll + (int) ((p.y - listArea.getY()) / rowH());
    return r < numMatches() ? r : -1;
}

void CommandPalette::mouseDown (const juce::MouseEvent& e)
{
    if (! box.contains (e.position)) { close(); return; }
    if (const int r = rowAt (e.position); r >= 0) { selected = r; runSelected(); }
}

void CommandPalette::mouseMove (const juce::MouseEvent& e)
{
    if (const int r = rowAt (e.position); r >= 0 && r != selected) { selected = r; repaint(); }
}

void CommandPalette::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scroll = juce::jlimit (0, std::max (0, numMatches() - visibleRows()), scroll + (w.deltaY < 0 ? 3 : -3));
    repaint();
}

void CommandPalette::resized()
{
    const float px = Theme::px;
    const float w = std::min ((float) getWidth() - px * 8, PixelFont::textWidth ("W", px) * 52);
    const float h = std::min ((float) getHeight() - px * 8, rowH() * 16 + px * 30);
    box = getLocalBounds().toFloat().withSizeKeepingCentre (w, h).withY (std::max (px * 4, getHeight() * 0.12f));
    auto r = box.reduced (px * 3);
    field = r.removeFromTop (px * 13);
    r.removeFromTop (px * 2);
    hintArea = r.removeFromBottom (px * 10);
    r.removeFromBottom (px * 1);
    listArea = r;
}

void CommandPalette::paint (juce::Graphics& g)
{
    const auto& P = Theme::lightPalette();
    const float px = Theme::px;
    g.fillAll (juce::Colours::black.withAlpha (0.35f));
    g.setColour (P.panel);
    g.fillRect (box);
    Theme::bevel (g, box, true, std::max (1.0f, px * 0.5f), P);
    // the field: what you've typed, and a cursor
    g.setColour (P.paper);
    g.fillRect (field);
    Theme::bevel (g, field, false, std::max (1.0f, px * 0.5f), P);
    PixButton::drawIcon (g, PixButton::Icon::Search, field.getX() + px * 2, std::round (field.getCentreY() - 3.5f * px), px, P.ink);
    const float tx = field.getX() + px * 12;
    const float ty = std::round (field.getCentreY() - 3.5f * px);
    if (query.isEmpty()) PixelFont::draw (g, "TYPE A COMMAND", tx, ty, px, P.faint);
    else PixelFont::draw (g, query, tx, ty, px, P.ink);
    g.setColour (P.ink);
    g.fillRect (tx + (query.isEmpty() ? 0.0f : PixelFont::textWidth (query, px)), ty - px, px, px * 9);
    // the matches: name, where it lives, its keys
    g.setColour (Theme::skin.bg);
    g.fillRect (listArea);
    const auto& sk = Theme::skin;
    g.saveState();
    g.reduceClipRegion (listArea.toNearestInt());
    for (int i = 0; i <= visibleRows(); ++i)
    {
        const int row = scroll + i;
        if (row >= numMatches()) break;
        const auto& it = *match (row);
        auto rr = juce::Rectangle<float> (listArea.getX(), listArea.getY() + i * rowH(), listArea.getWidth(), rowH());
        if (row == selected) { g.setColour (sk.band); g.fillRect (rr); g.setColour (sk.text); g.drawRect (rr.reduced (1.0f), std::max (1.0f, px * 0.5f)); }
        const float y = std::round (rr.getCentreY() - 3.5f * px);
        const float keysW = it.keys.isNotEmpty() ? PixelFont::textWidth (it.keys, px) + px * 3 : 0.0f;
        juce::String nm = it.name;
        const float room = rr.getWidth() - keysW - px * 6;
        while (nm.length() > 2 && PixelFont::textWidth (nm, px) > room) nm = nm.dropLastCharacters (1);
        PixelFont::draw (g, nm, rr.getX() + px * 3, y, px, sk.text);
        if (it.keys.isNotEmpty()) PixelFont::draw (g, it.keys, rr.getRight() - keysW, y, px, sk.hot);
    }
    if (numMatches() == 0) PixelFont::drawCentred (g, "NOTHING MATCHES", listArea, px, sk.dim);
    g.restoreState();
    Theme::bevel (g, listArea, false, std::max (1.0f, px * 0.5f), P);
    PixelFont::drawCentred (g, "ENTER DOES IT   UP/DOWN PICKS   ESC CLOSES", hintArea, px, P.ink);
}
} // namespace prv::ui
