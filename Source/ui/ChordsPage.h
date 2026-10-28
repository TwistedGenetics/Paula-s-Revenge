// Paula's Revenge - CHORDS: the song's chord table (0xy on a CHORD instrument's note plays slot xy),
// the global set every new song starts with, and the chord database to fill them from
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "Panels.h"
#include "ChoiceBox.h"
#include "../PluginProcessor.h"
#include <map>
#include <set>

namespace prv::ui
{
class ChordsPage : public juce::Component, private juce::Timer
{
public:
    explicit ChordsPage (PaulaProcessor&);
    ~ChordsPage() override;

    std::function<void()> onClose;
    std::function<void()> onStopSong;   // Space while the song plays: stop it
    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onChanged;            // the song's table changed (the pattern shows names)

    void opened (int slot = -1);                // slot: the one to select (-1 = as before)
    void refresh() { refreshButtons(); slots.repaint(); repaint(); }   // (another song, undo...)
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override { if (! isVisible()) { stopPreview(); stopRecording(); closeNoteEditor (true); } }
    bool keyStateChanged (bool) override;

    // actions (buttons and keys; tests call them too)
    void setEditingGlobal (bool global);
    bool editingGlobal() const { return global; }
    void selectSlot (int slot);
    int  selectedSlot() const { return slots.selected + 1; }
    void setCategory (int c);                   // -1 = all, else chordCategories()[c]
    void setSearch (const juce::String&);
    juce::String search() const { return filter; }
    int  databaseRows() const { return (int) shown.size(); }
    juce::String databaseRowText (int row) const;
    void selectDatabaseRow (int row);
    int  selectedDatabaseRow() const { return db.selected; }
    bool selectDatabaseChord (const juce::String& symbol, const juce::String& voicing);
    void setSlotFromDatabase();                 // SET SLOT: the database's chord into the selected slot
    void clearSlot();                           // the picked slots (or the selected one)
    void saveAsGlobal (bool confirmed = false); // the song's table becomes the global set
    void useGlobal();                           // the song gets the global set
    void convertLegacy();                       // a v0.11 song's 0xy intervals into slots
    void preview();                             // PLAY: the slot (or database chord) on the current instrument
    void slotMenu (int slot);                   // right-click a slot: COPY, PASTE, CLEAR, COLOUR, PLAY, RECORD, NOTE
    // picking several slots (click-drag, Shift+click, Cmd/Ctrl+click) - for COLOUR, COPY, CLEAR
    void pickSlots (int from, int to);          // a range (the selected slot stays `from`)
    void togglePicked (int slot);
    std::vector<int> pickedSlots() const;       // in order; at least the selected slot
    void setColour (int colour);                // 0 = none, 1-8: the picked slots
    void colourMenu();                          // COLOUR: the 8 colours + NONE
    // AUDITION: clicking a chord plays it with the current instrument, on the last key played
    static bool auditionOn();
    static void setAuditionOn (bool);
    void audition (const ChordDef&, bool held);  // held: until the mouse is let go; else briefly
    int  auditionRoot() const;                   // the last key played (MIDI or laptop), else C of the octave
    juce::String slotRowText (int row) const;
    // RECORD CHORD: on until it's switched off - each chord played goes into the selected slot,
    // then the selection moves on to the next empty slot
    void startRecording();
    void stopRecording();
    void cancelRecording() { stopRecording(); }
    bool isRecording() const { return recording; }
    int  recordedCount() const { return recordedThisTime; }
    void inputNote (int note, bool on);         // a key down / up while recording (MIDI notes come here too)
    // the slot's own note ("VERSE PROG 2")
    void setSlotNote (int slot, const juce::String& text);
    void askSlotNote (int slot);                // double-click the NOTE column: type it there
    bool isEditingNote() const { return noteEditor != nullptr; }
    juce::TextEditor* noteEditorBox() { return noteEditor.get(); }
    void closeNoteEditor (bool keep);
    static constexpr int kNameChars = 12;       // the slot list's NAME column
    static constexpr int kNoteColumn = 17;      // where its NOTE column starts (characters)
    juce::String compareText() const;           // how the song's table compares with the global set
    ChoiceBox& question() { return ask; }

private:
    PaulaProcessor& proc;
    PixList slots, db;
    PixButton songBtn { "THIS SONG", true }, globalBtn { "GLOBAL SET", true };
    std::vector<std::unique_ptr<PixButton>> catBtns;
    PixButton setBtn { "SET SLOT" }, playBtn { "PLAY" }, clearBtn { "CLEAR SLOT" },
              saveGlobalBtn { "SAVE AS GLOBAL SET" }, useGlobalBtn { "USE GLOBAL SET" },
              convertBtn { "CONVERT OLD 0XY" }, closeBtn { "CLOSE" },
              recordBtn { "RECORD CHORD", true }, colourBtn { "COLOUR" }, auditionBtn { "AUDITION", true };
    ChoiceBox ask;
    bool global = false;
    int  category = -1;
    juce::String filter;
    std::vector<int> shown;                     // database entries after the category and search
    juce::Rectangle<int> titleArea, slotHead, colHead, searchArea, infoArea, compareArea;
    std::set<int> picked;                       // slots picked together (besides the selected one)
    int pickAnchor = 1;
    std::unique_ptr<juce::TextEditor> noteEditor;
    int noteEditorSlot = 0;
    int recordedThisTime = 0;
    bool heldAudition = false;
    std::vector<int> previewNotes;
    bool recording = false;
    std::vector<int> recNotes;                  // every note pressed in this take
    std::set<int> recHeld;                      // still held
    std::map<int, int> heldKeys;                // laptop key -> note (while recording)
    bool previewFromDb = true;                  // PLAY plays what was clicked last

    const ChordTable& table() const;
    void editTable (const juce::String& label, std::function<void (ChordTable&)> fn);
    void refilter (bool keepEntry = true);
    void refreshButtons();
    void stopPreview();
    void timerCallback() override;
    void status (const juce::String& m) { if (onStatus) onStatus (m); }
};
} // namespace prv::ui
