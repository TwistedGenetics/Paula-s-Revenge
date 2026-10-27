// Paula's Revenge - the pattern (block) editor
#include "PatternEditor.h"

namespace prv::ui
{
namespace
{
    constexpr int kColOffset[6] = { 0, 4, 5, 7, 8, 9 };   // char offset of each cursor column in a track
    constexpr int kTrackChars = 13;                        // "C-3 01 C20" + gap + VU bar
    constexpr int kLineNumChars = 4;

    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
}

PatternEditor::PatternEditor (PaulaProcessor& p) : proc (p), st (p.editorState)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
}

// ---------------------------------------------------------------- geometry
int PatternEditor::visibleTracks() const
{
    const int nt = proc.song.numTracks;
    return st.fourTrackView ? std::min (4, nt) : nt;
}

int PatternEditor::firstTrack() const
{
    if (visibleTracks() >= proc.song.numTracks) return 0;
    return st.track >= 4 ? 4 : 0;
}

float PatternEditor::fontPx() const
{
    const int chars = kLineNumChars + visibleTracks() * kTrackChars;
    const float maxPx = Theme::px * 1.5f;
    return PixelFont::fitPx (chars, (float) getWidth() - 8.0f, maxPx);
}

float PatternEditor::rowH() const     { return fontPx() * (PixelFont::kCellH + 3); }
float PatternEditor::headerH() const  { return fontPx() * 14; }
float PatternEditor::trackW() const   { return fontPx() * PixelFont::kCellW * kTrackChars; }
float PatternEditor::lineNumW() const { return fontPx() * (PixelFont::kCellW * kLineNumChars + 3); }
int   PatternEditor::colChars (int column) const { return column == 0 ? 3 : 1; }

float PatternEditor::colX (int slot, int column) const
{
    return lineNumW() + slot * trackW() + kColOffset[column] * fontPx() * PixelFont::kCellW;
}

int PatternEditor::blockLines() const
{
    return proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].lines;
}

// ---------------------------------------------------------------- painting
void PatternEditor::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = fontPx();
    const float rh = rowH();
    const float hh = headerH();
    const auto bounds = getLocalBounds().toFloat();
    const float line = std::max (1.0f, px * 0.5f);

    g.fillAll (sk.bg);

    const int nVis = visibleTracks();
    const int first = firstTrack();
    const float tw = trackW();
    const float lnw = lineNumW();
    const float cw = px * PixelFont::kCellW;

    // ---- track headers: grey bevelled tabs with a coloured strip per track
    g.setColour (Theme::panel);
    g.fillRect (bounds.withHeight (hh));
    for (int s = 0; s < nVis; ++s)
    {
        const int t = first + s;
        auto r = juce::Rectangle<float> (lnw + s * tw - cw * 0.5f, 0, tw - cw * 0.5f, hh).reduced (px * 0.5f, px);
        const bool muted = proc.player.mute[t].load();
        g.setColour (muted ? Theme::pressed : Theme::panel);
        g.fillRect (r);
        Theme::bevel (g, r, ! muted, line);
        auto strip = r.removeFromBottom (px * 3).reduced (px, 0);
        g.setColour (muted ? Theme::faint : sk.track[t]);
        g.fillRect (strip);
        juce::String label = muted ? "MUTED" : "TRACK " + juce::String (t + 1);
        if (PixelFont::textWidth (label, px) > r.getWidth() - px * 2) label = muted ? "M" + juce::String (t + 1) : "T" + juce::String (t + 1);
        PixelFont::drawCentred (g, label, r, px, Theme::ink);
    }

    // ---- rows
    const auto& song = proc.song;
    const int blk = song.safeBlock (st.block);
    const auto& b = song.blocks[(size_t) blk];
    const int lines = b.lines;
    const float area = bounds.getHeight() - hh;
    const int visRows = std::max (1, (int) (area / rh));
    const int centre = visRows / 2;
    const float y0 = hh + (area - visRows * rh) * 0.5f;

    const bool playing = proc.player.isPlaying();
    const int playBlock = proc.player.uiBlock.load();
    const int playLine  = proc.player.uiLine.load();

    juce::RectangleList<float> text, dimText;
    for (int r = 0; r < visRows; ++r)
    {
        const int l = st.line + (r - centre);
        if (l < 0 || l >= lines) continue;
        const float y = y0 + r * rh;
        auto rowRect = juce::Rectangle<float> (0, y, bounds.getWidth(), rh);

        if (r == centre)       { g.setColour (sk.band);   g.fillRect (rowRect); }
        else if (l % 4 == 0)   { g.setColour (sk.stripe); g.fillRect (rowRect); }
        if (playing && playBlock == blk && playLine == l && ! (st.follow && r == centre))
        {
            g.setColour (sk.text);
            g.fillRect (rowRect.withHeight (line));
            g.fillRect (rowRect.withHeight (line).withBottomY (rowRect.getBottom()));
        }

        const float ty = std::round (y + (rh - 7 * px) * 0.5f);
        PixelFont::addText (text, juce::String (l).paddedLeft ('0', 3), px * 2.5f, ty, px);

        for (int s = 0; s < nVis; ++s)
        {
            const int t = first + s;
            const auto& c = b.at (l, t);
            const float x = lnw + s * tw;
            const juce::String note = noteName (c.note);
            const juce::String ins = c.instr ? hex2 (c.instr) : "--";
            const juce::String cmd = (c.cmd == 0 && c.param == 0) ? "---"
                                   : juce::String::toHexString ((int) c.cmd).toUpperCase() + hex2 (c.param);
            PixelFont::addText (c.note ? text : dimText, note, x, ty, px);
            PixelFont::addText (c.instr ? text : dimText, ins, x + 4 * cw, ty, px);
            PixelFont::addText ((c.cmd || c.param) ? text : dimText, cmd, x + 7 * cw, ty, px);
        }

        if (r == centre && st.track >= first && st.track < first + nVis)
        {
            const float cx = colX (st.track - first, st.column) - px * 0.5f;
            auto cr = juce::Rectangle<float> (cx, y + px * 0.5f, colChars (st.column) * cw, rh - px);
            g.setColour (sk.cursor);
            g.fillRect (cr);
            g.setColour (sk.cursorEdge);
            g.drawRect (cr, std::max (1.0f, px * (st.editMode ? 0.75f : 0.4f)));
        }
    }

    // ---- per-track VU bars: a segmented column at the right edge of each track
    const float segH = std::max (2.0f, px * 3);
    const float vuTop = hh + px * 2, vuBottom = bounds.getBottom() - px * 2;
    const int segs = std::max (4, (int) ((vuBottom - vuTop) / segH));
    for (int s = 0; s < nVis; ++s)
    {
        const int t = first + s;
        const float x = lnw + s * tw + 10.4f * cw;
        const float w = 1.4f * cw;
        // dB scale: -42 dB .. -3 dB fills the column
        const float db = juce::Decibels::gainToDecibels (vu[t], -100.0f);
        const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 42.0f) / 39.0f) * segs);
        g.setColour (sk.track[t].withAlpha (sk.dark ? 0.16f : 0.12f));
        g.fillRect (x, vuTop, w, vuBottom - vuTop);
        for (int k = 0; k < lit; ++k)
        {
            const float yy = vuBottom - (k + 1) * segH;
            g.setColour (k >= segs - std::max (2, segs / 8) ? sk.hot : sk.track[t]);
            g.fillRect (x, yy + 1.0f, w, segH - 2.0f);
        }
    }

    // ---- coloured rules between tracks
    for (int s = 0; s <= nVis; ++s)
    {
        const float x = std::round (lnw + s * tw - cw * 0.75f);
        g.setColour (s == 0 ? sk.rule : sk.track[first + s - 1]);
        g.fillRect (x - line * 0.5f, hh, line * 2.0f, bounds.getHeight() - hh);
    }

    g.setColour (sk.dim);
    g.fillRectList (dimText);
    g.setColour (sk.text);
    g.fillRectList (text);

    g.setColour (Theme::ink);
    if (st.editMode) g.drawRect (bounds.reduced (1.0f), std::max (2.0f, px));
    else             g.drawRect (bounds, 1.0f);

    if (hasKeyboardFocus (false) == false)
    {
        const float hp = std::max (1.5f, px * 0.75f);
        const juce::String hint = "CLICK HERE TO TYPE";
        auto box = juce::Rectangle<float> (PixelFont::textWidth (hint, hp) + hp * 4, hp * 11)
                       .withPosition (bounds.getRight() - PixelFont::textWidth (hint, hp) - hp * 6, bounds.getBottom() - hp * 14);
        g.setColour (Theme::panel);
        g.fillRect (box);
        Theme::bevel (g, box, true, 1.0f);
        PixelFont::drawCentred (g, hint, box, hp, Theme::ink);
    }
}

// ---------------------------------------------------------------- refresh
void PatternEditor::tick()
{
    bool dirty = false;
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const float lv = proc.player.uiLevel[t].load();
        const float nv = std::max (lv, vu[t] * 0.82f);
        if (std::abs (nv - vu[t]) > 0.004f) dirty = true;
        vu[t] = nv < 0.002f ? 0.0f : nv;
    }

    if (proc.player.isPlaying() && st.follow)
    {
        const int b = proc.player.uiBlock.load(), l = proc.player.uiLine.load();
        if (b != st.block || l != st.line)
        {
            st.block = b; st.line = l;
            if (proc.player.getMode() == Player::Mode::Song) st.seqPos = proc.player.uiSeqPos.load();
            changed();
        }
    }

    // MIDI notes from the host or a MIDI keyboard
    int note;
    while (proc.popMidiNote (note))
        if (st.editMode) { pushUndo (st.block); proc.withLock ([&] {
                auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
                c.note = (uint8_t) note; c.instr = (uint8_t) (st.instrument + 1); });
            advance(); changed(); }

    if (dirty || st.line != lastDrawnLine || st.block != lastDrawnBlock) repaint();
    lastDrawnLine = st.line; lastDrawnBlock = st.block;
}

void PatternEditor::changed()
{
    repaint();
    if (onStateChanged) onStateChanged();
}

// ---------------------------------------------------------------- mouse
void PatternEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const float px = fontPx();
    const float hh = headerH();
    const int nVis = visibleTracks();
    const int first = firstTrack();

    const int slot = (int) std::floor ((e.position.x - lineNumW() + px * PixelFont::kCellW * 0.5f) / trackW());
    if (e.position.y < hh)
    {
        if (slot >= 0 && slot < nVis)
        {
            auto& m = proc.player.mute[first + slot];
            if (e.mods.isRightButtonDown() || e.mods.isAltDown())
            {
                // solo: mute everything else (or unmute all if already soloed)
                bool soloed = ! m.load();
                for (int t = 0; t < kMaxTracks; ++t) if (t != first + slot && ! proc.player.mute[t].load()) soloed = false;
                for (int t = 0; t < kMaxTracks; ++t) proc.player.mute[t] = soloed ? false : (t != first + slot);
            }
            else m = ! m.load();
            repaint();
        }
        return;
    }

    const float rh = rowH();
    const float area = (float) getHeight() - hh;
    const int visRows = std::max (1, (int) (area / rh));
    const float y0 = hh + (area - visRows * rh) * 0.5f;
    const int r = (int) std::floor ((e.position.y - y0) / rh);
    const int l = st.line + (r - visRows / 2);
    if (l >= 0 && l < blockLines() && ! (proc.player.isPlaying() && st.follow)) st.line = l;

    if (slot >= 0 && slot < nVis)
    {
        st.track = first + slot;
        const float rel = e.position.x - colX (slot, 0);
        const int ch = (int) std::floor (rel / (px * PixelFont::kCellW));
        int col = 0;
        for (int c = 5; c >= 0; --c) if (ch >= kColOffset[c]) { col = c; break; }
        st.column = col;
    }
    changed();
}

void PatternEditor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (proc.player.isPlaying() && st.follow) return;
    const int d = w.deltaY > 0 ? -1 : (w.deltaY < 0 ? 1 : 0);
    if (d) moveLine (d * (std::abs (w.deltaY) > 0.3f ? 4 : 1));
}

void PatternEditor::focusLost (FocusChangeType)
{
    for (const auto& kv : heldKeys)
    {
        const int jam = kv.second;
        proc.withLock ([&] { proc.player.jamNoteOff (jam); });
    }
    heldKeys.clear();
    repaint();
}

// ---------------------------------------------------------------- movement
void PatternEditor::moveLine (int delta)
{
    const int lines = blockLines();
    st.line = ((st.line + delta) % lines + lines) % lines;
    changed();
}

void PatternEditor::moveColumn (int delta)
{
    st.column += delta;
    if (st.column < 0) { st.column = 5; moveTrack (-1); return; }
    if (st.column > 5) { st.column = 0; moveTrack (1); return; }
    changed();
}

void PatternEditor::moveTrack (int delta)
{
    const int nt = proc.song.numTracks;
    st.track = ((st.track + delta) % nt + nt) % nt;
    changed();
}

void PatternEditor::advance()
{
    if (proc.player.isPlaying() && st.follow) return; // the play position moves the cursor
    if (st.step > 0) moveLine (st.step);
}

// ---------------------------------------------------------------- editing
void PatternEditor::pushUndo (int blockIndex)
{
    blockIndex = proc.song.safeBlock (blockIndex);
    undoStack.push_back ({ blockIndex, proc.song.blocks[(size_t) blockIndex] });
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PatternEditor::undo()
{
    if (undoStack.empty()) return;
    auto e = std::move (undoStack.back());
    undoStack.pop_back();
    if (e.block >= (int) proc.song.blocks.size()) return;
    redoStack.push_back ({ e.block, proc.song.blocks[(size_t) e.block] });
    proc.withLock ([&] { proc.song.blocks[(size_t) e.block] = e.data; });
    st.block = e.block;
    st.line = std::min (st.line, blockLines() - 1);
    changed();
}

void PatternEditor::redo()
{
    if (redoStack.empty()) return;
    auto e = std::move (redoStack.back());
    redoStack.pop_back();
    if (e.block >= (int) proc.song.blocks.size()) return;
    undoStack.push_back ({ e.block, proc.song.blocks[(size_t) e.block] });
    proc.withLock ([&] { proc.song.blocks[(size_t) e.block] = e.data; });
    st.block = e.block;
    st.line = std::min (st.line, blockLines() - 1);
    changed();
}

void PatternEditor::enterNote (int note)
{
    note = juce::jlimit (1, (int) kNoteMax, note);
    proc.withLock ([&] { proc.player.jamNoteOn (note, st.instrument); });
    if (! st.editMode) return;
    pushUndo (st.block);
    proc.withLock ([&] {
        auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
        c.note = (uint8_t) note;
        c.instr = (uint8_t) (st.instrument + 1);
    });
    advance();
    changed();
}

void PatternEditor::enterHex (int v)
{
    if (! st.editMode) return;
    pushUndo (st.block);
    proc.withLock ([&] {
        auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
        switch (st.column)
        {
            case 1: c.instr = (uint8_t) std::min (kMaxInstruments, (v << 4) | (c.instr & 15)); break;
            case 2: c.instr = (uint8_t) std::min (kMaxInstruments, (c.instr & 0xF0) | v); break;
            case 3: c.cmd = (uint8_t) v; break;
            case 4: c.param = (uint8_t) ((v << 4) | (c.param & 15)); break;
            case 5: c.param = (uint8_t) ((c.param & 0xF0) | v); break;
            default: break;
        }
    });
    advance();
    changed();
}

void PatternEditor::clearField()
{
    if (! st.editMode) return;
    pushUndo (st.block);
    proc.withLock ([&] {
        auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
        if (st.column == 0) { c.note = 0; c.instr = 0; }
        else if (st.column <= 2) c.instr = 0;
        else { c.cmd = 0; c.param = 0; }
    });
    advance();
    changed();
}

void PatternEditor::insertLine()
{
    if (! st.editMode) return;
    pushUndo (st.block);
    proc.withLock ([&] {
        auto& b = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)];
        for (int l = b.lines - 1; l > st.line; --l) b.at (l, st.track) = b.at (l - 1, st.track);
        b.at (st.line, st.track) = Cell{};
    });
    changed();
}

void PatternEditor::deleteLine()
{
    if (! st.editMode) return;
    pushUndo (st.block);
    proc.withLock ([&] {
        auto& b = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)];
        for (int l = st.line; l < b.lines - 1; ++l) b.at (l, st.track) = b.at (l + 1, st.track);
        b.at (b.lines - 1, st.track) = Cell{};
    });
    changed();
}

void PatternEditor::copyTrack (bool cut)
{
    const auto& b = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)];
    trackClip.clear();
    for (int l = 0; l < b.lines; ++l) trackClip.push_back (b.at (l, st.track));
    trackClipLines = b.lines;
    if (cut)
    {
        pushUndo (st.block);
        proc.withLock ([&] {
            auto& bb = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)];
            for (int l = 0; l < bb.lines; ++l) bb.at (l, st.track) = Cell{};
        });
        changed();
    }
}

void PatternEditor::pasteTrack()
{
    if (trackClip.empty()) return;
    pushUndo (st.block);
    proc.withLock ([&] {
        auto& b = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)];
        for (int l = 0; l < b.lines && l < (int) trackClip.size(); ++l) b.at (l, st.track) = trackClip[(size_t) l];
    });
    changed();
}

void PatternEditor::copyBlock (bool cut)
{
    blockClip = std::make_unique<Block> (proc.song.blocks[(size_t) proc.song.safeBlock (st.block)]);
    if (cut)
    {
        pushUndo (st.block);
        proc.withLock ([&] { proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].clear(); });
        changed();
    }
}

void PatternEditor::pasteBlock()
{
    if (! blockClip) return;
    pushUndo (st.block);
    proc.withLock ([&] { proc.song.blocks[(size_t) proc.song.safeBlock (st.block)] = *blockClip; });
    st.line = std::min (st.line, blockLines() - 1);
    changed();
}

void PatternEditor::transposeTrack (int semis)
{
    pushUndo (st.block);
    proc.withLock ([&] {
        auto& b = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)];
        for (int l = 0; l < b.lines; ++l)
        {
            auto& c = b.at (l, st.track);
            if (c.note > 0 && c.note <= kNoteMax) c.note = (uint8_t) juce::jlimit (1, (int) kNoteMax, c.note + semis);
        }
    });
    changed();
}

void PatternEditor::togglePlayStop (bool songMode)
{
    proc.withLock ([&] {
        if (proc.player.isPlaying()) proc.player.stop();
        else if (songMode) proc.player.playSong (st.seqPos, 0);
        else proc.player.playBlock (st.block, 0);
    });
    changed();
}

void PatternEditor::stopPlayback()
{
    proc.withLock ([&] { proc.player.stop(); proc.player.jamAllOff(); });
    changed();
}

// ---------------------------------------------------------------- keys
int PatternEditor::noteForKey (const juce::KeyPress& k) const
{
    static const char* lower = "zsxdcvgbhnjm,l.;/";
    static const char* upper = "q2w3er5t6y7ui9o0p";
    const juce::juce_wchar ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (ch == 0 || k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return -1;
    for (int i = 0; lower[i]; ++i) if (ch == (juce::juce_wchar) lower[i]) return st.octave * 12 + i + 1;
    for (int i = 0; upper[i]; ++i) if (ch == (juce::juce_wchar) upper[i]) return (st.octave + 1) * 12 + i + 1;
    return -1;
}

bool PatternEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const bool cmd = mods.isCommandDown();
    const bool shift = mods.isShiftDown();

    // --- navigation
    if (code == juce::KeyPress::upKey && ! cmd)   { if (! (proc.player.isPlaying() && st.follow)) moveLine (shift ? -4 : -1); return true; }
    if (code == juce::KeyPress::downKey && ! cmd) { if (! (proc.player.isPlaying() && st.follow)) moveLine (shift ? 4 : 1); return true; }
    if (code == juce::KeyPress::leftKey)  { if (shift) moveTrack (-1); else moveColumn (-1); return true; }
    if (code == juce::KeyPress::rightKey) { if (shift) moveTrack (1);  else moveColumn (1);  return true; }
    if (code == juce::KeyPress::tabKey)   { st.column = 0; moveTrack (shift ? -1 : 1); return true; }
    if (code == juce::KeyPress::pageUpKey)   { moveLine (-16); return true; }
    if (code == juce::KeyPress::pageDownKey) { moveLine (16); return true; }
    if (code == juce::KeyPress::homeKey) { st.line = 0; changed(); return true; }
    if (code == juce::KeyPress::endKey)  { st.line = blockLines() - 1; changed(); return true; }

    // --- transport
    if (code == juce::KeyPress::spaceKey)
    {
        if (proc.player.isPlaying()) stopPlayback();
        else { st.editMode = ! st.editMode; changed(); }
        return true;
    }
    if (code == juce::KeyPress::returnKey)
    {
        proc.withLock ([&] {
            if (shift) proc.player.playSong (st.seqPos, 0);
            else if (cmd) proc.player.playBlock (st.block, st.line);
            else proc.player.playBlock (st.block, 0);
        });
        changed();
        return true;
    }
    if (code == juce::KeyPress::escapeKey) { stopPlayback(); return true; }

    // --- clipboard / undo
    if (cmd)
    {
        const auto ch = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        if (ch == 'z') { if (shift) redo(); else undo(); return true; }
        if (ch == 'y') { redo(); return true; }
        if (ch == 'c') { if (shift) copyBlock (false); else copyTrack (false); return true; }
        if (ch == 'x') { if (shift) copyBlock (true);  else copyTrack (true);  return true; }
        if (ch == 'v') { if (shift) pasteBlock(); else pasteTrack(); return true; }
        if (code == juce::KeyPress::upKey)   { transposeTrack (shift ? 12 : 1); return true; }
        if (code == juce::KeyPress::downKey) { transposeTrack (shift ? -12 : -1); return true; }
        return false; // let the host have other shortcuts
    }

    // --- line edits
    // the Mac "delete" key is backspace: plain = clear field, shift = remove line, alt = insert line
    if (code == juce::KeyPress::deleteKey) { clearField(); return true; }
    if (code == juce::KeyPress::backspaceKey)
    {
        if (mods.isAltDown()) insertLine();
        else if (shift) deleteLine();
        else clearField();
        return true;
    }
    if (code == juce::KeyPress::insertKey) { insertLine(); return true; }

    const juce::juce_wchar ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());

    // --- octave / instrument / step
    if (ch == '[') { st.octave = std::max (0, st.octave - 1); changed(); return true; }
    if (ch == ']') { st.octave = std::min (6, st.octave + 1); changed(); return true; }
    if (ch == '-') { st.instrument = std::max (0, st.instrument - 1); proc.jamInstrument = st.instrument; changed(); return true; }
    if (ch == '=') { st.instrument = std::min (kMaxInstruments - 1, st.instrument + 1); proc.jamInstrument = st.instrument; changed(); return true; }

    if (st.column == 0)
    {
        if (ch == '1' || ch == '`')
        {
            if (! st.editMode) return true;
            pushUndo (st.block);
            proc.withLock ([&] {
                auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
                c.note = kNoteOff; c.instr = 0;
            });
            advance(); changed();
            return true;
        }
        const int note = noteForKey (k);
        if (note > 0)
        {
            if (heldKeys.count (code) == 0)
            {
                heldKeys[code] = juce::jlimit (1, (int) kNoteMax, note);
                enterNote (note);
            }
            return true;
        }
        return false;
    }

    // hex columns
    int hv = -1;
    if (ch >= '0' && ch <= '9') hv = (int) (ch - '0');
    else if (ch >= 'a' && ch <= 'f') hv = (int) (ch - 'a' + 10);
    if (hv >= 0) { enterHex (hv); return true; }
    return false;
}

bool PatternEditor::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const int n = it->second;
            proc.withLock ([&] { proc.player.jamNoteOff (n); });
            it = heldKeys.erase (it);
        }
        else ++it;
    }
    return false;
}
} // namespace prv::ui
