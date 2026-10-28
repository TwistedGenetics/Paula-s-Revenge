// Paula's Revenge - CHORDS page
#include "ChordsPage.h"
#include "ChoiceBox.h"
#include "../engine/SongOps.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    const char* kCatShort[] = { "MAJOR", "MINOR", "DOM", "SUS", "DIM", "AUG", "OTHER" };
    // the database's text, in capitals, for searching ("MIN7 INV 1 MINOR SEVENTH"; flat = B)
    juce::String searchText (const ChordEntry& e)
    {
        return (juce::String (e.symbol) + " " + e.voicing + " " + e.name + " " + e.category).replaceCharacter ('\x07', 'B');
    }
}

ChordsPage::ChordsPage (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    slots.getNumRows = [] { return kChordSlots - 1; };
    slots.getRowText = [this] (int r) { return slotRowText (r); };
    slots.isRowDim = [this] (int r) { return table()[(size_t) r + 1].empty(); };
    slots.onClick = [this] (int r, const juce::MouseEvent& e)
    {
        selectSlot (r + 1); previewFromDb = false; grabKeyboardFocus();
        if (e.mods.isPopupMenu()) slotMenu (r + 1);
    };
    slots.onDoubleClick = [this] (int) { previewFromDb = false; preview(); };
    addAndMakeVisible (slots);

    db.getNumRows = [this] { return (int) shown.size(); };
    db.getRowText = [this] (int r) { return databaseRowText (r); };
    db.onClick = [this] (int r, const juce::MouseEvent&) { selectDatabaseRow (r); previewFromDb = true; grabKeyboardFocus(); };
    db.onDoubleClick = [this] (int r) { selectDatabaseRow (r); setSlotFromDatabase(); };
    addAndMakeVisible (db);

    songBtn.onClick = [this] { setEditingGlobal (false); };
    globalBtn.onClick = [this] { setEditingGlobal (true); };
    addAndMakeVisible (songBtn);
    addAndMakeVisible (globalBtn);
    for (int c = -1; c < (int) chordCategories().size(); ++c)
    {
        auto b = std::make_unique<PixButton> (c < 0 ? juce::String ("ALL") : juce::String (kCatShort[c]), true);
        b->onClick = [this, c] { setCategory (c); };
        addAndMakeVisible (*b);
        catBtns.push_back (std::move (b));
    }
    setBtn.onClick = [this] { setSlotFromDatabase(); };
    playBtn.onClick = [this] { preview(); };
    clearBtn.onClick = [this] { clearSlot(); };
    saveGlobalBtn.onClick = [this] { saveAsGlobal(); };
    useGlobalBtn.onClick = [this] { useGlobal(); };
    convertBtn.onClick = [this] { convertLegacy(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &setBtn, &playBtn, &clearBtn, &saveGlobalBtn, &useGlobalBtn, &convertBtn, &closeBtn })
        addAndMakeVisible (*b);
    addChildComponent (ask);
    refilter (false);
    slots.selected = 0;
    refreshButtons();
}

ChordsPage::~ChordsPage() { stopPreview(); }

const ChordTable& ChordsPage::table() const { return global ? proc.globalChords : proc.song.chords; }

void ChordsPage::editTable (const juce::String& label, std::function<void (ChordTable&)> fn)
{
    if (global)
    {
        fn (proc.globalChords);
        saveGlobalChords (proc.globalChords);
    }
    else
    {
        proc.history.settings (label);
        ChordTable t = proc.song.chords;
        fn (t);
        proc.withLock ([&] { std::swap (proc.song.chords, t); });   // (the old table is freed here, outside the lock)
        if (onChanged) onChanged();
    }
    repaint();
    slots.repaint();
}

void ChordsPage::opened (int slot)
{
    if (slot > 0) { setEditingGlobal (false); selectSlot (slot); }
    refreshButtons();
    grabKeyboardFocus();
    repaint();
    status (global ? "THE GLOBAL SET: NEW SONGS START WITH IT"
                   : "CLICK A SLOT, THEN DOUBLE-CLICK A CHORD");
}

// ---------------------------------------------------------------- lists
juce::String ChordsPage::slotRowText (int row) const
{
    // "03*MIN7 INV 1      +3 +7 +10 +12"  (* = not the same as the other table)
    const int slot = row + 1;
    const auto& d = table()[(size_t) slot];
    const auto& other = global ? proc.song.chords : proc.globalChords;
    const bool differs = ! d.sameNotes (other[(size_t) slot]);
    juce::String s = hex2 (slot) + (differs ? "*" : " ");
    if (d.empty()) return s + "---";
    return s + juce::String (d.name).paddedRight (' ', 11) + " " + juce::String (notesText (d));
}

juce::String ChordsPage::databaseRowText (int row) const
{
    if (row < 0 || row >= (int) shown.size()) return {};
    int n = 0;
    const ChordEntry& e = chordDatabase (n)[shown[(size_t) row]];
    // (the full name is on the DATABASE line below)
    return juce::String (e.symbol).paddedRight (' ', 11) + juce::String (e.voicing).paddedRight (' ', 7) + juce::String (notesText (chordFrom (e)));
}

void ChordsPage::refilter (bool keepEntry)
{
    int n = 0;
    const ChordEntry* all = chordDatabase (n);
    const int before = keepEntry && db.selected >= 0 && db.selected < (int) shown.size() ? shown[(size_t) db.selected] : -1;
    shown.clear();
    const auto cats = chordCategories();
    juce::StringArray words;
    words.addTokens (filter.toUpperCase(), " ", "");
    words.removeEmptyStrings();
    for (int i = 0; i < n; ++i)
    {
        if (category >= 0 && cats[(size_t) category] != all[i].category) continue;
        const auto text = searchText (all[i]);
        const auto sym = juce::String (all[i].symbol).replaceCharacter ('\x07', 'B');
        bool ok = true;
        for (int w = 0; w < words.size() && ok; ++w)
            ok = w == 0 ? (sym.startsWith (words[w]) || text.contains (" " + words[w]) || text.startsWith (words[w])) : text.contains (words[w]);
        if (ok) shown.push_back (i);
    }
    db.selected = 0;
    for (int r = 0; r < (int) shown.size(); ++r) if (shown[(size_t) r] == before) db.selected = r;
    db.ensureVisible (db.selected);
    db.repaint();
    repaint (searchArea);
    repaint (infoArea);
}

void ChordsPage::selectSlot (int slot)
{
    slots.selected = juce::jlimit (1, kChordSlots - 1, slot) - 1;
    slots.ensureVisible (slots.selected);
    slots.repaint();
    repaint (infoArea);
}

void ChordsPage::selectDatabaseRow (int row)
{
    if (shown.empty()) return;
    db.selected = juce::jlimit (0, (int) shown.size() - 1, row);
    db.ensureVisible (db.selected);
    db.repaint();
    repaint (infoArea);
}

bool ChordsPage::selectDatabaseChord (const juce::String& symbol, const juce::String& voicing)
{
    const int i = findChord (symbol.toStdString(), voicing.toStdString());
    if (i < 0) return false;
    if (std::find (shown.begin(), shown.end(), i) == shown.end()) { category = -1; filter = {}; refilter (false); refreshButtons(); }
    for (int r = 0; r < (int) shown.size(); ++r) if (shown[(size_t) r] == i) { selectDatabaseRow (r); previewFromDb = true; return true; }
    return false;
}

void ChordsPage::setCategory (int c)
{
    category = c;
    refilter();
    refreshButtons();
}

void ChordsPage::setSearch (const juce::String& s)
{
    filter = s.toUpperCase();
    refilter();
}

void ChordsPage::setEditingGlobal (bool g)
{
    global = g;
    refreshButtons();
    slots.repaint();
    repaint();
    status (global ? "EDITING THE GLOBAL SET (THIS SONG KEEPS ITS OWN)"
                   : "EDITING THIS SONG'S CHORDS (SAVED IN THE SONG)");
}

void ChordsPage::refreshButtons()
{
    songBtn.setToggleState (! global, juce::dontSendNotification);
    globalBtn.setToggleState (global, juce::dontSendNotification);
    for (int i = 0; i < (int) catBtns.size(); ++i) catBtns[(size_t) i]->setToggleState (i - 1 == category, juce::dontSendNotification);
    convertBtn.setVisible (proc.song.chordsLegacy && ! global);
    saveGlobalBtn.setEnabled (! global);
    useGlobalBtn.setEnabled (! global);
}

// ---------------------------------------------------------------- actions
void ChordsPage::setSlotFromDatabase()
{
    if (db.selected < 0 || db.selected >= (int) shown.size()) return;
    int n = 0;
    const ChordEntry& e = chordDatabase (n)[shown[(size_t) db.selected]];
    const int slot = selectedSlot();
    const ChordDef d = chordFrom (e);
    editTable ("CHORD " + hex2 (slot), [&] (ChordTable& t) { t[(size_t) slot] = d; });
    previewFromDb = false;
    status ((global ? "GLOBAL " : "SLOT ") + hex2 (slot) + " = " + juce::String (d.name) + "  " + notesText (d)
            );
}

void ChordsPage::clearSlot()
{
    const int slot = selectedSlot();
    if (table()[(size_t) slot].empty()) return;
    editTable ("CLEAR CHORD " + hex2 (slot), [slot] (ChordTable& t) { t[(size_t) slot] = ChordDef {}; });
    status ("SLOT " + hex2 (slot) + " CLEARED");
}

void ChordsPage::saveAsGlobal (bool confirmed)
{
    if (! confirmed)
    {
        juce::Component::SafePointer<ChordsPage> safe (this);
        ask.show (*this, "MAKE THIS SONG'S CHORDS THE GLOBAL SET?", { "YES - NEW SONGS START WITH THEM" }, [safe] (int a)
        {
            if (safe != nullptr && a == 0) safe->saveAsGlobal (true);
        });
        return;
    }
    proc.globalChords = proc.song.chords;
    saveGlobalChords (proc.globalChords);
    slots.repaint();
    repaint();
    status ("SAVED AS THE GLOBAL SET");
}

void ChordsPage::useGlobal()
{
    global = false;
    const auto diff = differingSlots (proc.song.chords, proc.globalChords);
    if (diff.empty() && ! proc.song.chordsLegacy) { status ("THIS SONG ALREADY HAS THE GLOBAL SET"); return; }
    proc.history.settings ("USE GLOBAL CHORDS");
    ChordTable t = proc.globalChords;
    proc.withLock ([&] { std::swap (proc.song.chords, t); proc.song.chordsLegacy = false; });
    if (onChanged) onChanged();
    refreshButtons();
    slots.repaint();
    repaint();
    status ("THIS SONG NOW HAS THE GLOBAL SET (" + juce::String ((int) diff.size()) + " SLOTS CHANGED)");
}

void ChordsPage::convertLegacy()
{
    if (! proc.song.chordsLegacy) return;
    Song copy;
    proc.withLock ([&] { copy.blocks = proc.song.blocks; copy.sequence = proc.song.sequence; });
    copy.instruments = proc.song.instruments;
    copy.chords = proc.song.chords;
    copy.chordsLegacy = true;
    const int added = convertLegacyChords (copy);
    if (added < 0) { status ("NO ROOM: CLEAR SOME CHORD SLOTS FIRST"); return; }
    proc.history.structure ("CONVERT OLD CHORDS");   // (the patterns and the table, one undo step)
    proc.withLock ([&]
    {
        std::swap (proc.song.blocks, copy.blocks);
        std::swap (proc.song.chords, copy.chords);
        proc.song.chordsLegacy = false;
    });
    if (onChanged) onChanged();
    refreshButtons();
    slots.repaint();
    repaint();
    status ("CONVERTED: " + juce::String (added) + (added == 1 ? " NEW SLOT" : " NEW SLOTS") + " - SOUNDS THE SAME");
}

void ChordsPage::preview()
{
    stopPreview();
    ChordDef d;
    if (previewFromDb && db.selected >= 0 && db.selected < (int) shown.size())
    {
        int n = 0;
        d = chordFrom (chordDatabase (n)[shown[(size_t) db.selected]]);
    }
    else d = table()[(size_t) selectedSlot()];
    if (d.empty()) { status ("AN EMPTY SLOT: NOTHING TO PLAY"); return; }
    const int ins = proc.editorState.instrument;
    for (int i = 0; i < d.count; ++i)
    {
        const int note = 37 + d.notes[(size_t) i];   // on C-3
        if (note >= 1 && note <= kNoteMax) previewNotes.push_back (note);
    }
    proc.withLock ([&] { for (int nt : previewNotes) proc.player.jamNoteOn (nt, ins); });
    startTimer (1400);
    status ("PLAYING " + juce::String (d.name) + " ON C-3 (INSTRUMENT " + hex2 (ins + 1) + ")");
}

namespace { ChordDef chordClip; bool haveChordClip = false; }

void ChordsPage::slotMenu (int slot)
{
    juce::Component::SafePointer<ChordsPage> safe (this);
    const juce::String hx = juce::String::toHexString (slot).toUpperCase().paddedLeft ('0', 2);
    showMenu ((global ? "GLOBAL SLOT " : "SLOT ") + hx, {
        { "COPY", [safe, slot, hx] { if (safe == nullptr) return; chordClip = safe->table()[(size_t) slot]; haveChordClip = true; safe->status ("SLOT " + hx + " COPIED"); } },
        { "PASTE", [safe, slot, hx]
            {
                if (safe == nullptr) return;
                if (! haveChordClip) { safe->status ("NOTHING COPIED YET"); return; }
                safe->editTable ("PASTE CHORD " + hx, [slot] (ChordTable& t) { t[(size_t) slot] = chordClip; });
                safe->status ("PASTED INTO SLOT " + hx);
            } },
        { "CLEAR", [safe] { if (safe != nullptr) safe->clearSlot(); } },
        { "PLAY", [safe] { if (safe != nullptr) { safe->previewFromDb = false; safe->preview(); } } },
    });
}

void ChordsPage::stopPreview()
{
    stopTimer();
    if (previewNotes.empty()) return;
    proc.withLock ([&] { for (int nt : previewNotes) proc.player.jamNoteOff (nt); });
    previewNotes.clear();
}

void ChordsPage::timerCallback() { stopPreview(); }

juce::String ChordsPage::compareText() const
{
    if (proc.song.chordsLegacy) return "AN OLD SONG: ITS 0XY CHORDS ARE TWO INTERVALS - CONVERT OLD 0XY";
    const auto diff = differingSlots (proc.song.chords, proc.globalChords);
    if (diff.empty()) return "THIS SONG'S CHORDS ARE THE SAME AS THE GLOBAL SET";
    return "* " + juce::String ((int) diff.size()) + (diff.size() == 1 ? " SLOT DIFFERS" : " SLOTS DIFFER") + " FROM THE GLOBAL SET ("
           + slotList (diff, 5) + ") - THE SONG PLAYS ITS OWN";
}

// ---------------------------------------------------------------- drawing
void ChordsPage::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    g.fillAll (Theme::panel);
    PixelFont::drawLeft (g, "CHORDS", titleArea.toFloat(), px, Theme::ink);
    PixelFont::drawLeft (g, global ? "GLOBAL SET (NEW SONGS)" : "THIS SONG'S CHORDS", slotHead.toFloat(), px, Theme::ink);

    // search box
    g.setColour (Theme::paper);
    g.fillRect (searchArea);
    Theme::bevel (g, searchArea.toFloat(), false, std::max (1.0f, px * 0.5f));
    const juce::String s = filter.isEmpty() ? juce::String ("TYPE TO SEARCH (MIN7, SUS4...)")
                                            : "SEARCH: " + filter + "_   " + juce::String ((int) shown.size()) + " FOUND";
    PixelFont::drawLeft (g, s, searchArea.toFloat().reduced (px * 3, 0), px, filter.isEmpty() ? Theme::faint : Theme::ink);

    // what's selected
    int n = 0;
    const ChordEntry* all = chordDatabase (n);
    const int slot = selectedSlot();
    const auto& d = table()[(size_t) slot];
    juce::String line1 = (global ? "GLOBAL " : "SLOT ") + hex2 (slot) + ": " + (d.empty() ? juce::String ("EMPTY") : juce::String (d.name) + "  " + notesText (d))
                       + (global ? juce::String() : "   (#" + hex2 (slot) + " PLAYS IT)");
    juce::String line2;
    if (db.selected >= 0 && db.selected < (int) shown.size())
    {
        const auto& e = all[shown[(size_t) db.selected]];
        line2 = juce::String (e.name) + ", " + e.voicing + ": " + notesText (chordFrom (e));
    }
    auto ia = infoArea.toFloat();
    const float rh = px * 11;
    const int fit = (int) (ia.getWidth() / (px * PixelFont::kCellW));
    auto clip = [fit] (juce::String t) { return t.length() > fit ? t.substring (0, std::max (0, fit - 2)) + ".." : t; };
    PixelFont::drawLeft (g, clip (line1), ia.removeFromTop (rh), px, Theme::ink);
    PixelFont::drawLeft (g, clip ("DATABASE: " + line2), ia.removeFromTop (rh), px, Theme::ink);
    const int cfit = (int) (compareArea.getWidth() / (px * PixelFont::kCellW));
    juce::String cmp = compareText();
    if (cmp.length() > cfit) cmp = cmp.substring (0, std::max (0, cfit - 2)) + "..";
    PixelFont::drawLeft (g, cmp, compareArea.toFloat(), px, proc.song.chordsLegacy ? Theme::ink : Theme::faint);
}

void ChordsPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11), gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int c) { return (int) (px * 6 * c + px * 4); };
    auto r = getLocalBounds().reduced (gap);

    // top: title, which table, close
    auto head = r.removeFromTop (rowH);
    closeBtn.setBounds (head.removeFromRight (charsW (5))); head.removeFromRight (gap * 3);
    globalBtn.setBounds (head.removeFromRight (charsW (10))); head.removeFromRight (gap);
    songBtn.setBounds (head.removeFromRight (charsW (9)));
    titleArea = head;
    r.removeFromTop (gap);

    // bottom: two info lines, the comparison, the buttons
    auto buttons = r.removeFromBottom (rowH);
    r.removeFromBottom (gap);
    compareArea = r.removeFromBottom (rowH);
    infoArea = r.removeFromBottom (rowH * 2);
    r.removeFromBottom (gap);
    {
        auto b = buttons;
        auto put = [&] (PixButton& btn, int chars) { if (b.getWidth() >= charsW (chars)) { btn.setBounds (b.removeFromLeft (charsW (chars))); b.removeFromLeft (gap); } else btn.setBounds ({}); };
        put (setBtn, 8); put (playBtn, 4); put (clearBtn, 10);
        b.removeFromLeft (gap * 2);
        put (saveGlobalBtn, 18); put (useGlobalBtn, 14);
        b.removeFromLeft (gap * 2);
        put (convertBtn, 15);
    }

    // left: the slots; right: the database
    auto left = r.removeFromLeft (std::min (charsW (44), r.getWidth() / 2));
    r.removeFromLeft (gap * 2);
    slotHead = left.removeFromTop (rowH);
    left.removeFromTop (gap);
    slots.setBounds (left);

    // category buttons (a second row when they don't fit)
    int need = 0;
    for (auto& b : catBtns) need += charsW (b->getButtonText().length()) + gap;
    const bool twoRows = need > r.getWidth();
    auto cats = r.removeFromTop (rowH);
    r.removeFromTop (gap);
    auto cats2 = twoRows ? r.removeFromTop (rowH) : juce::Rectangle<int>();
    if (twoRows) r.removeFromTop (gap);
    for (int i = 0; i < (int) catBtns.size(); ++i)
    {
        auto& b = *catBtns[(size_t) i];
        auto& row = (twoRows && i >= 4) ? cats2 : cats;
        b.setBounds (row.removeFromLeft (charsW (b.getButtonText().length())));
        row.removeFromLeft (gap);
    }
    searchArea = r.removeFromTop (rowH);
    r.removeFromTop (gap);
    db.setBounds (r);
}

bool ChordsPage::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    const auto m = k.getModifiers();
    if (m.isCommandDown()) return false;   // (window shortcuts: undo, save...)
    if (code == juce::KeyPress::escapeKey)
    {
        if (filter.isNotEmpty()) setSearch ({});
        else if (onClose) onClose();
        return true;
    }
    if (code == juce::KeyPress::returnKey) { setSlotFromDatabase(); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
    {
        const int step = (code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : code == juce::KeyPress::pageUpKey ? -10 : 10);
        // shift: the slots; else the database
        if (m.isShiftDown()) { selectSlot (selectedSlot() + step); previewFromDb = false; }
        else { selectDatabaseRow (db.selected + step); previewFromDb = true; }
        return true;
    }
    if (code == juce::KeyPress::backspaceKey)
    {
        if (filter.isNotEmpty()) setSearch (filter.dropLastCharacters (1));
        return true;
    }
    if (code == juce::KeyPress::deleteKey) { clearSlot(); return true; }
    if (code == juce::KeyPress::spaceKey && filter.isEmpty()) { preview(); return true; }
    const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '#' || ch == ' ' || ch == '+')
    {
        setSearch (filter + juce::String::charToString (ch));
        return true;
    }
    return false;
}
} // namespace prv::ui
