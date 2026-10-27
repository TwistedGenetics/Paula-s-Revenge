// Paula's Revenge - full-size song arrangement page
#include "ArrangePage.h"
#include "Panels.h"
#include "../engine/SongOps.h"

namespace prv::ui
{
ArrangePage::ArrangePage (PaulaProcessor& p) : proc (p)
{
    ops.onDone = [this] (const juce::String& m) { changed (m); };
    setWantsKeyboardFocus (true);
    setOpaque (true);
    upBtn.onClick     = [this] { moveSelection (-1); };
    downBtn.onClick   = [this] { moveSelection (1); };
    copyBtn.onClick   = [this] { copySelection(); };
    cutBtn.onClick    = [this] { cutSelection(); };
    pasteBtn.onClick  = [this] { paste(); };
    repeatBtn.onClick = [this] { repeatSelection(); };
    insertBtn.onClick = [this] { insertBlock (blockBox.getValue()); };
    setBtn.onClick    = [this] { setBlock (blockBox.getValue()); };
    deleteBtn.onClick = [this] { deleteSelection(); };
    undoBtn.onClick   = [this] { proc.history.undo(); opened(); };
    redoBtn.onClick   = [this] { proc.history.redo(); opened(); };
    closeBtn.onClick  = [this] { if (onClose) onClose(); };
    sectionBtn.onClick = [this]
    {
        juce::Component::SafePointer<ArrangePage> safe (this);
        editSectionDialog (proc, selStart(), [safe] (const juce::String& m) { if (safe != nullptr) safe->changed (m); });
    };
    loopSectBtn.onClick = [this]
    {
        proc.editorState.sectionLoop = loopSectBtn.getToggleState();
        syncSectionLoop (proc);
        changed (proc.editorState.sectionLoop ? "LOOPING THE SELECTED POSITION'S SECTION" : "SECTION LOOP OFF");
    };
    blockBox.format = [] (int v) { return juce::String (v).paddedLeft ('0', 3); };
    blockBox.labelChars = 6;
    for (auto* c : std::initializer_list<juce::Component*> { &upBtn, &downBtn, &copyBtn, &cutBtn, &pasteBtn, &repeatBtn,
                                                             &insertBtn, &blockBox, &setBtn, &deleteBtn, &undoBtn, &redoBtn, &closeBtn, &sectionBtn, &loopSectBtn })
        addAndMakeVisible (c);
}

void ArrangePage::opened()
{
    const int n = (int) proc.song.sequence.size();
    juce::ignoreUnused (n);
    ops.clamp();   // (keeps a highlight made in the sequence panel)
    blockBox.setRange (0, (int) proc.song.blocks.size() - 1);
    blockBox.setValue (proc.song.sequence[(size_t) cursorPos()]);
    ensureVisible();
    grabKeyboardFocus();
    repaint();
}

void ArrangePage::tick()
{
    if (proc.player.isPlaying()) repaint (listArea.toNearestInt());
}

// ------------------------------------------------------------------ editing
void ArrangePage::changed (const juce::String& status)
{
    auto& st = proc.editorState;
    const int n = (int) proc.song.sequence.size();
    juce::ignoreUnused (n);
    ops.clamp();
    st.block = proc.song.safeBlock (proc.song.sequence[(size_t) cursorPos()]);
    syncSectionLoop (proc);
    loopSectBtn.setToggleState (st.sectionLoop, juce::dontSendNotification);
    blockBox.setRange (0, (int) proc.song.blocks.size() - 1);
    blockBox.setValue (proc.song.sequence[(size_t) cursorPos()]);
    ensureVisible();
    repaint();
    if (onChanged) onChanged();
    if (onStatus && status.isNotEmpty()) onStatus (status);
}

void ArrangePage::select (int from, int to) { ops.select (from, to); changed ({}); }
void ArrangePage::moveSelection (int delta)       { ops.moveBy (delta); }
void ArrangePage::moveSelectionTo (int insertBefore) { ops.moveTo (insertBefore); }
void ArrangePage::copySelection()                 { ops.copy(); }
void ArrangePage::cutSelection()                  { ops.cut(); }
void ArrangePage::paste()                         { ops.paste(); }
void ArrangePage::repeatSelection()               { ops.duplicate(); }
void ArrangePage::deleteSelection()               { ops.remove(); }
void ArrangePage::insertBlock (int blk)           { ops.insertBlock (blk); }
void ArrangePage::setBlock (int blk)              { ops.setBlock (blk); }

// ------------------------------------------------------------------ layout / paint
void ArrangePage::resized()
{
    const float px = Theme::px;
    const int rowHgt = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    auto r = getLocalBounds().reduced (gap);
    titleArea = r.removeFromTop (rowHgt).toFloat();
    r.removeFromTop (gap);
    auto row3 = r.removeFromBottom (rowHgt); r.removeFromBottom (gap);
    auto row2 = r.removeFromBottom (rowHgt); r.removeFromBottom (gap);
    auto row1 = r.removeFromBottom (rowHgt); r.removeFromBottom (gap * 2);
    {
        // widths follow the labels so DUPLICATE fits at full size
        std::vector<PixButton*> bs { &upBtn, &downBtn, &copyBtn, &cutBtn, &pasteBtn, &repeatBtn };
        float total = 0; for (auto* b : bs) total += (float) b->getButtonText().length() + 2.0f;
        const float unit = (row1.getWidth() - gap * 5) / total;
        for (auto* b : bs)
        {
            const int w = b == bs.back() ? row1.getWidth() : (int) (unit * ((float) b->getButtonText().length() + 2.0f));
            b->setBounds (row1.removeFromLeft (w)); row1.removeFromLeft (gap);
        }
    }
    {
        const int w = (row2.getWidth() - gap * 3) / 5;
        insertBtn.setBounds (row2.removeFromLeft (w)); row2.removeFromLeft (gap);
        blockBox.setBounds (row2.removeFromLeft (w * 2)); row2.removeFromLeft (gap);
        setBtn.setBounds (row2.removeFromLeft (w)); row2.removeFromLeft (gap);
        deleteBtn.setBounds (row2);
    }
    {
        // LOOP SECTION needs the most room
        const float unit = (row3.getWidth() - gap * 4) / 6.6f;
        sectionBtn.setBounds (row3.removeFromLeft ((int) (unit * 1.3f))); row3.removeFromLeft (gap);
        loopSectBtn.setBounds (row3.removeFromLeft ((int) (unit * 2.2f))); row3.removeFromLeft (gap);
        undoBtn.setBounds (row3.removeFromLeft ((int) unit)); row3.removeFromLeft (gap);
        redoBtn.setBounds (row3.removeFromLeft ((int) unit)); row3.removeFromLeft (gap);
        closeBtn.setBounds (row3);
    }
    listArea = r.toFloat();
}

float ArrangePage::tagX() const
{
    return listArea.getRight() - (PixelFont::textWidth ("WWWWWWWWWWWW", Theme::px) + Theme::px * 4);
}

int ArrangePage::rowAt (float y) const { return scroll + (int) std::floor ((y - listArea.getY()) / rowH()); }

void ArrangePage::ensureVisible()
{
    const int cur = cursorPos();
    if (cur < scroll) scroll = cur;
    else if (cur >= scroll + visibleRows()) scroll = cur - visibleRows() + 1;
    scroll = juce::jlimit (0, std::max (0, (int) proc.song.sequence.size() - visibleRows()), scroll);
}

void ArrangePage::paintPreview (juce::Graphics& g, juce::Rectangle<float> area, int blk)
{
    // one lane per track; a mark wherever a note starts
    const auto& sk = Theme::skin;
    const auto& b = proc.song.blocks[(size_t) proc.song.safeBlock (blk)];
    const int nt = proc.song.numTracks;
    g.setColour (sk.bg.brighter (0.05f));
    g.fillRect (area);
    const float laneH = area.getHeight() / (float) nt;
    const float colW = area.getWidth() / (float) std::max (1, b.lines);
    for (int t = 0; t < nt; ++t)
    {
        g.setColour (sk.track[t]);
        for (int l = 0; l < b.lines; ++l)
        {
            const auto& c = b.at (l, t);
            if (c.note > 0 && c.note <= kNoteMax)
                g.fillRect (area.getX() + l * colW, area.getY() + t * laneH + 1.0f, std::max (1.5f, colW * 0.8f), std::max (1.0f, laneH - 2.0f));
        }
    }
    Theme::bevel (g, area, false, 1.0f);
}

void ArrangePage::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    g.fillAll (Theme::panel);
    const juce::String title = "ARRANGE   SONG LENGTH " + juce::String ((int) proc.song.sequence.size())
                             + "   BLOCKS " + juce::String ((int) proc.song.blocks.size());
    PixelFont::drawLeft (g, title, titleArea, px, Theme::ink);

    g.setColour (sk.bg);
    g.fillRect (listArea);
    const int n = (int) proc.song.sequence.size();
    const float rh = rowH();
    const int playPos = proc.player.getMode() == Player::Mode::Song ? proc.player.uiSeqPos.load() : -1;
    g.saveState();
    g.reduceClipRegion (listArea.toNearestInt());
    for (int i = 0; i <= visibleRows(); ++i)
    {
        const int row = scroll + i;
        if (row >= n) break;
        auto rr = juce::Rectangle<float> (listArea.getX(), listArea.getY() + i * rh, listArea.getWidth(), rh);
        const bool sel = row >= selStart() && row <= selEnd();
        if (sel) { g.setColour (sk.band); g.fillRect (rr); }
        if (row == cursorPos()) { g.setColour (sk.text); g.drawRect (rr.reduced (1.0f), std::max (1.0f, px * 0.75f)); }
        const int blk = proc.song.sequence[(size_t) row];
        const float ty = std::round (rr.getCentreY() - 3.5f * px);
        if (row == playPos) PixelFont::draw (g, juce::String::charToString ((juce::juce_wchar) 1), rr.getX() + px * 2, ty, px, sk.hot);
        PixelFont::draw (g, juce::String (row).paddedLeft ('0', 3) + "   BLOCK " + juce::String (blk).paddedLeft ('0', 3),
                         rr.getX() + px * 9, ty, px, sk.text);
        const float textW = px * PixelFont::kCellW * 19 + px * 12;
        // section: divider line above the row and its name at the right
        const int si = sectionAt (proc.song.sections, row);
        const juce::String tag = si >= 0 ? juce::String (proc.song.sections[(size_t) si].name).toUpperCase() : juce::String();
        const float tagW = PixelFont::textWidth ("WWWWWWWWWWWW", px) + px * 4;   // room for 12 characters
        paintPreview (g, rr.withTrimmedLeft (textW).withTrimmedRight (tagW).reduced (px * 2, px * 2), blk);
        if (proc.editorState.sectionLoop && row >= proc.player.songLoopStart() && row <= proc.player.songLoopEnd())
        {
            g.setColour (sk.hot);
            g.fillRect (rr.getX() + px * 6.5f, rr.getY(), px * 1.5f, rh);
        }
        if (si >= 0)
        {
            g.setColour (sk.hot);
            g.fillRect (rr.getX(), rr.getY(), rr.getWidth(), std::max (1.5f, px));
            juce::String t = tag;
            while (t.length() > 1 && PixelFont::textWidth (t, px) > tagW - px * 2) t = t.dropLastCharacters (1);
            PixelFont::draw (g, t, rr.getRight() - PixelFont::textWidth (t, px) - px * 2, ty, px, sk.hot);
        }
    }
    // drop marker while dragging
    if (dragging && ! selecting && dropAt >= 0)
    {
        const float y = listArea.getY() + (dropAt - scroll) * rh;
        g.setColour (sk.hot);
        g.fillRect (listArea.getX(), y - px, listArea.getWidth(), px * 2);
    }
    g.restoreState();
    Theme::bevel (g, listArea, false, std::max (1.0f, px * 0.5f));

    if (hasKeyboardFocus (true))
    {
        g.setColour (Theme::ink);
        g.drawRect (getLocalBounds().toFloat(), std::max (2.0f, px * 0.5f));
    }
}

// ------------------------------------------------------------------ mouse & keys
void ArrangePage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! listArea.contains (e.position)) return;
    const int row = rowAt (e.position.y);
    const int n = (int) proc.song.sequence.size();
    if (row < 0 || row >= n) return;
    if (e.position.x >= tagX())
    {
        // the label zone at the right: add / rename / remove a section here
        juce::Component::SafePointer<ArrangePage> safe (this);
        editSectionDialog (proc, row, [safe] (const juce::String& m) { if (safe != nullptr) safe->changed (m); });
        return;
    }
    selecting = false;
    if (e.mods.isShiftDown()) { ops.select (ops.anchor(), row); changed ({}); return; }
    if (row >= selStart() && row <= selEnd()) { dragFrom = row; return; }   // drag the highlight to move it
    // a new position: dragging from here highlights a range
    ops.select (row, row);
    dragFrom = row;
    selecting = true;
    changed ({});
}

void ArrangePage::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! dragging && e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    lastDragY = e.position.y;
    auto place = [this]
    {
        const float y = juce::jlimit (listArea.getY(), listArea.getBottom() - 1.0f, lastDragY);
        if (selecting)
        {
            // highlighting: from where the drag started to the row under the mouse
            const int row = juce::jlimit (0, (int) proc.song.sequence.size() - 1, rowAt (y));
            if (row != cursorPos()) { ops.select (dragFrom, row); changed ({}); }
            return;
        }
        const float rel = (y - listArea.getY()) / rowH();
        dropAt = juce::jlimit (0, (int) proc.song.sequence.size(), scroll + juce::roundToInt (rel));
        repaint();
    };
    // held past the top or bottom: scroll at a steady pace
    edgeScroll.onStep = [this, place] (int dir)
    {
        scroll = juce::jlimit (0, std::max (0, (int) proc.song.sequence.size() - visibleRows()), scroll + dir);
        place();
    };
    edgeScroll.update (e.position.y < listArea.getY() ? e.position.y - listArea.getY()
                         : (e.position.y > listArea.getBottom() ? e.position.y - listArea.getBottom() : 0.0f), rowH());
    place();
}

void ArrangePage::mouseUp (const juce::MouseEvent&)
{
    edgeScroll.stop();
    if (dragging && ! selecting && dropAt >= 0) moveSelectionTo (dropAt);
    dragging = selecting = false; dragFrom = dropAt = -1;
    repaint();
}

void ArrangePage::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click a position to edit its block in the pattern
    if (listArea.contains (e.position) && rowAt (e.position.y) < (int) proc.song.sequence.size() && onClose) onClose();
}

void ArrangePage::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int n = wheel (w);
    if (n == 0) return;
    scroll = juce::jlimit (0, std::max (0, (int) proc.song.sequence.size() - visibleRows()), scroll + n);
    repaint();
}

bool ArrangePage::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    const int n = (int) proc.song.sequence.size();
    if (m.isCommandDown())
    {
        const auto ch = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        if (ch == 'c') { copySelection(); return true; }
        if (ch == 'x') { cutSelection(); return true; }
        if (ch == 'v') { paste(); return true; }
        if (ch == 'd') { repeatSelection(); return true; }
        if (ch == 'a') { select (0, n - 1); return true; }
        if (ch == 'z') { if (m.isShiftDown()) proc.history.redo(); else proc.history.undo(); opened(); return true; }
        return false;
    }
    if (m.isAltDown() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)) { moveSelection (code == juce::KeyPress::upKey ? -1 : 1); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const int c = juce::jlimit (0, n - 1, cursorPos() + (code == juce::KeyPress::upKey ? -1 : 1));
        ops.select (m.isShiftDown() ? ops.anchor() : c, c);
        changed ({});
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}
} // namespace prv::ui
