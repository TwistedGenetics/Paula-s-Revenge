// Paula's Revenge - the BREAK page (see BreakPage.h)
#include "BreakPage.h"
#include "ChoiceBox.h"
#include "../engine/Groove.h"
#include "../engine/Slicer.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    juce::Colour kindColour (HitKind k)
    {
        const auto& sk = Theme::skin;
        switch (k)
        {
            case HitKind::Kick:  return sk.hot;
            case HitKind::Snare: return sk.accentMod;
            case HitKind::Hat:   return sk.accentSel;
            default:             return sk.dim;
        }
    }
}

BreakPage::BreakPage (PaulaProcessor& p) : proc (p)
{
    Theme::markDarkPage (*this);
    setWantsKeyboardFocus (true);
    setOpaque (true);
    for (auto* b : { &closeBtn, &insDown, &insUp, &autoBtn, &evenBtn, &sensDown, &sensUp, &barsDown, &barsUp, &trackDown, &trackUp,
                     &makeBtn, &chopBtn, &playBtn, &stopBtn, &fitBtn, &keepBtn, &shuffleBtn, &ghostBtn, &halfBtn, &doubleBtn, &variationsBtn, &fillBtn })
        addAndMakeVisible (*b);
    for (auto& b : rebuildBtns) addAndMakeVisible (b);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    auto& st = proc.editorState;
    auto syncKeep = [this] { keepBtn.setToggleState (proc.song.instruments[(size_t) instrument()].stretchMode > 0, juce::dontSendNotification); };
    insDown.onClick = [this, &st, syncKeep] { st.instrument = (st.instrument + kMaxInstruments - 1) % kMaxInstruments; syncKeep(); changed(); };
    insUp.onClick   = [this, &st, syncKeep] { st.instrument = (st.instrument + 1) % kMaxInstruments; syncKeep(); changed(); };
    autoBtn.onClick = [this] { sliceAuto(); };
    evenBtn.onClick = [this] { sliceEven(); };
    sensDown.onClick = [this] { sens = std::max (1, sens - 5); repaint(); };
    sensUp.onClick   = [this] { sens = std::min (100, sens + 5); repaint(); };
    barsDown.onClick = [this] { bars = std::max (1, bars / 2); repaint(); };
    barsUp.onClick   = [this] { bars = std::min (4, bars * 2); repaint(); };
    trackDown.onClick = [this, &st] { st.track = std::max (0, st.track - 1); changed(); };
    trackUp.onClick   = [this, &st] { st.track = std::min (proc.song.numTracks - 1, st.track + 1); changed(); };
    makeBtn.onClick = [this] { makeBlock(); };
    chopBtn.onClick = [this] { chopHere(); };
    playBtn.onClick = [this, &st] { proc.withLock ([&] { proc.player.playBlock (proc.song.safeBlock (st.block)); }); status ("PLAYING BLOCK " + juce::String (st.block).paddedLeft ('0', 3)); };
    stopBtn.onClick = [this] { proc.withLock ([&] { proc.player.stop(); }); };
    fitBtn.setToggleState (true, juce::dontSendNotification);
    shuffleBtn.onClick = [this] { reEdit (0); };
    ghostBtn.onClick   = [this] { reEdit (1); };
    halfBtn.onClick    = [this] { reEdit (2); };
    doubleBtn.onClick  = [this] { reEdit (3); };
    for (int i = 0; i < kRebuilds; ++i) rebuildBtns[i].onClick = [this, i] { rebuildAs (i); };
    variationsBtn.onClick = [this] { variations(); };
}

int BreakPage::instrument() const { return juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument); }

juce::String BreakPage::insName (int i) const
{
    const auto& in = proc.song.instruments[(size_t) i];
    return hex2 (i + 1) + " " + (in.name.empty() ? juce::String (in.hasData() ? "(NO NAME)" : "(EMPTY)") : juce::String (in.name).toUpperCase());
}

void BreakPage::opened()
{
    // (the cursor's instrument; when that's empty, the first one with a sample)
    auto& st = proc.editorState;
    if (! proc.song.instruments[(size_t) instrument()].hasData())
        for (int i = 0; i < kMaxInstruments; ++i) if (proc.song.instruments[(size_t) i].hasData()) { st.instrument = i; break; }
    keepBtn.setToggleState (proc.song.instruments[(size_t) instrument()].stretchMode > 0, juce::dontSendNotification);
    grabKeyboardFocus();
    status ("BREAK: SLICE IT, MAKE BLOCK, THEN RE-EDIT - RIGHT-CLICK A SLICE TO SAY WHAT IT IS");
    repaint();
}

void BreakPage::tick()
{
    // (the cursor's block can change under us: the grid follows it)
    static int lastBlock = -1, lastPlayLine = -1;
    const int line = proc.player.isPlaying() ? proc.player.uiLine.load() : -1;
    if (proc.editorState.block != lastBlock || line != lastPlayLine) { lastBlock = proc.editorState.block; lastPlayLine = line; repaint (gridArea.toNearestInt().expanded (4)); }
}

const std::vector<HitKind>& BreakPage::kinds()
{
    const int ins = instrument();
    const auto& in = proc.song.instruments[(size_t) ins];
    if (ins != kindIns || in.slices != kindSlices)
    {
        kindCache = classifySlices (in);
        kindSlices = in.slices;
        kindIns = ins;
    }
    return kindCache;
}

void BreakPage::setKind (int slice, HitKind k)
{
    kinds();
    if (slice >= 0 && slice < (int) kindCache.size()) { kindCache[(size_t) slice] = k; repaint(); }
}

bool BreakPage::ensureSlices (int ins)
{
    const auto& in = proc.song.instruments[(size_t) ins];
    if (! in.hasData()) { status ("INSTRUMENT " + hex2 (ins + 1) + " HAS NO SAMPLE - LOAD A BREAK FIRST"); return false; }
    if (in.numSlices() >= 2 && in.sliceKeys) return true;
    proc.history.instrument (ins, "BREAK: SLICE");
    proc.withLock ([&]
    {
        auto& w = proc.song.instruments[(size_t) ins];
        if (w.numSlices() < 2)
        {
            auto sl = detectSlices (w.data, w.dataR, w.rate > 0 ? w.rate : kAmigaC3Rate, sens / 100.0f, 32);
            if (sl.size() < 2) sl = evenSlices ((int) w.data.size(), 16);
            w.slices = sl;
        }
        w.sliceKeys = true;
    });
    return true;
}

void BreakPage::sliceAuto()
{
    const int ins = instrument();
    const auto& in = proc.song.instruments[(size_t) ins];
    if (! in.hasData()) { status ("NO SAMPLE IN INSTRUMENT " + hex2 (ins + 1)); return; }
    proc.history.instrument (ins, "BREAK: AUTO SLICE");
    int n = 0;
    proc.withLock ([&]
    {
        auto& w = proc.song.instruments[(size_t) ins];
        w.slices = detectSlices (w.data, w.dataR, w.rate > 0 ? w.rate : kAmigaC3Rate, sens / 100.0f, 64);
        w.sliceKeys = true;
        n = w.numSlices();
    });
    const auto& k = kinds();
    int kk = 0, ss = 0, hh = 0;
    for (auto x : k) { kk += x == HitKind::Kick; ss += x == HitKind::Snare; hh += x == HitKind::Hat; }
    status ("FOUND " + juce::String (n) + " HITS: " + juce::String (kk) + " KICKS, " + juce::String (ss) + " SNARES, " + juce::String (hh) + " HATS (SENS " + juce::String (sens) + ")");
    changed();
}

void BreakPage::sliceEven()
{
    const int ins = instrument();
    const auto& in = proc.song.instruments[(size_t) ins];
    if (! in.hasData()) { status ("NO SAMPLE IN INSTRUMENT " + hex2 (ins + 1)); return; }
    proc.history.instrument (ins, "BREAK: EVEN SLICES");
    proc.withLock ([&] { auto& w = proc.song.instruments[(size_t) ins]; w.slices = evenSlices ((int) w.data.size(), 16 * bars); w.sliceKeys = true; });
    status ("SLICED INTO " + juce::String (16 * bars) + " EVEN 16THS");
    changed();
}

int BreakPage::partLines() const
{
    return proc.song.blocks[(size_t) proc.song.safeBlock (proc.editorState.block)].lines;
}

int BreakPage::chopHere()
{
    const int ins = instrument();
    if (! ensureSlices (ins)) return 0;
    auto& st = proc.editorState;
    const int blk = proc.song.safeBlock (st.block);
    proc.history.block (blk, "BREAK: CHOP");
    int placed = 0;
    proc.withLock ([&]
    {
        auto& b = proc.song.blocks[(size_t) blk];
        const int span = std::min (b.lines, 16 * bars);
        placed = prv::chopToPattern (proc.song, ins, blk, st.track, span);
        for (int l = span; l < b.lines; ++l) b.at (l, st.track) = b.at (l % span, st.track);   // (again to the block's end)
        auto& w = proc.song.instruments[(size_t) ins];
        if (fitBtn.getToggleState()) w.syncLines = span;
        // KEEP PITCH: fitted by stretching (the window's timer makes the stretch)
        if (fitBtn.getToggleState() && keepBtn.getToggleState() && w.stretchMode == 0) w.stretchMode = 1;
    });
    status ("CHOPPED " + juce::String (placed) + " SLICES INTO BLOCK " + juce::String (blk).paddedLeft ('0', 3) + ", TRACK " + juce::String (st.track + 1)
            + (fitBtn.getToggleState() ? " - FITTED TO THE TEMPO (BEAT SYNC " + juce::String (std::min (partLines(), 16 * bars)) + " LINES)" : juce::String()));
    changed();
    return placed;
}

int BreakPage::makeBlock()
{
    const int ins = instrument();
    if (! proc.song.instruments[(size_t) ins].hasData()) { status ("INSTRUMENT " + hex2 (ins + 1) + " HAS NO SAMPLE - LOAD A BREAK FIRST"); return -1; }
    if (proc.song.blocks.size() >= (size_t) kMaxBlocks) { status ("THE SONG HAS 256 BLOCKS ALREADY"); return -1; }
    proc.history.structure ("BREAK: MAKE BLOCK");
    ensureSlices (ins);
    auto& st = proc.editorState;
    int nb = -1;
    proc.withLock ([&]
    {
        Block fresh;
        fresh.lines = std::max (16 * bars, 64);
        proc.song.blocks.push_back (fresh);
        nb = (int) proc.song.blocks.size() - 1;
        const int at = juce::jlimit (0, (int) proc.song.sequence.size(), st.seqPos + 1);
        proc.song.sequence.insert (proc.song.sequence.begin() + at, nb);
        st.seqPos = at;
        st.block = nb;
    });
    chopHere();
    status ("NEW BLOCK " + juce::String (nb).paddedLeft ('0', 3) + " AT POSITION " + juce::String (st.seqPos).paddedLeft ('0', 3) + ": THE BREAK ON TRACK " + juce::String (st.track + 1)
            + " - NOW RE-EDIT IT");
    changed();
    return nb;
}

void BreakPage::reEdit (int which)
{
    const int ins = instrument();
    if (! ensureSlices (ins)) return;
    auto& st = proc.editorState;
    const int blk = proc.song.safeBlock (st.block);
    static const char* names[] = { "SHUFFLE", "GHOST ROLL", "HALF-TIME", "DOUBLE-TIME" };
    which = juce::jlimit (0, 3, which);
    const auto& k = kinds();
    proc.history.block (blk, juce::String ("BREAK: ") + names[which]);
    int n = 0;
    proc.withLock ([&]
    {
        auto& b = proc.song.blocks[(size_t) blk];
        switch (which)
        {
            case 0: n = resequenceSlices (b, 0, b.lines - 1, st.track, st.track, shuffleSeed++ * 2654435761u); break;
            case 1: n = ghostRoll (b, st.track, ins, k, b.lines); break;
            case 2: n = halfTime (b, st.track, b.lines); break;
            default: n = doubleTime (b, st.track, b.lines); break;
        }
    });
    status (juce::String (names[which]) + ": " + juce::String (n) + " NOTES ON TRACK " + juce::String (st.track + 1) + (which == 0 ? " (AGAIN FOR ANOTHER ORDER)" : "") + " - UNDO GOES BACK");
    changed();
}

void BreakPage::rebuildAs (int i)
{
    const int ins = instrument();
    if (! ensureSlices (ins)) return;
    auto& st = proc.editorState;
    const int blk = proc.song.safeBlock (st.block);
    const auto& k = kinds();
    proc.history.block (blk, juce::String ("BREAK: ") + rebuildName (i));
    int n = 0;
    proc.withLock ([&]
    {
        auto& b = proc.song.blocks[(size_t) blk];
        n = rebuild (b, st.track, ins, k, b.lines, std::max (1, b.lines / 16), rebuildTemplate (i), 0);
    });
    status (juce::String (rebuildName (i)) + ": REBUILT FROM ITS KICKS, SNARES AND HATS (" + juce::String (n) + " NOTES)");
    changed();
}

int BreakPage::variations()
{
    const int ins = instrument();
    if (! ensureSlices (ins)) return 0;
    if (proc.song.blocks.size() + 4 > (size_t) kMaxBlocks) { status ("NO ROOM FOR 4 MORE BLOCKS"); return 0; }
    auto& st = proc.editorState;
    const auto k = kinds();
    proc.history.structure ("BREAK: 4 VARIATIONS");
    int first = -1;
    proc.withLock ([&]
    {
        const int src = proc.song.safeBlock (st.block);
        int at = juce::jlimit (0, (int) proc.song.sequence.size(), st.seqPos + 1);
        for (int v = 0; v < 4; ++v)
        {
            Block b = proc.song.blocks[(size_t) src];
            const int lines = b.lines;
            // each a different rebuild, with a different choice of hits; the last ends in a ghost roll
            rebuild (b, st.track, ins, k, lines, std::max (1, lines / 16), rebuildTemplate (v), v);
            if (v == 3) ghostRoll (b, st.track, ins, k, lines);
            proc.song.blocks.push_back (b);
            const int nb = (int) proc.song.blocks.size() - 1;
            if (first < 0) first = nb;
            proc.song.sequence.insert (proc.song.sequence.begin() + at++, nb);
        }
    });
    status ("4 VARIATIONS: BLOCKS " + juce::String (first).paddedLeft ('0', 3) + "-" + juce::String (first + 3).paddedLeft ('0', 3)
            + " AFTER POSITION " + juce::String (st.seqPos).paddedLeft ('0', 3) + " (" + rebuildName (0) + ", " + rebuildName (1) + ", " + rebuildName (2) + ", " + rebuildName (3) + " + GHOST ROLL)");
    changed();
    return 4;
}

int BreakPage::sliceAt (float x) const
{
    const auto& in = proc.song.instruments[(size_t) instrument()];
    if (! in.hasData() || in.numSlices() < 1 || ! waveArea.contains (x, waveArea.getCentreY())) return -1;
    const double pos = (x - waveArea.getX()) / waveArea.getWidth() * (double) in.data.size();
    int found = -1;
    for (int i = 0; i < in.numSlices(); ++i) if (in.slices[(size_t) i] <= pos) found = i;
    return found;
}

void BreakPage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! waveArea.contains (e.position)) return;
    const int s = sliceAt (e.position.x);
    if (s < 0) return;
    if (e.mods.isPopupMenu())
    {
        juce::Component::SafePointer<BreakPage> safe (this);
        std::vector<MenuItem> items;
        for (auto k : { HitKind::Kick, HitKind::Snare, HitKind::Hat, HitKind::Other })
            items.push_back ({ juce::String ("SLICE ") + juce::String (s + 1) + " IS A " + hitName (k), [safe, s, k] { if (safe) safe->setKind (s, k); } });
        showMenu ("SLICE " + juce::String (s + 1), items);
        return;
    }
    // a click: hear it (slices play from C-3 up)
    const int ins = instrument();
    if (! proc.song.instruments[(size_t) ins].sliceKeys) ensureSlices (ins);
    proc.withLock ([&] { proc.player.jamNoteOn (std::min ((int) kNoteMax, kSliceC3 + s), ins); });
    const auto& k = kinds();
    status ("SLICE " + juce::String (s + 1) + ": " + (s < (int) k.size() ? hitName (k[(size_t) s]) : "?") + " (RIGHT-CLICK TO CHANGE)");
}

bool BreakPage::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) { (proc.player.isPlaying() ? stopBtn : playBtn).triggerClick(); return true; }
    return false;
}

void BreakPage::resized()
{
    const float px = Theme::px;
    auto charsW = [px] (int c) { return (int) (px * 6 * c + px * 4); };
    const int rowH = (int) (px * 11), gap = (int) px;
    auto r = getLocalBounds().reduced ((int) (px * 3));
    auto top = r.removeFromTop (rowH);
    closeBtn.setBounds (top.removeFromRight (charsW (5)));
    titleArea = top.toFloat();
    r.removeFromTop (gap * 2);

    // instrument, slicing, bars
    auto row = r.removeFromTop (rowH);
    insDown.setBounds (row.removeFromLeft (charsW (1))); row.removeFromLeft (gap);
    insArea = row.removeFromLeft (charsW (22)).toFloat(); row.removeFromLeft (gap);
    insUp.setBounds (row.removeFromLeft (charsW (1))); row.removeFromLeft (gap * 4);
    autoBtn.setBounds (row.removeFromLeft (charsW (4))); row.removeFromLeft (gap);
    evenBtn.setBounds (row.removeFromLeft (charsW (7))); row.removeFromLeft (gap * 3);
    sensDown.setBounds (row.removeFromLeft (charsW (1)));
    sensArea = row.removeFromLeft (charsW (8)).toFloat();
    sensUp.setBounds (row.removeFromLeft (charsW (1))); row.removeFromLeft (gap * 3);
    barsDown.setBounds (row.removeFromLeft (charsW (1)));
    barsArea = row.removeFromLeft (charsW (7)).toFloat();
    barsUp.setBounds (row.removeFromLeft (charsW (1)));
    r.removeFromTop (gap * 2);

    // the bottom: three rows of buttons, then the grid of the block
    auto rows = r;
    waveArea = rows.removeFromTop ((int) (rows.getHeight() * 0.36f)).toFloat();
    rows.removeFromTop (gap * 2);
    row = rows.removeFromTop (rowH);
    trackDown.setBounds (row.removeFromLeft (charsW (1)));
    trackArea = row.removeFromLeft (charsW (8)).toFloat();
    trackUp.setBounds (row.removeFromLeft (charsW (1))); row.removeFromLeft (gap * 3);
    makeBtn.setBounds (row.removeFromLeft (charsW (10))); row.removeFromLeft (gap);
    chopBtn.setBounds (row.removeFromLeft (charsW (9))); row.removeFromLeft (gap);
    fitBtn.setBounds (row.removeFromLeft (charsW (12))); row.removeFromLeft (gap);
    keepBtn.setBounds (row.removeFromLeft (charsW (10))); row.removeFromLeft (gap * 3);
    playBtn.setBounds (row.removeFromLeft (charsW (4))); row.removeFromLeft (gap);
    stopBtn.setBounds (row.removeFromLeft (charsW (4))); row.removeFromLeft (gap);
    fillBtn.setBounds (row.removeFromLeft (charsW (4)));
    rows.removeFromTop (gap);
    row = rows.removeFromTop (rowH);
    reLabel = row.removeFromLeft (charsW (8)).toFloat();
    for (auto* b : { &shuffleBtn, &ghostBtn, &halfBtn, &doubleBtn }) { b->setBounds (row.removeFromLeft (charsW (b->getButtonText().length()))); row.removeFromLeft (gap); }
    rows.removeFromTop (gap);
    row = rows.removeFromTop (rowH);
    rbLabel = row.removeFromLeft (charsW (8)).toFloat();
    for (auto& b : rebuildBtns) { b.setBounds (row.removeFromLeft (charsW (b.getButtonText().length()))); row.removeFromLeft (gap); }
    row.removeFromLeft (gap * 3);
    variationsBtn.setBounds (row.removeFromLeft (charsW (12)));
    rows.removeFromTop (gap * 2);
    gridTitle = rows.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2)).toFloat();
    gridArea = rows.toFloat();
}

void BreakPage::paintWave (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    const auto& pal = Theme::palFor (*this);
    g.setColour (sk.bg);
    g.fillRect (waveArea);
    const auto& in = proc.song.instruments[(size_t) instrument()];
    auto w = waveArea.reduced (px);
    const float tabH = PixelFont::textHeight (px) + px * 2;
    auto wave = w.withTrimmedBottom (tabH);
    if (! in.hasData())
    {
        PixelFont::drawCentred (g, "NO SAMPLE - PICK AN INSTRUMENT WITH A BREAK IN IT, OR LOAD ONE", wave, px, sk.dim);
        Theme::bevel (g, waveArea, false, std::max (1.0f, px * 0.5f), pal);
        return;
    }
    const auto& k = kinds();
    const size_t n = in.data.size();
    const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f - px;
    // the slices' backgrounds, tinted by kind
    for (int i = 0; i < in.numSlices(); ++i)
    {
        int a = 0, e = 0; in.sliceRange (i, a, e);
        const float x0 = wave.getX() + (float) a / n * wave.getWidth(), x1 = wave.getX() + (float) e / n * wave.getWidth();
        const auto c = i < (int) k.size() ? kindColour (k[(size_t) i]) : sk.dim;
        g.setColour (c.withAlpha (0.10f));
        g.fillRect (x0, wave.getY(), x1 - x0, wave.getHeight());
        // its tab: number and letter
        auto tab = juce::Rectangle<float> (x0, w.getBottom() - tabH, std::max (px * 2, x1 - x0), tabH);
        g.setColour (c.withAlpha (0.85f));
        g.fillRect (x0, wave.getY(), std::max (1.0f, px * 0.5f), wave.getHeight() + tabH);
        const juce::String label = juce::String (i + 1) + (i < (int) k.size() ? juce::String::charToString ((juce::juce_wchar) hitLetter (k[(size_t) i])) : juce::String());
        if (PixelFont::textWidth (label, px) + px * 2 < tab.getWidth()) PixelFont::drawLeft (g, label, tab.withTrimmedLeft (px * 1.5f), px, c);
        else if (PixelFont::textWidth (label.getLastCharacters (1), px) + px < tab.getWidth()) PixelFont::drawLeft (g, label.getLastCharacters (1), tab.withTrimmedLeft (px), px, c);
    }
    // the wave
    const int cols = std::max (2, (int) (wave.getWidth() / std::max (1.0f, px * 0.5f)));
    const float cw = wave.getWidth() / cols;
    juce::RectangleList<float> bars;
    for (int c = 0; c < cols; ++c)
    {
        const size_t a = n * (size_t) c / (size_t) cols, b = std::max (a + 1, n * (size_t) (c + 1) / (size_t) cols);
        const size_t step = std::max<size_t> (1, (b - a) / 64);
        int lo = 0, hi = 0;
        for (size_t i = a; i < b && i < n; i += step) { lo = std::min<int> (lo, in.data[i]); hi = std::max<int> (hi, in.data[i]); }
        bars.addWithoutMerging ({ wave.getX() + c * cw, mid - hi / 32768.0f * half, std::max (1.0f, cw), std::max (1.0f, (hi - lo) / 32768.0f * half) });
    }
    g.setColour (sk.text.withAlpha (0.85f));
    g.fillRectList (bars);
    Theme::bevel (g, waveArea, false, std::max (1.0f, px * 0.5f), pal);
}

void BreakPage::paintGrid (juce::Graphics& g)
{
    // the cursor's block, the cursor's track: every line as a step (16 to a row = a bar), its slice and kind
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    const auto& pal = Theme::palFor (*this);
    const int blk = proc.song.safeBlock (proc.editorState.block);
    const auto& b = proc.song.blocks[(size_t) blk];
    const int t = proc.editorState.track;
    PixelFont::drawLeft (g, "BLOCK " + juce::String (blk).paddedLeft ('0', 3) + "  TRACK " + juce::String (t + 1) + "  (" + juce::String (b.lines) + " LINES, 16 A ROW)", gridTitle, px, pal.ink);
    g.setColour (sk.bg);
    g.fillRect (gridArea);
    const auto& k = kinds();
    const int rows = (b.lines + 15) / 16;
    auto in = gridArea.reduced (px);
    const float cw = in.getWidth() / 16.0f, ch = std::min (px * 12, in.getHeight() / std::max (1, rows));
    const int playLine = proc.player.isPlaying() && proc.player.uiBlock.load() == blk ? proc.player.uiLine.load() : -1;
    for (int l = 0; l < b.lines; ++l)
    {
        auto cell = juce::Rectangle<float> (in.getX() + (l % 16) * cw, in.getY() + (l / 16) * ch, cw, ch).reduced (px * 0.5f);
        if (cell.getBottom() > in.getBottom() + 1) break;
        g.setColour ((l % 4 == 0 ? sk.band : sk.stripe));
        g.fillRect (cell);
        const auto& c = b.at (l, t);
        if (c.note >= kSliceC3 && c.note <= kNoteMax && c.instr == instrument() + 1)
        {
            const int s = c.note - kSliceC3;
            const auto col = s < (int) k.size() ? kindColour (k[(size_t) s]) : sk.text;
            const bool quiet = c.cmd == 0x0C && c.param < 0x20;
            g.setColour (col.withAlpha (quiet ? 0.35f : 0.75f));
            g.fillRect (cell.reduced (px * 0.5f));
            const juce::String label = (s < (int) k.size() ? juce::String::charToString ((juce::juce_wchar) hitLetter (k[(size_t) s])) : juce::String()) + juce::String (s + 1);
            if (PixelFont::textWidth (label, px) < cell.getWidth()) PixelFont::drawCentred (g, label, cell, px, sk.bg);
        }
        else if (c.note > 0)
        {
            g.setColour (sk.dim);
            g.fillRect (cell.reduced (cell.getWidth() * 0.3f, cell.getHeight() * 0.3f));
        }
        if (l == playLine) { g.setColour (sk.text); g.drawRect (cell, std::max (1.0f, px * 0.5f)); }
    }
    Theme::bevel (g, gridArea, false, std::max (1.0f, px * 0.5f), pal);
}

void BreakPage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    PixelFont::drawLeft (g, "BREAK", titleArea, px, pal.ink);
    // the instrument
    g.setColour (Theme::skin.bg);
    g.fillRect (insArea);
    Theme::bevel (g, insArea, false, std::max (1.0f, px * 0.5f), pal);
    auto nm = insName (instrument());
    while (nm.length() > 3 && PixelFont::textWidth (nm, px) > insArea.getWidth() - px * 3) nm = nm.dropLastCharacters (1);
    PixelFont::drawLeft (g, nm, insArea.withTrimmedLeft (px * 2), px, Theme::skin.text);
    PixelFont::drawCentred (g, "SENS " + juce::String (sens), sensArea, px, pal.ink);
    PixelFont::drawCentred (g, juce::String (bars) + (bars == 1 ? " BAR" : " BARS"), barsArea, px, pal.ink);
    PixelFont::drawCentred (g, "TRACK " + juce::String (track() + 1), trackArea, px, pal.ink);
    PixelFont::drawLeft (g, "RE-EDIT", reLabel, px, pal.ink);
    PixelFont::drawLeft (g, "REBUILD", rbLabel, px, pal.ink);
    paintWave (g);
    paintGrid (g);
}
} // namespace prv::ui
