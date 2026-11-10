// Paula's Revenge - PHRASES on an instrument's PARAMS page
#include "PhraseView.h"
#include "ChoiceBox.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    juce::String noteText (int n) { return juce::String (prv::noteName ((uint8_t) juce::jlimit (1, (int) kNoteMax, n))); }
    // where each column's characters sit in a row ("000 C-3 01 C20 ---")
    constexpr int kColStart[9] = { 4, 8, 9, 11, 12, 13, 15, 16, 17 };
    constexpr int kColChars[9] = { 3, 1, 1, 1, 1, 1, 1, 1, 1 };
    constexpr int kRowChars = 18;
}

// =====================================================================================  the grid
PhraseGrid::PhraseGrid (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
}

const Phrase* PhraseGrid::phrase() const
{
    const int i = phraseIndex ? phraseIndex() : -1;
    const auto& in = proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
    if (i < 0 || i >= (int) in.phrases.size()) return nullptr;
    const auto& ph = in.phrases[(size_t) i];
    return ph.block >= 0 && ph.block < (int) proc.song.blocks.size() ? &ph : nullptr;
}

Block* PhraseGrid::block() const
{
    const auto* ph = phrase();
    return ph != nullptr ? &proc.song.blocks[(size_t) ph->block] : nullptr;
}

float PhraseGrid::rowH() const  { return Theme::px * 10.0f; }
float PhraseGrid::charW() const { return Theme::px * PixelFont::kCellW; }
int   PhraseGrid::linesShown() const { return std::max (1, (int) (getHeight() / rowH())); }

juce::String PhraseGrid::rowText (int l) const
{
    const auto* b = block();
    const auto* ph = phrase();
    if (b == nullptr || l < 0 || l >= b->lines) return {};
    const Cell c = b->at (l, ph->track);
    auto fx = [] (uint8_t cmd, uint8_t p) { return cmd == 0 && p == 0 ? juce::String ("---") : juce::String::charToString ((juce::juce_wchar) commandChar (cmd)) + hex2 (p); };
    return juce::String (l).paddedLeft ('0', 3) + " " + juce::String (noteName (c.note)) + " "
         + (c.instr > 0 ? hex2 (c.instr) : juce::String ("--")) + " " + fx (c.cmd, c.param) + " " + fx (c.cmd2, c.param2);
}

void PhraseGrid::setCursor (int l, int c)
{
    const auto* b = block();
    line = juce::jlimit (0, b != nullptr ? b->lines - 1 : 0, l);
    column = juce::jlimit (0, 8, c);
    keepVisible();
    repaint();
}

void PhraseGrid::keepVisible()
{
    const int n = linesShown();
    if (line < top) top = line;
    if (line >= top + n) top = line - n + 1;
    const auto* b = block();
    top = juce::jlimit (0, std::max (0, (b != nullptr ? b->lines : 1) - n), top);
}

void PhraseGrid::write (const juce::String& what, std::function<void (Cell&)> fn, bool advance)
{
    const auto* ph = phrase();
    if (ph == nullptr) return;
    const int b = ph->block, t = ph->track, l = line;
    proc.history.block (b, what);
    proc.withLock ([&] { fn (proc.song.blocks[(size_t) b].at (l, t)); });
    if (advance) setCursor (line + 1, column);
    if (onEdited) onEdited();
    repaint();
}

bool PhraseGrid::typeKey (const juce::KeyPress& k)
{
    const auto* b = block();
    if (b == nullptr) return false;
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown()) return false;   // (undo etc.: the main window)
    const int code = k.getKeyCode();
    const int step = mods.isShiftDown() ? 4 : 1;
    if (code == juce::KeyPress::upKey)       { setCursor (line - step, column); return true; }
    if (code == juce::KeyPress::downKey)     { setCursor (line + step, column); return true; }
    if (code == juce::KeyPress::pageUpKey)   { setCursor (line - 16, column); return true; }
    if (code == juce::KeyPress::pageDownKey) { setCursor (line + 16, column); return true; }
    if (code == juce::KeyPress::homeKey)     { setCursor (0, column); return true; }
    if (code == juce::KeyPress::endKey)      { setCursor (b->lines - 1, column); return true; }
    if (code == juce::KeyPress::leftKey)     { setCursor (line, column - 1); return true; }
    if (code == juce::KeyPress::rightKey)    { setCursor (line, column + 1); return true; }
    if (code == juce::KeyPress::tabKey)      { setCursor (line, column < 3 ? 3 : column < 6 ? 6 : 0); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        const int col = column;
        write ("CLEAR PHRASE STEP", [col] (Cell& c)
        {
            if (col == 0) { c.note = 0; c.instr = 0; }
            else if (col <= 2) c.instr = 0;
            else if (col <= 5) { c.cmd = 0; c.param = 0; }
            else { c.cmd2 = 0; c.param2 = 0; }
        });
        return true;
    }
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    auto& st = proc.editorState;
    if (ch == '[') { st.octave = std::max (0, st.octave - 1); if (onStatus) onStatus ("OCTAVE " + juce::String (st.octave)); return true; }
    if (ch == ']') { st.octave = std::min (6, st.octave + 1); if (onStatus) onStatus ("OCTAVE " + juce::String (st.octave)); return true; }

    if (column == 0)
    {
        if (ch == '1' || ch == '`') { write ("PHRASE NOTE OFF", [] (Cell& c) { c.note = kNoteOff; c.instr = 0; }); return true; }
        static const char* lower = "zsxdcvgbhnjm,l.;/";
        static const char* upper = "q2w3er5t6y7ui9o0p";
        int note = -1;
        for (int i = 0; lower[i]; ++i) if (ch == (juce::juce_wchar) lower[i]) note = st.octave * 12 + i + 1;
        for (int i = 0; upper[i]; ++i) if (ch == (juce::juce_wchar) upper[i]) note = (st.octave + 1) * 12 + i + 1;
        if (note <= 0) return false;
        note = juce::jlimit (1, (int) kNoteMax, note);
        write ("PHRASE NOTE", [note] (Cell& c) { c.note = (uint8_t) note; });
        if (onStatus) onStatus ("PHRASE NOTE " + noteText (note) + " (C-3 PLAYS AS THE NOTE PLAYED; NO INSTRUMENT = THIS ONE)");
        return true;
    }
    if (column == 3 || column == 6)
    {
        const int cmd = commandFromChar ((char) k.getTextCharacter());
        if (cmd < 0) return false;
        const bool two = column == 6;
        write ("PHRASE COMMAND", [cmd, two] (Cell& c) { if (two) c.cmd2 = (uint8_t) cmd; else c.cmd = (uint8_t) cmd; }, false);
        setCursor (line, column + 1);
        return true;
    }
    int hv = -1;
    if (ch >= '0' && ch <= '9') hv = (int) (ch - '0');
    else if (ch >= 'a' && ch <= 'f') hv = (int) (ch - 'a' + 10);
    if (hv < 0) return false;
    const int col = column;
    const bool lastDigit = col == 2 || col == 5 || col == 8;   // (the first digit moves to the second; the second, down a line)
    write ("PHRASE STEP", [col, hv] (Cell& c)
    {
        auto setNib = [hv] (uint8_t& v, bool high) { v = (uint8_t) (high ? (hv << 4) | (v & 15) : (v & 0xF0) | hv); };
        if (col == 1 || col == 2) { setNib (c.instr, col == 1); c.instr = (uint8_t) std::min ((int) c.instr, kMaxInstruments); }
        else if (col == 4 || col == 5) setNib (c.param, col == 4);
        else if (col == 7 || col == 8) setNib (c.param2, col == 7);
    }, lastDigit);
    if (lastDigit) setCursor (line, col - 1);
    else setCursor (line, col + 1);
    return true;
}

void PhraseGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int l = top + (int) (e.position.y / rowH());
    const int ch = (int) (e.position.x / charW());
    int col = 0;
    for (int c = 0; c < 9; ++c) if (ch >= kColStart[c]) col = c;
    setCursor (l, col);
}

void PhraseGrid::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const auto* b = block();
    if (b == nullptr) return;
    top = juce::jlimit (0, std::max (0, b->lines - linesShown()), top - (int) std::round (w.deltaY * 12.0f));
    repaint();
}

void PhraseGrid::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px, rh = rowH(), cw = charW();
    g.fillAll (sk.bg);
    const auto* b = block();
    if (b == nullptr)
    {
        PixelFont::drawCentred (g, "NO PHRASE - ADD ONE", getLocalBounds().toFloat(), px, sk.dim);
        return;
    }
    keepVisible();
    const float x0 = std::max (0.0f, ((float) getWidth() - cw * kRowChars) * 0.5f);
    const int n = linesShown();
    const int lpb = std::max (1, phrase()->lpb);
    for (int i = 0; i <= n && top + i < b->lines; ++i)
    {
        const int l = top + i;
        const float y = (float) i * rh;
        if (l % lpb == 0) { g.setColour (sk.stripe); g.fillRect (0.0f, y, (float) getWidth(), rh); }
        if (l == playLine) { g.setColour (sk.hot.withAlpha (0.35f)); g.fillRect (0.0f, y, (float) getWidth(), rh); }
        if (l == line)
        {
            g.setColour (sk.band); g.fillRect (0.0f, y, (float) getWidth(), rh);
            const juce::Rectangle<float> cr (x0 + cw * kColStart[column] - px * 0.5f, y, cw * kColChars[column] + px, rh);
            g.setColour (sk.cursor); g.fillRect (cr);
            if (hasKeyboardFocus (false)) { g.setColour (sk.cursorEdge); g.drawRect (cr, std::max (1.0f, px * 0.5f)); }
        }
        const juce::String t = rowText (l);
        const float ty = y + (rh - PixelFont::textHeight (px)) * 0.5f;
        PixelFont::draw (g, t.substring (0, 3), x0, ty, px, l % lpb == 0 ? sk.text : sk.dim);
        // each field: dim when empty
        auto field = [&] (int start, int len)
        {
            const auto s = t.substring (start, start + len);
            PixelFont::draw (g, s, x0 + cw * start, ty, px, s.containsOnly ("-") ? sk.dim : sk.text);
        };
        field (4, 3); field (8, 2); field (11, 3); field (15, 3);
    }
}

// =====================================================================================  the page
PhraseView::PhraseView (PaulaProcessor& p) : proc (p)
{
    setOpaque (false);
    phraseGrid.phraseIndex = [this] { return sel; };
    phraseGrid.onStatus = [this] (const juce::String& m) { if (onStatus) onStatus (m); };
    phraseGrid.onEdited = [this] { if (onChanged) onChanged(); };
    addAndMakeVisible (phraseGrid);

    addBtn.onClick = [this] { addPhrase(); };
    cloneBtn.onClick = [this] { clonePhrase(); };
    delBtn.onClick = [this] { deletePhrase(); };
    for (auto* nb : { &loBox, &hiBox }) { nb->format = [] (int v) { return noteText (v); }; nb->valueChars = 3; nb->labelChars = 8; nb->compact = true; }
    loBox.onChange = [this] (int v) { const auto* c = current(); if (c != nullptr) setKeys (v, std::max (v, c->keyHi)); };
    hiBox.onChange = [this] (int v) { const auto* c = current(); if (c != nullptr) setKeys (std::min (v, c->keyLo), v); };
    for (auto* nb : { &linesBox, &lpbBox, &startBox, &endBox }) { nb->compact = true; nb->valueChars = 3; }
    linesBox.labelChars = 5; lpbBox.labelChars = 3; startBox.labelChars = 5; endBox.labelChars = 3;
    linesBox.onChange = [this] (int v) { setLines (v); };
    lpbBox.onChange = [this] (int v) { setLpb (v); };
    startBox.onChange = [this] (int v) { const auto* c = current(); if (c != nullptr) setLoopRange (v, std::max (v, endBox.getValue())); };
    endBox.onChange = [this] (int v) { const auto* c = current(); if (c != nullptr) setLoopRange (std::min (v, startBox.getValue()), v); };
    loopBtn.setClickingTogglesState (false);
    loopBtn.onClick = [this] { const auto* c = current(); if (c != nullptr) setLoop (! c->loop); };
    transposeBtn.setClickingTogglesState (false);
    transposeBtn.onClick = [this] { const auto* c = current(); if (c != nullptr) setTranspose (! c->transpose); };
    playBtn.setClickingTogglesState (false);
    playBtn.onClick = [this] { play (! isPlaying()); };
    backBtn.onClick = [this] { play (false); if (onBack) onBack(); };
    for (auto* c : std::initializer_list<juce::Component*> { &addBtn, &cloneBtn, &delBtn, &loBox, &hiBox, &linesBox, &lpbBox, &loopBtn, &startBox,
                                                             &transposeBtn, &endBox, &playBtn, &backBtn })
        addAndMakeVisible (c);
    refresh();
}

PhraseView::~PhraseView()
{
    proc.withLock ([&] { proc.player.stopPhrasePreview(); });
}

Instrument& PhraseView::inst()
{
    return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
}

const Phrase* PhraseView::current() const
{
    const auto& in = proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
    return sel >= 0 && sel < (int) in.phrases.size() ? &in.phrases[(size_t) sel] : nullptr;
}

template <typename Fn> void PhraseView::edit (const juce::String& what, Fn&& fn)
{
    proc.history.instrument (proc.editorState.instrument, what, true);
    proc.withLock ([&] { fn (inst()); });
    refresh();
    if (onChanged) onChanged();
}

void PhraseView::select (int index)
{
    const int n = (int) inst().phrases.size();
    sel = n == 0 ? 0 : juce::jlimit (0, n - 1, index);
    if (isPlaying()) play (false);
    phraseGrid.setCursor (0, 0);
    refresh();
}

bool PhraseView::addPhrase()
{
    if ((int) inst().phrases.size() >= kMaxPhrases) { if (onStatus) onStatus ("16 PHRASES IS THE MOST AN INSTRUMENT CAN HAVE"); return false; }
    // a new, empty 16-line block to write it in (not in the song's sequence)
    int b = 0;
    proc.withLock ([&] { prv::Block nb; nb.lines = 16; proc.song.blocks.push_back (nb); b = (int) proc.song.blocks.size() - 1; });
    const bool first = inst().phrases.empty();
    edit ("NEW PHRASE", [b] (Instrument& i) { Phrase p; p.block = b; i.phrases.push_back (p); });
    select ((int) inst().phrases.size() - 1);
    const juce::String n = hex2 (sel + 1);
    if (onStatus) onStatus (first ? "PHRASE " + n + ": ITS NOTES NOW PLAY INSTEAD OF THE SAMPLE - WRITE IT FROM C-3 ON THE RIGHT"
                                  : "PHRASE " + n + ": @" + n + " ON A NOTE PLAYS IT, OR GIVE IT ITS OWN KEYS (LOW KEY / HIGH KEY)");
    return true;
}

bool PhraseView::clonePhrase()
{
    const auto* c = current();
    if (c == nullptr) return addPhrase();
    if ((int) inst().phrases.size() >= kMaxPhrases) { if (onStatus) onStatus ("16 PHRASES IS THE MOST AN INSTRUMENT CAN HAVE"); return false; }
    Phrase copy = *c;
    int b = 0;
    proc.withLock ([&] {
        prv::Block nb;
        const auto& from = proc.song.blocks[(size_t) copy.block];
        nb.lines = from.lines;
        for (int l = 0; l < from.lines; ++l) nb.at (l, 0) = from.at (l, copy.track);
        proc.song.blocks.push_back (nb);
        b = (int) proc.song.blocks.size() - 1;
    });
    copy.block = b; copy.track = 0;
    edit ("CLONE PHRASE", [copy] (Instrument& i) { i.phrases.push_back (copy); });
    select ((int) inst().phrases.size() - 1);
    if (onStatus) onStatus ("PHRASE " + hex2 (sel + 1) + ": A COPY TO CHANGE (@" + hex2 (sel + 1) + " PLAYS IT)");
    return true;
}

void PhraseView::deletePhrase()
{
    if (current() == nullptr) return;
    play (false);
    const int k = sel;
    edit ("DELETE PHRASE", [k] (Instrument& i) { i.phrases.erase (i.phrases.begin() + k); });
    select (k - 1 < 0 ? 0 : k - 1);
    if (onStatus) onStatus (inst().phrases.empty() ? juce::String ("NO PHRASES: NOTES PLAY THE SAMPLE AGAIN") : "PHRASE " + hex2 (k + 1) + " DELETED (THE ONES AFTER MOVE UP: THEIR @ NUMBERS TOO)");
}

void PhraseView::setKeys (int lo, int hi)
{
    if (current() == nullptr) return;
    lo = juce::jlimit (1, (int) kNoteMax, lo); hi = juce::jlimit (lo, (int) kNoteMax, hi);
    const int k = sel;
    edit ("PHRASE KEYS", [k, lo, hi] (Instrument& i) { i.phrases[(size_t) k].keyLo = lo; i.phrases[(size_t) k].keyHi = hi; });
    if (onStatus) onStatus ("PHRASE " + hex2 (k + 1) + " PLAYS FROM " + noteText (lo) + " TO " + noteText (hi) + " (THE FIRST PHRASE IN THE LIST THAT HOLDS A NOTE PLAYS IT)");
}

void PhraseView::setLines (int lines)
{
    const auto* c = current();
    if (c == nullptr) return;
    lines = juce::jlimit (1, kMaxLines, lines);
    const int b = c->block;
    proc.history.block (b, "PHRASE LINES");
    proc.withLock ([&] { proc.song.blocks[(size_t) b].lines = lines; });
    phraseGrid.setCursor (phraseGrid.line, phraseGrid.column);
    refresh();
    if (onChanged) onChanged();
    if (onStatus) onStatus ("PHRASE " + hex2 (sel + 1) + ": " + juce::String (lines) + " LINES");
}

void PhraseView::setLpb (int lpb)
{
    if (current() == nullptr) return;
    lpb = juce::jlimit (1, 32, lpb);
    const int k = sel;
    edit ("PHRASE SPEED", [k, lpb] (Instrument& i) { i.phrases[(size_t) k].lpb = lpb; });
    if (onStatus) onStatus ("PHRASE " + hex2 (k + 1) + ": " + juce::String (lpb) + " LINES A BEAT" + (lpb == 4 ? juce::String (" (THE SONG'S OWN SPEED)") : lpb < 4 ? juce::String (" (SLOWER THAN THE SONG)") : juce::String (" (FASTER THAN THE SONG)")));
}

void PhraseView::setLoop (bool on)
{
    if (current() == nullptr) return;
    const int k = sel;
    edit (on ? "PHRASE LOOP ON" : "PHRASE LOOP OFF", [k, on] (Instrument& i) { i.phrases[(size_t) k].loop = on; });
    if (onStatus) onStatus (on ? "PHRASE " + hex2 (k + 1) + " LOOPS FROM START TO END UNTIL THE NEXT NOTE OR OFF" : "PHRASE " + hex2 (k + 1) + " PLAYS ONCE");
}

void PhraseView::setLoopRange (int start, int end)
{
    const auto* c = current();
    if (c == nullptr) return;
    const int last = proc.song.blocks[(size_t) c->block].lines - 1;
    start = juce::jlimit (0, last, start); end = juce::jlimit (start, last, end);
    const int k = sel;
    edit ("PHRASE LOOP", [k, start, end] (Instrument& i) { i.phrases[(size_t) k].loopStart = start; i.phrases[(size_t) k].loopEnd = end; });
    if (onStatus) onStatus ("PHRASE " + hex2 (k + 1) + " LOOPS LINES " + juce::String (start).paddedLeft ('0', 3) + " TO " + juce::String (end).paddedLeft ('0', 3)
                            + (c->loop ? juce::String() : juce::String (" (SWITCH LOOP ON)")));
}

void PhraseView::setTranspose (bool on)
{
    if (current() == nullptr) return;
    const int k = sel;
    edit (on ? "PHRASE TRANSPOSE ON" : "PHRASE TRANSPOSE OFF", [k, on] (Instrument& i) { i.phrases[(size_t) k].transpose = on; });
    if (onStatus) onStatus (on ? "PHRASE " + hex2 (k + 1) + ": ITS NOTES MOVE WITH THE NOTE PLAYED (C-3 = AS WRITTEN)"
                               : "PHRASE " + hex2 (k + 1) + ": ITS NOTES PLAY AS WRITTEN, WHATEVER NOTE STARTS IT");
}

void PhraseView::play (bool on)
{
    const int i = proc.editorState.instrument, k = sel;
    proc.withLock ([&] { if (on && current() != nullptr) proc.player.previewPhrase (i, k, 37); else proc.player.stopPhrasePreview(); });
    playBtn.setToggleState (isPlaying(), juce::dontSendNotification);
}

bool PhraseView::isPlaying() const { return proc.player.previewPhraseIndex() >= 0; }

void PhraseView::refresh()
{
    if (shownInstrument != proc.editorState.instrument) { shownInstrument = proc.editorState.instrument; sel = 0; listTop = 0; }
    const int n = (int) inst().phrases.size();
    sel = n == 0 ? 0 : juce::jlimit (0, n - 1, sel);
    const auto* c = current();
    const bool has = c != nullptr;
    for (auto* comp : std::initializer_list<juce::Component*> { &cloneBtn, &delBtn, &loBox, &hiBox, &linesBox, &lpbBox, &loopBtn, &startBox, &transposeBtn, &endBox, &playBtn })
    {
        comp->setEnabled (has);
        comp->setAlpha (has ? 1.0f : 0.45f);
    }
    addBtn.setEnabled (n < kMaxPhrases);
    if (has)
    {
        const int lines = proc.song.blocks[(size_t) c->block].lines;
        loBox.setValue (c->keyLo); hiBox.setValue (c->keyHi);
        linesBox.setValue (lines); lpbBox.setValue (c->lpb);
        startBox.setRange (0, lines - 1); endBox.setRange (0, lines - 1);
        startBox.setValue (std::min (c->loopStart, lines - 1));
        endBox.setValue (c->loopEnd < 0 ? lines - 1 : std::min (c->loopEnd, lines - 1));
        loopBtn.setToggleState (c->loop, juce::dontSendNotification);
        transposeBtn.setToggleState (c->transpose, juce::dontSendNotification);
        startBox.setAlpha (c->loop ? 1.0f : 0.6f); endBox.setAlpha (c->loop ? 1.0f : 0.6f);
    }
    playBtn.setToggleState (isPlaying(), juce::dontSendNotification);
    const int rows = listRows();
    listTop = juce::jlimit (0, std::max (0, n - rows), std::min (listTop, sel));
    if (sel >= listTop + rows) listTop = sel - rows + 1;
    phraseGrid.repaint();
    repaint();
}

void PhraseView::tick()
{
    if (shownInstrument != proc.editorState.instrument) { play (false); refresh(); }
    // the line playing: heard from here, else a track playing this phrase in the song
    int pl = -1;
    const int i = proc.editorState.instrument;
    if (proc.player.previewPhraseIndex() == sel) pl = proc.player.previewPhraseLine();
    else
        for (int t = 0; t < kMaxTracks && pl < 0; ++t)
            if (proc.player.phraseInstrument (t) == i && proc.player.phraseIndex (t) == sel) pl = proc.player.phraseLine (t) - 1;
    if (pl != phraseGrid.playLine) { phraseGrid.playLine = pl; phraseGrid.repaint(); }
    if (playBtn.getToggleState() != isPlaying()) playBtn.setToggleState (isPlaying(), juce::dontSendNotification);
}

int PhraseView::listRows() const
{
    return std::max (1, (int) (listArea.getHeight() / (Theme::px * 10.0f)));
}

juce::Rectangle<float> PhraseView::listRow (int i) const
{
    const float rh = Theme::px * 10.0f;
    return { listArea.getX(), listArea.getY() + (float) (i - listTop) * rh, listArea.getWidth(), rh };
}

void PhraseView::phraseMenu (int k)
{
    const auto& in = inst();
    if (k < 0 || k >= (int) in.phrases.size()) return;
    select (k);
    juce::Component::SafePointer<PhraseView> safe (this);
    const auto ph = in.phrases[(size_t) k];
    const int n = (int) in.phrases.size();
    std::vector<MenuItem> items;
    items.push_back ({ "EDIT IT IN THE PATTERN (BLOCK " + juce::String (ph.block).paddedLeft ('0', 3) + " TRACK " + juce::String (ph.track + 1) + ")",
                       [safe, ph] { if (safe && safe->onEditInPattern) safe->onEditInPattern (ph.block, ph.track); } });
    if (k > 0)
        items.push_back ({ "MOVE UP (@" + hex2 (k) + ")", [safe, k] { if (safe) { safe->edit ("MOVE PHRASE", [k] (Instrument& i) { std::swap (i.phrases[(size_t) k], i.phrases[(size_t) k - 1]); }); safe->select (k - 1); } } });
    if (k < n - 1)
        items.push_back ({ "MOVE DOWN (@" + hex2 (k + 2) + ")", [safe, k] { if (safe) { safe->edit ("MOVE PHRASE", [k] (Instrument& i) { std::swap (i.phrases[(size_t) k], i.phrases[(size_t) k + 1]); }); safe->select (k + 1); } } });
    items.push_back ({ "ALL KEYS (C-0 TO B-7)", [safe] { if (safe) safe->setKeys (1, kNoteMax); } });
    items.push_back ({ "TRACK " + juce::String ((ph.track + 1) % kMaxTracks + 1) + " OF ITS BLOCK INSTEAD",
                       [safe, k] { if (safe) safe->edit ("PHRASE TRACK", [k] (Instrument& i) { i.phrases[(size_t) k].track = (i.phrases[(size_t) k].track + 1) % kMaxTracks; }); } });
    items.push_back ({ "ANOTHER BLOCK...", [safe, k]
    {
        if (! safe) return;
        std::vector<MenuItem> bl;
        const int nb = (int) safe->proc.song.blocks.size();
        for (int b = std::max (0, nb - 14); b < nb; ++b)
            bl.push_back ({ "BLOCK " + juce::String (b).paddedLeft ('0', 3) + (safe->proc.song.blocks[(size_t) b].name.empty() ? juce::String() : " " + juce::String (safe->proc.song.blocks[(size_t) b].name).toUpperCase()),
                            [safe, k, b] { if (safe) safe->edit ("PHRASE BLOCK", [k, b] (Instrument& i) { i.phrases[(size_t) k].block = b; }); } });
        showMenu ("PHRASE " + hex2 (k + 1) + "'S BLOCK", std::move (bl));
    } });
    items.push_back ({ "DELETE PHRASE " + hex2 (k + 1), [safe] { if (safe) safe->deletePhrase(); } });
    showMenu ("PHRASE " + hex2 (k + 1), std::move (items));
}

void PhraseView::mouseDown (const juce::MouseEvent& e)
{
    if (! listArea.contains (e.position)) return;
    const int k = listTop + (int) ((e.position.y - listArea.getY()) / (Theme::px * 10.0f));
    if (k >= (int) inst().phrases.size()) return;
    if (e.mods.isPopupMenu()) { phraseMenu (k); return; }
    select (k);
    if (onStatus) onStatus ("PHRASE " + hex2 (k + 1) + " - RIGHT-CLICK IT FOR MORE (MOVE, ITS BLOCK, EDIT IT IN THE PATTERN)");
}

void PhraseView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! listArea.contains (e.position)) return;
    const int n = (int) inst().phrases.size();
    listTop = juce::jlimit (0, std::max (0, n - listRows()), listTop - (int) std::round (w.deltaY * 6.0f));
    repaint();
}

void PhraseView::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    const int title = (int) (px * 11);
    auto r = getLocalBounds();
    auto left = r.removeFromLeft ((r.getWidth() - gap * 3) / 2);
    r.removeFromLeft (gap * 3);

    // the phrase picked: its keys, length, speed, loop, TRANSPOSE; PLAY and back to SOUND
    const int setH = gap * 4 + title + rowH * 6 + gap * 5;
    auto set = left.removeFromBottom (std::min (left.getHeight() / 2 + rowH * 2, setH));
    left.removeFromBottom (gap * 2);
    setBox = set.toFloat();
    {
        auto in = set.reduced (gap * 2).withTrimmedTop (title);
        auto row = [&] { auto x = in.removeFromTop (rowH); in.removeFromTop (gap); return x; };
        loBox.setBounds (row());
        hiBox.setBounds (row());
        // (the number on the right gets the room it needs; the left one, the rest)
        auto pair = [&] (juce::Component& l, NumberBox& rb)
        {
            auto x = row();
            rb.setBounds (x.removeFromRight (std::min (x.getWidth() - (int) (px * (6 * 9 + 4)) - gap, std::max (x.getWidth() / 2, rb.preferredWidth (px)))));   // (TRANSPOSE keeps its room)
            x.removeFromRight (gap); l.setBounds (x);
        };
        {
            // (two numbers: LPB just what it needs, LINES the rest)
            auto x = row();
            lpbBox.setBounds (x.removeFromRight (lpbBox.preferredWidth (px))); x.removeFromRight (gap);
            linesBox.setBounds (x);
        }
        pair (loopBtn, startBox);
        pair (transposeBtn, endBox);
        auto x = row(); const int half = (x.getWidth() - gap) / 2;
        playBtn.setBounds (x.removeFromLeft (half)); x.removeFromLeft (gap); backBtn.setBounds (x);
    }
    // the list, ADD / CLONE / DEL under it
    listBox = left.toFloat();
    {
        auto in = left.reduced (gap * 2).withTrimmedTop (title);
        auto btns = in.removeFromBottom (rowH); in.removeFromBottom (gap);
        const int w = (btns.getWidth() - gap * 2) / 3;
        addBtn.setBounds (btns.removeFromLeft (w)); btns.removeFromLeft (gap);
        cloneBtn.setBounds (btns.removeFromLeft (w)); btns.removeFromLeft (gap);
        delBtn.setBounds (btns);
        listArea = in.toFloat();
    }
    // the phrase, written here
    gridBox = r.toFloat();
    phraseGrid.setBounds (r.reduced (gap * 2).withTrimmedTop (title));
    refresh();
}

void PhraseView::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    const auto& pal = Theme::palFor (*this);
    const auto& sk = Theme::skin;
    const auto& in = inst();
    const auto* c = current();
    Theme::panelBox (g, listBox, "PHRASES", pal);
    Theme::panelBox (g, setBox, c != nullptr ? "PHRASE " + hex2 (sel + 1) + " (@" + hex2 (sel + 1) + ")" : juce::String ("PHRASE"), pal);
    Theme::panelBox (g, gridBox, c != nullptr ? "BLOCK " + juce::String (c->block).paddedLeft ('0', 3) + " TRACK " + juce::String (c->track + 1)
                                              : juce::String ("PHRASE"), pal);
    g.setColour (sk.bg);
    g.fillRect (listArea);
    const int n = (int) in.phrases.size();
    if (n == 0)
    {
        PixelFont::drawCentred (g, "NONE YET - ADD", listArea, px, sk.dim);
        return;
    }
    const int rows = listRows();
    for (int i = listTop; i < n && i < listTop + rows; ++i)
    {
        const auto row = listRow (i);
        const auto& ph = in.phrases[(size_t) i];
        const int lines = ph.block >= 0 && ph.block < (int) proc.song.blocks.size() ? proc.song.blocks[(size_t) ph.block].lines : 0;
        if (i == sel)
        {
            g.setColour (sk.cursor); g.fillRect (row);
            g.setColour (sk.cursorEdge); g.drawRect (row, std::max (1.0f, px * 0.5f));
        }
        const juce::String t = hex2 (i + 1) + " " + noteText (ph.keyLo) + "-" + noteText (ph.keyHi) + " " + juce::String (lines).paddedLeft ('0', 3) + "L"
                             + (ph.loop ? " LOOP" : "");
        PixelFont::drawLeft (g, t, row.reduced (px * 2.0f, 0.0f), px, sk.text);
    }
}
} // namespace prv::ui
