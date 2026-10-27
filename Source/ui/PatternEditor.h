// Paula's Revenge - the pattern (block) editor
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class PatternEditor : public juce::Component
{
public:
    explicit PatternEditor (PaulaProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

    // called ~30x a second by the main editor
    void tick();

    // actions also reachable from buttons
    void undo();
    void redo();
    void copyTrack (bool cut);
    void pasteTrack();
    void copyBlock (bool cut);
    void pasteBlock();
    void transposeTrack (int semis);
    void insertLine();
    void deleteLine();
    void togglePlayStop (bool song);
    void stopPlayback();

    // notifies the main editor that cursor/block/edit/octave etc. changed
    std::function<void()> onStateChanged;

    void pushUndo (int blockIndex);

private:
    PaulaProcessor& proc;
    PaulaProcessor::EditorState& st;

    struct UndoEntry { int block; prv::Block data; };
    std::vector<UndoEntry> undoStack, redoStack;

    std::vector<prv::Cell> trackClip;
    int trackClipLines = 0;
    std::unique_ptr<prv::Block> blockClip;

    std::map<int, int> heldKeys;   // key code -> jam note
    float vu[prv::kMaxTracks] {};
    int lastDrawnLine = -1, lastDrawnBlock = -1;

    // geometry
    int visibleTracks() const;
    int firstTrack() const;
    float fontPx() const;
    float rowH() const;
    float headerH() const;
    float trackW() const;
    float lineNumW() const;
    float colX (int trackSlot, int column) const;
    int   colChars (int column) const;

    int  noteForKey (const juce::KeyPress&) const;
    void moveLine (int delta);
    void moveColumn (int delta);
    void moveTrack (int delta);
    void advance();
    void enterNote (int note);
    void enterHex (int value);
    void clearField();
    void changed();
    int  blockLines() const;
};
} // namespace prv::ui
