// Paula's Revenge - side panels
#include "Panels.h"
#include "../SongFile.h"
#include "../engine/SongOps.h"
#include "ChoiceBox.h"

namespace prv::ui
{
// =========================================================================== PixList
int PixList::maxScroll() const { return std::max (0, (getNumRows ? getNumRows() : 0) - visibleRows()); }

juce::Rectangle<float> PixList::thumb() const
{
    const auto t = track();
    const int n = getNumRows ? getNumRows() : 0;
    if (n <= visibleRows()) return t;
    const float h = std::max (barWidth(), t.getHeight() * visibleRows() / (float) n);
    const float y = t.getY() + (t.getHeight() - h) * scroll / (float) std::max (1, maxScroll());
    return { t.getX(), y, t.getWidth(), h };
}

void PixList::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    const float rh = rowHeight();
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::skin.bg);
    g.fillRect (r);
    const float listW = r.getWidth() - barWidth();

    const int n = getNumRows ? getNumRows() : 0;
    scroll = juce::jlimit (0, maxScroll(), scroll);
    juce::RectangleList<float> text, dim;
    bool anyScroll = false;
    for (int i = 0; i < visibleRows() + 1; ++i)
    {
        const int row = scroll + i;
        if (row >= n) break;
        auto rr = juce::Rectangle<float> (0, i * rh, listW, rh);
        if (isRowInRange && isRowInRange (row) && row != selected)
        {
            g.setColour (Theme::skin.hot.withAlpha (0.30f));
            g.fillRect (rr);
        }
        if (row == selected)
        {
            g.setColour (Theme::skin.band);
            g.fillRect (rr);
            g.setColour (Theme::skin.text);
            g.drawRect (rr.reduced (1.0f), std::max (1.0f, px * 0.5f));
        }
        if (getRowStripe)
            if (auto c = getRowStripe (row); ! c.isTransparent())
            {
                g.setColour (c);
                g.fillRect (rr.getX() + px * 0.5f, rr.getY() + 1.0f, px * 4.0f, rh - 2.0f);
            }
        const float ty = std::round (rr.getY() + (rh - 7 * px) * 0.5f);
        if (row == playing) PixelFont::addText (text, juce::String::charToString ((juce::juce_wchar) 1), px * 1.0f, ty, px);
        PixelFont::addText (isRowDim (row) ? dim : text, getRowText (row), px * 8.0f, ty, px);
        if (isRowLooped && isRowLooped (row))
        {
            g.setColour (Theme::skin.hot);
            g.fillRect (px * 5.5f, rr.getY(), px * 1.5f, rh);
        }
        const juce::String tag = getRowTag ? getRowTag (row) : juce::String();
        if (getRowName && getRowNumber && nameX >= 0)
        {
            // the name in its zone (scrolling when it doesn't fit), then the block number
            const juce::String nm = getRowName (row);
            const float zoneW = nameEndX - nameX;
            if (nm.isNotEmpty() && zoneW > px * 6)
            {
                const float off = nameScroll (row, nm);
                if (PixelFont::textWidth (nm, px) > zoneW && (row == playing || row == selected || row == hoverRow)) anyScroll = true;
                g.saveState();
                g.reduceClipRegion (juce::Rectangle<float> (nameX, rr.getY(), zoneW, rh).toNearestInt());
                PixelFont::draw (g, nm, nameX - off, ty, px, isRowDim (row) ? Theme::skin.dim : Theme::skin.text);
                g.restoreState();
                if (PixelFont::textWidth (nm, px) > zoneW && off < PixelFont::textWidth (nm, px) - zoneW - 0.5f)
                {
                    g.setColour (Theme::skin.dim);   // (more to come: a mark at the zone's right edge)
                    g.fillRect (nameEndX - px * 0.5f, rr.getY() + px * 2, px * 0.5f, rh - px * 4);
                }
            }
            PixelFont::addText (isRowDim (row) ? dim : text, getRowNumber (row), numberX, ty, px);
        }
        else if (getRowName && tagZoneX >= 0)
        {
            // the block's name, cut short so it never runs into the section label
            juce::String nm = getRowName (row);
            if (nm.isNotEmpty())
            {
                const juce::String shownTag = tag.isNotEmpty() ? tag : (row == hoverTagRow ? juce::String ("+ SECTION") : juce::String());
                const float tagW = shownTag.isNotEmpty() ? PixelFont::textWidth (shownTag, px) + px * 4 : 0.0f;
                const float room = listW - tagZoneX - tagW - px * 2;
                if (PixelFont::textWidth (nm, px) > room)
                {
                    while (nm.length() > 1 && PixelFont::textWidth (nm + "..", px) > room) nm = nm.dropLastCharacters (1);
                    nm = room > PixelFont::textWidth ("A..", px) ? nm + ".." : juce::String();
                }
                if (nm.isNotEmpty()) PixelFont::draw (g, nm, tagZoneX, ty, px, Theme::skin.text);
            }
        }
        if (tag.isNotEmpty() || (row == hoverTagRow && tagZoneX >= 0))
        {
            g.setColour (Theme::skin.hot);
            if (tag.isNotEmpty()) g.fillRect (rr.getX(), rr.getY(), listW, std::max (1.5f, px * 0.75f));   // the divider
            juce::String t = tag.isNotEmpty() ? tag : juce::String ("+ SECTION");
            const float zoneW = listW - std::max (0.0f, tagZoneX) - px * 2;
            while (t.length() > 1 && PixelFont::textWidth (t, px) > zoneW) t = t.dropLastCharacters (1);
            PixelFont::draw (g, t, listW - PixelFont::textWidth (t, px) - px * 2, ty, px,
                             tag.isNotEmpty() ? Theme::skin.hot : Theme::skin.dim);
        }
    }
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (0, 0, listW, r.getHeight()).toNearestInt());
    g.setColour (Theme::skin.dim);
    g.fillRectList (dim);
    g.setColour (Theme::skin.text);
    g.fillRectList (text);
    g.restoreState();
    // a long name on the playing / selected / hovered row scrolls: keep repainting while it does
    if (anyScroll && testClockMs() < 0) { if (! ticker.isTimerRunning()) { ticker.fn = [this] { repaint(); }; ticker.startTimerHz (30); } }
    else ticker.stopTimer();

    // drop marker while dragging a row
    if (reordering && ! rangeDragging && dropRow >= 0)
    {
        const float y = (dropRow - scroll + (dropRow > dragRow ? 1 : 0)) * rh;
        g.setColour (Theme::skin.hot);
        g.fillRect (0.0f, y - px * 0.75f, listW, px * 1.5f);
    }

    // scrollbar: arrow buttons + track + thumb
    const float t = std::max (1.0f, px * 0.5f);
    for (int dir : { -1, 1 })
    {
        auto a = dir < 0 ? upArrow() : downArrow();
        const bool down = repeatDir == dir;
        g.setColour (down ? Theme::pressed : Theme::panel);
        g.fillRect (a);
        Theme::bevel (g, a.reduced (1.0f), ! down, t);
        PixelFont::drawCentred (g, juce::String::charToString ((juce::juce_wchar) (dir < 0 ? 3 : 4)), a, std::max (1.5f, px * 0.75f), Theme::ink);
    }
    g.setColour (Theme::skin.stripe);
    g.fillRect (track());
    if (n > visibleRows())
    {
        auto th = thumb().reduced (1.0f, 0.0f);
        g.setColour (Theme::panel);
        g.fillRect (th);
        Theme::bevel (g, th, true, t);
    }
    Theme::bevel (g, r, false, t);
}

int PixList::rowAt (float y) const { return scroll + (int) std::floor (y / rowHeight()); }

void PixList::scrollBy (int rows)
{
    scroll = juce::jlimit (0, maxScroll(), scroll + rows);
    repaint();
}

bool PixList::onTag (int row, float x) const
{
    if (tagZoneX < 0 || x < tagZoneX || ! getRowTag) return false;
    // only on the label itself (or where "+ SECTION" shows), at the right
    const float px = Theme::px;
    const float listW = (float) getWidth() - barWidth();
    const auto tag = getRowTag (row);
    const float w = PixelFont::textWidth (tag.isNotEmpty() ? tag : juce::String ("+ SECTION"), px);
    return x >= listW - w - px * 4 && x < listW;
}

float PixList::nameScroll (int row, const juce::String& name) const
{
    // only the playing, selected or hovered row scrolls: wait, glide to the end, wait, jump back
    const float px = Theme::px;
    const float over = PixelFont::textWidth (name, px) - (nameEndX - nameX);
    if (over <= 0.0f || ! (row == playing || row == selected || row == hoverRow)) return 0.0f;
    const double speed = px * 6.0 / 0.16;              // a character every 0.16 s
    const double wait = 1200.0, glide = over / speed * 1000.0;
    const double now = testClockMs() >= 0 ? testClockMs() : juce::Time::getMillisecondCounterHiRes();
    const double t = std::fmod (now, wait * 2.0 + glide);
    if (t < wait) return 0.0f;
    if (t < wait + glide) return (float) ((t - wait) / glide * over);
    return over;
}

void PixList::timerCallback()
{
    if (repeatDir == 0) { stopTimer(); return; }
    scrollBy (repeatDir);
    startTimer (60);
}

void PixList::mouseDown (const juce::MouseEvent& e)
{
    if (barArea().contains (e.position))
    {
        if (upArrow().contains (e.position))        { repeatDir = -1; scrollBy (-1); startTimer (350); }
        else if (downArrow().contains (e.position)) { repeatDir = 1;  scrollBy (1);  startTimer (350); }
        else if (thumb().contains (e.position))     thumbGrab = e.position.y - thumb().getY();
        else scrollBy (e.position.y < thumb().getY() ? -visibleRows() : visibleRows());
        repaint();
        return;
    }
    const int row = rowAt (e.position.y);
    dragRow = -1; reordering = false; rangeDragging = false;
    if (getNumRows && row >= 0 && row < getNumRows() && onTagClick && ! e.mods.isPopupMenu() && ! e.mods.isShiftDown() && onTag (row, e.position.x))
    {
        onTagClick (row);
        return;
    }
    if (getNumRows && row >= 0 && row < getNumRows())
    {
        // decided before the click changes the selection: a drag from the grab column moves, else it highlights
        const bool moves = (grabX < 0 || e.position.x < grabX) && (! dragMoves || dragMoves (row));
        if (onClick) onClick (row, e);
        if (onMove && moves) dragRow = row;
        else if (onDragSelect && ! e.mods.isShiftDown()) { dragRow = row; rangeDragging = true; }
    }
}

void PixList::mouseDrag (const juce::MouseEvent& e)
{
    if (thumbGrab >= 0)
    {
        const auto t = track();
        const float h = thumb().getHeight();
        const float frac = (e.position.y - thumbGrab - t.getY()) / std::max (1.0f, t.getHeight() - h);
        scroll = juce::jlimit (0, maxScroll(), juce::roundToInt (frac * maxScroll()));
        repaint();
        return;
    }
    if (dragRow < 0 || (! onMove && ! rangeDragging)) return;
    if (! reordering && e.getDistanceFromDragStart() < 6) return;
    reordering = true;
    // held past the ends: scroll at a steady pace
    lastDragY = e.position.y;
    auto place = [this]
    {
        dropRow = juce::jlimit (0, getNumRows() - 1, rowAt (juce::jlimit (0.0f, (float) getHeight() - 1, lastDragY)));
        if (rangeDragging && onDragSelect) onDragSelect (dropRow);
        repaint();
    };
    edgeScroll.onStep = [this, place] (int dir) { scrollBy (dir); place(); };
    edgeScroll.update (e.position.y < 0 ? e.position.y : (e.position.y > getHeight() ? e.position.y - (float) getHeight() : 0.0f), Theme::px * 11);
    place();
}

void PixList::mouseUp (const juce::MouseEvent&)
{
    if (onRelease && thumbGrab < 0 && repeatDir == 0) onRelease();
    if (reordering && ! rangeDragging && dropRow >= 0 && dropRow != dragRow && onMove) onMove (dragRow, dropRow);
    edgeScroll.stop();
    reordering = rangeDragging = false; dragRow = dropRow = -1;
    repeatDir = 0; thumbGrab = -1.0f;
    stopTimer();
    repaint();
}

void PixList::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position.y);
    const int n = getNumRows ? getNumRows() : 0;
    const int hr = row >= 0 && row < n && ! barArea().contains (e.position) ? row : -1;
    if (hr != hoverRow) { hoverRow = hr; repaint(); }
    const int h = (row >= 0 && row < n && ! barArea().contains (e.position) && onTag (row, e.position.x)) ? row : -1;
    if (h != hoverTagRow) { hoverTagRow = h; repaint(); }
    // the grab column: a hand (a drag there moves the highlighted positions)
    const bool grab = grabX >= 0 && e.position.x < grabX && hr >= 0 && onMove && (! dragMoves || dragMoves (hr));
    setMouseCursor (grab ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (onHover) onHover (row >= 0 && row < n && ! barArea().contains (e.position) ? row : -1);
}

void PixList::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0) { hoverRow = -1; repaint(); }
    if (hoverTagRow >= 0) { hoverTagRow = -1; repaint(); }
}

void PixList::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (barArea().contains (e.position)) return;
    const int row = rowAt (e.position.y);
    if (getNumRows && row >= 0 && row < getNumRows())
    {
        if (onDoubleClickAt) onDoubleClickAt (row, e.position.x);
        else if (onDoubleClick) onDoubleClick (row);
    }
}

void PixList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int n = wheel (w);
    if (n != 0) scrollBy (n);
}

void PixList::ensureVisible (int row)
{
    if (row < scroll) scroll = row;
    else if (row >= scroll + visibleRows()) scroll = row - visibleRows() + 1;
    repaint();
}

// =========================================================================== Sequence
SequencePanel::SequencePanel (PaulaProcessor& p, std::function<void()> c) : proc (p), changed (std::move (c))
{
    auto& st = proc.editorState;
    list.getNumRows = [this] { return (int) proc.song.sequence.size(); };
    list.getRowText = [this] (int r)
    {
        return juce::String (r).paddedLeft ('0', 3);   // (then the block's name and number: getRowName / getRowNumber)
    };
    ops.onDone = [this] (const juce::String& m) { this->changed(); if (onStatus && m.isNotEmpty()) onStatus (m); };
    setWantsKeyboardFocus (true);
    list.onClick = [this] (int r, const juce::MouseEvent& e)
    {
        grabKeyboardFocus();   // so CUT/COPY/PASTE/DUP keys work here; other keys go on to the pattern
        if (e.mods.isPopupMenu())
        {
            // right-click: the position (or the highlight it's in), then what to do with it
            if (! (ops.hasRange() && r >= ops.selStart() && r <= ops.selEnd())) selectPosition (r);
            juce::Component::SafePointer<SequencePanel> safe (this);
            auto act = [safe] (std::function<void (SequencePanel&)> f) { return [safe, f] { if (safe != nullptr) f (*safe); }; };
            showMenu ("SONG POSITION " + juce::String (r).paddedLeft ('0', 3), {
                { "COPY", act ([] (SequencePanel& s) { s.ops.copy(); }) },
                { "CUT", act ([] (SequencePanel& s) { s.ops.cut(); }) },
                { "PASTE", act ([] (SequencePanel& s) { s.ops.paste(); }) },
                { "DUPLICATE", act ([] (SequencePanel& s) { s.ops.duplicate(); }) },
                { "DELETE", act ([] (SequencePanel& s) { s.ops.remove(); }) },
                { "INSERT THE SAME BLOCK", act ([] (SequencePanel& s) { s.insertPosition (false); }) },
                { "INSERT A NEW BLOCK", act ([] (SequencePanel& s) { s.insertPosition (true); }) },
                { "CLONE IN PLACE", act ([] (SequencePanel& s) { s.cloneInPlace(); }) },
                { "COMBINE INTO ONE BLOCK", act ([] (SequencePanel& s) { s.combineSelection(); }) },
                { proc.editorState.selectionLoop ? "LOOP SELECTION: OFF" : "LOOP SELECTION", act ([] (SequencePanel& s) { s.setLoopSelection (! s.proc.editorState.selectionLoop); }) },
                { "SECTION NAME...", act ([r] (SequencePanel& s) { s.list.onTagClick (r); }) },
                { "BLOCK NAME...", act ([r] (SequencePanel& s) { s.nameBlockAt (r); }) },
            });
            return;
        }
        if (e.mods.isShiftDown()) { ops.select (ops.anchor(), r); this->changed(); return; }
        if (ops.hasRange() && r >= ops.selStart() && r <= ops.selEnd() && e.position.x < list.grabX) return;   // (a drag from the numbers moves the highlight)
        selectPosition (r);
    };
    list.isRowInRange = [this] (int r) { return ops.hasRange() && r >= ops.selStart() && r <= ops.selEnd(); };
    list.dragMoves = [this] (int r) { return r >= ops.selStart() && r <= ops.selEnd(); };
    list.onDragSelect = [this] (int r)
    {
        const int a = ops.anchor();
        if (r != proc.editorState.seqPos) { ops.select (a, r); this->changed(); }
    };
    cutBtn.onClick   = [this] { ops.cut(); };
    copyBtn.onClick  = [this] { ops.copy(); };
    pasteBtn.onClick = [this] { ops.paste(); };
    dupBtn.onClick   = [this] { ops.duplicate(); };
    for (auto* b : { &cutBtn, &copyBtn, &pasteBtn, &dupBtn }) addAndMakeVisible (b);
    list.getRowTag = [this] (int r)
    {
        const int i = sectionAt (proc.song.sections, r);
        return i < 0 ? juce::String() : juce::String (proc.song.sections[(size_t) i].name).toUpperCase();
    };
    list.isRowLooped = [this] (int r)
    {
        if (! proc.editorState.sectionLoop && ! proc.editorState.selectionLoop) return false;
        return r >= proc.player.songLoopStart() && r <= proc.player.songLoopEnd();
    };
    list.getRowName = [this] (int r)
    {
        if (r < 0 || r >= (int) proc.song.sequence.size()) return juce::String();
        const auto nm = juce::String (proc.song.blocks[(size_t) proc.song.safeBlock (proc.song.sequence[(size_t) r])].name).toUpperCase().trim();
        return nm.isEmpty() ? juce::String ("BLOCK") : nm;   // (the name takes the place of the word BLOCK)
    };
    list.getRowNumber = [this] (int r)
    {
        if (r < 0 || r >= (int) proc.song.sequence.size()) return juce::String();
        return juce::String (proc.song.sequence[(size_t) r]).paddedLeft ('0', 3);
    };
    list.onDoubleClick = [this] (int r) { nameBlockAt (r); };
    list.onTagClick = [this] (int r)
    {
        juce::Component::SafePointer<SequencePanel> safe (this);
        editSectionDialog (proc, r, [safe] (const juce::String& m) { if (safe == nullptr) return; safe->changed(); if (safe->onStatus) safe->onStatus (m); });
    };
    loopSelBtn.onClick = [this] { setLoopSelection (loopSelBtn.getToggleState()); };
    addAndMakeVisible (loopSelBtn);
    loopSectBtn.onClick = [this, &st]
    {
        st.sectionLoop = loopSectBtn.getToggleState();
        if (st.sectionLoop) st.selectionLoop = false;
        syncSectionLoop (proc);
        int a = 0, b = 0; sectionRange (proc.song, st.seqPos, a, b);
        if (onStatus) onStatus (st.sectionLoop ? "SONG PLAY LOOPS POSITIONS " + juce::String (a).paddedLeft ('0', 3) + "-" + juce::String (b).paddedLeft ('0', 3)
                                                 + " (CLICK ANOTHER SECTION TO GO THERE NEXT)" : juce::String ("SECTION LOOP OFF"));
        changed();
    };
    addAndMakeVisible (loopSectBtn);
    list.onMove = [this] (int from, int to)
    {
        // moves the highlighted range (or the one position) to where it was dropped
        ops.moveTo (to > from ? to + 1 : to);
    };
    addAndMakeVisible (list);
    upBtn.onClick = [this] { moveSelected (-1); };
    downBtn.onClick = [this] { moveSelected (1); };
    cloneInPlaceBtn.onClick = [this] { cloneInPlace(); };
    arrangeBtn.onClick = [this] { if (openArrange) openArrange(); };
    addAndMakeVisible (arrangeBtn);
    addAndMakeVisible (upBtn);
    addAndMakeVisible (downBtn);
    addAndMakeVisible (cloneInPlaceBtn);

    insBtn.onClick = [this] { insertPosition (false); };
    newBtn.onClick = [this] { insertPosition (true); };
    delBtn.onClick = [this] { ops.remove(); };   // the highlighted positions (or the one selected)
    blockBox.valueChars = 3;
    blockBox.labelChars = 6;
    blockBox.format = [] (int v) { return juce::String (v).paddedLeft ('0', 3); };
    blockBox.onChange = [this, &st] (int v)
    {
        if (pushStructUndo) pushStructUndo ("SEQUENCE BLOCK NUMBER");
        proc.withLock ([&] { proc.song.sequence[(size_t) st.seqPos] = proc.song.safeBlock (v); });
        st.block = proc.song.safeBlock (v);
        st.line = std::min (st.line, proc.song.blocks[(size_t) st.block].lines - 1);
        changed();
    };
    addAndMakeVisible (insBtn);
    addAndMakeVisible (newBtn);
    addAndMakeVisible (delBtn);
    addAndMakeVisible (blockBox);
}

// ADD: a new position after the current one, playing the same block (pick another with BLOCK - / +)
// NEW: a new empty block (as long as the current one), placed in a new position after the current one
void SequencePanel::insertPosition (bool newBlock)
{
    auto& st = proc.editorState;
    auto& song = proc.song;
    if (song.sequence.size() >= (size_t) kMaxSequence) { if (onStatus) onStatus ("THE SEQUENCE IS FULL"); return; }
    if (newBlock && song.blocks.size() >= (size_t) kMaxBlocks) { if (onStatus) onStatus ("CAN'T ADD A NEW BLOCK: THE SONG HAS 256 BLOCKS"); return; }
    if (pushStructUndo) pushStructUndo (newBlock ? "NEW BLOCK IN SEQUENCE" : "ADD SEQUENCE POSITION");
    const int at = st.seqPos + 1;
    int b = song.sequence[(size_t) st.seqPos];
    Block fresh;
    if (newBlock) fresh.lines = song.blocks[(size_t) song.safeBlock (b)].lines;
    proc.withLock ([&] {
        if (newBlock) { song.blocks.push_back (fresh); b = (int) song.blocks.size() - 1; }
        song.sequence.insert (song.sequence.begin() + at, b);
        sectionsInserted (song.sections, at, 1, (int) song.sequence.size());
    });
    st.seqPos = at;
    st.block = b;
    selectPosition (at);
    if (onStatus)
        onStatus ("POSITION " + juce::String (at).paddedLeft ('0', 3) + " ADDED, PLAYING "
                  + (newBlock ? "NEW EMPTY BLOCK " : "BLOCK ") + juce::String (b).paddedLeft ('0', 3)
                  + (newBlock ? juce::String() : juce::String (" - CHANGE IT WITH BLOCK - / +")));
}

void SequencePanel::selectPosition (int r)
{
    auto& st = proc.editorState;
    if (r < 0 || r >= (int) proc.song.sequence.size()) return;
    ops.select (r, r);
    // while a block plays, the new block carries on from the same line (see PaulaEditor::refreshAll)
    if (! (proc.player.getMode() == Player::Mode::Block)) st.line = 0;
    changed();
}

void SequencePanel::moveSelected (int delta)
{
    ops.moveBy (delta);   // the highlighted range, or the one position
}

bool SequencePanel::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    if (m.isCommandDown() && ! m.isShiftDown())
    {
        const auto ch = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        if (ch == 'x') { ops.cut(); return true; }
        if (ch == 'c') { ops.copy(); return true; }
        if (ch == 'v') { ops.paste(); return true; }
        if (ch == 'd') { ops.duplicate(); return true; }
        if (ch == 'a') { ops.select (0, (int) proc.song.sequence.size() - 1); changed(); return true; }
    }
    if (! m.isAnyModifierKeyDown() && (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)) { ops.remove(); return true; }
    // anything else (notes, space, undo ...) goes on to the pattern, which gets the keyboard back
    if (returnFocus) returnFocus();
    return false;
}

void SequencePanel::cloneInPlace()
{
    auto& st = proc.editorState;
    if (nextFreeBlock (proc.song) < 0) { if (onStatus) onStatus ("CAN'T CLONE: NO FREE BLOCK (THE SONG HAS 256 BLOCKS)"); return; }
    if (pushStructUndo) pushStructUndo ("CLONE IN PLACE");
    int nb = -1;
    // build the new song structure off the audio thread, then swap it in
    std::vector<Block> blocks = proc.song.blocks;
    std::vector<int> seq = proc.song.sequence;
    {
        Song tmp;
        tmp.blocks = std::move (blocks);
        tmp.sequence = std::move (seq);
        nb = prv::cloneInPlace (tmp, st.seqPos);
        blocks = std::move (tmp.blocks);
        seq = std::move (tmp.sequence);
    }
    if (nb < 0) return;
    const int oldBlock = proc.song.sequence[(size_t) st.seqPos];
    proc.withLock ([&] { std::swap (proc.song.blocks, blocks); std::swap (proc.song.sequence, seq); });
    st.block = nb;
    changed();
    if (onStatus)
        onStatus ("POSITION " + juce::String (st.seqPos).paddedLeft ('0', 3) + " NOW PLAYS ITS OWN COPY OF BLOCK "
                  + juce::String (oldBlock).paddedLeft ('0', 3) + ": BLOCK " + juce::String (nb).paddedLeft ('0', 3) + " (NEXT FREE BLOCK)");
}

void SequencePanel::refresh()
{
    auto& st = proc.editorState;
    st.seqPos = juce::jlimit (0, (int) proc.song.sequence.size() - 1, st.seqPos);
    list.selected = st.seqPos;
    list.playing = proc.player.getMode() == Player::Mode::Song ? proc.player.uiSeqPos.load() : -1;
    list.ensureVisible (list.playing >= 0 ? list.playing : st.seqPos);   // (the list follows the song as it plays)
    syncSectionLoop (proc);
    loopSectBtn.setToggleState (st.sectionLoop, juce::dontSendNotification);
    loopSelBtn.setToggleState (st.selectionLoop, juce::dontSendNotification);
    blockBox.setRange (0, (int) proc.song.blocks.size() - 1);
    blockBox.setValue (proc.song.sequence[(size_t) st.seqPos]);
    list.repaint();
}

void SequencePanel::paint (juce::Graphics& g) { Theme::panelBox (g, getLocalBounds().toFloat(), "SEQUENCE"); }

void SequencePanel::combineSelection()
{
    const int a = ops.selStart(), b = ops.selEnd();
    if (a == b) { if (onStatus) onStatus ("HIGHLIGHT TWO OR MORE POSITIONS TO COMBINE (DRAG OVER THEM, OR SHIFT+CLICK)"); return; }
    int total = 0;
    for (int p = a; p <= b; ++p) total += proc.song.blocks[(size_t) proc.song.safeBlock (proc.song.sequence[(size_t) p])].lines;
    if (total > kMaxLines) { if (onStatus) onStatus ("CAN'T COMBINE: THAT'S " + juce::String (total) + " LINES (A BLOCK HOLDS 256)"); return; }
    proc.history.structure ("COMBINE " + juce::String (b - a + 1) + " POSITIONS");
    int made = -1;
    proc.withLock ([&] { made = combinePositions (proc.song, a, b); });
    if (made < 0) { proc.history.dropLast(); if (onStatus) onStatus ("CAN'T COMBINE: NO ROOM FOR ANOTHER BLOCK"); return; }
    if (proc.editorState.selectionLoop) setLoopSelection (false);
    selectPosition (a);
    if (onStatus) onStatus ("COMBINED POSITIONS " + juce::String (a).paddedLeft ('0', 3) + "-" + juce::String (b).paddedLeft ('0', 3)
                            + " INTO BLOCK " + juce::String (made).paddedLeft ('0', 3) + " (" + juce::String (total) + " LINES)");
}

void SequencePanel::setLoopSelection (bool on)
{
    auto& st = proc.editorState;
    st.selectionLoop = on;
    if (on)
    {
        st.sectionLoop = false;
        st.selLoopFrom = ops.selStart(); st.selLoopTo = ops.selEnd();
    }
    syncSectionLoop (proc);
    changed();
    if (onStatus) onStatus (on ? "SONG PLAY LOOPS POSITIONS " + juce::String (st.selLoopFrom).paddedLeft ('0', 3) + "-" + juce::String (st.selLoopTo).paddedLeft ('0', 3)
                               : juce::String ("LOOP SELECTION OFF"));
}

void SequencePanel::resized()
{
    // four rows of buttons under the list:
    //   BLOCK [-] 000 [+]   ADD  NEW  DEL
    //   [UP] [DOWN]  CUT  COPY  PASTE  ARRANGE
    //   CLONE IN PLACE   DUPLICATE
    //   LOOP SELECTION   LOOP SECTION
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    const int gap = (int) px;
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };

    auto row4 = r.removeFromBottom (rowH); r.removeFromBottom (gap);
    auto row3 = r.removeFromBottom (rowH); r.removeFromBottom (gap);
    auto row2 = r.removeFromBottom (rowH); r.removeFromBottom (gap);
    auto row1 = r.removeFromBottom (rowH); r.removeFromBottom ((int) px * 2);

    delBtn.setBounds (row1.removeFromRight (charsW (4))); row1.removeFromRight (gap);
    newBtn.setBounds (row1.removeFromRight (charsW (4))); row1.removeFromRight (gap);
    insBtn.setBounds (row1.removeFromRight (charsW (4))); row1.removeFromRight (gap * 2);
    blockBox.setBounds (row1);

    const int aw = (int) (px * 6 * 3);
    upBtn.setBounds (row2.removeFromLeft (aw)); row2.removeFromLeft (gap);
    downBtn.setBounds (row2.removeFromLeft (aw)); row2.removeFromLeft (gap * 2);
    // widths follow the labels; whatever is left over is shared out
    auto share = [&] (juce::Rectangle<int> row, std::vector<PixButton*> bs)
    {
        int need = gap * ((int) bs.size() - 1);
        for (auto* b : bs) need += charsW (b->getButtonText().length());
        const int extra = std::max (0, row.getWidth() - need) / (int) bs.size();
        for (auto* b : bs)
        {
            b->setBounds (b == bs.back() ? row : row.removeFromLeft (charsW (b->getButtonText().length()) + extra));
            row.removeFromLeft (gap);
        }
    };
    share (row2, { &cutBtn, &copyBtn, &pasteBtn, &arrangeBtn });
    share (row3, { &cloneInPlaceBtn, &dupBtn });
    share (row4, { &loopSelBtn, &loopSectBtn });

    list.setBounds (r);
    // each row: "000  NAME....  000  SECTION": the position, the block's name (BLOCK when it has none) in a zone
    // that takes about half of what's left, the block number (it never changes), then the section label
    {
        const float cell = px * PixelFont::kCellW;
        const float listW = (float) r.getWidth() - px * 9;   // (less the scrollbar)
        list.nameX = px * 8.0f + PixelFont::textWidth ("000  ", px);
        const float rest = listW - list.nameX - PixelFont::textWidth (" 000  ", px);
        const int nameChars = juce::jlimit (5, 18, (int) (rest * 0.55f / cell));
        list.nameEndX = list.nameX + nameChars * cell;
        list.numberX = list.nameEndX + cell;
        list.tagZoneX = list.numberX + PixelFont::textWidth ("000 ", px);
        list.grabX = list.nameX - cell;   // (the position numbers: drag there to move)
    }
}

// =========================================================================== Sections
void syncSectionLoop (PaulaProcessor& proc)
{
    auto& st = proc.editorState;
    int a = -1, b = -1;
    const int n = (int) proc.song.sequence.size();
    if (st.selectionLoop)
    {
        a = juce::jlimit (0, n - 1, std::min (st.selLoopFrom, st.selLoopTo));
        b = juce::jlimit (0, n - 1, std::max (st.selLoopFrom, st.selLoopTo));
    }
    else if (st.sectionLoop) sectionRange (proc.song, st.seqPos, a, b);
    if (a != proc.player.songLoopStart() || b != proc.player.songLoopEnd())
        proc.withLock ([&] { proc.player.setSongLoop (a, b); });
}

void editSectionDialog (PaulaProcessor& proc, int pos, std::function<void (const juce::String&)> done)
{
    const int idx = sectionAt (proc.song.sections, pos);
    const juce::String current = idx >= 0 ? juce::String (proc.song.sections[(size_t) idx].name)
                                          : "SECTION " + juce::String ((int) proc.song.sections.size() + 1);
    auto* w = new juce::AlertWindow (idx >= 0 ? "Section" : "New section",
                                     (idx >= 0 ? "Rename the section starting at position " : "Start a section at position ") + juce::String (pos).paddedLeft ('0', 3),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", current);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (idx >= 0) w->addButton ("Remove", 2);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    auto* p = &proc;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([p, w, pos, done] (int r)
    {
        if (r == 0) return;
        auto& song = p->song;
        p->history.structure (r == 2 ? "REMOVE SECTION" : "SECTION");
        auto secs = song.sections;
        const int i = sectionAt (secs, pos);
        juce::String msg;
        if (r == 2) { if (i >= 0) secs.erase (secs.begin() + i); msg = "SECTION REMOVED"; }
        else
        {
            auto name = w->getTextEditorContents ("name").toUpperCase().trim().substring (0, 16).toStdString();
            if (name.empty()) name = "SECTION";
            if (i >= 0) secs[(size_t) i].name = name; else secs.push_back ({ pos, name });
            msg = "SECTION \"" + juce::String (name) + "\" STARTS AT POSITION " + juce::String (pos).paddedLeft ('0', 3);
        }
        tidySections (secs, (int) song.sequence.size());
        p->withLock ([&] { std::swap (song.sections, secs); });
        syncSectionLoop (*p);
        if (done) done (msg);
    }), true);
}

void SequencePanel::nameBlockAt (int row)
{
    if (row < 0 || row >= (int) proc.song.sequence.size()) return;
    juce::Component::SafePointer<SequencePanel> safe (this);
    editBlockNameDialog (proc, proc.song.sequence[(size_t) row], [safe] (const juce::String& m)
    {
        if (safe == nullptr) return;
        safe->changed(); safe->repaint();
        if (safe->onStatus) safe->onStatus (m);
    });
}

void editBlockNameDialog (PaulaProcessor& proc, int block, std::function<void (const juce::String&)> done)
{
    block = proc.song.safeBlock (block);
    const juce::String num = juce::String (block).paddedLeft ('0', 3);
    auto* w = new juce::AlertWindow ("Block name", "Name block " + num + " (empty: no name)", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", juce::String (proc.song.blocks[(size_t) block].name));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    auto* p = &proc;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([p, w, block, num, done] (int r)
    {
        if (r != 1) return;
        setBlockName (*p, block, w->getTextEditorContents ("name"));
        const juce::String n (p->song.blocks[(size_t) block].name);
        if (done) done (n.isEmpty() ? "BLOCK " + num + ": NO NAME" : "BLOCK " + num + " IS \"" + n + "\"");
    }), true);
}

void setBlockName (PaulaProcessor& proc, int block, const juce::String& name)
{
    block = proc.song.safeBlock (block);
    const auto n = name.toUpperCase().trim().substring (0, 24).toStdString();
    if (n == proc.song.blocks[(size_t) block].name) return;
    proc.history.block (block, "BLOCK NAME");
    proc.withLock ([&] { proc.song.blocks[(size_t) block].name = n; });
}

// =========================================================================== Block
BlockPanel::BlockPanel (PaulaProcessor& p, std::function<void()> c)
    : proc (p), changed (std::move (c))
{
    auto& st = proc.editorState;
    current.format = [] (int v) { return juce::String (v).paddedLeft ('0', 3); };
    current.onChange = [this, &st] (int v)
    {
        st.block = proc.song.safeBlock (v);
        st.line = std::min (st.line, proc.song.blocks[(size_t) st.block].lines - 1);
        changed();
    };
    lines.onChange = [this, &st] (int v)
    {
        proc.history.block (st.block, "BLOCK LINES", true);
        proc.withLock ([&] { proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].lines = juce::jlimit (1, kMaxLines, v); });
        st.line = std::min (st.line, v - 1);
        changed();
    };
    newBtn.onClick = [this, &st]
    {
        if (proc.song.blocks.size() >= (size_t) kMaxBlocks) return;
        if (pushStructUndo) pushStructUndo ("NEW BLOCK");
        proc.withLock ([&] { proc.song.blocks.push_back (Block{}); });
        st.block = (int) proc.song.blocks.size() - 1;
        st.line = 0;
        changed();
    };
    cloneBtn.onClick = [this, &st]
    {
        if (proc.song.blocks.size() >= (size_t) kMaxBlocks) return;
        if (pushStructUndo) pushStructUndo ("CLONE BLOCK");
        proc.withLock ([&] { proc.song.blocks.push_back (proc.song.blocks[(size_t) proc.song.safeBlock (st.block)]); });
        st.block = (int) proc.song.blocks.size() - 1;
        changed();
    };
    delBtn.onClick = [this, &st]
    {
        if (proc.song.blocks.size() <= 1) return;
        if (pushStructUndo) pushStructUndo ("DELETE BLOCK");
        const int del = proc.song.safeBlock (st.block);
        proc.withLock ([&] {
            proc.song.blocks.erase (proc.song.blocks.begin() + del);
            for (auto& s : proc.song.sequence)
                if (s > del) --s;
                else if (s == del) s = std::max (0, del - 1);
        });
        st.block = std::max (0, del - 1);
        st.line = std::min (st.line, proc.song.blocks[(size_t) st.block].lines - 1);
        changed();
    };
    auto swapWith = [this, &st] (int delta)
    {
        const int a = proc.song.safeBlock (st.block), b = a + delta;
        if (b < 0 || b >= (int) proc.song.blocks.size()) return;
        if (pushStructUndo) pushStructUndo ("CHANGE BLOCK NUMBER");
        proc.withLock ([&] { swapBlockNumbers (proc.song, a, b); });
        st.block = b;
        changed();
    };
    numDownBtn.onClick = [swapWith] { swapWith (-1); };
    numUpBtn.onClick = [swapWith] { swapWith (1); };
    doubleBtn.onClick = [this] { if (onDouble) onDouble(); };
    halveBtn.onClick = [this] { if (onHalve) onHalve(); };
    for (auto* c2 : std::initializer_list<juce::Component*> { &doubleBtn, &halveBtn, &current, &lines, &newBtn, &cloneBtn, &delBtn, &countLabel, &numDownBtn, &numUpBtn })
        addAndMakeVisible (c2);
}

void BlockPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showBlockMenu();
}

void BlockPanel::showBlockMenu()
{
    const int b = proc.song.safeBlock (proc.editorState.block);
    juce::Component::SafePointer<BlockPanel> safe (this);
    auto act = [safe] (std::function<void (BlockPanel&)> f) { return [safe, f] { if (safe != nullptr) f (*safe); }; };
    showMenu ("BLOCK " + juce::String (b).paddedLeft ('0', 3), {
        { "COPY BLOCK", act ([] (BlockPanel& p) { if (p.onCopyBlock) p.onCopyBlock (false); }) },
        { "CUT BLOCK", act ([] (BlockPanel& p) { if (p.onCopyBlock) p.onCopyBlock (true); }) },
        { "PASTE BLOCK", act ([] (BlockPanel& p) { if (p.onPasteBlock) p.onPasteBlock(); }) },
        { "CLEAR BLOCK", act ([b] (BlockPanel& p)
            {
                p.proc.history.block (b, "CLEAR BLOCK");
                p.proc.withLock ([&] { auto& blk = p.proc.song.blocks[(size_t) b]; for (int l = 0; l < blk.lines; ++l) for (int t = 0; t < kMaxTracks; ++t) blk.at (l, t) = Cell {}; });
                p.changed();
            }) },
        { "DUPLICATE BLOCK", act ([] (BlockPanel& p) { p.cloneBtn.onClick(); }) },
        { "NEW BLOCK", act ([] (BlockPanel& p) { p.newBtn.onClick(); }) },
        { "DELETE BLOCK", act ([] (BlockPanel& p) { p.delBtn.onClick(); }) },
        { "DOUBLE LENGTH", act ([] (BlockPanel& p) { if (p.onDouble) p.onDouble(); }) },
        { "HALVE LENGTH", act ([] (BlockPanel& p) { if (p.onHalve) p.onHalve(); }) },
    });
}

void BlockPanel::refresh()
{
    auto& st = proc.editorState;
    current.setRange (0, (int) proc.song.blocks.size() - 1);
    current.setValue (proc.song.safeBlock (st.block));
    lines.setValue (proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].lines);
    countLabel.setText ("OF " + juce::String ((int) proc.song.blocks.size()).paddedLeft ('0', 3));
    repaint();   // (the title shows the block's name)
}

void BlockPanel::paint (juce::Graphics& g)
{
    // the title carries the block's name when it has one and it fits
    const auto nm = juce::String (proc.song.blocks[(size_t) proc.song.safeBlock (proc.editorState.block)].name).toUpperCase().trim();
    juce::String title = "BLOCK";
    if (nm.isNotEmpty() && PixelFont::textWidth ("BLOCK: " + nm, Theme::px) < getWidth() - Theme::px * 24) title = "BLOCK: " + nm;
    Theme::panelBox (g, getLocalBounds().toFloat(), title);
}

void BlockPanel::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    auto row1 = r.removeFromTop (rowH);
    countLabel.setBounds (row1.removeFromRight ((int) (px * 6 * 7)));
    row1.removeFromRight ((int) (px * 3));
    current.setBounds (row1);
    r.removeFromTop ((int) px * 2);
    auto row2 = r.removeFromTop (rowH);
    const int sb = (int) (px * 6 * 3 + px * 4);
    halveBtn.setBounds (row2.removeFromRight (sb)); row2.removeFromRight ((int) px);
    doubleBtn.setBounds (row2.removeFromRight (sb)); row2.removeFromRight ((int) px * 2);
    lines.setBounds (row2);
    r.removeFromTop ((int) px * 2);
    auto row3 = r.removeFromTop (rowH);
    const int bw = (row3.getWidth() - (int) px * 4) / 5;
    for (auto* b : { &newBtn, &cloneBtn, &delBtn, &numDownBtn, &numUpBtn })
    {
        b->setBounds (row3.removeFromLeft (bw));
        row3.removeFromLeft ((int) px);
    }
}

// =========================================================================== Wave view
int WaveView::sampleAt (float x) const
{
    const auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
    const float w = (float) std::max (1, getWidth() - 4);
    return juce::jlimit (0, (int) in.data.size(), (int) std::lround ((x - 2.0f) / w * (float) in.data.size()));
}

void WaveView::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::skin.bg);
    g.fillRect (r);
    const auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
    auto inner = r.reduced (2.0f);
    if (! in.hasData())
    {
        PixelFont::drawCentred (g, openSample ? "NO SAMPLE - CLICK TO RECORD" : "NO SAMPLE", inner, px, Theme::skin.dim);
        Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f));
        return;
    }
    const float n = (float) in.data.size();
    if (in.isLooped())
    {
        const float x1 = inner.getX() + inner.getWidth() * in.loopStart / n;
        const float x2 = inner.getX() + inner.getWidth() * (in.loopStart + in.loopLength) / n;
        g.setColour (Theme::skin.stripe);
        g.fillRect (x1, inner.getY(), x2 - x1, inner.getHeight());
        g.setColour (Theme::skin.track[0]);
        g.fillRect (x1, inner.getY(), std::max (1.0f, px * 0.5f), inner.getHeight());
        g.fillRect (x2 - std::max (1.0f, px * 0.5f), inner.getY(), std::max (1.0f, px * 0.5f), inner.getHeight());
    }
    g.setColour (Theme::skin.text);
    const float mid = inner.getCentreY();
    g.fillRect (inner.getX(), mid, inner.getWidth(), 1.0f);
    const int cols = (int) inner.getWidth();
    for (int x = 0; x < cols; ++x)
    {
        const int a = (int) ((int64_t) x * (int64_t) in.data.size() / cols);
        const int b = std::max (a + 1, (int) ((int64_t) (x + 1) * (int64_t) in.data.size() / cols));
        int lo = 32767, hi = -32768;
        for (int i = std::max (0, a - 1); i < b && i < (int) in.data.size(); ++i) { lo = std::min (lo, (int) in.data[(size_t) i]); hi = std::max (hi, (int) in.data[(size_t) i]); }
        const float y1 = mid - hi / 32768.0f * inner.getHeight() * 0.48f;
        const float y2 = mid - lo / 32768.0f * inner.getHeight() * 0.48f;
        g.fillRect (inner.getX() + x, y1, 1.0f, std::max (1.0f, y2 - y1));
    }
    const juce::String rateText = juce::String (juce::roundToInt (in.rate)) + "HZ" + (in.isStereo() ? " ST" : "");
    {
        auto box = juce::Rectangle<float> (PixelFont::textWidth (rateText, px) + px * 2, px * 9)
                       .withPosition (r.getRight() - PixelFont::textWidth (rateText, px) - px * 5, r.getY() + px);
        g.setColour (Theme::skin.bg.withAlpha (0.85f));
        g.fillRect (box);
        PixelFont::drawCentred (g, rateText, box, px, Theme::skin.text);
    }
    Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f));
}

void WaveView::mouseDoubleClick (const juce::MouseEvent&) { if (openEditor) openEditor(); }

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    const auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
    dragging = 0;
    if (! in.hasData() && openSample && ! e.mods.isPopupMenu()) { openSample(); return; }
    if (! in.isLooped()) return;
    const float w = (float) std::max (1, getWidth() - 4);
    const float n = (float) in.data.size();
    const float xs = 2.0f + w * in.loopStart / n;
    const float xe = 2.0f + w * (in.loopStart + in.loopLength) / n;
    dragging = std::abs (e.position.x - xs) < std::abs (e.position.x - xe) ? 1 : 2;
    proc.history.instrument (proc.editorState.instrument, "LOOP POINTS");
    mouseDrag (e);
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == 0) return;
    const int s = sampleAt (e.position.x);
    proc.withLock ([&] {
        auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
        int start = in.loopStart, end = in.loopStart + in.loopLength;
        if (dragging == 1) start = juce::jlimit (0, end - 4, s);
        else end = juce::jlimit (start + 4, (int) in.data.size(), s);
        in.loopStart = start; in.loopLength = end - start;
    });
    if (changed) changed();
}

// =========================================================================== Instruments
InstrumentPanel::InstrumentPanel (PaulaProcessor& p, std::function<void()> c, std::function<void (const juce::String&)> s)
    : proc (p), changed (std::move (c)), status (std::move (s)), wave (p)
{
    wave.openSample = [this] { if (openSample) openSample(); };
    auto& st = proc.editorState;
    list.getNumRows = [] { return kMaxInstruments; };
    list.getRowText = [this] (int r)
    {
        const auto& in = proc.song.instruments[(size_t) r];
        const juce::String hex = juce::String::toHexString (r + 1).toUpperCase().paddedLeft ('0', 2)
                               + (proc.player.instrumentIsOn (r) || (! in.hasData() && ! in.isPlugin()) ? " " : " OFF ");   // switched off
        if (in.isPlugin())   // a plug-in / MIDI-out instrument: a note mark, and "!" when it isn't loaded
            return hex + juce::String::charToString ((juce::juce_wchar) 6) + (proc.pluginLoadedInstrument (r) ? "" : "!")
                 + (in.name.empty() ? juce::String (in.pluginName) : juce::String (in.name));
        return hex + (in.name.empty() ? (in.hasData() ? juce::String ("(NO NAME)") : juce::String ("-")) : juce::String (in.name));
    };
    list.isRowDim = [this] (int r)
    {
        const auto& in = proc.song.instruments[(size_t) r];
        return (! in.hasData() && ! in.isPlugin()) || ! proc.player.instrumentIsOn (r);
    };
    list.onClick = [this, &st] (int r, const juce::MouseEvent& e)
    {
        st.instrument = r;
        proc.jamInstrument = r;
        if (e.mods.isPopupMenu()) { changed(); showInstrumentMenu (r); return; }
        if (e.mods.isAltDown()) proc.withLock ([&] { proc.player.jamNoteOn (37, r); });
        changed();
    };
    list.onDoubleClick = [this] (int) { nameBtn.triggerClick(); };
    addAndMakeVisible (list);

    loadBtn.onClick = [this] { if (openBrowser) openBrowser(); else loadSample(); };
    editBtn.onClick = [this] { if (openSampleEditor) openSampleEditor(); };
    paramsBtn.onClick = [this] { if (openParams) openParams(); };
    clearBtn.onClick = [this]
    {
        Instrument empty;
        const int idx = proc.editorState.instrument;
        proc.history.instrument (idx, "CLEAR INSTRUMENT");
        proc.withLock ([&]
        {
            std::swap (proc.song.instruments[(size_t) idx], empty);
            for (auto& sl : proc.song.mixer.inst[idx]) sl = FxSlot {};   // (and its own inserts)
            proc.player.jamAllOff();
        });
        proc.updateMixerEffects();
        changed();
    };
    nameBtn.onClick = [this]
    {
        const int idx = proc.editorState.instrument;
        auto* w = new juce::AlertWindow ("Instrument name", "Name for instrument " + juce::String (idx + 1), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", juce::String (proc.song.instruments[(size_t) idx].name));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<InstrumentPanel> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, idx] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            const auto name = w->getTextEditorContents ("name").toUpperCase().substring (0, 22).toStdString();
            safe->proc.history.instrument (idx, "INSTRUMENT NAME");
            safe->proc.withLock ([&] { safe->proc.song.instruments[(size_t) idx].name = name; });
            safe->changed();
        }), true);
    };

    vol.onChange   = [this] (int v) { editInstrument ("INSTRUMENT VOLUME", [v] (Instrument& in) { in.volume = v; }); };
    fine.onChange  = [this] (int v) { editInstrument ("FINETUNE", [v] (Instrument& in) { in.finetune = v; }); };
    trans.onChange = [this] (int v) { editInstrument ("TRANSPOSE", [v] (Instrument& in) { in.transpose = v; }); };
    loopBtn.onClick = [this]
    {
        const bool on = loopBtn.getToggleState();
        editInstrument ("LOOP", [on] (Instrument& in)
        {
            if (on && in.hasData()) { in.loopStart = 0; in.loopLength = (int) in.data.size(); }
            else { in.loopStart = 0; in.loopLength = 0; }
        });
    };
    loopStart.onChange = [this] (int v) { editInstrument ("LOOP START", [v] (Instrument& in) {
        if (! in.isLooped()) return;
        const int end = in.loopStart + in.loopLength;
        in.loopStart = juce::jlimit (0, end - 4, v); in.loopLength = end - in.loopStart; }); };
    loopLen.onChange = [this] (int v) { editInstrument ("LOOP LENGTH", [v] (Instrument& in) {
        if (! in.isLooped()) return;
        in.loopLength = juce::jlimit (4, (int) in.data.size() - in.loopStart, v); }); };
    loopStart.labelChars = loopLen.labelChars = 5;
    loopStart.valueChars = loopLen.valueChars = 6;
    vol.labelChars = fine.labelChars = trans.labelChars = 4;
    wave.changed = [this] { changed(); };
    wave.openEditor = [this] { if (openSampleEditor) openSampleEditor(); };

    for (auto* comp : std::initializer_list<juce::Component*> { &loadBtn, &editBtn, &paramsBtn, &clearBtn, &nameBtn, &loopBtn, &vol, &fine, &trans, &loopStart, &loopLen, &wave })
        addAndMakeVisible (comp);
}

template <typename Fn> void InstrumentPanel::editInstrument (const juce::String& what, Fn&& fn)
{
    proc.history.instrument (proc.editorState.instrument, what, true);
    proc.withLock ([&] { fn (proc.song.instruments[(size_t) proc.editorState.instrument]); });
    changed();
}

void InstrumentPanel::loadSample()
{
    auto& st = proc.editorState;
    juce::File start = st.lastFolder.isNotEmpty() ? juce::File (st.lastFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into instrument " + juce::String (st.instrument + 1), start,
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.8svx;*.iff;*.svx");
    juce::Component::SafePointer<InstrumentPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto file = fc.getResult();
        if (! file.existsAsFile()) return;
        auto& self = *safe;
        self.proc.editorState.lastFolder = file.getParentDirectory().getFullPathName();
        Instrument in;
        juce::String err;
        if (! importSample (file, in, err)) { self.status ("ERROR: " + err); return; }
        const int idx = self.proc.editorState.instrument;
        self.proc.history.instrument (idx, "LOAD SAMPLE");
        self.proc.withLock ([&] { self.proc.player.jamAllOff(); std::swap (self.proc.song.instruments[(size_t) idx], in); });
        self.status ("LOADED " + file.getFileName().toUpperCase() + " INTO " + juce::String::toHexString (idx + 1).toUpperCase().paddedLeft ('0', 2));
        self.changed();
    });
}

void InstrumentPanel::refresh()
{
    auto& st = proc.editorState;
    st.instrument = juce::jlimit (0, kMaxInstruments - 1, st.instrument);
    list.selected = st.instrument;
    list.ensureVisible (st.instrument);
    const auto& in = proc.song.instruments[(size_t) st.instrument];
    vol.setValue (in.volume);
    fine.setValue (in.finetune);
    trans.setValue (in.transpose);
    loopBtn.setToggleState (in.isLooped(), juce::dontSendNotification);
    const int len = std::max (4, (int) in.data.size());
    loopStart.setRange (0, len);
    loopLen.setRange (0, len);
    loopStart.setValue (in.isLooped() ? in.loopStart : 0);
    loopLen.setValue (in.isLooped() ? in.loopLength : 0);
    loopStart.setEnabled (in.isLooped());
    loopLen.setEnabled (in.isLooped());
    list.repaint();
    wave.repaint();
}

void InstrumentPanel::paint (juce::Graphics& g) { Theme::panelBox (g, getLocalBounds().toFloat(), "INSTRUMENTS"); }

void InstrumentPanel::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    const int gap = (int) (px * 2);

    // bottom up: wave, params, buttons. (Exact loop points are set in the sample
    // editor now; the loop markers can still be dragged in the little waveform.)
    wave.setBounds (r.removeFromBottom ((int) (px * 14)));
    r.removeFromBottom (gap);
    loopStart.setVisible (false);
    loopLen.setVisible (false);
    auto pRow2 = r.removeFromBottom (rowH);
    trans.setBounds (pRow2.removeFromLeft (pRow2.getWidth() / 2 - (int) px));
    pRow2.removeFromLeft ((int) px * 2);
    loopBtn.setBounds (pRow2);
    r.removeFromBottom (gap);
    auto pRow1 = r.removeFromBottom (rowH);
    vol.setBounds (pRow1.removeFromLeft (pRow1.getWidth() / 2 - (int) px));
    pRow1.removeFromLeft ((int) px * 2);
    fine.setBounds (pRow1);
    r.removeFromBottom (gap);
    auto bRow = r.removeFromBottom (rowH);
    {
        // widths follow the label lengths so every label stays full size
        const std::pair<PixButton*, int> btns[] = { { &loadBtn, 4 }, { &editBtn, 4 }, { &paramsBtn, 6 }, { &clearBtn, 5 }, { &nameBtn, 4 } };
        const int total = 4 + 4 + 6 + 5 + 4 + 5 * 2;
        const int avail = bRow.getWidth() - (int) px * 4;
        for (auto& [b, chars] : btns)
        {
            b->setBounds (bRow.removeFromLeft (avail * (chars + 2) / total));
            bRow.removeFromLeft ((int) px);
        }
    }
    r.removeFromBottom (gap);
    list.setBounds (r);
}

// ---- right-click on an instrument
namespace { Instrument instrumentClip; FxSlot instrumentFxClip[kInserts]; bool haveInstrumentClip = false; }

void InstrumentPanel::showInstrumentMenu (int r)
{
    const auto name = juce::String::toHexString (r + 1).toUpperCase().paddedLeft ('0', 2);
    juce::Component::SafePointer<InstrumentPanel> safe (this);
    auto put = [safe] (int idx, const Instrument& in, const FxSlot* fx, const juce::String& what)
    {
        if (safe == nullptr) return;
        auto& self = *safe;
        Instrument copy = in;
        FxSlot fxCopy[kInserts];
        for (int k = 0; k < kInserts; ++k) fxCopy[k] = fx[k];   // (its own inserts go with it)
        self.proc.history.instrument (idx, what);
        self.proc.withLock ([&]
        {
            self.proc.player.jamAllOff();
            std::swap (self.proc.song.instruments[(size_t) idx], copy);
            for (int k = 0; k < kInserts; ++k) std::swap (self.proc.song.mixer.inst[idx][k], fxCopy[k]);
        });
        self.proc.updateMixerEffects();   // (plug-in instruments: load / drop their plug-in)
        self.changed();
    };
    showMenu ("INSTRUMENT " + name, {
        { "COPY", [safe, r] { if (safe == nullptr) return; instrumentClip = safe->proc.song.instruments[(size_t) r];
                               for (int k = 0; k < kInserts; ++k) instrumentFxClip[k] = safe->proc.song.mixer.inst[r][k];
                               haveInstrumentClip = true;
                               safe->status ("INSTRUMENT " + juce::String::toHexString (r + 1).toUpperCase().paddedLeft ('0', 2) + " COPIED"); } },
        { "PASTE", [safe, r, put] { if (safe == nullptr) return; if (! haveInstrumentClip) { safe->status ("NOTHING COPIED YET"); return; }
                                    put (r, instrumentClip, instrumentFxClip, "PASTE INSTRUMENT"); safe->status ("PASTED INTO INSTRUMENT " + juce::String::toHexString (r + 1).toUpperCase().paddedLeft ('0', 2)); } },
        { "DUPLICATE TO NEXT FREE", [safe, r, put]
            {
                if (safe == nullptr) return;
                auto& song = safe->proc.song;
                for (int i = 0; i < kMaxInstruments; ++i)
                {
                    const auto& in = song.instruments[(size_t) i];
                    if (i == r || in.hasData() || in.isPlugin() || ! in.name.empty()) continue;
                    FxSlot fx[kInserts];
                    for (int k = 0; k < kInserts; ++k) fx[k] = song.mixer.inst[r][k];
                    put (i, song.instruments[(size_t) r], fx, "DUPLICATE INSTRUMENT");
                    safe->proc.editorState.instrument = i;
                    safe->status ("DUPLICATED TO INSTRUMENT " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2));
                    safe->changed();
                    return;
                }
                safe->status ("NO FREE INSTRUMENT SLOT");
            } },
        { "CLEAR", [safe] { if (safe != nullptr) safe->clearBtn.onClick(); } },
        { "RENAME...", [safe] { if (safe != nullptr) safe->nameBtn.onClick(); } },
        { "LOAD SAMPLE...", [safe] { if (safe != nullptr) safe->loadBtn.onClick(); } },
        { "SAMPLE (RECORD AUDIO)...", [safe] { if (safe != nullptr && safe->openSample) safe->openSample(); } },
        { "PLAY C-3", [safe, r] { if (safe != nullptr) safe->proc.withLock ([&] { safe->proc.player.jamNoteOn (37, r); }); } },
    });
}
} // namespace prv::ui
