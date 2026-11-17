// Paula's Revenge - the TABLE page (see TablePage.h)
#include "TablePage.h"
#include "ChoiceBox.h"
#include "../engine/Commands.h"

namespace prv::ui
{
namespace
{
    juce::String hx (int v, int digits = 2) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', digits); }

    // the commands a table row can carry (in the order the menu lists them), with a value to start from
    struct TblCmd { uint8_t cmd, param; const char* code; const char* name; const char* what; };
    const std::vector<TblCmd>& tableCommands()
    {
        static const std::vector<TblCmd> list {
            { kCmdHop, 0x00, ">", "HOP",          "BACK TO ROW Y - X MORE TIMES, OR FOR EVER (X 0)" },
            { 0xE, 0x90, "E9", "AGAIN (RETRIGGER)", "PLAYS THE NOTE AGAIN FROM ITS START (A RATCHET, A FLAM)" },
            { 0x9, 0x10, "9", "AGAIN, FROM XX",   "PLAYS IT AGAIN, XX x 256 SAMPLES IN" },
            { cmdLetter ('P'), 0x80, "P", "AGAIN, FROM XX/256", "PLAYS IT AGAIN, PART-WAY IN: 80 = HALFWAY" },
            { cmdLetter ('S'), 0x01, "S", "SLICE",  "PLAYS SLICE XX+1 (OR SXX PART) FROM HERE ON" },
            { 0xC, 0x40, "C", "VOLUME",             "THE NOTE'S VOLUME, 00-40" },
            { 0xE, 0xC0, "EC", "CUT",               "SILENCES THE NOTE" },
            { 0x8, 0x80, "8", "PAN",                "00 LEFT, 80 CENTRE, FF RIGHT" },
            { 0x1, 0x08, "1", "PITCH UP A STEP",    "BENDS IT UP BY XX, ONCE" },
            { 0x2, 0x08, "2", "PITCH DOWN A STEP",  "BENDS IT DOWN BY XX, ONCE" },
            { cmdLetter ('T'), 0x88, "T", "MICRO-TUNE", "CENTS, 80 = IN TUNE" },
            { cmdLetter ('L'), 0x80, "L", "LOW-PASS",   "CUTOFF 00-FF" },
            { cmdLetter ('H'), 0x40, "H", "HIGH-PASS",  "CUTOFF 00-FF" },
            { cmdLetter ('W'), 0x80, "W", "BAND-PASS",  "CENTRE 00-FF" },
            { cmdLetter ('Q'), 0x80, "Q", "RESONANCE",  "FOR L, H AND W" },
            { cmdLetter ('O'), 0x80, "O", "OVERDRIVE",  "00-FF" },
            { cmdLetter ('Z'), 0x06, "Z", "BIT CRUSH",  "TO XX BITS (01-0F)" },
            { cmdLetter ('R'), 0x30, "R", "ROLL",       "REPEATS EVERY X TICKS, Y FADES" },
            { cmdLetter ('N'), 0x03, "N", "NOTE LENGTH", "RELEASES IT AFTER XX TICKS" },
            { cmdLetter ('U'), 0x01, "U", "REVERSE",    "U01 BACKWARDS, U00 FORWARDS" },
            { cmdLetter ('X'), 0x12, "X", "RANDOMISE",  "X 1 VOLUME, 2 PAN; Y AMOUNT" },
            { cmdLetter ('V'), 0x80, "V", "AUTO A",     "THE TRACK'S AUTO A KNOB (ON A TRACK)" },
            { cmdLetter ('J'), 0x80, "J", "AUTO B",     "THE TRACK'S AUTO B KNOB (ON A TRACK)" },
        };
        return list;
    }
    bool tableAllows (uint8_t cmd)
    {
        for (const auto& t : tableCommands()) if (t.cmd == cmd) return true;
        return cmd == 0xE;
    }

    // the factory tables: a name, SPEED, and its rows as { note, vol, fx1 cmd, param }
    struct PRow { int note, vol; uint8_t cmd, param; };
    struct Preset { const char* name; int speed; std::vector<PRow> rows; };
    constexpr int N = kTableNoNote, V = kTableNoVol;
    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list {
            { "MINOR ARP",        2, { { 0, V, 0, 0 }, { 3, V, 0, 0 }, { 7, V, 0, 0 }, { N, V, kCmdHop, 0x00 } } },
            { "MAJOR ARP",        2, { { 0, V, 0, 0 }, { 4, V, 0, 0 }, { 7, V, 0, 0 }, { N, V, kCmdHop, 0x00 } } },
            { "OCTAVE TRILL",     3, { { 0, V, 0, 0 }, { 12, V, 0, 0 }, { N, V, kCmdHop, 0x00 } } },
            { "POWER CHORD ARP",  1, { { 0, V, 0, 0 }, { 7, V, 0, 0 }, { 12, V, 0, 0 }, { N, V, kCmdHop, 0x00 } } },
            { "FLAM",             1, { { N, 0x18, 0, 0 }, { N, 0x40, 0xE, 0x90 } } },
            { "RATCHET x4",       2, { { N, 0x40, 0, 0 }, { N, 0x30, 0xE, 0x90 }, { N, 0x20, 0xE, 0x90 }, { N, 0x10, 0xE, 0x90 } } },
            { "BUZZ ROLL",        1, { { N, V, 0xE, 0x90 }, { N, V, kCmdHop, 0x00 } } },
            { "STUTTER x3, THEN ON", 2, { { N, V, 0xE, 0x90 }, { N, V, kCmdHop, 0x20 } } },
            { "808 ZAP (PITCH DROP)", 1, { { 12, V, 0, 0 }, { 7, V, 0, 0 }, { 4, V, 0, 0 }, { 2, V, 0, 0 }, { 1, V, 0, 0 }, { 0, V, 0, 0 } } },
            { "FILTER PLUCK",     1, { { N, V, cmdLetter ('L'), 0xF0 }, { N, V, cmdLetter ('L'), 0xC0 }, { N, V, cmdLetter ('L'), 0x98 },
                                        { N, V, cmdLetter ('L'), 0x78 }, { N, V, cmdLetter ('L'), 0x60 }, { N, V, cmdLetter ('L'), 0x50 } } },
            { "PAN SWEEP",        3, { { N, V, 0x8, 0x00 }, { N, V, 0x8, 0x40 }, { N, V, 0x8, 0x80 }, { N, V, 0x8, 0xC0 }, { N, V, 0x8, 0xFF },
                                        { N, V, 0x8, 0xC0 }, { N, V, 0x8, 0x80 }, { N, V, 0x8, 0x40 }, { N, V, kCmdHop, 0x00 } } },
            { "GATE (ON, OFF)",   3, { { N, 0x40, 0, 0 }, { N, 0x00, 0, 0 }, { N, V, kCmdHop, 0x00 } } },
            { "FADE OUT (A LINE A ROW)", 0, { { N, 0x40, 0, 0 }, { N, 0x30, 0, 0 }, { N, 0x20, 0, 0 }, { N, 0x14, 0, 0 }, { N, 0x0C, 0, 0 }, { N, 0x06, 0, 0 }, { N, 0x00, 0, 0 } } },
            { "OCTAVE BOUNCE (A LINE A ROW)", 0, { { 0, V, 0, 0 }, { 12, V, 0, 0 }, { N, V, kCmdHop, 0x00 } } },
        };
        return list;
    }
}

TablePage::TablePage (PaulaProcessor& p) : proc (p)
{
    auto& st = proc.editorState;
    auto changed = [this] { play (false); row = 0; col = 0; digit = 0; repaint(); if (onChanged) onChanged(); };
    insDown.onClick = [&st, changed] { st.instrument = (st.instrument + kMaxInstruments - 1) % kMaxInstruments; changed(); };
    insUp.onClick   = [&st, changed] { st.instrument = (st.instrument + 1) % kMaxInstruments; changed(); };
    onBtn.setClickingTogglesState (false);
    onBtn.onClick = [this] { setOn (! table().on); };
    speedDown.onClick = [this] { setSpeed (table().speed - 1); };
    speedUp.onClick = [this] { setSpeed (table().speed + 1); };
    presetBtn.onClick = [this]
    {
        std::vector<MenuItem> items;
        const auto names = presetNames();
        for (int i = 0; i < names.size(); ++i) items.push_back ({ names[i], [this, i] { applyPreset (i); } });
        showMenu ("TABLE PRESETS (REPLACE THIS TABLE)", std::move (items));
    };
    copyBtn.onClick = [this] { clip = table(); hasClip = true; if (onStatus) onStatus ("TABLE COPIED - PICK ANOTHER INSTRUMENT AND PASTE"); };
    pasteBtn.onClick = [this]
    {
        if (! hasClip) { if (onStatus) onStatus ("NOTHING COPIED YET - COPY A TABLE FIRST"); return; }
        const auto c = clip;
        edit ("PASTE TABLE", [c] (InstrumentTable& t) { t = c; });
        if (onStatus) onStatus ("TABLE PASTED INTO " + insName (instrument()));
    };
    clearBtn.onClick = [this] { clearTable(); };
    playBtn.onStateChange = [this] { play (playBtn.isDown()); };
    closeBtn.onClick = [this] { play (false); if (onClose) onClose(); };
    for (auto* b : { &insDown, &insUp, &onBtn, &speedDown, &speedUp, &presetBtn, &copyBtn, &pasteBtn, &clearBtn, &playBtn, &closeBtn })
        addAndMakeVisible (b);
    setWantsKeyboardFocus (true);
}

TablePage::~TablePage() { play (false); }

int TablePage::instrument() const { return juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument); }
InstrumentTable& TablePage::table() { return proc.song.instruments[(size_t) instrument()].table; }
const InstrumentTable& TablePage::table() const { return proc.song.instruments[(size_t) instrument()].table; }

juce::String TablePage::insName (int i) const
{
    const auto& in = proc.song.instruments[(size_t) i];
    return hx (i + 1) + " " + (in.name.empty() ? juce::String (in.hasData() ? "(NO NAME)" : "(EMPTY)") : juce::String (in.name).toUpperCase());
}

template <typename Fn> void TablePage::edit (const juce::String& what, Fn&& fn, bool merge)
{
    proc.history.instrument (instrument(), what, merge);
    proc.withLock ([&] { fn (table()); });
    repaint();
    if (onChanged) onChanged();
}

void TablePage::opened()
{
    row = juce::jlimit (0, kTableRows - 1, row);
    digit = 0; negPending = false;
    grabKeyboardFocus();
    if (onStatus)
        onStatus (table().used() ? juce::String ("TABLE: TYPE IN THE ROWS - HOLD TO PLAY HEARS IT; ON PLAYS IT WITH EVERY NOTE")
                                 : juce::String ("TABLE: EMPTY - TYPE A NOTE (+7, -12) OR A COMMAND, OR PICK ONE FROM PRESETS..."));
    repaint();
}

int TablePage::playingRow() const
{
    // the page's own note, else a track playing this instrument's table
    const int pr = proc.player.previewTableRow();
    if (pr >= 0) return pr;
    for (int t = 0; t < kMaxTracks; ++t)
        if (proc.player.tableInstrument (t) == instrument()) return proc.player.tableRow (t);
    return -1;
}

void TablePage::tick()
{
    const int r = playingRow();
    if (r != shownRow) { shownRow = r; repaint (gridArea.toNearestInt().expanded (4)); }
}

void TablePage::play (bool on)
{
    proc.withLock ([&] { if (on) proc.player.previewTable (instrument(), 12 * proc.editorState.octave + 1); else proc.player.stopTablePreview(); });
}

void TablePage::setOn (bool on)
{
    edit (on ? "TABLE ON" : "TABLE OFF", [on] (InstrumentTable& t) { t.on = on; });
    if (onStatus) onStatus (on ? (table().used() ? "TABLE ON: EVERY NOTE OF " + insName (instrument()) + " PLAYS WITH IT" : juce::String ("TABLE ON - IT'S EMPTY, SO NOTHING CHANGES YET"))
                               : juce::String ("TABLE OFF: NOTES PLAY AS THEY ARE ($XX STILL PLAYS IT ON A NOTE)"));
}

void TablePage::setSpeed (int ticks)
{
    const int v = juce::jlimit (0, 16, ticks);
    edit ("TABLE SPEED", [v] (InstrumentTable& t) { t.speed = v; }, true);
    if (onStatus) onStatus (v == 0 ? juce::String ("SPEED: A ROW A LINE (AS THE SONG'S SPEED)")
                                    : "SPEED: A ROW EVERY " + juce::String (v) + (v == 1 ? " TICK" : " TICKS"));
}

void TablePage::setNote (int r, int semis)
{
    if (r < 0 || r >= kTableRows) return;
    const int v = semis == kTableNoNote ? kTableNoNote : juce::jlimit (-48, 48, semis);
    edit ("TABLE NOTE", [r, v] (InstrumentTable& t) { t.rows[r].note = v; }, true);
}

void TablePage::setVol (int r, int vol)
{
    if (r < 0 || r >= kTableRows) return;
    const int v = vol == kTableNoVol ? kTableNoVol : juce::jlimit (0, 64, vol);
    edit ("TABLE VOL", [r, v] (InstrumentTable& t) { t.rows[r].vol = v; }, true);
}

void TablePage::setFx (int r, int which, uint8_t cmd, uint8_t param)
{
    if (r < 0 || r >= kTableRows) return;
    edit ("TABLE COMMAND", [r, which, cmd, param] (InstrumentTable& t)
    {
        if (which == 0) { t.rows[r].cmd = cmd; t.rows[r].param = param; }
        else { t.rows[r].cmd2 = cmd; t.rows[r].param2 = param; }
    }, true);
    if (onStatus && ! (cmd == 0 && param == 0))
        onStatus (juce::String (describeCommand (cmd, param)) + (tableAllows (cmd) ? juce::String() : juce::String (" - NOT USED IN A TABLE")));
}

void TablePage::clearCell (int r, int c)
{
    if (c == 0) setNote (r, kTableNoNote);
    else if (c == 1) setVol (r, kTableNoVol);
    else setFx (r, c - 2, 0, 0);
}

void TablePage::clearTable()
{
    edit ("CLEAR TABLE", [] (InstrumentTable& t) { const bool on = t.on; t = InstrumentTable {}; t.on = on; });
    if (onStatus) onStatus ("TABLE CLEARED (UNDO BRINGS IT BACK)");
}

juce::StringArray TablePage::presetNames()
{
    juce::StringArray s;
    for (const auto& p : presets()) s.add (p.name);
    return s;
}

void TablePage::applyPreset (int i)
{
    if (i < 0 || i >= (int) presets().size()) return;
    const auto& p = presets()[(size_t) i];
    edit ("TABLE PRESET", [&p] (InstrumentTable& t)
    {
        t = InstrumentTable {};
        t.on = true; t.speed = p.speed;
        for (size_t k = 0; k < p.rows.size() && k < (size_t) kTableRows; ++k)
        {
            auto& r = t.rows[k];
            r.note = p.rows[k].note; r.vol = p.rows[k].vol; r.cmd = p.rows[k].cmd; r.param = p.rows[k].param;
        }
    });
    if (onStatus) onStatus ("TABLE: " + juce::String (p.name) + " - ON. HOLD TO PLAY, OR PLAY THE INSTRUMENT");
}

// ---- drawing
float TablePage::textPx() const { return gridPx > 0 ? gridPx : Theme::px * 1.5f; }
float TablePage::rowH() const { return gridRowH > 0 ? gridRowH : textPx() * 10.0f; }

juce::Rectangle<float> TablePage::cellRect (int r, int c) const
{
    const float tp = textPx(), cw = tp * 6.0f;
    const float rowLabel = cw * 4;
    const float widths[4] = { cw * 5, cw * 4, cw * 5, cw * 5 };
    float x = gridArea.getX() + rowLabel;
    for (int k = 0; k < c; ++k) x += widths[k];
    return { x, headArea.getBottom() + rowH() * r, widths[c] - tp, rowH() };
}

juce::String TablePage::cellText (int r, int c) const
{
    const auto& rw = table().rows[(size_t) juce::jlimit (0, kTableRows - 1, r)];
    switch (c)
    {
        case 0: return rw.note == kTableNoNote ? juce::String ("---") : (rw.note < 0 ? "-" : "+") + juce::String (std::abs (rw.note)).paddedLeft ('0', 2);
        case 1: return rw.vol == kTableNoVol ? juce::String ("--") : hx (rw.vol);
        case 2: return rw.fx1Empty() ? juce::String ("---") : juce::String::charToString ((juce::juce_wchar) commandChar (rw.cmd)) + hx (rw.param);
        default: return rw.fx2Empty() ? juce::String ("---") : juce::String::charToString ((juce::juce_wchar) commandChar (rw.cmd2)) + hx (rw.param2);
    }
}

juce::String TablePage::helpFor() const
{
    const auto& rw = table().rows[(size_t) row];
    switch (col)
    {
        case 0: return "NOTE: SEMITONES FROM THE NOTE PLAYED. TYPE 7 FOR +07, -12 FOR AN OCTAVE DOWN; SHIFT+UP/DOWN NUDGES, ALT AN OCTAVE. EMPTY: THE PITCH STAYS.";
        case 1: return "VOL: 00-40 (40 = AS PLAYED), IN HEX. EMPTY: THE LEVEL STAYS.";
        default:
        {
            const uint8_t c = col == 2 ? rw.cmd : rw.cmd2, p = col == 2 ? rw.param : rw.param2;
            if (c == 0 && p == 0) return "COMMAND: TYPE ITS LETTER THEN TWO HEX DIGITS, OR RIGHT-CLICK FOR THE LIST. >XY LOOPS BACK TO ROW Y (X MORE TIMES; 0 FOR EVER).";
            return juce::String (describeCommand (c, p));
        }
    }
}

void TablePage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const auto& sk = Theme::skin;
    const float px = Theme::px, tp = textPx();
    g.fillAll (pal.panel);
    PixelFont::drawLeft (g, "TABLE", titleArea, px, pal.ink);
    g.setColour (sk.bg);
    g.fillRect (insArea);
    Theme::bevel (g, insArea, false, std::max (1.0f, px * 0.5f), pal);
    auto nm = insName (instrument());
    while (nm.length() > 3 && PixelFont::textWidth (nm, px) > insArea.getWidth() - px * 3) nm = nm.dropLastCharacters (1);
    PixelFont::drawLeft (g, nm, insArea.withTrimmedLeft (px * 2), px, sk.text);
    const auto& t = table();
    PixelFont::drawCentred (g, t.speed == 0 ? juce::String ("SPEED: A LINE") : "SPEED " + juce::String (t.speed) + (t.speed == 1 ? " TICK" : " TICKS"), speedArea, px, pal.ink);

    // the grid
    g.setColour (sk.bg);
    g.fillRect (gridArea);
    Theme::bevel (g, gridArea, false, std::max (1.0f, px * 0.5f), pal);
    static const char* heads[4] = { "NOTE", "VOL", "FX1", "FX2" };
    PixelFont::drawLeft (g, "ROW", headArea.withX (gridArea.getX() + tp * 3).withWidth (tp * 6 * 4), tp, sk.dim);
    for (int c = 0; c < 4; ++c)
        PixelFont::drawLeft (g, heads[c], cellRect (0, c).withY (headArea.getY()).withHeight (headArea.getHeight()), tp, sk.dim);
    const int playing = shownRow >= 0 ? shownRow : playingRow();
    for (int r = 0; r < kTableRows; ++r)
    {
        const auto line = juce::Rectangle<float> (gridArea.getX() + px, cellRect (r, 0).getY(), gridArea.getWidth() - px * 2, rowH());
        if (r % 4 == 0) { g.setColour (sk.band.withAlpha (0.5f)); g.fillRect (line); }
        if (r == playing && playing >= 0) { g.setColour (sk.accentSel.withAlpha (0.35f)); g.fillRect (line); }
        if (r == row) { g.setColour (sk.cursor.withAlpha (0.35f)); g.fillRect (line); }
        PixelFont::drawLeft (g, hx (r), line.withTrimmedLeft (tp * 3).withWidth (tp * 6 * 3), tp, sk.dim);
        for (int c = 0; c < 4; ++c)
        {
            const auto cr = cellRect (r, c);
            const auto text = cellText (r, c);
            const bool empty = text.startsWith ("-") && text.containsOnly ("-");
            if (r == row && c == col)
            {
                g.setColour (sk.cursor);
                g.fillRect (cr.reduced (0, px * 0.5f));
                g.setColour (sk.cursorEdge);
                g.drawRect (cr.reduced (0, px * 0.5f), std::max (1.0f, px * 0.5f));
            }
            PixelFont::drawLeft (g, text, cr.withTrimmedLeft (tp), tp, empty ? sk.dim : sk.text);
        }
    }
    // the help for the cursor's cell
    Theme::panelBox (g, helpArea, "ROW " + hx (row) + " " + juce::String (heads[col]), pal);
    auto h = helpArea.reduced (px * 4).withTrimmedTop (px * 10);
    const float lineH = PixelFont::textHeight (px) + px * 4;
    auto para = [&] (const juce::String& text)
    {
        // word-wrapped to the box (never cut short): what doesn't fit below is left out whole
        juce::String lineText;
        for (const auto& w : juce::StringArray::fromTokens (text, " ", ""))
        {
            const auto trial = lineText.isEmpty() ? w : lineText + " " + w;
            if (PixelFont::textWidth (trial, px) > h.getWidth() && lineText.isNotEmpty())
            {
                if (h.getHeight() >= lineH) PixelFont::drawLeft (g, lineText, h.removeFromTop (lineH), px, pal.ink);
                lineText = w;
            }
            else lineText = trial;
        }
        if (lineText.isNotEmpty() && h.getHeight() >= lineH) PixelFont::drawLeft (g, lineText, h.removeFromTop (lineH), px, pal.ink);
        if (h.getHeight() >= lineH) h.removeFromTop (lineH * 0.5f);
    };
    para (helpFor());
    para ("ON: EVERY NOTE OF THE INSTRUMENT PLAYS WITH ITS TABLE.");
    para ("$XX IN THE PATTERN: THAT NOTE PLAYS WITH INSTRUMENT XX'S TABLE.");
    para ("SPEED: TICKS A ROW (0 = A ROW A LINE).");
    para ("DELETE CLEARS A CELL. ARROWS MOVE. SPACE PLAYS.");
}

void TablePage::resized()
{
    const float px = Theme::px;
    auto charsW = [px] (int c) { return (int) (px * 6 * c + px * 4); };
    const int rH = (int) (px * 11), gap = (int) px;
    auto r = getLocalBounds().reduced ((int) (px * 3));
    auto top = r.removeFromTop (rH);
    closeBtn.setBounds (top.removeFromRight (charsW (5)));
    titleArea = top.toFloat();
    r.removeFromTop (gap * 2);
    auto bar = r.removeFromTop (rH);
    insDown.setBounds (bar.removeFromLeft (charsW (1))); bar.removeFromLeft (gap);
    insArea = bar.removeFromLeft (charsW (22)).toFloat(); bar.removeFromLeft (gap);
    insUp.setBounds (bar.removeFromLeft (charsW (1))); bar.removeFromLeft (gap * 4);
    onBtn.setBounds (bar.removeFromLeft (charsW (2))); bar.removeFromLeft (gap * 4);
    speedDown.setBounds (bar.removeFromLeft (charsW (1)));
    speedArea = bar.removeFromLeft (charsW (14)).toFloat();
    speedUp.setBounds (bar.removeFromLeft (charsW (1))); bar.removeFromLeft (gap * 4);
    presetBtn.setBounds (bar.removeFromLeft (charsW (10))); bar.removeFromLeft (gap);
    copyBtn.setBounds (bar.removeFromLeft (charsW (4))); bar.removeFromLeft (gap);
    pasteBtn.setBounds (bar.removeFromLeft (charsW (5))); bar.removeFromLeft (gap);
    clearBtn.setBounds (bar.removeFromLeft (charsW (5)));
    r.removeFromTop (gap * 2);
    auto playRow = r.removeFromBottom (rH);
    playBtn.setBounds (playRow.removeFromLeft (charsW (14)));
    r.removeFromBottom (gap * 2);
    // the grid on the left: its text as big as lets all 16 rows and the heading fit (up to 1.5x), the help on the right
    gridRowH = std::min (Theme::px * 15.0f, ((float) r.getHeight() - px * 4) / (float) (kTableRows + 1));
    gridPx = std::max (Theme::px, std::min (Theme::px * 1.5f, gridRowH / 9.0f));
    const float tp = textPx();
    const float gridW = tp * 6 * 23 + tp * 4;
    const float needH = rowH() * (kTableRows + 1) + px * 4;
    gridArea = r.toFloat().removeFromLeft (std::min ((float) r.getWidth() * 0.62f, gridW)).withHeight (std::min ((float) r.getHeight(), needH));
    headArea = gridArea.withHeight (rowH()).translated (0, px * 2);
    helpArea = r.toFloat().withTrimmedLeft (gridArea.getWidth() + px * 4);
}

// ---- mouse and keys
void TablePage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    for (int r = 0; r < kTableRows; ++r)
        for (int c = 0; c < 4; ++c)
            if (cellRect (r, c).contains (e.position))
            {
                row = r; col = c; digit = 0; negPending = false;
                repaint();
                if (e.mods.isPopupMenu() && c >= 2) showFxMenu (r, c - 2);
                else if (e.mods.isPopupMenu()) clearCell (r, c);
                return;
            }
}

void TablePage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    for (int r = 0; r < kTableRows; ++r)
        for (int c = 0; c < 2; ++c)
            if (cellRect (r, c).contains (e.position))
            {
                const int d = w.deltaY > 0 ? 1 : -1;
                const auto& rw = table().rows[(size_t) r];
                if (c == 0) setNote (r, rw.note == kTableNoNote ? 0 : rw.note + d);
                else setVol (r, rw.vol == kTableNoVol ? 0x40 : rw.vol + d);
                row = r; col = c;
                return;
            }
}

void TablePage::showFxMenu (int r, int which)
{
    std::vector<MenuItem> items;
    for (const auto& t : tableCommands())
    {
        const uint8_t c = t.cmd, p = t.param;
        items.push_back ({ juce::String (t.code).paddedRight (' ', 3) + " " + t.name + " - " + t.what, [this, r, which, c, p] { setFx (r, which, c, p); col = which + 2; digit = 1; } });
    }
    items.push_back ({ "CLEAR THIS COMMAND", [this, r, which] { setFx (r, which, 0, 0); } });
    showMenu ("TABLE COMMAND (ROW " + hx (r) + ")", std::move (items));
}

void TablePage::moveCursor (int dr, int dc)
{
    row = juce::jlimit (0, kTableRows - 1, row + dr);
    col = juce::jlimit (0, 3, col + dc);
    digit = 0; negPending = false;
    repaint();
}

bool TablePage::typeKey (juce::juce_wchar ch)
{
    auto& rw = table().rows[(size_t) row];
    const juce::juce_wchar up = juce::CharacterFunctions::toUpperCase (ch);
    auto hexVal = [] (juce::juce_wchar c) { return c >= '0' && c <= '9' ? (int) (c - '0') : c >= 'A' && c <= 'F' ? (int) (c - 'A' + 10) : -1; };
    if (col == 0)
    {
        if (ch == '-' || ch == '+')
        {
            if (rw.note != kTableNoNote && digit == 0) setNote (row, ch == '-' ? -std::abs (rw.note) : std::abs (rw.note));
            else { negPending = ch == '-'; digit = 0; }
            return true;
        }
        if (ch < '0' || ch > '9') return false;
        const int d = (int) (ch - '0');
        if (digit == 0) { setNote (row, negPending ? -d : d); digit = 1; }
        else { const int a = std::abs (rw.note) * 10 + d; setNote (row, (negPending || rw.note < 0) ? -std::min (48, a) : std::min (48, a)); digit = 0; negPending = false; moveCursor (1, 0); }
        return true;
    }
    if (col == 1)
    {
        const int h = hexVal (up);
        if (h < 0) return false;
        if (digit == 0) { setVol (row, h << 4); digit = 1; }
        else { setVol (row, ((rw.vol == kTableNoVol ? 0 : rw.vol) & 0xF0) | h); digit = 0; moveCursor (1, 0); }
        return true;
    }
    const int which = col - 2;
    uint8_t& c = which == 0 ? rw.cmd : rw.cmd2;
    uint8_t& p = which == 0 ? rw.param : rw.param2;
    if (digit == 0)
    {
        const int cm = commandFromChar ((char) up);
        if (cm < 0) return false;
        uint8_t start = 0;
        for (const auto& t : tableCommands()) if (t.cmd == cm) { start = t.param; break; }
        setFx (row, which, (uint8_t) cm, start);
        digit = 1;
        return true;
    }
    const int h = hexVal (up);
    if (h < 0) return false;
    if (digit == 1) { setFx (row, which, c, (uint8_t) ((h << 4) | (p & 15))); digit = 2; }
    else { setFx (row, which, c, (uint8_t) ((p & 0xF0) | h)); digit = 0; moveCursor (1, 0); }
    return true;
}

bool TablePage::keyPressed (const juce::KeyPress& k)
{
    const auto code = k.getKeyCode();
    const auto& rw = table().rows[(size_t) row];
    if (code == juce::KeyPress::escapeKey) { play (false); if (onClose) onClose(); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const int d = code == juce::KeyPress::upKey ? 1 : -1;
        if (k.getModifiers().isShiftDown() || k.getModifiers().isAltDown())
        {
            const int step = k.getModifiers().isAltDown() ? 12 : 1;
            if (col == 0) setNote (row, (rw.note == kTableNoNote ? 0 : rw.note) + d * step);
            else if (col == 1) setVol (row, (rw.vol == kTableNoVol ? 0x40 : rw.vol) + d * step);
            return true;
        }
        moveCursor (-d, 0);
        return true;
    }
    if (code == juce::KeyPress::leftKey) { moveCursor (0, -1); return true; }
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::tabKey) { moveCursor (0, k.getModifiers().isShiftDown() ? -1 : 1); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { clearCell (row, col); digit = 0; moveCursor (1, 0); return true; }
    if (code == juce::KeyPress::spaceKey) { play (proc.player.previewTableRow() < 0); return true; }
    if (k.getModifiers().isCommandDown()) return false;
    return typeKey (k.getTextCharacter());
}
} // namespace prv::ui
