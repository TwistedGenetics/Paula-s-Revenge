// Paula's Revenge - full-size song arrangement page
#include "ArrangePage.h"

namespace prv::ui
{
ArrangePage::ArrangePage (PaulaProcessor& p) : proc (p)
{
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
    undoBtn.onClick   = [this] { if (undo) undo(); opened(); };
    redoBtn.onClick   = [this] { if (redo) redo(); opened(); };
    closeBtn.onClick  = [this] { if (onClose) onClose(); };
    blockBox.format = [] (int v) { return juce::String (v).paddedLeft ('0', 3); };
    blockBox.labelChars = 6;
    for (auto* c : std::initializer_list<juce::Component*> { &upBtn, &downBtn, &copyBtn, &cutBtn, &pasteBtn, &repeatBtn,
                                                             &insertBtn, &blockBox, &setBtn, &deleteBtn, &undoBtn, &redoBtn, &closeBtn })
        addAndMakeVisible (c);
}

void ArrangePage::opened()
{
    const int n = (int) proc.song.sequence.size();
    cursor = anchor = juce::jlimit (0, n - 1, proc.editorState.seqPos);
    blockBox.setRange (0, (int) proc.song.blocks.size() - 1);
    blockBox.setValue (proc.song.sequence[(size_t) cursor]);
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
    cursor = juce::jlimit (0, n - 1, cursor);
    anchor = juce::jlimit (0, n - 1, anchor);
    st.seqPos = cursor;
    st.block = proc.song.safeBlock (proc.song.sequence[(size_t) cursor]);
    blockBox.setRange (0, (int) proc.song.blocks.size() - 1);
    blockBox.setValue (proc.song.sequence[(size_t) cursor]);
    ensureVisible();
    repaint();
    if (onChanged) onChanged();
    if (onStatus && status.isNotEmpty()) onStatus (status);
}

void ArrangePage::edit (std::function<void (std::vector<int>&)> fn, const juce::String& status)
{
    if (pushUndo) pushUndo();
    std::vector<int> seq = proc.song.sequence;
    fn (seq);
    if (seq.empty()) seq.push_back (0);
    if (seq.size() > (size_t) kMaxSequence) seq.resize ((size_t) kMaxSequence);
    proc.withLock ([&] { std::swap (proc.song.sequence, seq); });
    changed (status);
}

void ArrangePage::select (int from, int to)
{
    const int n = (int) proc.song.sequence.size();
    anchor = juce::jlimit (0, n - 1, from);
    cursor = juce::jlimit (0, n - 1, to);
    changed ({});
}

void ArrangePage::moveSelection (int delta)
{
    const int a = selStart(), b = selEnd(), n = (int) proc.song.sequence.size();
    if (a + delta < 0 || b + delta >= n) return;
    edit ([&] (std::vector<int>& s)
    {
        std::vector<int> part (s.begin() + a, s.begin() + b + 1);
        s.erase (s.begin() + a, s.begin() + b + 1);
        s.insert (s.begin() + a + delta, part.begin(), part.end());
    }, {});
    anchor += delta; cursor += delta;
    changed ({});
}

void ArrangePage::moveSelectionTo (int insertBefore)
{
    const int a = selStart(), b = selEnd(), len = b - a + 1;
    if (insertBefore >= a && insertBefore <= b + 1) return;   // dropped on itself
    int target = insertBefore > b ? insertBefore - len : insertBefore;
    edit ([&] (std::vector<int>& s)
    {
        std::vector<int> part (s.begin() + a, s.begin() + b + 1);
        s.erase (s.begin() + a, s.begin() + b + 1);
        target = juce::jlimit (0, (int) s.size(), target);
        s.insert (s.begin() + target, part.begin(), part.end());
    }, "MOVED " + juce::String (len) + (len == 1 ? " POSITION" : " POSITIONS"));
    anchor = target; cursor = target + len - 1;
    changed ({});
}

void ArrangePage::copySelection()
{
    clip.assign (proc.song.sequence.begin() + selStart(), proc.song.sequence.begin() + selEnd() + 1);
    if (onStatus) onStatus ("COPIED " + juce::String ((int) clip.size()) + " POSITIONS");
}

void ArrangePage::cutSelection()
{
    copySelection();
    if ((int) clip.size() >= (int) proc.song.sequence.size()) { if (onStatus) onStatus ("A SONG NEEDS AT LEAST ONE POSITION"); return; }
    const int a = selStart(), b = selEnd();
    edit ([&] (std::vector<int>& s) { s.erase (s.begin() + a, s.begin() + b + 1); }, "CUT " + juce::String (b - a + 1) + " POSITIONS");
    anchor = cursor = std::min (a, (int) proc.song.sequence.size() - 1);
    changed ({});
}

void ArrangePage::paste()
{
    if (clip.empty()) { if (onStatus) onStatus ("NOTHING TO PASTE"); return; }
    const int at = selEnd() + 1;
    edit ([&] (std::vector<int>& s) { s.insert (s.begin() + std::min (at, (int) s.size()), clip.begin(), clip.end()); },
          "PASTED " + juce::String ((int) clip.size()) + " POSITIONS");
    anchor = at; cursor = at + (int) clip.size() - 1;
    changed ({});
}

void ArrangePage::repeatSelection()
{
    const int a = selStart(), b = selEnd(), len = b - a + 1;
    edit ([&] (std::vector<int>& s)
    {
        std::vector<int> part (s.begin() + a, s.begin() + b + 1);
        s.insert (s.begin() + b + 1, part.begin(), part.end());
    }, "REPEATED " + juce::String (len) + " POSITIONS");
    anchor = b + 1; cursor = b + len;
    changed ({});
}

void ArrangePage::deleteSelection()
{
    const int a = selStart(), b = selEnd();
    if (b - a + 1 >= (int) proc.song.sequence.size()) { if (onStatus) onStatus ("A SONG NEEDS AT LEAST ONE POSITION"); return; }
    edit ([&] (std::vector<int>& s) { s.erase (s.begin() + a, s.begin() + b + 1); }, "DELETED " + juce::String (b - a + 1) + " POSITIONS");
    anchor = cursor = std::min (a, (int) proc.song.sequence.size() - 1);
    changed ({});
}

void ArrangePage::insertBlock (int blk)
{
    blk = proc.song.safeBlock (blk);
    const int at = cursor + 1;
    edit ([&] (std::vector<int>& s) { s.insert (s.begin() + std::min (at, (int) s.size()), blk); }, "INSERTED BLOCK " + juce::String (blk).paddedLeft ('0', 3));
    anchor = cursor = at;
    changed ({});
}

void ArrangePage::setBlock (int blk)
{
    blk = proc.song.safeBlock (blk);
    const int a = selStart(), b = selEnd();
    edit ([&] (std::vector<int>& s) { for (int i = a; i <= b; ++i) s[(size_t) i] = blk; }, "NOW PLAYS BLOCK " + juce::String (blk).paddedLeft ('0', 3));
}

// ------------------------------------------------------------------ layout / paint
void ArrangePage::resized()
{
    const float px = Theme::px;
    const int rowHgt = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    auto r = getLocalBounds().reduced (gap);
    titleArea = r.removeFromTop (rowHgt).toFloat();
    r.removeFromTop (gap);
    auto layoutRow = [&] (juce::Rectangle<int> row, std::initializer_list<juce::Component*> comps)
    {
        const int n = (int) comps.size();
        const int w = (row.getWidth() - gap * (n - 1)) / n;
        for (auto* c : comps) { c->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
    };
    auto row3 = r.removeFromBottom (rowHgt); r.removeFromBottom (gap);
    auto row2 = r.removeFromBottom (rowHgt); r.removeFromBottom (gap);
    auto row1 = r.removeFromBottom (rowHgt); r.removeFromBottom (gap * 2);
    layoutRow (row1, { &upBtn, &downBtn, &copyBtn, &cutBtn, &pasteBtn, &repeatBtn });
    {
        const int w = (row2.getWidth() - gap * 3) / 5;
        insertBtn.setBounds (row2.removeFromLeft (w)); row2.removeFromLeft (gap);
        blockBox.setBounds (row2.removeFromLeft (w * 2)); row2.removeFromLeft (gap);
        setBtn.setBounds (row2.removeFromLeft (w)); row2.removeFromLeft (gap);
        deleteBtn.setBounds (row2);
    }
    layoutRow (row3, { &undoBtn, &redoBtn, &closeBtn });
    listArea = r.toFloat();
}

int ArrangePage::rowAt (float y) const { return scroll + (int) std::floor ((y - listArea.getY()) / rowH()); }

void ArrangePage::ensureVisible()
{
    if (cursor < scroll) scroll = cursor;
    else if (cursor >= scroll + visibleRows()) scroll = cursor - visibleRows() + 1;
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
        if (row == cursor) { g.setColour (sk.text); g.drawRect (rr.reduced (1.0f), std::max (1.0f, px * 0.75f)); }
        const int blk = proc.song.sequence[(size_t) row];
        const float ty = std::round (rr.getCentreY() - 3.5f * px);
        if (row == playPos) PixelFont::draw (g, juce::String::charToString ((juce::juce_wchar) 1), rr.getX() + px * 2, ty, px, sk.hot);
        PixelFont::draw (g, juce::String (row).paddedLeft ('0', 3) + "   BLOCK " + juce::String (blk).paddedLeft ('0', 3),
                         rr.getX() + px * 9, ty, px, sk.text);
        const float textW = px * PixelFont::kCellW * 19 + px * 12;
        paintPreview (g, rr.withTrimmedLeft (textW).reduced (px * 2, px * 2), blk);
    }
    // drop marker while dragging
    if (dragging && dropAt >= 0)
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
    if (e.mods.isShiftDown()) { cursor = row; changed ({}); return; }
    if (row >= selStart() && row <= selEnd() && selStart() != selEnd()) { dragFrom = row; return; }   // drag a range
    anchor = cursor = row;
    dragFrom = row;
    changed ({});
}

void ArrangePage::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! dragging && e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    if (e.position.y < listArea.getY()) scroll = std::max (0, scroll - 1);
    if (e.position.y > listArea.getBottom()) scroll = std::min (std::max (0, (int) proc.song.sequence.size() - visibleRows()), scroll + 1);
    const float rel = (e.position.y - listArea.getY()) / rowH();
    dropAt = juce::jlimit (0, (int) proc.song.sequence.size(), scroll + juce::roundToInt (rel));
    repaint();
}

void ArrangePage::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dropAt >= 0) moveSelectionTo (dropAt);
    dragging = false; dragFrom = dropAt = -1;
    repaint();
}

void ArrangePage::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click a position to edit its block in the pattern
    if (listArea.contains (e.position) && rowAt (e.position.y) < (int) proc.song.sequence.size() && onClose) onClose();
}

void ArrangePage::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scroll = juce::jlimit (0, std::max (0, (int) proc.song.sequence.size() - visibleRows()), scroll + (w.deltaY > 0 ? -1 : 1));
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
        if (ch == 'z') { if (m.isShiftDown()) { if (redo) redo(); } else if (undo) undo(); opened(); return true; }
        return false;
    }
    if (m.isAltDown() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)) { moveSelection (code == juce::KeyPress::upKey ? -1 : 1); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        cursor = juce::jlimit (0, n - 1, cursor + (code == juce::KeyPress::upKey ? -1 : 1));
        if (! m.isShiftDown()) anchor = cursor;
        changed ({});
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}
} // namespace prv::ui
