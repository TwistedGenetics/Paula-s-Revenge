#include "LanePads.h"
#include "PixelFont.h"

namespace prv::ui
{
// ============================================================================ THE LANE
ArrangeLane::ArrangeLane (PaulaProcessor& p) : proc (p)
{
    setOpaque (true);
}

float ArrangeLane::colW() const    { return PixelFont::textWidth ("0000000", Theme::px); }   // (room for a short section name)
float ArrangeLane::headerH() const { return PixelFont::textHeight (Theme::px) * 2 + Theme::px * 4; }   // (section names over position numbers)
int ArrangeLane::tracksShown() const { return juce::jlimit (1, 8, proc.song.numTracks); }

int ArrangeLane::preferredHeight() const
{
    return (int) std::ceil (headerH() + (float) tracksShown() * Theme::px * 5.0f + Theme::px * 2);
}

int ArrangeLane::visibleColumns() const
{
    return std::max (1, (int) ((float) getWidth() / colW()));
}

int ArrangeLane::currentPos() const
{
    if (proc.player.isPlaying() && proc.player.getMode() == Player::Mode::Song) return proc.player.uiSeqPos.load();
    return proc.editorState.seqPos;
}

void ArrangeLane::keepVisible (int pos)
{
    const int n = (int) proc.song.sequence.size();
    const int vis = visibleColumns();
    if (pos < first) first = pos;
    else if (pos >= first + vis) first = pos - vis + 1;
    first = juce::jlimit (0, std::max (0, n - vis), first);
}

juce::Rectangle<float> ArrangeLane::columnRect (int pos) const
{
    const int vis = visibleColumns();
    if (pos < first || pos >= first + vis || pos >= (int) proc.song.sequence.size()) return {};
    return { (float) (pos - first) * colW(), 0.0f, colW(), (float) getHeight() };
}

int ArrangeLane::columnAt (float x) const
{
    if (x < 0) return -1;
    const int pos = first + (int) (x / colW());
    return pos < (int) proc.song.sequence.size() && (pos - first) < visibleColumns() ? pos : -1;
}

juce::String ArrangeLane::sectionNameAt (int pos) const
{
    for (auto& s : proc.song.sections) if (s.pos == pos) return juce::String (s.name);
    return {};
}

juce::Colour ArrangeLane::sectionColour (int pos, int track) const
{
    // the colour of the section this position is in (by the section's number), or the track's own before any section
    const auto& sk = Theme::skin;
    int idx = -1;
    for (int i = 0; i < (int) proc.song.sections.size(); ++i)
        if (proc.song.sections[(size_t) i].pos <= pos) idx = i;
    return idx < 0 ? sk.track[track % 8] : sk.track[idx % 8];
}

void ArrangeLane::tick()
{
    const int pos = currentPos();
    const bool playing = proc.player.isPlaying();
    const int key = pos * 4096 + (playing ? proc.player.uiLine.load() : 0) + (playing ? 1 << 30 : 0);
    if (key == lastKey) return;
    lastKey = key;
    keepVisible (pos);
    repaint();
}

void ArrangeLane::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    const auto& song = proc.song;
    g.fillAll (sk.bg);
    const float hh = headerH();
    const int nT = tracksShown();
    const float rowH = std::max (px * 2, ((float) getHeight() - hh - px * 2) / (float) nT);
    const float cw = colW();
    const int n = (int) song.sequence.size();
    const int cur = currentPos();
    const bool playing = proc.player.isPlaying() && proc.player.getMode() == Player::Mode::Song;
    const float gapX = std::max (1.0f, px * 0.5f), gapY = std::max (1.0f, px * 0.4f);
    const float rad = std::min (px * Theme::skin.round, rowH * 0.4f);

    for (int pos = first; pos < n && pos < first + visibleColumns() + 1; ++pos)
    {
        const float x = (float) (pos - first) * cw;
        const bool isCur = pos == cur;
        // the header: a section's name where it starts (running on until the next), the position under it
        const auto name = sectionNameAt (pos);
        const auto num = juce::String (pos).paddedLeft ('0', 3);
        const float nameY = px * 1.0f, numY = px * 2.0f + PixelFont::textHeight (px);
        PixelFont::draw (g, num, x + px, numY, px, isCur ? sk.text : sk.dim);
        if (name.isNotEmpty())
        {
            int span = 1;
            while (pos + span < n && sectionNameAt (pos + span).isEmpty()) ++span;
            const float room = (float) span * cw - px * 2;
            juce::String shown = name.toUpperCase();
            while (shown.isNotEmpty() && PixelFont::textWidth (shown, px) - px > room) shown = shown.dropLastCharacters (1);
            g.setColour (sectionColour (pos, 0));
            g.fillRect (x + px * 0.25f, nameY, std::max (1.0f, px * 0.5f), PixelFont::textHeight (px));   // (a tick where it starts)
            PixelFont::draw (g, shown, x + px, nameY, px, sectionColour (pos, 0));
        }
        // the clips
        const int blk = song.safeBlock (song.sequence[(size_t) pos]);
        const auto& b = song.blocks[(size_t) blk];
        const int lines = std::max (1, b.lines);
        for (int t = 0; t < nT; ++t)
        {
            auto clip = juce::Rectangle<float> (x + gapX, hh + (float) t * rowH + gapY, cw - gapX * 2, rowH - gapY * 2);
            if (t >= song.numTracks) continue;
            int lo = 1000, hi = -1;
            for (int l = 0; l < b.lines; ++l)
            {
                const int nn = b.at (l, t).note;
                if (nn != 0 && nn != kNoteOff) { lo = std::min (lo, nn); hi = std::max (hi, nn); }
            }
            if (hi < 0)
            {
                g.setColour (sk.stripe);
                g.fillRoundedRectangle (clip, rad);
                continue;
            }
            const auto col = sectionColour (pos, t);
            g.setColour (col);
            g.fillRoundedRectangle (clip, rad);
            // its notes, small: across = when, up = how high (within the clip's own range)
            juce::RectangleList<float> dashes;
            const float dh = std::max (1.0f, std::min (px * 0.8f, clip.getHeight() * 0.25f));
            const float inner = clip.getHeight() - dh - gapY * 2;
            for (int l = 0; l < b.lines; ++l)
            {
                const int nn = b.at (l, t).note;
                if (nn == 0 || nn == kNoteOff) continue;
                int len = 1;
                while (l + len < b.lines && b.at (l + len, t).note == 0 && len < lines / 4) ++len;
                const float fx = clip.getX() + px * 0.5f + (clip.getWidth() - px) * (float) l / (float) lines;
                const float fw = std::max (1.0f, (clip.getWidth() - px) * (float) len / (float) lines - std::max (1.0f, px * 0.3f));
                const float fy = clip.getY() + gapY + (hi == lo ? inner * 0.5f : inner * (1.0f - (float) (nn - lo) / (float) (hi - lo)));
                dashes.addWithoutMerging ({ fx, fy, fw, dh });
            }
            g.setColour (juce::Colour (0xff14161a).withAlpha (0.6f));
            g.fillRectList (dashes);
        }
        if (isCur)
        {
            // the position being edited (or playing): ringed
            g.setColour (sk.text);
            g.drawRoundedRectangle (juce::Rectangle<float> (x + gapX * 0.5f, hh - gapY, cw - gapX, (float) nT * rowH + gapY * 2), rad + 1.0f, std::max (1.5f, px * 0.6f));
            if (playing)
            {
                const float frac = (float) proc.player.uiLine.load() / (float) lines;
                const float phx = std::round (x + gapX + (cw - gapX * 2) * juce::jlimit (0.0f, 1.0f, frac));
                g.setColour (sk.text);
                g.fillRect (phx, hh, std::max (1.0f, px * 0.5f), (float) nT * rowH);
            }
        }
    }
    // a hairline under the strip, between it and the pattern
    g.setColour (sk.rule);
    g.fillRect (0.0f, (float) getHeight() - 1.0f, (float) getWidth(), 1.0f);
}

void ArrangeLane::mouseDown (const juce::MouseEvent& e)
{
    const int pos = columnAt ((float) e.x);
    if (pos >= 0 && onPick) onPick (pos);
}

void ArrangeLane::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY;
    const int step = d > 0 ? 1 : d < 0 ? -1 : 0;
    const int n = (int) proc.song.sequence.size();
    first = juce::jlimit (0, std::max (0, n - visibleColumns()), first + step * 2);
    repaint();
}

// ============================================================================ THE PADS
PadPanel::PadPanel (PaulaProcessor& p) : proc (p)
{
    addAndMakeVisible (bankDown);
    addAndMakeVisible (bankUp);
    bankDown.setClickingTogglesState (false);
    bankUp.setClickingTogglesState (false);
    bankDown.onClick = [this] { setBank (bank() - 1); };
    bankUp.onClick = [this] { setBank (bank() + 1); };
    addAndMakeVisible (slicesBtn);
    slicesBtn.setClickingTogglesState (false);
    slicesBtn.onClick = [this] { setSlicesMode (! slicesMode()); };
    slicesBtn.setToggleState (slicesMode(), juce::dontSendNotification);
    for (int t = 0; t < kMaxTracks; ++t) lastNotes[t] = proc.player.uiNotes[t].load();
    startTimerHz (30);
}

PadPanel::~PadPanel() { stopTimer(); }

int PadPanel::bank() const { return slicesMode() ? sliceBank : juce::jlimit (0, 3, proc.editorState.padBank); }

bool PadPanel::slicesMode() const { return proc.editorState.padSlices; }

void PadPanel::setSlicesMode (bool on)
{
    proc.editorState.padSlices = on;
    sliceBank = 0;
    slicesBtn.setToggleState (on, juce::dontSendNotification);
    for (auto& f : flash) f = 0;
    if (onBank) onBank();
    repaint();
}

int PadPanel::sliceOfPad (int i) const
{
    if (! slicesMode() || i < 0 || i >= 16) return -1;
    const auto& in = proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
    const int s = sliceBank * 16 + i;
    return in.numSlices() >= 2 && s < in.numSlices() ? s : -1;
}

const std::vector<HitKind>& PadPanel::sliceKinds()
{
    const int i = juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument);
    const auto& in = proc.song.instruments[(size_t) i];
    if (kindsFor != i || kindsSlices != in.slices.size() || kindsLen != in.data.size() || (in.numSlices() >= 2 && kinds.size() != (size_t) in.numSlices()))
    {
        kindsFor = i; kindsSlices = in.slices.size(); kindsLen = in.data.size();
        kinds = in.numSlices() >= 2 ? classifySlices (in) : std::vector<HitKind> {};
    }
    return kinds;
}

void PadPanel::setBank (int b)
{
    if (slicesMode())
    {
        const auto& in = proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
        const int banks = std::max (1, (in.numSlices() + 15) / 16);
        b = juce::jlimit (0, banks - 1, b);
        if (b == sliceBank) return;
        sliceBank = b;
        for (auto& f : flash) f = 0;
        if (onBank) onBank();
        repaint();
        return;
    }
    b = juce::jlimit (0, 3, b);
    if (b == proc.editorState.padBank) return;
    proc.editorState.padBank = b;
    for (auto& f : flash) f = 0;
    if (onBank) onBank();
    repaint();
}

float PadPanel::titleH() const { return PixelFont::textHeight (Theme::px) + Theme::px * 3; }

float PadPanel::sideW() const
{
    // the strip (two rows of eight): the title and the bank down the left, not over the top
    return isStrip() ? PixelFont::textWidth ("PADS A", Theme::px) + Theme::px * 4 : 0.0f;
}

bool PadPanel::isStrip() const { return getWidth() >= getHeight() * 3; }

void PadPanel::resized()
{
    const float px = Theme::px;
    const float th = titleH();
    const int bw = (int) (PixelFont::textWidth ("+", px) + px * 4);
    const int bh = (int) (th - px);
    const int sw = (int) (PixelFont::textWidth ("SLICES", px) + px * 4);
    if (isStrip())
    {
        const int x0 = (int) px * 2;
        bankDown.setBounds (x0, (int) (th + px), bw, bh);
        bankUp.setBounds (bankDown.getRight() + (int) px, (int) (th + px), bw, bh);
        slicesBtn.setBounds (x0, bankDown.getBottom() + (int) px, std::max (sw, bankUp.getRight() - x0), bh);
        return;
    }
    bankUp.setBounds (getWidth() - bw - (int) px, (int) (px * 0.5f), bw, bh);
    bankDown.setBounds (bankUp.getX() - bw - (int) px, (int) (px * 0.5f), bw, bh);
    slicesBtn.setBounds (bankDown.getX() - sw - (int) px, (int) (px * 0.5f), sw, bh);
}

int PadPanel::columns() const
{
    // four by four, or (a strip under the pattern) two rows of eight
    return isStrip() ? 8 : 4;
}

juce::Rectangle<float> PadPanel::padArea() const
{
    auto r = isStrip() ? getLocalBounds().toFloat().withTrimmedLeft (sideW()).reduced (Theme::px)
                       : getLocalBounds().toFloat().withTrimmedTop (titleH() + Theme::px).reduced (Theme::px);
    const int cols = columns(), rows = 16 / cols;
    // square four by four; two rows of eight may be wider than tall (a strip under the pattern)
    const float cw = r.getWidth() / (float) cols;
    const float ch = std::min (cw, r.getHeight() / (float) rows);
    const float w = cols == 4 ? std::min (cw, ch) * 4.0f : r.getWidth();
    return { r.getCentreX() - w * 0.5f, r.getY(), w, ch * (float) rows };
}

juce::Rectangle<float> PadPanel::padRect (int i) const
{
    if (i < 0 || i >= 16) return {};
    const auto a = padArea();
    const int cols = columns(), rows = 16 / cols;
    const float cw = a.getWidth() / (float) cols, ch = a.getHeight() / (float) rows, g = std::max (2.0f, Theme::px);
    // pad 1 at the bottom left, like a drum machine: 1-4 (1-8) along the bottom, 13-16 (9-16) along the top
    const int col = i % cols, row = rows - 1 - i / cols;
    return { a.getX() + (float) col * cw + g * 0.5f, a.getY() + (float) row * ch + g * 0.5f, cw - g, ch - g };
}

int PadPanel::preferredHeightFor (int width) const
{
    // two rows of eight across this width: each as tall as its number and name
    const float cell = std::min (((float) width - Theme::px * 2) / 8.0f, PixelFont::textHeight (Theme::px) * 2 + Theme::px * 5);
    return (int) std::ceil (Theme::px * 2 + cell * 2);
}

int PadPanel::padAt (juce::Point<float> p) const
{
    for (int i = 0; i < 16; ++i) if (padRect (i).contains (p)) return i;
    return -1;
}

void PadPanel::paint (juce::Graphics& g)
{
    const auto& P = Theme::palFor (*this);
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    Theme::panelBox (g, getLocalBounds().toFloat(), {}, P);
    // the title and the bank
    const auto bankLetter = juce::String::charToString ((juce::juce_wchar) ('A' + bank()));
    const auto insHex = juce::String::toHexString (proc.editorState.instrument + 1).toUpperCase().paddedLeft ('0', 2);
    const auto title = slicesMode() ? (isStrip() ? insHex + " " + bankLetter : "SLICES OF " + insHex + " " + bankLetter)
                                    : juce::String ("PADS ") + bankLetter;
    if (slicesMode())
    {
        // SLICES: the current instrument's slices, each named for what it is
        const auto& in = proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
        const auto& kinds = sliceKinds();
        if (isStrip()) PixelFont::drawLeft (g, title, juce::Rectangle<float> (px * 2, px * 0.5f, sideW() - px * 2, titleH()), px, P.ink);
        else PixelFont::drawLeft (g, title, juce::Rectangle<float> (px * 2, 0, (float) slicesBtn.getX() - px * 3, titleH()), px, P.ink);
        if (in.numSlices() < 2)
        {
            PixelFont::drawCentred (g, in.hasData() ? "NOT SLICED - BREAK PAGE OR SLICE" : "NO SAMPLE", padArea(), px, P.faint);
            return;
        }
        for (int i = 0; i < 16; ++i)
        {
            const auto r = padRect (i);
            const int s = sliceOfPad (i);
            const float lit = std::max (flash[i], held == i ? 1.0f : 0.0f);
            const auto kind = s >= 0 && s < (int) kinds.size() ? kinds[(size_t) s] : HitKind::Other;
            const auto col = kind == HitKind::Kick ? juce::Colour (0xffff5a4a) : kind == HitKind::Snare ? juce::Colour (0xffffa53d)
                           : kind == HitKind::Hat ? juce::Colour (0xff5ec8ff) : sk.track[i % 8];
            const float rad = std::max (1.0f, px * std::max (0.6f, sk.round));
            auto face = (s >= 0 ? P.light : P.panel).interpolatedWith (col, lit * 0.85f);
            g.setColour (face);
            g.fillRoundedRectangle (r, rad);
            if (! sk.smoothType) Theme::bevel (g, r, held != i, std::max (1.0f, px * 0.5f), P);
            if (s < 0) continue;
            const float fh = std::max (2.0f, px * 0.7f);
            g.setColour (col.withAlpha (0.9f));
            g.fillRoundedRectangle (r.getX() + px * 1.5f, r.getBottom() - fh - px * 1.2f, r.getWidth() - px * 3, fh, fh * 0.5f);
            const auto ink = lit > 0.5f ? juce::Colour (0xff14161a) : P.ink;
            PixelFont::draw (g, juce::String (s + 1).paddedLeft ('0', 2) + " " + juce::String::charToString ((juce::juce_wchar) hitLetter (kind)),
                             r.getX() + px * 1.5f, r.getY() + px * 1.5f, px, ink);
            if (r.getHeight() >= PixelFont::textHeight (px) * 2 + px * 4)
            {
                juce::String nm (hitName (kind));
                while (nm.isNotEmpty() && PixelFont::textWidth (nm, px) - px > r.getWidth() - px * 3) nm = nm.dropLastCharacters (1);
                PixelFont::draw (g, nm, r.getX() + px * 1.5f, r.getY() + px * 3.0f + PixelFont::textHeight (px), px, ink);
            }
        }
        return;
    }
    if (isStrip()) PixelFont::drawLeft (g, title, juce::Rectangle<float> (px * 2, px * 0.5f, sideW() - px * 2, titleH()), px, P.ink);
    else PixelFont::drawLeft (g, title, juce::Rectangle<float> (px * 2, 0, (float) slicesBtn.getX() - px * 3, titleH()), px, P.ink);
    for (int i = 0; i < 16; ++i)
    {
        const auto r = padRect (i);
        const int ins = bank() * 16 + i;
        const bool exists = ins < kMaxInstruments;
        const auto& in = proc.song.instruments[(size_t) std::min (ins, kMaxInstruments - 1)];
        const bool used = exists && (! in.data.empty() || in.isPlugin());
        const float lit = std::max (flash[i], held == i ? 1.0f : 0.0f);
        const auto col = sk.track[i % 8];
        const float rad = std::max (1.0f, px * std::max (0.6f, sk.round));
        auto face = used ? P.light : P.panel.darker (0.1f);
        if (! exists) face = P.panel;
        face = face.interpolatedWith (col, lit * 0.85f);
        g.setColour (face);
        g.fillRoundedRectangle (r, rad);
        if (! sk.smoothType) Theme::bevel (g, r, held != i, std::max (1.0f, px * 0.5f), P);
        if (! exists) continue;
        // its colour along the foot (a lit LED on HARDWARE's pads)
        const float fh = std::max (2.0f, px * 0.7f);
        g.setColour (col.withAlpha (used ? (sk.leds ? 0.45f + 0.55f * lit : 0.9f) : 0.3f));
        g.fillRoundedRectangle (r.getX() + px * 1.5f, r.getBottom() - fh - px * 1.2f, r.getWidth() - px * 3, fh, fh * 0.5f);
        // the number, and the name as far as it fits
        const auto ink = lit > 0.5f ? juce::Colour (0xff14161a) : (used ? P.ink : P.faint);
        PixelFont::draw (g, juce::String (ins + 1).paddedLeft ('0', 2), r.getX() + px * 1.5f, r.getY() + px * 1.5f, px, ink);
        if (used)
        {
            juce::String nm = juce::String (in.name).toUpperCase().trim();
            const float room = r.getWidth() - px * 3;
            const float npx = px;
            while (nm.isNotEmpty() && PixelFont::textWidth (nm, npx) - npx > room) nm = nm.dropLastCharacters (1);
            if (r.getHeight() >= PixelFont::textHeight (px) * 2 + px * 4)
                PixelFont::draw (g, nm, r.getX() + px * 1.5f, r.getY() + px * 3.0f + PixelFont::textHeight (px), npx, ink);
        }
    }
}

void PadPanel::mouseDown (const juce::MouseEvent& e)
{
    const int i = padAt (e.position);
    if (i < 0) return;
    if (slicesMode())
    {
        const int s = sliceOfPad (i);
        if (s < 0) return;
        held = i;
        flash[i] = 1.0f;
        repaint();
        if (onSliceDown) onSliceDown (s);
        return;
    }
    const int ins = bank() * 16 + i;
    if (ins >= kMaxInstruments) return;
    held = i;
    flash[i] = 1.0f;
    repaint();
    if (onPadDown) onPadDown (ins);
}

void PadPanel::mouseUp (const juce::MouseEvent&)
{
    if (held < 0) return;
    held = -1;
    repaint();
    if (onPadUp) onPadUp();
}

void PadPanel::timerCallback()
{
    bool changed = false;
    if (slicesBtn.getToggleState() != slicesMode()) { slicesBtn.setToggleState (slicesMode(), juce::dontSendNotification); changed = true; }
    if (lastInstrument != proc.editorState.instrument)
    {
        // (SLICES follow the current instrument)
        lastInstrument = proc.editorState.instrument;
        if (slicesMode()) { sliceBank = 0; for (auto& f : flash) f = 0; changed = true; }
    }
    // a pad lights when its instrument plays on any track
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const int n = proc.player.uiNotes[t].load();
        if (n != lastNotes[t])
        {
            lastNotes[t] = n;
            if (slicesMode())
            {
                // SLICES: a pad lights when the song plays its slice (the current instrument, SLICE TO KEYS)
                if (proc.player.uiTrackInstr[t].load() == proc.editorState.instrument)
                {
                    const int p = proc.player.uiTrackKey[t].load() - 37 - sliceBank * 16;
                    if (p >= 0 && p < 16) { flash[p] = 1.0f; changed = true; }
                }
                continue;
            }
            const int ins = proc.player.uiTrackInstr[t].load() - bank() * 16;
            if (ins >= 0 && ins < 16) { flash[ins] = 1.0f; changed = true; }
        }
    }
    for (auto& f : flash)
        if (f > 0.0f) { f = f < 0.04f ? 0.0f : f * 0.82f; changed = true; }
    if (changed) repaint();
}
} // namespace prv::ui
