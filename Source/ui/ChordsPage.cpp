// Paula's Revenge - CHORDS page
#include "ChordsPage.h"
#include "ChoiceBox.h"
#include "../engine/SongOps.h"
#include "../engine/ChordDetect.h"
#include "ChordColours.h"
#include "../Prefs.h"

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
    slots.getRowStripe = [this] (int r) { return chordColours::colour (table()[(size_t) r + 1].colour); };
    // picking several: drag down the list, Shift+click, Cmd/Ctrl+click
    slots.isRowInRange = [this] (int r) { return picked.count (r + 1) > 0; };
    slots.dragMoves = [] (int) { return false; };
    slots.onDragSelect = [this] (int r) { pickSlots (pickAnchor, r + 1); };
    slots.onClick = [this] (int r, const juce::MouseEvent& e)
    {
        closeNoteEditor (true);
        const int slot = r + 1;
        grabKeyboardFocus();
        previewFromDb = false;
        if (e.mods.isPopupMenu())
        {
            if (picked.count (slot) == 0 && slot != selectedSlot()) { picked.clear(); selectSlot (slot); pickAnchor = slot; }
            slotMenu (slot);
            return;
        }
        if (e.mods.isShiftDown()) { pickSlots (pickAnchor, slot); return; }
        if (e.mods.isCommandDown()) { togglePicked (slot); return; }
        picked.clear();
        pickAnchor = slot;
        selectSlot (slot);
        if (auditionOn() && ! recording && ! table()[(size_t) slot].empty()) audition (table()[(size_t) slot], true);
    };
    slots.onRelease = [this] { if (heldAudition) stopPreview(); };
    slots.onDoubleClickAt = [this] (int r, float x)
    {
        // the NOTE column: type the note there; elsewhere: play the chord
        const float noteX = slots.textX() + (kNoteColumn - 1) * Theme::px * PixelFont::kCellW;
        if (x >= noteX) askSlotNote (r + 1);
        else { previewFromDb = false; preview(); }
    };
    addAndMakeVisible (slots);

    db.getNumRows = [this] { return (int) shown.size(); };
    db.getRowText = [this] (int r) { return databaseRowText (r); };
    db.onClick = [this] (int r, const juce::MouseEvent&)
    {
        closeNoteEditor (true);
        selectDatabaseRow (r); previewFromDb = true; grabKeyboardFocus();
        if (auditionOn() && ! recording && r >= 0 && r < (int) shown.size())
        {
            int n = 0;
            audition (chordFrom (chordDatabase (n)[shown[(size_t) r]]), true);
        }
    };
    db.onRelease = [this] { if (heldAudition) stopPreview(); };
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
    recordBtn.onClick = [this] { if (recording) stopRecording(); else startRecording(); };
    colourBtn.onClick = [this] { colourMenu(); };
    auditionBtn.setToggleState (auditionOn(), juce::dontSendNotification);
    auditionBtn.onClick = [this]
    {
        setAuditionOn (auditionBtn.getToggleState());
        if (! auditionOn()) stopPreview();
        status (auditionOn() ? "AUDITION ON: CLICKING A CHORD PLAYS IT (ON THE LAST KEY YOU PLAYED)" : "AUDITION OFF");
    };
    for (auto* b : { &setBtn, &playBtn, &clearBtn, &saveGlobalBtn, &useGlobalBtn, &convertBtn, &closeBtn, &recordBtn, &colourBtn, &auditionBtn })
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
    // "40* MIN7 DROP 2  VERSE PROG 1": fixed columns (SLOT, NAME, NOTE); * = not the same as the
    // other table. The intervals are on the line under the lists.
    const int slot = row + 1;
    const auto& d = table()[(size_t) slot];
    const auto& other = global ? proc.song.chords : proc.globalChords;
    const bool differs = ! d.sameNotes (other[(size_t) slot]);
    juce::String name = d.empty() ? juce::String ("---") : juce::String (d.name);
    if (name.length() > kNameChars) name = name.substring (0, kNameChars - 2) + "..";
    return hex2 (slot) + (differs ? "* " : "  ") + name.paddedRight (' ', kNameChars) + " " + juce::String (d.note);
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
    ChordDef d = chordFrom (e);
    d.note = table()[(size_t) slot].note;       // (the slot keeps its note and colour)
    d.colour = table()[(size_t) slot].colour;
    editTable ("CHORD " + hex2 (slot), [&] (ChordTable& t) { t[(size_t) slot] = d; });
    previewFromDb = false;
    status ((global ? "GLOBAL " : "SLOT ") + hex2 (slot) + " = " + juce::String (d.name) + "  " + notesText (d));
}

void ChordsPage::clearSlot()
{
    const auto slotsToClear = pickedSlots();
    bool any = false;
    for (int sl : slotsToClear) any = any || ! table()[(size_t) sl].empty() || ! table()[(size_t) sl].note.empty() || table()[(size_t) sl].colour != 0;
    if (! any) return;
    editTable (slotsToClear.size() == 1 ? "CLEAR CHORD " + hex2 (slotsToClear[0]) : juce::String ("CLEAR CHORDS"),
               [&] (ChordTable& t) { for (int sl : slotsToClear) t[(size_t) sl] = ChordDef {}; });
    status (slotsToClear.size() == 1 ? "SLOT " + hex2 (slotsToClear[0]) + " CLEARED" : juce::String ((int) slotsToClear.size()) + " SLOTS CLEARED");
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
    ChordDef d;
    if (previewFromDb && db.selected >= 0 && db.selected < (int) shown.size())
    {
        int n = 0;
        d = chordFrom (chordDatabase (n)[shown[(size_t) db.selected]]);
    }
    else d = table()[(size_t) selectedSlot()];
    if (d.empty()) { stopPreview(); status ("AN EMPTY SLOT: NOTHING TO PLAY"); return; }
    audition (d, false);
    status ("PLAYING " + juce::String (d.name) + " ON " + juce::String (noteName ((uint8_t) juce::jlimit (1, (int) kNoteMax, auditionRoot())))
            + " (INSTRUMENT " + hex2 (proc.editorState.instrument + 1) + ")");
}

bool ChordsPage::auditionOn()           { return prv::prefs().getBoolValue ("chordAudition", true); }
void ChordsPage::setAuditionOn (bool on) { prv::prefs().setValue ("chordAudition", on); }

int ChordsPage::auditionRoot() const
{
    // the last key played on the MIDI keyboard or the laptop keys; before any, C of the octave
    const int last = proc.lastPlayedNote.load();
    if (last >= 1 && last <= (int) kNoteMax) return last;
    return juce::jlimit (1, (int) kNoteMax, proc.editorState.octave * 12 + 1);
}

void ChordsPage::audition (const ChordDef& d, bool held)
{
    stopPreview();
    if (d.empty()) return;
    const int ins = proc.editorState.instrument;
    const int root = auditionRoot();
    for (int i = 0; i < d.count; ++i)
    {
        const int note = root + d.notes[(size_t) i];
        if (note >= 1 && note <= (int) kNoteMax) previewNotes.push_back (note);
    }
    proc.withLock ([&] { for (int nt : previewNotes) proc.player.jamNoteOn (nt, ins); });
    heldAudition = held;
    startTimer (held ? 6000 : 1400);   // (held: until the mouse is let go - this is just in case)
}

namespace { std::vector<ChordDef> chordClip; }

void ChordsPage::slotMenu (int slot)
{
    juce::Component::SafePointer<ChordsPage> safe (this);
    const auto sel = pickedSlots();
    const bool many = sel.size() > 1;
    const juce::String hx = hex2 (slot);
    const juce::String title = many ? juce::String ((int) sel.size()) + " SLOTS (" + hex2 (sel.front()) + "-" + hex2 (sel.back()) + ")"
                                    : (global ? "GLOBAL SLOT " : "SLOT ") + hx;
    showMenu (title, {
        { many ? "COPY THE SLOTS" : "COPY", [safe, sel]
            {
                if (safe == nullptr) return;
                chordClip.clear();
                for (int s2 : sel) chordClip.push_back (safe->table()[(size_t) s2]);
                safe->status (juce::String ((int) sel.size()) + (sel.size() == 1 ? " SLOT COPIED" : " SLOTS COPIED"));
            } },
        { "PASTE", [safe, slot, hx]
            {
                if (safe == nullptr) return;
                if (chordClip.empty()) { safe->status ("NOTHING COPIED YET"); return; }
                const auto clip = chordClip;
                safe->editTable ("PASTE CHORDS " + hx, [slot, clip] (ChordTable& t)
                {
                    for (size_t i = 0; i < clip.size() && slot + (int) i < kChordSlots; ++i) t[(size_t) slot + i] = clip[i];
                });
                safe->status ("PASTED " + juce::String ((int) clip.size()) + (clip.size() == 1 ? " SLOT INTO " : " SLOTS FROM ") + hx);
            } },
        { many ? "CLEAR THE SLOTS" : "CLEAR", [safe] { if (safe != nullptr) safe->clearSlot(); } },
        { "COLOUR...", [safe] { if (safe != nullptr) safe->colourMenu(); } },
        { "PLAY", [safe] { if (safe != nullptr) { safe->previewFromDb = false; safe->preview(); } } },
        { "RECORD CHORDS (PLAY THEM IN)", [safe, slot] { if (safe != nullptr) { safe->picked.clear(); safe->selectSlot (slot); safe->startRecording(); } } },
        { "NOTE...", [safe, slot] { if (safe != nullptr) safe->askSlotNote (slot); } },
    });
}

void ChordsPage::pickSlots (int from, int to)
{
    from = juce::jlimit (1, kChordSlots - 1, from);
    to = juce::jlimit (1, kChordSlots - 1, to);
    picked.clear();
    for (int sl = std::min (from, to); sl <= std::max (from, to); ++sl) picked.insert (sl);
    pickAnchor = from;
    selectSlot (from);
    slots.repaint();
    if (picked.size() > 1) status (juce::String ((int) picked.size()) + " SLOTS PICKED - RIGHT-CLICK OR COLOUR TO GIVE THEM A COLOUR");
}

void ChordsPage::togglePicked (int slot)
{
    slot = juce::jlimit (1, kChordSlots - 1, slot);
    if (picked.empty()) picked.insert (selectedSlot());
    if (picked.count (slot) > 0 && picked.size() > 1) picked.erase (slot);
    else picked.insert (slot);
    if (picked.count (selectedSlot()) == 0) selectSlot (*picked.begin());
    slots.repaint();
    repaint (infoArea);
}

std::vector<int> ChordsPage::pickedSlots() const
{
    std::set<int> all (picked);
    all.insert (selectedSlot());
    return { all.begin(), all.end() };
}

void ChordsPage::setColour (int colour)
{
    colour = juce::jlimit (0, kChordColours, colour);
    const auto sel = pickedSlots();
    editTable ("CHORD COLOUR", [&] (ChordTable& t) { for (int sl : sel) t[(size_t) sl].colour = colour; });
    status ((sel.size() == 1 ? "SLOT " + hex2 (sel[0]) : juce::String ((int) sel.size()) + " SLOTS") + ": "
            + (colour == 0 ? juce::String ("NO COLOUR") : chordColours::name (colour)));
}

void ChordsPage::colourMenu()
{
    juce::Component::SafePointer<ChordsPage> safe (this);
    const auto sel = pickedSlots();
    std::vector<MenuItem> items;
    for (int c = 1; c <= kChordColours; ++c) items.push_back ({ chordColours::name (c), [safe, c] { if (safe != nullptr) safe->setColour (c); } });
    items.push_back ({ "NONE", [safe] { if (safe != nullptr) safe->setColour (0); } });
    showMenu (sel.size() == 1 ? "COLOUR FOR SLOT " + hex2 (sel[0]) : "COLOUR FOR " + juce::String ((int) sel.size()) + " SLOTS", items);
}

// ---------------------------------------------------------------- RECORD CHORD
namespace
{
    juce::String noteList (const std::vector<int>& notes)
    {
        auto sorted = notes;
        std::sort (sorted.begin(), sorted.end());
        juce::StringArray a;
        for (int n : sorted) a.add (noteName ((uint8_t) n));
        return a.joinIntoString (" ");
    }
}

void ChordsPage::startRecording()
{
    closeNoteEditor (true);
    stopPreview();
    recording = true;
    recordedThisTime = 0;
    recNotes.clear();
    recHeld.clear();
    picked.clear();
    recordBtn.setToggleState (true, juce::dontSendNotification);
    grabKeyboardFocus();
    repaint();
    status ("RECORDING CHORDS FROM " + juce::String (global ? "GLOBAL " : "") + "SLOT " + hex2 (selectedSlot())
            + ": PLAY EACH CHORD AND LET GO - CLICK RECORD CHORD (OR ESC) TO STOP");
}

void ChordsPage::stopRecording()
{
    if (! recording) return;
    recording = false;
    for (auto& kv : heldKeys) { const int n = kv.second; proc.withLock ([&] { proc.player.jamNoteOff (n); }); }
    heldKeys.clear();
    recNotes.clear();
    recHeld.clear();
    recordBtn.setToggleState (false, juce::dontSendNotification);
    repaint();
    if (isVisible()) status ("RECORD CHORD OFF - " + juce::String (recordedThisTime) + (recordedThisTime == 1 ? " CHORD RECORDED" : " CHORDS RECORDED"));
}

void ChordsPage::inputNote (int note, bool on)
{
    if (! recording) return;
    if (on)
    {
        recHeld.insert (note);
        if (std::find (recNotes.begin(), recNotes.end(), note) == recNotes.end()) recNotes.push_back (note);
        repaint (infoArea);
        return;
    }
    recHeld.erase (note);
    if (! recHeld.empty() || recNotes.empty()) return;
    // every key let go: that's a chord (a single note isn't one - it's ignored)
    if (recNotes.size() < 2)
    {
        recNotes.clear();
        repaint (infoArea);
        status ("ONLY ONE NOTE - PLAY TWO OR MORE TOGETHER (RECORD CHORD IS STILL ON)");
        return;
    }
    const int slot = selectedSlot();
    ChordDef d = chordFromNotes (recNotes);
    d.note = table()[(size_t) slot].note;
    d.colour = table()[(size_t) slot].colour;
    const bool tooMany = recNotes.size() > (size_t) kMaxChordNotes;
    const auto played = noteList (recNotes);
    recNotes.clear();
    editTable ("RECORD CHORD " + hex2 (slot), [&] (ChordTable& t) { t[(size_t) slot] = d; });
    ++recordedThisTime;
    previewFromDb = false;
    const juce::String what = (global ? "GLOBAL " : "SLOT ") + hex2 (slot) + " = " + juce::String (d.name)
                            + "  (PLAYED " + played + (tooMany ? ": THE LOWEST 8 KEPT)" : ")");
    // on to the next empty slot, so nothing gets written over
    int next = -1;
    for (int sl = slot + 1; sl < kChordSlots && next < 0; ++sl) if (table()[(size_t) sl].empty()) next = sl;
    if (next < 0)
    {
        stopRecording();
        status (what + " - NO EMPTY SLOTS LEFT, RECORDING STOPPED");
        return;
    }
    selectSlot (next);
    status (what + " - " + juce::String (recordedThisTime) + " RECORDED, SLOT " + hex2 (next) + " NEXT");
}

bool ChordsPage::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const int n = it->second;
            proc.withLock ([&] { proc.player.jamNoteOff (n); });
            it = heldKeys.erase (it);
            inputNote (n, false);
        }
        else ++it;
    }
    return false;
}

void ChordsPage::setSlotNote (int slot, const juce::String& text)
{
    slot = juce::jlimit (1, kChordSlots - 1, slot);
    // capitals, and only what the pixel font draws
    juce::String clean;
    for (auto c : text.toUpperCase())
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || juce::String (" -+#/.,:()!?'&").containsChar (c)) clean << juce::String::charToString (c);
    clean = clean.trim().substring (0, kChordNoteLen);
    if (clean.toStdString() == table()[(size_t) slot].note) return;
    const auto n = clean.toStdString();
    editTable ("CHORD NOTE " + hex2 (slot), [slot, n] (ChordTable& t) { t[(size_t) slot].note = n; });
    status (clean.isEmpty() ? "SLOT " + hex2 (slot) + ": NOTE REMOVED" : "SLOT " + hex2 (slot) + " NOTE: " + clean);
}

void ChordsPage::askSlotNote (int slot)
{
    // a text box right on the slot's NOTE column
    closeNoteEditor (true);
    slot = juce::jlimit (1, kChordSlots - 1, slot);
    picked.clear();
    selectSlot (slot);
    noteEditorSlot = slot;
    noteEditor = std::make_unique<juce::TextEditor>();
    auto& ed = *noteEditor;
    const float px = Theme::px;
    ed.setFont (juce::Font (juce::FontOptions (px * 10.0f, juce::Font::bold)));
    ed.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    ed.setColour (juce::TextEditor::textColourId, juce::Colours::black);
    ed.setColour (juce::TextEditor::highlightColourId, juce::Colour (0xffffd24a));
    ed.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    ed.setColour (juce::TextEditor::outlineColourId, juce::Colours::black);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::black);
    ed.setColour (juce::CaretComponent::caretColourId, juce::Colours::black);
    // capitals as you type (like everything else in the app), up to kChordNoteLen
    struct Caps final : public juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor& e, const juce::String& t) override
        {
            const int room = kChordNoteLen - (e.getTotalNumChars() - e.getHighlightedRegion().getLength());
            return t.toUpperCase().substring (0, std::max (0, room));
        }
    };
    ed.setInputFilter (new Caps(), true);
    ed.setIndents (2, 0);
    ed.setText (juce::String (table()[(size_t) slot].note), false);
    ed.onReturnKey = [this] { closeNoteEditor (true); };
    ed.onEscapeKey = [this] { closeNoteEditor (false); };
    ed.onFocusLost = [this] { closeNoteEditor (true); };
    const float cw = px * PixelFont::kCellW;
    const float x = (float) slots.getX() + slots.textX() + kNoteColumn * cw - px * 2;
    const float y = (float) slots.getY() + slots.rowY (slot - 1);
    const float right = (float) slots.getRight() - px * 10;
    ed.setBounds (juce::Rectangle<float> (x, y, std::max (px * 40, right - x), slots.rowHeight()).toNearestInt());
    addAndMakeVisible (ed);
    ed.grabKeyboardFocus();
    ed.selectAll();
    status ("SLOT " + hex2 (slot) + " NOTE: TYPE IT, ENTER KEEPS IT, ESC CANCELS");
}

void ChordsPage::closeNoteEditor (bool keep)
{
    if (noteEditor == nullptr) return;
    // (deleted later: this can be called from the box's own Enter / Esc / focus callbacks)
    std::shared_ptr<juce::TextEditor> ed (noteEditor.release());
    const auto text = ed->getText();
    removeChildComponent (ed.get());
    juce::MessageManager::callAsync ([ed] {});
    if (keep) setSlotNote (noteEditorSlot, text);
    if (isShowing()) grabKeyboardFocus();
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
    {
        const juce::String head = global ? "GLOBAL SET (NEW SONGS)" : "THIS SONG'S CHORDS";
        const int fitHead = (int) (slotHead.getWidth() / (px * PixelFont::kCellW));
        PixelFont::drawLeft (g, head.length() > fitHead ? head.substring (0, fitHead) : head, slotHead.toFloat(), px, Theme::ink);
        // the list's columns, over them
        const float cw = px * PixelFont::kCellW;
        const float x0 = (float) slots.getX() + slots.textX();
        auto ch = colHead.toFloat();
        PixelFont::drawLeft (g, "#",    ch.withX (x0).withWidth (cw * 3), px, Theme::faint);
        PixelFont::drawLeft (g, "NAME", ch.withX (x0 + 4 * cw).withWidth (cw * kNameChars), px, Theme::faint);
        PixelFont::drawLeft (g, "NOTE (DOUBLE-CLICK)", ch.withX (x0 + kNoteColumn * cw).withRight ((float) slots.getRight()), px, Theme::faint);
    }

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
                       + (global ? juce::String() : "  (#" + hex2 (slot) + ")")
                       + (d.colour != 0 ? "  " + chordColours::name (d.colour) : juce::String())
                       + (d.note.empty() ? juce::String() : "  NOTE: " + juce::String (d.note));
    if (picked.size() > 1)
        line1 = juce::String ((int) pickedSlots().size()) + " SLOTS PICKED (" + hex2 (pickedSlots().front()) + "-" + hex2 (pickedSlots().back()) + ") - COLOUR, COPY OR CLEAR THEM TOGETHER";
    if (recording)
        line1 = "RECORDING INTO SLOT " + hex2 (slot) + " (" + juce::String (recordedThisTime) + " DONE): "
              + (recNotes.empty() ? juce::String ("PLAY A CHORD AND LET GO...") : noteList (recNotes));
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
    songBtn.setBounds (head.removeFromRight (charsW (9))); head.removeFromRight (gap * 3);
    auditionBtn.setBounds (head.removeFromRight (charsW (8)));
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
    colourBtn.setBounds (slotHead.removeFromRight (charsW (6))); slotHead.removeFromRight (gap);
    recordBtn.setBounds (slotHead.removeFromRight (charsW (12))); slotHead.removeFromRight (gap);
    left.removeFromTop (gap);
    colHead = left.removeFromTop (rowH);
    slots.setBounds (left);
    if (noteEditor != nullptr) closeNoteEditor (true);

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
    if (recording)
    {
        // the laptop keys play the chord; Esc cancels
        if (code == juce::KeyPress::escapeKey) { stopRecording(); return true; }
        static const char* lower = "zsxdcvgbhnjm,l.;/";
        static const char* upper = "q2w3er5t6y7ui9o0p";
        const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        int note = -1;
        for (int i = 0; lower[i]; ++i) if (c == (juce::juce_wchar) lower[i]) note = proc.editorState.octave * 12 + i + 1;
        for (int i = 0; upper[i]; ++i) if (c == (juce::juce_wchar) upper[i]) note = (proc.editorState.octave + 1) * 12 + i + 1;
        if (note > 0 && heldKeys.count (code) == 0)
        {
            note = juce::jlimit (1, (int) kNoteMax, note);
            heldKeys[code] = note;
            proc.withLock ([&] { proc.player.jamNoteOn (note, proc.editorState.instrument); });
            proc.lastPlayedNote = note;
            inputNote (note, true);
        }
        return true;
    }
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
        if (m.isShiftDown()) { picked.clear(); selectSlot (selectedSlot() + step); pickAnchor = selectedSlot(); previewFromDb = false; }
        else { selectDatabaseRow (db.selected + step); previewFromDb = true; }
        // AUDITION: a short play of each chord as you go
        if (auditionOn())
        {
            int n = 0;
            const ChordDef d = previewFromDb ? (shown.empty() ? ChordDef {} : chordFrom (chordDatabase (n)[shown[(size_t) db.selected]]))
                                             : table()[(size_t) selectedSlot()];
            if (! d.empty()) audition (d, false); else stopPreview();
        }
        return true;
    }
    if (code == juce::KeyPress::backspaceKey)
    {
        if (filter.isNotEmpty()) setSearch (filter.dropLastCharacters (1));
        return true;
    }
    if (code == juce::KeyPress::deleteKey) { clearSlot(); return true; }
    if (code == juce::KeyPress::spaceKey && filter.isEmpty() && proc.player.isPlaying()) { if (onStopSong) onStopSong(); return true; }   // (Space stops playback everywhere)
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
