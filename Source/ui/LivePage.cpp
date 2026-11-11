// Paula's Revenge - LIVE MODE (see LivePage.h)
#include "LivePage.h"
#include "../engine/Groove.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    const int kQuantLines[4] = { 16, 4, 1, 0 };
    const char* kQuantName[4] = { "AT: NEXT BAR", "AT: NEXT BEAT", "AT: NEXT LINE", "AT: AT ONCE" };
    // the pads on the keyboard: four rows of four, as they sit on the screen
    const char* kPadKeys = "1234QWERASDFZXCV";
    // the held effects: 5 6 7 8, T, G, B
    const char* kFxKeys = "5678TGB";
}

LivePage::LivePage (PaulaProcessor& p) : proc (p)
{
    Theme::markDarkPage (*this);
    setWantsKeyboardFocus (true);
    setOpaque (true);
    for (auto* b : { &closeBtn, &blocksBtn, &slicesBtn, &quantBtn, &recBtn, &visualsBtn, &playBtn, &stopBtn, &pageDown, &pageUp }) addAndMakeVisible (*b);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    blocksBtn.setClickingTogglesState (false); slicesBtn.setClickingTogglesState (false); recBtn.setClickingTogglesState (false);
    blocksBtn.onClick = [this] { padMode = Blocks; refreshButtons(); status ("PADS: BLOCKS - EACH LAUNCHES A BLOCK, IN TIME, AND LOOPS IT"); };
    slicesBtn.onClick = [this] { padMode = Slices; refreshButtons(); status ("PADS: SLICES - EACH PLAYS A SLICE OF INSTRUMENT " + hex2 (proc.editorState.instrument + 1)); };
    quantBtn.onClick = [this] { quant = (quant + 1) % 4; refreshButtons(); };
    recBtn.onClick = [this] { setRecording (! recOn); };
    playBtn.onClick = [this]
    {
        const int b = proc.song.safeBlock (proc.editorState.block);
        proc.withLock ([&] { proc.player.playBlock (b, 0); });
        lastBlock = -1; lastLine = -1;
    };
    stopBtn.onClick = [this] { proc.withLock ([&] { proc.player.stop(); }); };
    pageDown.onClick = [this] { padPage = std::max (0, padPage - 1); repaint(); };
    pageUp.onClick = [this] { padPage = std::min ((kMaxBlocks - 1) / 16, padPage + 1); repaint(); };
    for (int k = 0; k < kFx; ++k)
    {
        addAndMakeVisible (fxBtn[k]);
        fxBtn[k].onStateChange = [this, k]
        {
            const bool down = fxBtn[k].isDown();
            if (down && proc.player.liveFx().holding() != (LiveFx::Kind) (k + 1)) fxDown (k + 1);
            else if (! down && proc.player.liveFx().holding() == (LiveFx::Kind) (k + 1) && keyFx.empty()) fxUp (k + 1);
        };
    }
    refreshButtons();
}

void LivePage::refreshButtons()
{
    blocksBtn.setToggleState (padMode == Blocks, juce::dontSendNotification);
    slicesBtn.setToggleState (padMode == Slices, juce::dontSendNotification);
    recBtn.setToggleState (recOn, juce::dontSendNotification);
    quantBtn.setButtonText (kQuantName[quant]);
    pageDown.setVisible (padMode == Blocks); pageUp.setVisible (padMode == Blocks);
    repaint();
}

void LivePage::opened() { grabKeyboardFocus(); refreshButtons(); }

int LivePage::padBlock (int pad) const
{
    const int b = padPage * 16 + pad;
    return b >= 0 && b < (int) proc.song.blocks.size() ? b : -1;
}

int LivePage::padNote (int pad) const
{
    // a sliced instrument: its slices (C-3 = slice 1); otherwise its notes up from C-3
    return kSliceC3 + pad;
}

void LivePage::padDown (int pad)
{
    if (pad < 0 || pad >= 16) return;
    if (padMode == Blocks)
    {
        const int b = padBlock (pad);
        if (b < 0) { status ("NO BLOCK " + juce::String (padPage * 16 + pad).paddedLeft ('0', 3) + " YET"); return; }
        auto& pl = proc.player;
        if (! pl.isPlaying())
        {
            proc.withLock ([&] { pl.playBlock (b, 0); });
            proc.editorState.block = b;
            lastBlock = -1; lastLine = -1;
            status ("BLOCK " + juce::String (b).paddedLeft ('0', 3) + " PLAYING (IT LOOPS) - PRESS ANOTHER PAD TO LAUNCH IT " + juce::String (kQuantName[quant]).fromFirstOccurrenceOf ("AT: ", false, false));
        }
        else
        {
            proc.withLock ([&] { pl.queueBlock (b, kQuantLines[quant]); });
            status ("BLOCK " + juce::String (b).paddedLeft ('0', 3) + " NEXT - " + juce::String (kQuantName[quant]).fromFirstOccurrenceOf ("AT: ", false, false));
        }
    }
    else
    {
        const int ins = proc.editorState.instrument;
        const int note = padNote (pad);
        proc.withLock ([&] { proc.player.jamNoteOn (note, ins, 127); });
        downNote = note;
        // recording (EDIT on, playing): the slice goes into the pattern at the line playing, on the cursor's track
        auto& st = proc.editorState;
        if (st.editMode && proc.player.isPlaying())
        {
            const int blk = proc.song.safeBlock (proc.player.uiBlock.load());
            const int line = std::clamp (proc.player.uiLine.load(), 0, proc.song.blocks[(size_t) blk].lines - 1);
            proc.history.block (blk, "LIVE: SLICE", true);
            proc.withLock ([&]
            {
                auto& c = proc.song.blocks[(size_t) blk].at (line, std::clamp (st.track, 0, kMaxTracks - 1));
                c.note = (uint8_t) note; c.instr = (uint8_t) (ins + 1);
            });
            if (onChanged) onChanged();
        }
    }
    repaint();
}

void LivePage::padUp (int)
{
    if (padMode == Slices && downNote >= 0) { const int n = downNote; proc.withLock ([&] { proc.player.jamNoteOff (n); }); downNote = -1; }
    repaint();
}

void LivePage::fxDown (int k)
{
    if (k <= 0 || k >= LiveFx::Count) return;
    proc.player.liveFx().hold ((LiveFx::Kind) k);
    status (juce::String (LiveFx::name ((LiveFx::Kind) k)) + " - HELD (LET GO TO STOP)");
    repaint();
}

void LivePage::fxUp (int k)
{
    if (proc.player.liveFx().holding() == (LiveFx::Kind) k) proc.player.liveFx().release();
    repaint();
}

void LivePage::setRecording (bool on)
{
    if (on == recOn) return;
    recOn = on;
    if (on)
    {
        recBlocks.clear();
        lastBlock = -1; lastLine = -1;
        status ("REC ARRANGE: LAUNCH BLOCKS - EVERY TIME ONE PLAYS (OR LOOPS) IT'S WRITTEN DOWN. PRESS AGAIN TO STOP");
    }
    else if (! recBlocks.empty())
    {
        // the arrangement played becomes the song's sequence (one undo step brings the old one back)
        proc.history.structure ("LIVE: ARRANGEMENT");
        proc.withLock ([&] { proc.song.sequence = recBlocks; });
        status ("THE SONG'S SEQUENCE IS NOW WHAT YOU PLAYED: " + juce::String ((int) recBlocks.size()) + " POSITIONS (UNDO FOR THE OLD ONE)");
        if (onChanged) onChanged();
    }
    else status ("REC ARRANGE: NOTHING PLAYED, NOTHING CHANGED");
    refreshButtons();
}

void LivePage::tick()
{
    ++blink;
    // REC ARRANGE: a block starting (launched, or looping round) is written down
    if (recOn && proc.player.isPlaying())
    {
        const int b = proc.player.uiBlock.load(), l = proc.player.uiLine.load();
        if (b != lastBlock || (lastLine >= 0 && l < lastLine)) recBlocks.push_back (b);
        lastBlock = b; lastLine = l;
    }
    if (proc.player.isPlaying()) proc.editorState.block = proc.player.uiBlock.load();
    repaint();
}

juce::Rectangle<float> LivePage::padRect (int pad) const
{
    const float px = Theme::px;
    const float gw = padArea.getWidth() / 4.0f, gh = padArea.getHeight() / 4.0f;
    return { padArea.getX() + (pad % 4) * gw + px, padArea.getY() + (pad / 4) * gh + px, gw - px * 2, gh - px * 2 };
}

void LivePage::resized()
{
    const float px = Theme::px;
    auto charsW = [px] (int c) { return (int) (px * 6 * c + px * 4); };
    const int rowH = (int) (px * 11), gap = (int) px;
    auto r = getLocalBounds().reduced ((int) (px * 3));
    auto top = r.removeFromTop (rowH);
    closeBtn.setBounds (top.removeFromRight (charsW (5))); top.removeFromRight (gap * 3);
    stopBtn.setBounds (top.removeFromRight (charsW (4))); top.removeFromRight (gap);
    playBtn.setBounds (top.removeFromRight (charsW (4))); top.removeFromRight (gap * 3);
    visualsBtn.setBounds (top.removeFromRight (charsW (7))); top.removeFromRight (gap * 3);
    titleArea = top.toFloat();
    r.removeFromTop (gap * 2);
    auto row = r.removeFromTop (rowH);
    blocksBtn.setBounds (row.removeFromLeft (charsW (6))); row.removeFromLeft (gap);
    slicesBtn.setBounds (row.removeFromLeft (charsW (6))); row.removeFromLeft (gap * 3);
    pageDown.setBounds (row.removeFromLeft (charsW (1)));
    pageArea = row.removeFromLeft (charsW (12)).toFloat();
    pageUp.setBounds (row.removeFromLeft (charsW (1))); row.removeFromLeft (gap * 3);
    quantBtn.setBounds (row.removeFromLeft (charsW (13))); row.removeFromLeft (gap * 3);
    recBtn.setBounds (row.removeFromLeft (std::min (row.getWidth(), charsW (11))));
    r.removeFromTop (gap * 2);
    // the effects down the bottom: a row (two when narrow)
    hintArea = r.removeFromBottom (rowH).toFloat();
    r.removeFromBottom (gap);
    int widest = 0;
    for (auto& b : fxBtn) widest = std::max (widest, charsW (b.getButtonText().length()));
    const int perRow = std::max (1, std::min (kFx, (r.getWidth() + gap) / (widest + gap)));
    const int fxRows = (kFx + perRow - 1) / perRow;
    auto fxArea = r.removeFromBottom (fxRows * rowH + (fxRows - 1) * gap);
    r.removeFromBottom (gap * 2);
    fxLabel = r.removeFromBottom (rowH).toFloat();
    r.removeFromBottom (gap);
    for (int i = 0; i < kFx; ++i)
    {
        const int rr = i / perRow, cc = i % perRow;
        const int w = (fxArea.getWidth() - gap * (perRow - 1)) / perRow;
        fxBtn[i].setBounds (fxArea.getX() + cc * (w + gap), fxArea.getY() + rr * (rowH + gap), w, rowH);
    }
    padArea = r.toFloat();
}

void LivePage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    g.fillAll (pal.panel);
    PixelFont::drawLeft (g, "LIVE", titleArea, px, pal.ink);
    if (padMode == Blocks) PixelFont::drawCentred (g, "BLOCKS " + juce::String (padPage * 16).paddedLeft ('0', 3) + "+", pageArea, px, pal.ink);
    PixelFont::drawLeft (g, "HOLD AN EFFECT (KEYS 5 6 7 8  T G B)", fxLabel, px, pal.ink);
    PixelFont::drawLeft (g, padMode == Blocks ? "PADS: KEYS 1-4 Q-R A-F Z-V. A PAD LAUNCHES ITS BLOCK, IT LOOPS UNTIL THE NEXT"
                                              : "PADS: KEYS 1-4 Q-R A-F Z-V. EDIT ON + PLAYING: THE SLICES GO INTO THE PATTERN",
                         hintArea, px, pal.ink);
    const int playing = proc.player.isPlaying() ? proc.player.uiBlock.load() : -1;
    const int queued = proc.player.queuedBlock();
    const auto& ins = proc.song.instruments[(size_t) proc.editorState.instrument];
    for (int pad = 0; pad < 16; ++pad)
    {
        auto rc = padRect (pad);
        juce::String big, small;
        bool on = false, next = false, empty = false;
        if (padMode == Blocks)
        {
            const int b = padBlock (pad);
            empty = b < 0;
            big = juce::String (padPage * 16 + pad).paddedLeft ('0', 3);
            small = empty ? juce::String ("---") : juce::String (proc.song.blocks[(size_t) b].lines) + " LINES";
            on = b >= 0 && b == playing; next = b >= 0 && b == queued;
        }
        else
        {
            const int sl = pad;
            empty = ins.numSlices() > 0 ? sl >= ins.numSlices() : ins.data.empty() && ! ins.isPlugin();
            big = ins.numSlices() > 0 ? "SLICE " + juce::String (sl + 1) : juce::String (noteName ((uint8_t) padNote (pad)));
            small = "INS " + hex2 (proc.editorState.instrument + 1);
            on = downNote == padNote (pad);
        }
        // playing: lit in the selection colour with a bold edge; next: the hot colour's edge, blinking
        const auto fill = on ? sk.accentSel : next && (blink / 4) % 2 == 0 ? pal.hover : pal.light;
        g.setColour (empty ? pal.panel : fill);
        g.fillRect (rc);
        Theme::bevel (g, rc, on, std::max (1.0f, px * 0.5f), pal);
        if (on) { g.setColour (sk.text); g.drawRect (rc, px * 1.5f); }
        if (next) { g.setColour (sk.hot); g.drawRect (rc, px * 1.5f); }
        const auto ink = on ? (sk.accentSel.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white) : pal.ink;
        auto t = rc.reduced (px * 2);
        const float big2 = std::min (px * 2.0f, std::max (px, rc.getHeight() / 40.0f));
        if (PixelFont::textWidth (big, big2) > t.getWidth()) PixelFont::drawCentred (g, big, t.withTrimmedBottom (t.getHeight() * 0.3f), px, empty ? ink.withAlpha (0.4f) : ink);
        else PixelFont::drawCentred (g, big, t.withTrimmedBottom (t.getHeight() * 0.3f), big2, empty ? ink.withAlpha (0.4f) : ink);
        if (PixelFont::textWidth (small, px) <= t.getWidth()) PixelFont::drawCentred (g, small, t.removeFromBottom (t.getHeight() * 0.3f), px, empty ? ink.withAlpha (0.4f) : ink);
        PixelFont::drawLeft (g, juce::String::charToString ((juce::juce_wchar) kPadKeys[pad]), rc.reduced (px * 2).removeFromTop (px * 9), px, ink.withAlpha (0.7f));
    }
    // the effect held lights its button
    const int held = (int) proc.player.liveFx().holding();
    for (int k = 0; k < kFx; ++k) fxBtn[k].setToggleState (held == k + 1, juce::dontSendNotification);
}

void LivePage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    for (int pad = 0; pad < 16; ++pad) if (padRect (pad).contains (e.position)) { downPad = pad; padDown (pad); return; }
}

void LivePage::mouseUp (const juce::MouseEvent&)
{
    if (downPad >= 0) { padUp (downPad); downPad = -1; }
}

int LivePage::padForKey (int code) const
{
    const int c = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
    for (int i = 0; kPadKeys[i]; ++i) if (c == kPadKeys[i]) return i;
    return -1;
}

int LivePage::fxForKey (int code) const
{
    const int c = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
    for (int i = 0; kFxKeys[i]; ++i) if (c == kFxKeys[i]) return i + 1;
    return -1;
}

bool LivePage::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k.getModifiers().isCommandDown()) return false;
    const int code = k.getKeyCode();
    if (const int pad = padForKey (code); pad >= 0)
    {
        if (keyPads.count (code) == 0) { keyPads[code] = pad; padDown (pad); }
        return true;
    }
    if (const int fx = fxForKey (code); fx > 0)
    {
        if (keyFx.count (code) == 0) { keyFx[code] = fx; fxDown (fx); }
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) { (proc.player.isPlaying() ? stopBtn : playBtn).triggerClick(); return true; }
    return false;
}

bool LivePage::keyStateChanged (bool)
{
    // keys let go: their pads / effects
    for (auto it = keyPads.begin(); it != keyPads.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first)) { padUp (it->second); it = keyPads.erase (it); }
        else ++it;
    }
    for (auto it = keyFx.begin(); it != keyFx.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first)) { const int k = it->second; it = keyFx.erase (it); fxUp (k); }
        else ++it;
    }
    return false;
}
} // namespace prv::ui
