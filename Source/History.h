// Paula's Revenge - one undo/redo history for every page
//
// Before a change, a page records the part of the song it is about to touch
// (one block, the block list + sequence, one instrument, or the song settings).
// Undo swaps that saved copy back in and keeps the current one for redo.
#pragma once
#include <array>

#include <JuceHeader.h>
#include "engine/Song.h"

class PaulaProcessor;

class History
{
public:
    explicit History (PaulaProcessor& p) : proc (p) {}

    // Record the current state before changing it. `merge` folds repeated
    // changes with the same label made within a second (dragging a slider,
    // typing in a number box) into one step.
    void block (int blockIndex, const juce::String& label, bool merge = false);
    void structure (const juce::String& label);
    void instrument (int index, const juce::String& label, bool merge = false);
    void settings (const juce::String& label, bool merge = false);
    void mixer (const juce::String& label, bool merge = false);

    void relabelLast (const juce::String& label);   // after an edit whose name is only known later
    void dropLast();                                  // the change didn't happen after all

    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    juce::String nextUndoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().label; }
    juce::String nextRedoLabel() const { return redoStack.empty() ? juce::String() : redoStack.back().label; }
    int  size() const { return (int) undoStack.size(); }

    void clear();                       // new or loaded song
    bool isDirty() const { return current != saved; }
    void markSaved() { saved = current; }
    void markChanged() { current = nextId++; }   // a change that isn't undoable

    // called after undo/redo with "UNDO: <what>" / "REDO: <what>"
    std::function<void (const juce::String&)> onRestored;

    static constexpr int kMaxSteps = 200;
    static constexpr size_t kMaxBytes = (size_t) 384 << 20;

private:
    enum class Kind { Block, Structure, Instrument, Settings, Mixer };
    struct Settings
    {
        std::string name;
        int numTracks = 8, tempo = 125, speed = 6;
        bool led = false;
        int trackPan[prv::kMaxTracks] {};
        prv::ChordTable chords; bool chordsLegacy = false;   // (the song's chord table)
    };
    struct Structure { std::vector<prv::Block> blocks; std::vector<int> sequence; std::vector<prv::SongSection> sections; prv::ChordTable chords; bool chordsLegacy = false; };
    struct Entry
    {
        Kind kind = Kind::Block;
        int index = 0;
        juce::String label;
        double time = 0;
        uint64_t state = 0;
        int seqPos = 0, blockSel = 0, instrument = 0;   // where the user was
        std::shared_ptr<prv::Block> block;
        std::shared_ptr<Structure> structure;
        std::shared_ptr<prv::Instrument> inst;
        std::shared_ptr<std::array<prv::FxSlot, prv::kInserts>> instFx;   // (the instrument's own inserts go with it)
        std::shared_ptr<Settings> settings;
        std::shared_ptr<prv::MixerSettings> mix;
        size_t bytes = 0;
    };

    PaulaProcessor& proc;
    std::vector<Entry> undoStack, redoStack;
    uint64_t current = 0, saved = 0, nextId = 1;

    Entry capture (Kind, int index) const;
    void apply (Entry&);
    void record (Kind, int index, const juce::String& label, bool merge);
    void trim();
    bool step (std::vector<Entry>& from, std::vector<Entry>& to, const char* verb);
};
