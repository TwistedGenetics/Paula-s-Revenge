// Paula's Revenge - side panels
#include "Panels.h"
#include "../SongFile.h"
#include "../engine/SongOps.h"

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
    for (int i = 0; i < visibleRows() + 1; ++i)
    {
        const int row = scroll + i;
        if (row >= n) break;
        auto rr = juce::Rectangle<float> (0, i * rh, listW, rh);
        if (row == selected)
        {
            g.setColour (Theme::skin.band);
            g.fillRect (rr);
            g.setColour (Theme::skin.text);
            g.drawRect (rr.reduced (1.0f), std::max (1.0f, px * 0.5f));
        }
        const float ty = std::round (rr.getY() + (rh - 7 * px) * 0.5f);
        if (row == playing) PixelFont::addText (text, juce::String::charToString ((juce::juce_wchar) 1), px * 1.0f, ty, px);
        PixelFont::addText (isRowDim (row) ? dim : text, getRowText (row), px * 8.0f, ty, px);
    }
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (0, 0, listW, r.getHeight()).toNearestInt());
    g.setColour (Theme::skin.dim);
    g.fillRectList (dim);
    g.setColour (Theme::skin.text);
    g.fillRectList (text);
    g.restoreState();

    // drop marker while dragging a row
    if (reordering && dropRow >= 0)
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
    dragRow = -1; reordering = false;
    if (getNumRows && row >= 0 && row < getNumRows())
    {
        if (onClick) onClick (row, e);
        if (onMove) dragRow = row;
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
    if (dragRow < 0 || ! onMove) return;
    if (! reordering && e.getDistanceFromDragStart() < 6) return;
    reordering = true;
    // drag past the ends to scroll
    if (e.position.y < 0) scrollBy (-1);
    if (e.position.y > getHeight()) scrollBy (1);
    dropRow = juce::jlimit (0, getNumRows() - 1, rowAt (juce::jlimit (0.0f, (float) getHeight() - 1, e.position.y)));
    repaint();
}

void PixList::mouseUp (const juce::MouseEvent&)
{
    if (reordering && dropRow >= 0 && dropRow != dragRow && onMove) onMove (dragRow, dropRow);
    reordering = false; dragRow = dropRow = -1;
    repeatDir = 0; thumbGrab = -1.0f;
    stopTimer();
    repaint();
}

void PixList::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (barArea().contains (e.position)) return;
    const int row = rowAt (e.position.y);
    if (getNumRows && row >= 0 && row < getNumRows() && onDoubleClick) onDoubleClick (row);
}

void PixList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scrollBy (w.deltaY > 0 ? -1 : (w.deltaY < 0 ? 1 : 0));
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
        return juce::String (r).paddedLeft ('0', 3) + "  BLOCK " + juce::String (proc.song.sequence[(size_t) r]).paddedLeft ('0', 3);
    };
    list.onClick = [this, &st] (int r, const juce::MouseEvent&)
    {
        st.seqPos = r;
        st.block = proc.song.safeBlock (proc.song.sequence[(size_t) r]);
        st.line = 0;
        changed();
    };
    list.onMove = [this, &st] (int from, int to)
    {
        if (pushStructUndo) pushStructUndo();
        bool moved = false;
        proc.withLock ([&] { moved = moveSequenceEntry (proc.song, from, to); });
        if (moved) { st.seqPos = to; changed(); }
    };
    addAndMakeVisible (list);
    upBtn.onClick = [this] { moveSelected (-1); };
    downBtn.onClick = [this] { moveSelected (1); };
    cloneInPlaceBtn.onClick = [this] { cloneInPlace(); };
    addAndMakeVisible (upBtn);
    addAndMakeVisible (downBtn);
    addAndMakeVisible (cloneInPlaceBtn);

    insBtn.onClick = [this, &st]
    {
        if (proc.song.sequence.size() >= (size_t) kMaxSequence) return;
        if (pushStructUndo) pushStructUndo();
        proc.withLock ([&] {
            const int b = proc.song.sequence[(size_t) st.seqPos];
            proc.song.sequence.insert (proc.song.sequence.begin() + st.seqPos + 1, b);
        });
        st.seqPos++;
        changed();
    };
    delBtn.onClick = [this, &st]
    {
        if (proc.song.sequence.size() <= 1) return;
        if (pushStructUndo) pushStructUndo();
        proc.withLock ([&] { proc.song.sequence.erase (proc.song.sequence.begin() + st.seqPos); });
        st.seqPos = std::min (st.seqPos, (int) proc.song.sequence.size() - 1);
        st.block = proc.song.safeBlock (proc.song.sequence[(size_t) st.seqPos]);
        changed();
    };
    blockBox.valueChars = 3;
    blockBox.labelChars = 6;
    blockBox.format = [] (int v) { return juce::String (v).paddedLeft ('0', 3); };
    blockBox.onChange = [this, &st] (int v)
    {
        if (pushStructUndo) pushStructUndo();
        proc.withLock ([&] { proc.song.sequence[(size_t) st.seqPos] = proc.song.safeBlock (v); });
        st.block = proc.song.safeBlock (v);
        st.line = std::min (st.line, proc.song.blocks[(size_t) st.block].lines - 1);
        changed();
    };
    addAndMakeVisible (insBtn);
    addAndMakeVisible (delBtn);
    addAndMakeVisible (blockBox);
}

void SequencePanel::moveSelected (int delta)
{
    auto& st = proc.editorState;
    const int to = st.seqPos + delta;
    if (to < 0 || to >= (int) proc.song.sequence.size()) return;
    if (pushStructUndo) pushStructUndo();
    proc.withLock ([&] { moveSequenceEntry (proc.song, st.seqPos, to); });
    st.seqPos = to;
    changed();
}

void SequencePanel::cloneInPlace()
{
    auto& st = proc.editorState;
    if ((int) proc.song.blocks.size() >= kMaxBlocks) return;
    if (pushStructUndo) pushStructUndo();
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
    proc.withLock ([&] { std::swap (proc.song.blocks, blocks); std::swap (proc.song.sequence, seq); });
    st.block = nb;
    changed();
}

void SequencePanel::refresh()
{
    auto& st = proc.editorState;
    st.seqPos = juce::jlimit (0, (int) proc.song.sequence.size() - 1, st.seqPos);
    list.selected = st.seqPos;
    list.playing = proc.player.getMode() == Player::Mode::Song ? proc.player.uiSeqPos.load() : -1;
    list.ensureVisible (st.seqPos);
    blockBox.setRange (0, (int) proc.song.blocks.size() - 1);
    blockBox.setValue (proc.song.sequence[(size_t) st.seqPos]);
    list.repaint();
}

void SequencePanel::paint (juce::Graphics& g) { Theme::panelBox (g, getLocalBounds().toFloat(), "SEQUENCE"); }

void SequencePanel::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    const int gap = (int) px;
    auto row2 = r.removeFromBottom (rowH);
    r.removeFromBottom (gap);
    auto bottom = r.removeFromBottom (rowH);
    const int bw = (int) (px * 6 * 5);
    delBtn.setBounds (bottom.removeFromRight (bw));
    bottom.removeFromRight (gap);
    insBtn.setBounds (bottom.removeFromRight (bw));
    bottom.removeFromRight (gap * 2);
    blockBox.setBounds (bottom);
    const int aw = (int) (px * 6 * 3);
    upBtn.setBounds (row2.removeFromLeft (aw)); row2.removeFromLeft (gap);
    downBtn.setBounds (row2.removeFromLeft (aw)); row2.removeFromLeft (gap * 2);
    cloneInPlaceBtn.setBounds (row2);
    r.removeFromBottom ((int) px * 2);
    list.setBounds (r);
}

// =========================================================================== Block
BlockPanel::BlockPanel (PaulaProcessor& p, std::function<void()> c, std::function<void (int)> u)
    : proc (p), changed (std::move (c)), pushUndo (std::move (u))
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
        pushUndo (st.block);
        proc.withLock ([&] { proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].lines = juce::jlimit (1, kMaxLines, v); });
        st.line = std::min (st.line, v - 1);
        changed();
    };
    newBtn.onClick = [this, &st]
    {
        if (pushStructUndo) pushStructUndo();
        if (proc.song.blocks.size() >= (size_t) kMaxBlocks) return;
        proc.withLock ([&] { proc.song.blocks.push_back (Block{}); });
        st.block = (int) proc.song.blocks.size() - 1;
        st.line = 0;
        changed();
    };
    cloneBtn.onClick = [this, &st]
    {
        if (pushStructUndo) pushStructUndo();
        if (proc.song.blocks.size() >= (size_t) kMaxBlocks) return;
        proc.withLock ([&] { proc.song.blocks.push_back (proc.song.blocks[(size_t) proc.song.safeBlock (st.block)]); });
        st.block = (int) proc.song.blocks.size() - 1;
        changed();
    };
    delBtn.onClick = [this, &st]
    {
        if (pushStructUndo) pushStructUndo();
        if (proc.song.blocks.size() <= 1) return;
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
        if (pushStructUndo) pushStructUndo();
        proc.withLock ([&] { swapBlockNumbers (proc.song, a, b); });
        st.block = b;
        changed();
    };
    numDownBtn.onClick = [swapWith] { swapWith (-1); };
    numUpBtn.onClick = [swapWith] { swapWith (1); };
    for (auto* c2 : std::initializer_list<juce::Component*> { &current, &lines, &newBtn, &cloneBtn, &delBtn, &countLabel, &numDownBtn, &numUpBtn })
        addAndMakeVisible (c2);
}

void BlockPanel::refresh()
{
    auto& st = proc.editorState;
    current.setRange (0, (int) proc.song.blocks.size() - 1);
    current.setValue (proc.song.safeBlock (st.block));
    lines.setValue (proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].lines);
    countLabel.setText ("OF " + juce::String ((int) proc.song.blocks.size()).paddedLeft ('0', 3));
}

void BlockPanel::paint (juce::Graphics& g) { Theme::panelBox (g, getLocalBounds().toFloat(), "BLOCK"); }

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
        PixelFont::drawCentred (g, "NO SAMPLE", inner, px, Theme::skin.dim);
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
    Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f));
}

void WaveView::mouseDoubleClick (const juce::MouseEvent&) { if (openEditor) openEditor(); }

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    const auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
    dragging = 0;
    if (! in.isLooped()) return;
    const float w = (float) std::max (1, getWidth() - 4);
    const float n = (float) in.data.size();
    const float xs = 2.0f + w * in.loopStart / n;
    const float xe = 2.0f + w * (in.loopStart + in.loopLength) / n;
    dragging = std::abs (e.position.x - xs) < std::abs (e.position.x - xe) ? 1 : 2;
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
    auto& st = proc.editorState;
    list.getNumRows = [] { return kMaxInstruments; };
    list.getRowText = [this] (int r)
    {
        const auto& in = proc.song.instruments[(size_t) r];
        const juce::String hex = juce::String::toHexString (r + 1).toUpperCase().paddedLeft ('0', 2);
        return hex + " " + (in.name.empty() ? (in.hasData() ? juce::String ("(NO NAME)") : juce::String ("-")) : juce::String (in.name));
    };
    list.isRowDim = [this] (int r) { return ! proc.song.instruments[(size_t) r].hasData(); };
    list.onClick = [this, &st] (int r, const juce::MouseEvent& e)
    {
        st.instrument = r;
        proc.jamInstrument = r;
        if (e.mods.isAltDown() || e.mods.isRightButtonDown()) proc.withLock ([&] { proc.player.jamNoteOn (37, r); });
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
        proc.withLock ([&] { std::swap (proc.song.instruments[(size_t) proc.editorState.instrument], empty); proc.player.jamAllOff(); });
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
            safe->proc.withLock ([&] { safe->proc.song.instruments[(size_t) idx].name = name; });
            safe->changed();
        }), true);
    };

    vol.onChange   = [this] (int v) { editInstrument ([v] (Instrument& in) { in.volume = v; }); };
    fine.onChange  = [this] (int v) { editInstrument ([v] (Instrument& in) { in.finetune = v; }); };
    trans.onChange = [this] (int v) { editInstrument ([v] (Instrument& in) { in.transpose = v; }); };
    loopBtn.onClick = [this]
    {
        const bool on = loopBtn.getToggleState();
        editInstrument ([on] (Instrument& in)
        {
            if (on && in.hasData()) { in.loopStart = 0; in.loopLength = (int) in.data.size(); }
            else { in.loopStart = 0; in.loopLength = 0; }
        });
    };
    loopStart.onChange = [this] (int v) { editInstrument ([v] (Instrument& in) {
        if (! in.isLooped()) return;
        const int end = in.loopStart + in.loopLength;
        in.loopStart = juce::jlimit (0, end - 4, v); in.loopLength = end - in.loopStart; }); };
    loopLen.onChange = [this] (int v) { editInstrument ([v] (Instrument& in) {
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

template <typename Fn> void InstrumentPanel::editInstrument (Fn&& fn)
{
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

    // bottom up: wave, loop row, params, buttons
    wave.setBounds (r.removeFromBottom ((int) (px * 14)));
    r.removeFromBottom (gap);
    auto loopRow = r.removeFromBottom (rowH);
    loopStart.setBounds (loopRow.removeFromLeft (loopRow.getWidth() / 2 - (int) px));
    loopRow.removeFromLeft ((int) px * 2);
    loopLen.setBounds (loopRow);
    r.removeFromBottom (gap);
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
} // namespace prv::ui
