// Paula's Revenge - CHORDS: the song's chord table (0xy on a CHORD instrument's note plays slot xy),
// the global set every new song starts with, and the chord database to fill them from
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "Panels.h"
#include "ChoiceBox.h"
#include "../PluginProcessor.h"

namespace prv::ui
{
class ChordsPage : public juce::Component, private juce::Timer
{
public:
    explicit ChordsPage (PaulaProcessor&);
    ~ChordsPage() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onChanged;            // the song's table changed (the pattern shows names)

    void opened (int slot = -1);                // slot: the one to select (-1 = as before)
    void refresh() { refreshButtons(); slots.repaint(); repaint(); }   // (another song, undo...)
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override { if (! isVisible()) stopPreview(); }

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
    void clearSlot();
    void saveAsGlobal (bool confirmed = false); // the song's table becomes the global set
    void useGlobal();                           // the song gets the global set
    void convertLegacy();                       // a v0.11 song's 0xy intervals into slots
    void preview();                             // PLAY: the slot (or database chord) on the current instrument
    void slotMenu (int slot);                   // right-click a slot: COPY, PASTE, CLEAR, PLAY
    juce::String slotRowText (int row) const;
    juce::String compareText() const;           // how the song's table compares with the global set
    ChoiceBox& question() { return ask; }

private:
    PaulaProcessor& proc;
    PixList slots, db;
    PixButton songBtn { "THIS SONG", true }, globalBtn { "GLOBAL SET", true };
    std::vector<std::unique_ptr<PixButton>> catBtns;
    PixButton setBtn { "SET SLOT" }, playBtn { "PLAY" }, clearBtn { "CLEAR SLOT" },
              saveGlobalBtn { "SAVE AS GLOBAL SET" }, useGlobalBtn { "USE GLOBAL SET" },
              convertBtn { "CONVERT OLD 0XY" }, closeBtn { "CLOSE" };
    ChoiceBox ask;
    bool global = false;
    int  category = -1;
    juce::String filter;
    std::vector<int> shown;                     // database entries after the category and search
    juce::Rectangle<int> titleArea, slotHead, searchArea, infoArea, compareArea;
    std::vector<int> previewNotes;
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
