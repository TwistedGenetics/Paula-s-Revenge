// Paula's Revenge - PHRASES on an instrument's PARAMS page
#include "PhraseView.h"
#include "ChoiceBox.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    juce::String noteText (int n) { return juce::String (prv::noteName ((uint8_t) juce::jlimit (1, (int) kNoteMax, n))); }
}

// =====================================================================================  the page
PhraseView::PhraseView (PaulaProcessor& p) : proc (p)
{
    setOpaque (false);
    phrasePattern.onStatus = [this] (const juce::String& m) { if (onStatus) onStatus (m); };
    phrasePattern.onStateChanged = [this] { syncBack(); if (onChanged) onChanged(); repaint(); };
    phrasePattern.playLineOverride = [this] { return playLine(); };
    addAndMakeVisible (phrasePattern);

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
    phraseState.line = 0; phraseState.column = 0;
    phrasePattern.clearSelection();
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
    syncPattern();
    phrasePattern.repaint();
    repaint();
}

void PhraseView::syncPattern()
{
    // the phrase's block and track in the editor, with the pattern's octave, step and instrument; EDIT always on
    auto& m = proc.editorState;
    auto& s = phraseState;
    s.editMode = true; s.follow = false; s.scopes = false; s.fourTrackView = true; s.loopOn = false;
    s.showFx2 = m.showFx2; s.octave = m.octave; s.step = m.step; s.instrument = m.instrument;
    lastOctave = s.octave; lastStep = s.step;
    const auto* c = current();
    phrasePattern.setVisible (c != nullptr && ! phraseBounds.isEmpty());
    if (c == nullptr) return;
    s.block = c->block;
    if (phrasePattern.onlyTrackShown() != c->track) phrasePattern.setOnlyTrack (c->track);
    s.track = c->track;
    s.line = juce::jlimit (0, std::max (0, proc.song.blocks[(size_t) c->block].lines - 1), s.line);
}

void PhraseView::syncBack()
{
    // (an octave or step changed in the phrase: the pattern's too)
    auto& m = proc.editorState;
    if (phraseState.octave != lastOctave) m.octave = lastOctave = phraseState.octave;
    if (phraseState.step != lastStep) m.step = lastStep = phraseState.step;
}

int PhraseView::playLine() const
{
    // the line playing: heard from here, else a track playing this phrase in the song
    const int i = proc.editorState.instrument;
    if (proc.player.previewPhraseIndex() == sel) return proc.player.previewPhraseLine();
    for (int t = 0; t < kMaxTracks; ++t)
        if (proc.player.phraseInstrument (t) == i && proc.player.phraseIndex (t) == sel) return proc.player.phraseLine (t) - 1;
    return -1;
}

void PhraseView::tick()
{
    if (shownInstrument != proc.editorState.instrument) { play (false); refresh(); }
    // (the pattern's octave / step changed elsewhere: here too; then the editor's own tick - the play line, MIDI notes)
    if (proc.editorState.octave != phraseState.octave || proc.editorState.step != phraseState.step) syncPattern();
    if (phrasePattern.isVisible()) phrasePattern.tick();
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
    phraseBounds = r.reduced (gap * 2).withTrimmedTop (title);
    phrasePattern.setBounds (phraseBounds);
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
    if (c == nullptr && ! phraseBounds.isEmpty())
    {
        g.setColour (sk.bg);
        g.fillRect (phraseBounds);
        PixelFont::drawCentred (g, "NO PHRASE - ADD ONE", phraseBounds.toFloat(), px, sk.dim);
    }
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
