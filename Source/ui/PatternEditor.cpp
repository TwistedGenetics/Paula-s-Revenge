// Paula's Revenge - the pattern (block) editor
#include "PatternEditor.h"
#include "../engine/Commands.h"
#include "../engine/SongOps.h"

namespace prv::ui
{
namespace
{
    constexpr int kColOffset[9] = { 0, 4, 5, 7, 8, 9, 11, 12, 13 };   // char offset of each cursor column in a track
    constexpr int kLineNumChars = 4;

    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
}

PatternEditor::PatternEditor (PaulaProcessor& p) : proc (p), st (p.editorState)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    wheel.stepsPerUnit = 12.0f;
    edgeScroll.onStep = [this] (int dir)
    {
        if (proc.player.isPlaying() && st.follow) return;
        const float maxTop = (float) std::max (0, blockLines() - fullRows());
        const float nt = juce::jlimit (0.0f, maxTop, viewTop + (float) dir);
        if (nt == viewTop) return;
        viewTop = nt;
        applyDrag (lastDragPos);
        repaint();
    };
}

float PatternEditor::rowsArea() const { return std::max (1.0f, (float) getHeight() - headerH() - helpHeight()); }
int PatternEditor::fullRows() const   { return std::max (1, (int) std::floor (rowsArea() / rowH())); }

void PatternEditor::updateView (bool fromTick)
{
    const int lines = blockLines();
    const int full = fullRows();
    const float maxTop = (float) std::max (0, lines - full);
    const int blk = proc.song.safeBlock (st.block);
    const bool following = proc.player.isPlaying() && st.follow;
    if (following)
    {
        // playback glides through the view, the playing line about 40% down
        const float target = juce::jlimit (0.0f, maxTop, (float) (st.line - (int) std::floor (full * 0.4f)));
        if (blk != viewBlock || std::abs (target - viewTop) > (float) full) viewTop = target;
        else if (fromTick)
        {
            viewTop += (target - viewTop) * 0.35f;
            if (std::abs (target - viewTop) < 0.02f) viewTop = target;
        }
    }
    else
    {
        viewTop = std::round (viewTop);
        if (st.line != viewLine || blk != viewBlock || full != viewFull)
        {
            // keep the cursor on screen: keys keep a few lines of context, a click doesn't scroll
            const int m = wantMargin ? std::min (3, (full - 1) / 4) : 0;
            if ((float) st.line < viewTop + (float) m) viewTop = (float) (st.line - m);
            if ((float) st.line > viewTop + (float) (full - 1 - m)) viewTop = (float) (st.line - (full - 1 - m));
        }
    }
    viewTop = juce::jlimit (0.0f, maxTop, viewTop);
    viewLine = st.line;
    viewBlock = blk;
    viewFull = full;
}

// ---------------------------------------------------------------- geometry
int PatternEditor::trackChars() const { return st.showFx2 ? 17 : 13; }   // "C-3 01 C20 L40" + gap + VU bar
int PatternEditor::numColumns() const { return st.showFx2 ? 9 : 6; }

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
    const int chars = kLineNumChars + visibleTracks() * trackChars();
    const float maxPx = Theme::px * 1.5f;
    return PixelFont::fitPx (chars, (float) getWidth() - 8.0f, maxPx);
}

float PatternEditor::helpHeight() const { return st.column >= 3 ? Theme::px * 12 : 0.0f; }

float PatternEditor::rowH() const     { return fontPx() * (PixelFont::kCellH + 3); }
float PatternEditor::headerH() const  { return fontPx() * 24; }

juce::Rectangle<float> PatternEditor::headerRect (int slot) const
{
    const float cw = fontPx() * PixelFont::kCellW;
    return juce::Rectangle<float> (lineNumW() + slot * trackW() - cw * 0.5f, 0, trackW() - cw * 0.5f, headerH()).reduced (fontPx() * 0.5f, fontPx());
}

juce::Rectangle<float> PatternEditor::panRect (int slot) const
{
    // the trough sits to the right of a 4-character label
    auto r = headerRect (slot);
    auto row = r.withTrimmedTop (r.getHeight() * 0.5f).reduced (fontPx() * 1.5f, fontPx() * 3.0f);
    return row.withTrimmedLeft (fontPx() * PixelFont::kCellW * 4.5f);
}

namespace
{
    juce::String panText (int p)
    {
        if (p < 0) return "AUTO";
        if (std::abs (p - 128) <= 2) return "C";
        const int pct = juce::roundToInt (std::abs (p - 128) / 127.0 * 100.0);
        return juce::String (p < 128 ? "L" : "R") + juce::String (std::min (pct, 99));
    }
}

void PatternEditor::setPanFromX (int t, float x)
{
    auto pr = panRect (t - firstTrack());
    int v = juce::jlimit (0, 255, juce::roundToInt ((x - pr.getX()) / std::max (1.0f, pr.getWidth()) * 255.0f));
    if (std::abs (v - 128) <= 6) v = 128;   // snap to centre
    proc.history.settings ("TRACK " + juce::String (t + 1) + " PAN", true);
    proc.withLock ([&] { proc.song.trackPan[t] = v; });
    repaint();
}
juce::Rectangle<float> PatternEditor::cornerRect() const
{
    const float px = fontPx();
    const float cw = px * PixelFont::kCellW;
    return juce::Rectangle<float> (0, 0, lineNumW() - cw * 0.75f, headerH()).reduced (px * 0.75f, px);
}

int PatternEditor::lineAtY (float y) const
{
    return (int) std::floor (viewTop + (y - headerH()) / rowH());
}

float PatternEditor::trackW() const   { return fontPx() * PixelFont::kCellW * trackChars(); }
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

    // ---- track headers: name/mute on top, pan control below
    g.setColour (Theme::panel);
    g.fillRect (bounds.withHeight (hh));
    for (int s = 0; s < nVis; ++s)
    {
        const int t = first + s;
        auto r = headerRect (s);
        const bool muted = proc.player.mute[t].load();
        g.setColour (muted ? Theme::pressed : Theme::panel);
        g.fillRect (r);
        Theme::bevel (g, r, ! muted, line);
        auto top = r.withHeight (r.getHeight() * 0.5f);
        g.setColour (muted ? Theme::faint : sk.track[t]);
        g.fillRect (top.removeFromBottom (px * 1.5f).reduced (px, 0));
        juce::String label = muted ? "MUTED" : "TRACK " + juce::String (t + 1);
        if (PixelFont::textWidth (label, px) > top.getWidth() - px * 2) label = muted ? "M" + juce::String (t + 1) : "T" + juce::String (t + 1);
        PixelFont::drawCentred (g, label, top, px, Theme::ink);

        // pan: a small trough with a marker; text above it
        const int pan = proc.song.trackPan[t];
        auto pr = panRect (s);
        const juce::String pt = panText (pan);
        auto txt = juce::Rectangle<float> (r.getX() + px * 1.5f, pr.getY(), pr.getX() - r.getX() - px * 2.0f, pr.getHeight());
        PixelFont::drawLeft (g, pt, txt, px, pan < 0 ? Theme::faint : Theme::ink);
        auto trough = pr.withSizeKeepingCentre (pr.getWidth(), std::max (4.0f, px * 3.0f));
        g.setColour (Theme::paper);
        g.fillRect (trough);
        Theme::bevel (g, trough, false, 1.0f);
        g.setColour (Theme::faint);
        g.fillRect (trough.getCentreX() - 0.5f, trough.getY(), 1.0f, trough.getHeight());
        const float mx = trough.getX() + trough.getWidth() * (pan < 0 ? 0.5f : pan / 255.0f);
        g.setColour (pan < 0 ? Theme::faint : Theme::ink);
        g.fillRect (mx - px * 0.75f, trough.getY() - px * 0.5f, px * 1.5f, trough.getHeight() + px);
    }

    // ---- corner: the block being edited
    {
        auto cr = cornerRect();
        g.setColour (Theme::panel);
        g.fillRect (cr);
        Theme::bevel (g, cr, true, line);
        auto top = cr.withHeight (cr.getHeight() * 0.42f);
        PixelFont::drawCentred (g, "BLK", top, px, Theme::ink);
        const juce::String num = juce::String (proc.song.safeBlock (st.block)).paddedLeft ('0', 3);
        float np = px * 1.6f;
        while (np > px && PixelFont::textWidth (num, np) > cr.getWidth() - px) np -= px * 0.1f;
        PixelFont::drawCentred (g, num, cr.withTrimmedTop (top.getHeight()).withTrimmedBottom (px * 0.5f), np, Theme::ink);
    }

    // ---- rows
    const auto& song = proc.song;
    const int blk = song.safeBlock (st.block);
    const auto& b = song.blocks[(size_t) blk];
    const int lines = b.lines;
    const float helpH = helpHeight();
    const float area = bounds.getHeight() - hh - helpH;
    if (! (proc.player.isPlaying() && st.follow)) updateView (false);
    const int firstRow = (int) std::floor (viewTop);
    const int visRows = (int) std::ceil (area / rh) + 1;
    auto rowY = [&] (int l) { return std::round (hh + ((float) l - viewTop) * rh); };
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (0, hh, bounds.getWidth(), area).toNearestInt());

    const bool playing = proc.player.isPlaying();
    const int playBlock = proc.player.uiBlock.load();
    const int playLine  = proc.player.uiLine.load();

    juce::RectangleList<float> text, dimText;
    for (int r = 0; r < visRows; ++r)
    {
        const int l = firstRow + r;
        if (l < 0 || l >= lines) continue;
        const float y = rowY (l);
        auto rowRect = juce::Rectangle<float> (0, y, bounds.getWidth(), rh);
        const bool cursorRow = l == st.line;

        if (cursorRow)         { g.setColour (sk.band);   g.fillRect (rowRect); }
        else if (l % 4 == 0)   { g.setColour (sk.stripe); g.fillRect (rowRect); }
        if (playing && playBlock == blk && playLine == l && ! (st.follow && cursorRow))
        {
            g.setColour (sk.text);
            g.fillRect (rowRect.withHeight (line));
            g.fillRect (rowRect.withHeight (line).withBottomY (rowRect.getBottom()));
        }

        if (st.loopBlock == blk && l >= st.loopFrom && l <= st.loopTo)
        {
            auto lr = rowRect.withWidth (lnw - cw * 0.75f - line);
            g.setColour (st.loopOn ? sk.hot.withAlpha (0.45f) : sk.dim.withAlpha (0.35f));
            g.fillRect (lr);
            g.setColour (st.loopOn ? sk.hot : sk.dim);
            g.fillRect (lr.withWidth (px));
            if (l == st.loopFrom) g.fillRect (lr.withHeight (line));
            if (l == st.loopTo)   g.fillRect (lr.withHeight (line).withBottomY (lr.getBottom()));
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
            auto fx = [] (uint8_t cm, uint8_t pa) { return (cm == 0 && pa == 0) ? juce::String ("---") : juce::String::charToString ((juce::juce_wchar) commandChar (cm)) + hex2 (pa); };
            PixelFont::addText (c.note ? text : dimText, note, x, ty, px);
            PixelFont::addText (c.instr ? text : dimText, ins, x + 4 * cw, ty, px);
            PixelFont::addText (c.fx1Empty() ? dimText : text, fx (c.cmd, c.param), x + 7 * cw, ty, px);
            if (st.showFx2) PixelFont::addText (c.fx2Empty() ? dimText : text, fx (c.cmd2, c.param2), x + 11 * cw, ty, px);
        }

        if (cursorRow && st.track >= first && st.track < first + nVis)
        {
            const float cx = colX (st.track - first, st.column) - px * 0.5f;
            auto cr = juce::Rectangle<float> (cx, y + px * 0.5f, colChars (st.column) * cw, rh - px);
            g.setColour (sk.cursor);
            g.fillRect (cr);
            g.setColour (sk.cursorEdge);
            g.drawRect (cr, std::max (1.0f, px * (st.editMode ? 0.75f : 0.4f)));
        }
    }

    // ---- mouse selection
    if (hasSelection())
    {
        const int gpt = groupsPerTrack();
        static const int groupCol[3] = { 0, 3, 6 };
        static const int groupChars[3] = { 6, 3, 3 };
        g.setColour (sk.hot.withAlpha (0.28f));
        for (int r = 0; r < visRows; ++r)
        {
            const int l = firstRow + r;
            if (l < sel.l0 || l > sel.l1) continue;
            const float y = rowY (l);
            for (int c = sel.c0; c <= sel.c1; ++c)
            {
                const int slot = c / gpt - first, grp = c % gpt;
                if (slot < 0 || slot >= nVis) continue;
                // join neighbouring groups so the selection reads as one block
                const float xa = colX (slot, groupCol[grp]) - px * 0.75f;
                const float xb = (c < sel.c1 && (c + 1) / gpt == c / gpt) ? colX (slot, groupCol[grp + 1]) - px * 0.75f
                                                                           : xa + groupChars[grp] * cw + px;
                g.fillRect (juce::Rectangle<float> (xa, y, xb - xa, rh));
            }
        }
    }

    // the rows' text, clipped to the rows area
    g.setColour (sk.dim);
    g.fillRectList (dimText);
    g.setColour (sk.text);
    g.fillRectList (text);
    g.restoreState();

    // ---- per-track VU bars: a segmented column at the right edge of each track
    const float segH = std::max (2.0f, px * 3);
    const float vuTop = hh + px * 2, vuBottom = bounds.getBottom() - px * 2;
    const int segs = std::max (4, (int) ((vuBottom - vuTop) / segH));
    for (int s = 0; s < nVis; ++s)
    {
        const int t = first + s;
        const float x = lnw + s * tw + (st.showFx2 ? 14.4f : 10.4f) * cw;
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

    // effect help: what the command under the cursor does, in full-size text
    if (helpH > 0)
    {
        auto hr = bounds.withTop (bounds.getBottom() - helpH);
        g.setColour (Theme::panel);
        g.fillRect (hr);
        Theme::bevel (g, hr.reduced (1.0f), true, std::max (1.0f, Theme::px * 0.5f));
        const auto& c = b.at (st.line, st.track);
        const bool second = st.column >= 6;
        const uint8_t cm = second ? c.cmd2 : c.cmd, pa = second ? c.param2 : c.param;
        juce::String help = (cm == 0 && pa == 0)
            ? juce::String ("FX ") + (second ? "2" : "1") + ": TYPE 0-F (CLASSIC) OR G-Z (EXTRA), THEN TWO HEX DIGITS"
            : contextDescribe (st.line, st.track, cm, pa);
        float hp = Theme::px;
        while (hp > 1.5f && PixelFont::textWidth (help, hp) > hr.getWidth() - Theme::px * 4) hp -= 0.25f;
        PixelFont::drawLeft (g, help, hr.reduced (Theme::px * 2, 0), hp, Theme::ink);
    }

    g.setColour (Theme::ink);
    if (st.editMode) g.drawRect (bounds.reduced (1.0f), std::max (2.0f, px));
    else             g.drawRect (bounds, 1.0f);

    if (hasKeyboardFocus (false) == false)
    {
        const float hp = std::max (1.5f, px * 0.75f);
        const juce::String hint = "CLICK HERE TO TYPE";
        auto box = juce::Rectangle<float> (PixelFont::textWidth (hint, hp) + hp * 4, hp * 11)
                       .withPosition (bounds.getRight() - PixelFont::textWidth (hint, hp) - hp * 6, bounds.getBottom() - helpHeight() - hp * 14);
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
        if (st.editMode) { pushUndo (st.block, "MIDI NOTE"); proc.withLock ([&] {
                auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
                c.note = (uint8_t) note; c.instr = (uint8_t) (st.instrument + 1); });
            advance(); changed(); }

    const float oldTop = viewTop;
    if (proc.player.isPlaying() && st.follow) updateView (true);
    if (viewTop != oldTop) dirty = true;

    if (dirty || st.line != lastDrawnLine || st.block != lastDrawnBlock) repaint();
    lastDrawnLine = st.line; lastDrawnBlock = st.block;
}

void PatternEditor::openCommandMenu (int line, int track, int group)
{
    if (picker == nullptr)
    {
        picker = std::make_unique<CommandPicker>();
        picker->onPick = [this] (const CommandInfo& c) { applyPickedCommand (c); };
        picker->onCancel = [this] { closeCommandMenu(); };
        // over the whole window, so the descriptions have room
        if (auto* top = getParentComponent()) top->addChildComponent (*picker);
        else addChildComponent (*picker);
    }
    if (group == 2 && ! st.showFx2) group = 1;
    pickLine = juce::jlimit (0, blockLines() - 1, line);
    pickTrack = juce::jlimit (0, proc.song.numTracks - 1, track);
    pickGroup = group;
    // the cursor goes to that effect cell
    st.line = pickLine; st.track = pickTrack; st.column = group == 2 ? 6 : 3;
    changed();
    const auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (pickLine, pickTrack);
    placePicker();
    const int L = pickLine, T = pickTrack;
    picker->nameFor = [this, L, T] (const CommandInfo& ci) { return contextName (L, T, ci.cmd); };
    picker->overviewFor = [this, L, T] (const CommandInfo& ci) { return contextOverview (L, T, ci.cmd); };
    picker->describe = [this, L, T] (uint8_t cm, uint8_t pa) { return contextDescribe (L, T, cm, pa); };
    picker->open (group == 2 ? c.cmd2 : c.cmd, group == 2 ? c.param2 : c.param);
    if (onStatus) onStatus ("PICK A COMMAND (CLICK, OR ARROWS + ENTER; TYPE ITS LETTER TO JUMP) - ESC CANCELS");
}

// ---------------------------------------------------------------- command context
int PatternEditor::instrumentAt (int line, int track) const
{
    const auto& b = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)];
    for (int l = std::min (line, b.lines - 1); l >= 0; --l)
        if (b.at (l, track).instr > 0) return b.at (l, track).instr - 1;
    return -1;
}

namespace
{
    const uint8_t kKnobCmds[kInstrumentKnobs] = { cmdLetter ('L'), cmdLetter ('Q'), cmdLetter ('H'), cmdLetter ('W') };
    int knobForCmd (uint8_t c) { for (int k = 0; k < kInstrumentKnobs; ++k) if (kKnobCmds[k] == c) return k; return -1; }
    bool sampleOnly (uint8_t c)
    {
        return c == 0x9 || c == cmdLetter ('P') || c == cmdLetter ('S') || c == cmdLetter ('U') || c == cmdLetter ('Z');
    }
}

juce::String PatternEditor::contextName (int line, int track, uint8_t cmd) const
{
    const int ins = instrumentAt (line, track);
    const bool plug = ins >= 0 && proc.song.instruments[(size_t) ins].isPlugin();
    if (plug && knobForCmd (cmd) >= 0) return "KNOB " + juce::String::charToString ((juce::juce_wchar) commandChar (cmd));
    if (plug && cmd == cmdLetter ('O')) return "MOD WHEEL";
    return {};
}

juce::String PatternEditor::contextOverview (int line, int track, uint8_t cmd) const
{
    const int ins = instrumentAt (line, track);
    const bool plug = ins >= 0 && proc.song.instruments[(size_t) ins].isPlugin();
    if (cmd == cmdLetter ('V') || cmd == cmdLetter ('J'))
    {
        const auto& t = proc.song.mixer.trackAuto[juce::jlimit (0, kMaxTracks - 1, track)][cmd == cmdLetter ('V') ? 0 : 1];
        return t.valid() ? juce::String (t.name) : juce::String ("NOT SET: LEARN IT IN THE MIXER (EFFECT PAGE)");
    }
    if (plug)
    {
        const int k = knobForCmd (cmd);
        if (k >= 0)
        {
            const auto& t = proc.song.instruments[(size_t) ins].knobs[k];
            return t.param >= 0 ? juce::String (t.name) : juce::String ("NOT SET: LEARN IT ON THE INSTRUMENT'S PAGE (PARAMS)");
        }
        if (cmd == cmdLetter ('O')) return "SENDS THE MOD WHEEL (CC1), 00-FF";
        if (sampleOnly (cmd)) return "NOT USED BY PLUG-IN INSTRUMENTS";
    }
    return {};
}

juce::String PatternEditor::contextDescribe (int line, int track, uint8_t cmd, uint8_t param) const
{
    const juce::String head = juce::String::charToString ((juce::juce_wchar) commandChar (cmd)) + juce::String::toHexString ((int) param).toUpperCase().paddedLeft ('0', 2) + ": ";
    const int pct = juce::roundToInt (param / 255.0 * 100.0);
    const int ins = instrumentAt (line, track);
    const bool plug = ins >= 0 && proc.song.instruments[(size_t) ins].isPlugin();
    if (cmd == cmdLetter ('V') || cmd == cmdLetter ('J'))
    {
        const bool a = cmd == cmdLetter ('V');
        const auto& t = proc.song.mixer.trackAuto[juce::jlimit (0, kMaxTracks - 1, track)][a ? 0 : 1];
        if (! t.valid()) return head + "AUTO " + (a ? "A" : "B") + " ISN'T SET - LEARN IT ON AN EFFECT PAGE IN THE MIXER";
        return head + juce::String (t.name) + " " + juce::String (pct) + "%";
    }
    if (plug)
    {
        const int k = knobForCmd (cmd);
        if (k >= 0)
        {
            const auto& t = proc.song.instruments[(size_t) ins].knobs[k];
            if (t.param < 0) return head + "KNOB " + juce::String::charToString ((juce::juce_wchar) commandChar (cmd)) + " ISN'T SET - LEARN IT ON THE INSTRUMENT'S PAGE";
            return head + juce::String (t.name) + " " + juce::String (pct) + "%";
        }
        if (cmd == cmdLetter ('O')) return head + "MOD WHEEL " + juce::String (param >> 1);
        if (sampleOnly (cmd)) return head + "NOT USED BY PLUG-IN INSTRUMENTS";
    }
    return juce::String (describeCommand (cmd, param));
}

void PatternEditor::placePicker()
{
    // across the window, from the top of the pattern down
    const float px = Theme::px;
    auto* parent = picker->getParentComponent();
    juce::Rectangle<int> r = parent == this ? getLocalBounds() : parent->getLocalBounds().withTop (getY()).withBottom (getBottom());
    r = r.reduced ((int) (px * 3));
    const int want = (int) (px * PixelFont::kCellW * 78 + px * 16);
    r = r.withSizeKeepingCentre (std::min (r.getWidth(), want), r.getHeight());
    picker->setBounds (r);
}

void PatternEditor::closeCommandMenu()
{
    if (picker != nullptr) picker->setVisible (false);
    grabKeyboardFocus();
    repaint();
}

void PatternEditor::applyPickedCommand (const CommandInfo& info)
{
    const int blk = proc.song.safeBlock (st.block);
    const auto& cur = proc.song.blocks[(size_t) blk].at (pickLine, pickTrack);
    const uint8_t oldCmd = pickGroup == 2 ? cur.cmd2 : cur.cmd, oldParam = pickGroup == 2 ? cur.param2 : cur.param;
    // keep the value already there when it's the same command, else start from a useful one
    uint8_t param = info.param;
    if (oldCmd == info.cmd && ! (oldCmd == 0 && oldParam == 0))
    {
        if (! info.extended) param = oldParam;
        else if ((oldParam >> 4) == (info.param >> 4)) param = oldParam;
    }
    pushUndo (blk, "COMMAND " + juce::String (info.code) + " " + juce::String (info.name));
    proc.withLock ([&]
    {
        auto& c = proc.song.blocks[(size_t) blk].at (pickLine, pickTrack);
        if (pickGroup == 2) { c.cmd2 = info.cmd; c.param2 = param; }
        else { c.cmd = info.cmd; c.param = param; }
    });
    closeCommandMenu();
    // the cursor sits on the value, ready to type over it (on the low digit for Exy)
    st.line = pickLine; st.track = pickTrack;
    st.column = (pickGroup == 2 ? 7 : 4) + (info.extended ? 1 : 0);
    changed();
    if (onStatus) onStatus (contextDescribe (pickLine, pickTrack, info.cmd, param) + "  -  TYPE HEX TO CHANGE THE VALUE");
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
    // hit-test against the view as it's shown (the cursor may have moved since the last paint)
    if (! (proc.player.isPlaying() && st.follow)) updateView (false);
    wantMargin = false;
    lastDragPos = e.position;
    panDragTrack = -1;
    const float px = fontPx();
    const float hh = headerH();
    const int nVis = visibleTracks();
    const int first = firstTrack();

    loopDragFrom = -1;
    if (cornerRect().contains (e.position)) { askBlockNumber(); return; }
    const int slot = (int) std::floor ((e.position.x - lineNumW() + px * PixelFont::kCellW * 0.5f) / trackW());
    if (e.position.y < hh)
    {
        if (slot >= 0 && slot < nVis && e.position.y > headerRect (slot).getCentreY())
        {
            // pan row: drag to pan, double-click for automatic (Amiga) panning
            panDragTrack = first + slot;
            setPanFromX (panDragTrack, e.position.x);
            return;
        }
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

    const int l = juce::jlimit (0, blockLines() - 1, lineAtY (e.position.y));
    if (e.position.x < lineNumW() - px * PixelFont::kCellW * 0.75f)
    {
        // line numbers: drag marks a loop; shift+click loops from the cursor line to here
        if (e.mods.isShiftDown()) { setLoopRange (st.line, l); return; }
        loopDragFrom = l;   // (the view stays put)
        return;
    }
    int line = 0, track = 0, column = 0;
    if (! cellAt (e.position, line, track, column)) return;
    if (e.mods.isPopupMenu())
    {
        // right-click: pick an effect command from a list (FX2 when clicked there)
        openCommandMenu (line, track, groupOfColumn (column) == 2 ? 2 : 1);
        return;
    }
    if (e.mods.isShiftDown())
    {
        // shift+click: select from the cursor to here
        selectRange (st.line, line, st.track, groupOfColumn (st.column), track, groupOfColumn (column));
        return;
    }
    // a click moves the cursor; a drag selects (decided on mouse up / first drag)
    downInGrid = true; selDragging = false;
    downLine = line; downTrack = track; downColumn = column;
}

bool PatternEditor::cellAt (juce::Point<float> p, int& line, int& track, int& column) const
{
    const float px = fontPx();
    const int slot = (int) std::floor ((p.x - lineNumW() + px * PixelFont::kCellW * 0.5f) / trackW());
    if (slot < 0 || slot >= visibleTracks() || p.y < headerH()) return false;
    line = juce::jlimit (0, blockLines() - 1, lineAtY (p.y));
    track = firstTrack() + slot;
    const float rel = p.x - colX (slot, 0);
    const int ch = (int) std::floor (rel / (px * PixelFont::kCellW));
    column = 0;
    for (int c = numColumns() - 1; c >= 0; --c) if (ch >= kColOffset[c]) { column = c; break; }
    return true;
}

void PatternEditor::mouseUp (const juce::MouseEvent&)
{
    edgeScroll.stop();
    if (downInGrid && ! selDragging)
    {
        // plain click: move the cursor there and drop any selection
        clearSelection();
        if (! (proc.player.isPlaying() && st.follow)) st.line = downLine;
        st.track = downTrack;
        st.column = downColumn;
        changed();
    }
    downInGrid = selDragging = false;
}

void PatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (panDragTrack >= 0) { setPanFromX (panDragTrack, e.position.x); return; }
    if (e.getDistanceFromDragStart() <= 3 && ! selDragging) return;
    if (downInGrid) selDragging = true;
    if (! selDragging && loopDragFrom < 0) return;
    lastDragPos = e.position;
    applyDrag (e.position);
    // held past the top or bottom: scroll at a steady pace (faster further out)
    const float top = headerH() + rowH() * 0.5f, bottom = (float) getHeight() - helpHeight() - rowH() * 0.5f;
    const float over = e.position.y < top ? e.position.y - top : (e.position.y > bottom ? e.position.y - bottom : 0.0f);
    edgeScroll.update (over, rowH());
}

void PatternEditor::applyDrag (juce::Point<float> pos)
{
    const float top = headerH(), bottom = (float) getHeight() - helpHeight();
    auto p = pos;
    p.y = juce::jlimit (top + 1.0f, bottom - 1.0f, p.y);
    if (loopDragFrom >= 0)
    {
        const int l = juce::jlimit (0, blockLines() - 1, lineAtY (p.y));
        setLoopRange (loopDragFrom, l);
    }
    if (downInGrid && selDragging)
    {
        p.x = juce::jlimit (lineNumW(), lineNumW() + trackW() * visibleTracks() - 1.0f, p.x);
        int line = 0, track = 0, column = 0;
        if (cellAt (p, line, track, column))
            selectRange (downLine, line, downTrack, groupOfColumn (downColumn), track, groupOfColumn (column));
    }
}

// ---------------------------------------------------------------- selection
bool PatternEditor::hasSelection() const { return sel.block >= 0 && sel.block == proc.song.safeBlock (st.block); }

void PatternEditor::selectRange (int line0, int line1, int track0, int group0, int track1, int group1)
{
    const int gpt = groupsPerTrack();
    const int lines = blockLines();
    sel.block = proc.song.safeBlock (st.block);
    sel.l0 = juce::jlimit (0, lines - 1, std::min (line0, line1));
    sel.l1 = juce::jlimit (0, lines - 1, std::max (line0, line1));
    const int a = track0 * gpt + std::min (group0, gpt - 1), b = track1 * gpt + std::min (group1, gpt - 1);
    sel.c0 = std::max (0, std::min (a, b));
    sel.c1 = std::min (proc.song.numTracks * gpt - 1, std::max (a, b));
    repaint();
}

void PatternEditor::clearSelection() { if (sel.block >= 0) { sel.block = -1; repaint(); } }

void PatternEditor::selectAll()
{
    selectRange (0, blockLines() - 1, 0, 0, proc.song.numTracks - 1, groupsPerTrack() - 1);
}

template <typename Fn> void PatternEditor::forEachSelected (Fn&& fn) const
{
    const int gpt = groupsPerTrack();
    for (int l = sel.l0; l <= sel.l1; ++l)
        for (int c = sel.c0; c <= sel.c1; ++c)
            fn (l, c / gpt, c % gpt);
}

namespace
{
    void clearGroup (Cell& c, int g)
    {
        if (g == 0) { c.note = 0; c.instr = 0; }
        else if (g == 1) { c.cmd = 0; c.param = 0; }
        else { c.cmd2 = 0; c.param2 = 0; }
    }
    void copyGroup (Cell& to, const Cell& from, int g)
    {
        if (g == 0) { to.note = from.note; to.instr = from.instr; }
        else if (g == 1) { to.cmd = from.cmd; to.param = from.param; }
        else { to.cmd2 = from.cmd2; to.param2 = from.param2; }
    }
}

void PatternEditor::copySelection (bool cut)
{
    if (! hasSelection()) return;
    const auto& b = proc.song.blocks[(size_t) sel.block];
    const int gpt = groupsPerTrack();
    selClip.cells.clear();
    selClip.lines = sel.l1 - sel.l0 + 1;
    const int t0 = sel.c0 / gpt;
    forEachSelected ([&] (int l, int t, int g) { selClip.cells.push_back ({ l - sel.l0, t - t0, g, b.at (l, t) }); });
    lastCopy = LastCopy::Selection;
    if (onStatus) onStatus ((cut ? "CUT " : "COPIED ") + juce::String (selClip.lines) + " LINES - CMD+V PASTES AT THE CURSOR");
    if (cut) deleteSelection();
}

void PatternEditor::deleteSelection()
{
    if (! hasSelection()) return;
    pushUndo (sel.block, "DELETE SELECTION");
    proc.withLock ([&] {
        auto& b = proc.song.blocks[(size_t) sel.block];
        forEachSelected ([&] (int l, int t, int g) { clearGroup (b.at (l, t), g); });
    });
    changed();
}

void PatternEditor::pasteSelection()
{
    if (selClip.cells.empty()) return;
    const int blk = proc.song.safeBlock (st.block);
    pushUndo (blk, "PASTE SELECTION");
    const int lines = blockLines();
    proc.withLock ([&] {
        auto& b = proc.song.blocks[(size_t) blk];
        for (const auto& c : selClip.cells)
        {
            const int l = st.line + c.line, t = st.track + c.track;
            if (l < lines && t < proc.song.numTracks) copyGroup (b.at (l, t), c.cell, c.group);
        }
    });
    // the pasted area becomes the selection
    int maxT = 0, minG = 2, maxG = 0;
    for (const auto& c : selClip.cells) maxT = std::max (maxT, c.track);
    for (const auto& c : selClip.cells) { if (c.track == 0) minG = std::min (minG, c.group); if (c.track == maxT) maxG = std::max (maxG, c.group); }
    selectRange (st.line, st.line + selClip.lines - 1, st.track, minG, st.track + maxT, maxG);
    changed();
}

void PatternEditor::askBlockNumber()
{
    auto* w = new juce::AlertWindow ("Go to block", "Block number (0-" + juce::String ((int) proc.song.blocks.size() - 1) + ")", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("num", juce::String (proc.song.safeBlock (st.block)));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PatternEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (r != 1 || safe == nullptr) return;
        const auto txt = w->getTextEditorContents ("num").trim();
        if (txt.isEmpty() || ! txt.containsOnly ("0123456789")) return;
        auto& self = *safe;
        self.st.block = self.proc.song.safeBlock (txt.getIntValue());
        self.st.line = std::min (self.st.line, self.blockLines() - 1);
        self.changed();
        self.grabKeyboardFocus();
    }), true);
}

// ---------------------------------------------------------------- loop
void PatternEditor::setLoopRange (int from, int to)
{
    st.loopBlock = proc.song.safeBlock (st.block);
    st.loopFrom = juce::jlimit (0, blockLines() - 1, std::min (from, to));
    st.loopTo = juce::jlimit (0, blockLines() - 1, std::max (from, to));
    st.loopOn = true;
    syncLoop();
    if (onStatus) onStatus ("LOOP LINES " + juce::String (st.loopFrom).paddedLeft ('0', 3) + "-" + juce::String (st.loopTo).paddedLeft ('0', 3)
                            + " OF BLOCK " + juce::String (st.loopBlock).paddedLeft ('0', 3) + " (LOOP BUTTON TURNS IT OFF)");
    changed();
}

void PatternEditor::setLoopOn (bool on)
{
    if (on && st.loopBlock < 0)
    {
        if (onStatus) onStatus ("MARK A LOOP FIRST: DRAG OVER THE LINE NUMBERS, OR SHIFT+CLICK A LINE");
        st.loopOn = false;
        changed();
        return;
    }
    st.loopOn = on;
    syncLoop();
    changed();
}

void PatternEditor::syncLoop()
{
    if (st.loopBlock >= (int) proc.song.blocks.size()) { st.loopBlock = -1; st.loopOn = false; }
    proc.withLock ([&] { proc.player.setLineLoop (st.loopOn ? st.loopBlock : -1, st.loopFrom, st.loopTo); });
}

void PatternEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.y >= headerH()) return;
    const int slot = (int) std::floor ((e.position.x - lineNumW() + fontPx() * PixelFont::kCellW * 0.5f) / trackW());
    if (slot >= 0 && slot < visibleTracks() && e.position.y > headerRect (slot).getCentreY())
    {
        const int t = firstTrack() + slot;
        proc.history.settings ("TRACK " + juce::String (t + 1) + " PAN AUTO");
        proc.withLock ([&] { proc.song.trackPan[t] = -1; });
        panDragTrack = -1;
        repaint();
    }
}

void PatternEditor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (proc.player.isPlaying() && st.follow) return;
    // one line per wheel notch; trackpad swipes are scaled down. No wrap-around.
    const int d = wheel (w);
    if (d == 0) return;
    wantMargin = true;
    const int nl = juce::jlimit (0, blockLines() - 1, st.line + d);
    if (nl != st.line) { st.line = nl; changed(); }
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
    if (st.column < 0) { st.column = numColumns() - 1; moveTrack (-1); return; }
    if (st.column >= numColumns()) { st.column = 0; moveTrack (1); return; }
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
void PatternEditor::pushUndo (int blockIndex, const juce::String& what)
{
    proc.history.block (blockIndex, what);
}

void PatternEditor::pushStructureUndo (const juce::String& what)
{
    proc.history.structure (what);
}

void PatternEditor::undo() { proc.history.undo(); }
void PatternEditor::redo() { proc.history.redo(); }

void PatternEditor::enterNote (int note)
{
    note = juce::jlimit (1, (int) kNoteMax, note);
    proc.withLock ([&] { proc.player.jamNoteOn (note, st.instrument); });
    if (! st.editMode) return;
    pushUndo (st.block, "NOTE " + juce::String (noteName ((uint8_t) note)));
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
    pushUndo (st.block, st.column <= 2 ? "INSTRUMENT NUMBER" : "EFFECT");
    proc.withLock ([&] {
        auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
        switch (st.column)
        {
            case 1: c.instr = (uint8_t) std::min (kMaxInstruments, (v << 4) | (c.instr & 15)); break;
            case 2: c.instr = (uint8_t) std::min (kMaxInstruments, (c.instr & 0xF0) | v); break;
            case 3: c.cmd = (uint8_t) v; break;
            case 4: c.param = (uint8_t) (((v & 15) << 4) | (c.param & 15)); break;
            case 5: c.param = (uint8_t) ((c.param & 0xF0) | (v & 15)); break;
            case 6: c.cmd2 = (uint8_t) v; break;
            case 7: c.param2 = (uint8_t) (((v & 15) << 4) | (c.param2 & 15)); break;
            case 8: c.param2 = (uint8_t) ((c.param2 & 0xF0) | (v & 15)); break;
            default: break;
        }
    });
    advance();
    changed();
}

void PatternEditor::clearField()
{
    if (! st.editMode) return;
    pushUndo (st.block, "CLEAR");
    proc.withLock ([&] {
        auto& c = proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].at (st.line, st.track);
        if (st.column == 0) { c.note = 0; c.instr = 0; }
        else if (st.column <= 2) c.instr = 0;
        else if (st.column <= 5) { c.cmd = 0; c.param = 0; }
        else { c.cmd2 = 0; c.param2 = 0; }
    });
    advance();
    changed();
}

void PatternEditor::insertLine()
{
    if (! st.editMode) return;
    pushUndo (st.block, "INSERT LINE");
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
    pushUndo (st.block, "DELETE LINE");
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
        pushUndo (st.block, "CUT TRACK");
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
    pushUndo (st.block, "PASTE TRACK");
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
        pushUndo (st.block, "CUT BLOCK");
        proc.withLock ([&] { proc.song.blocks[(size_t) proc.song.safeBlock (st.block)].clear(); });
        changed();
    }
}

void PatternEditor::pasteBlock()
{
    if (! blockClip) return;
    pushUndo (st.block, "PASTE BLOCK");
    proc.withLock ([&] { proc.song.blocks[(size_t) proc.song.safeBlock (st.block)] = *blockClip; });
    st.line = std::min (st.line, blockLines() - 1);
    changed();
}

void PatternEditor::transposeTrack (int semis)
{
    pushUndo (st.block, "TRANSPOSE TRACK " + juce::String (semis > 0 ? "+" : "") + juce::String (semis));
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
    if (proc.player.isPlaying()) { stopPlayback(); return; }
    syncLoop();
    const bool loopHere = st.loopOn && st.loopBlock == proc.song.safeBlock (st.block);
    proc.withLock ([&] {
        if (songMode) proc.player.playSong (st.seqPos, 0);
        else proc.player.playBlock (st.block, loopHere ? st.loopFrom : 0);
    });
    changed();
}

void PatternEditor::toggleEdit()
{
    st.editMode = ! st.editMode;
    changed();
}

void PatternEditor::doubleBlock()
{
    const int b = proc.song.safeBlock (st.block);
    const int n = proc.song.blocks[(size_t) b].lines;
    if (n * 2 > kMaxLines) { if (onStatus) onStatus ("CAN'T DOUBLE: BLOCKS CAN HAVE UP TO 256 LINES"); return; }
    pushUndo (b, "DOUBLE BLOCK LENGTH");
    Block copy = proc.song.blocks[(size_t) b];
    doubleBlockLength (copy);
    proc.withLock ([&] { std::swap (proc.song.blocks[(size_t) b], copy); });
    if (onStatus) onStatus ("BLOCK " + juce::String (b).paddedLeft ('0', 3) + " IS NOW " + juce::String (n * 2) + " LINES: LINES "
                            + juce::String (n).paddedLeft ('0', 3) + "-" + juce::String (n * 2 - 1).paddedLeft ('0', 3) + " REPEAT THE FIRST HALF");
    changed();
}

void PatternEditor::halveBlock()
{
    const int b = proc.song.safeBlock (st.block);
    const int n = proc.song.blocks[(size_t) b].lines;
    if (n < 2) { if (onStatus) onStatus ("CAN'T HALVE A 1-LINE BLOCK"); return; }
    Block copy = proc.song.blocks[(size_t) b];
    bool differed = false;
    halveBlockLength (copy, differed);
    auto doIt = [this, b, n, copy = std::make_shared<Block> (std::move (copy)), differed]
    {
        pushUndo (b, "HALVE BLOCK LENGTH");
        proc.withLock ([&] { std::swap (proc.song.blocks[(size_t) b], *copy); });
        st.line = std::min (st.line, blockLines() - 1);
        if (onStatus) onStatus ("BLOCK " + juce::String (b).paddedLeft ('0', 3) + " IS NOW " + juce::String ((n + 1) / 2) + " LINES"
                                + (differed ? juce::String (" - UNDO TO GET THE SECOND HALF BACK") : juce::String()));
        changed();
    };
    if (! differed) { doIt(); return; }
    juce::Component::SafePointer<PatternEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Halve block",
        "The second half of this block is different from the first half and will be removed.", "Halve", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, doIt] (int r) { if (r == 1 && safe != nullptr) doIt(); }));
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
    wantMargin = true;

    // --- move the current song position (Alt+Up/Down)
    if (mods.isAltDown() && ! cmd && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {
        if (onMoveSequence) onMoveSequence (code == juce::KeyPress::upKey ? -1 : 1);
        return true;
    }

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

    // --- transport: space plays/stops (shift = song), esc toggles edit mode
    if (code == juce::KeyPress::spaceKey) { togglePlayStop (shift); return true; }
    if (code == juce::KeyPress::escapeKey) { toggleEdit(); return true; }
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

    // --- clipboard / undo
    if (cmd)
    {
        const auto ch = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        if (ch == 'z') { if (shift) redo(); else undo(); return true; }
        if (ch == 'y') { redo(); return true; }
        const auto tc = k.getTextCharacter();
        if (code == '[' || tc == '[') { setLoopRange (st.line, st.loopBlock == proc.song.safeBlock (st.block) ? std::max (st.line, st.loopTo) : st.line); return true; }
        if (code == ']' || tc == ']') { setLoopRange (st.loopBlock == proc.song.safeBlock (st.block) ? std::min (st.line, st.loopFrom) : st.line, st.line); return true; }
        // with a mouse selection, copy/cut work on it; paste puts back whatever was copied last
        if (ch == 'c') { if (hasSelection() && ! shift) copySelection (false); else if (shift) { copyBlock (false); lastCopy = LastCopy::Block; } else { copyTrack (false); lastCopy = LastCopy::Track; } return true; }
        if (ch == 'x') { if (hasSelection() && ! shift) copySelection (true);  else if (shift) { copyBlock (true); lastCopy = LastCopy::Block; } else { copyTrack (true); lastCopy = LastCopy::Track; } return true; }
        if (ch == 'v') { if (shift) pasteBlock(); else if (lastCopy == LastCopy::Selection) pasteSelection(); else pasteTrack(); return true; }
        if (ch == 'a') { selectAll(); return true; }
        if (code == juce::KeyPress::upKey)   { transposeTrack (shift ? 12 : 1); return true; }
        if (code == juce::KeyPress::downKey) { transposeTrack (shift ? -12 : -1); return true; }
        return false; // let the host have other shortcuts
    }

    // --- line edits
    // the Mac "delete" key is backspace: plain = clear field, shift = remove line, alt = insert line
    if (hasSelection() && (code == juce::KeyPress::deleteKey || (code == juce::KeyPress::backspaceKey && ! shift && ! mods.isAltDown())))
    {
        deleteSelection();
        return true;
    }
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
            pushUndo (st.block, "NOTE OFF");
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

    // command columns take 0-9 and A-Z; the others are hex
    int hv = -1;
    if (st.column == 3 || st.column == 6) hv = commandFromChar ((char) ch);
    else if (ch >= '0' && ch <= '9') hv = (int) (ch - '0');
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
