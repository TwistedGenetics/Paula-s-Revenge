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
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
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
    void toggleEdit();
    void doubleBlock();
    void halveBlock();

    // marked loop (lines of the current block)
    void setLoopRange (int from, int to);   // also turns the loop on
    void setLoopOn (bool on);
    void syncLoop();                        // push the loop to the player
    std::function<void (const juce::String&)> onStatus;

    // notifies the main editor that cursor/block/edit/octave etc. changed
    std::function<void()> onStateChanged;

    void pushUndo (int blockIndex, const juce::String& what);
    void pushStructureUndo (const juce::String& what);   // before any change to block numbers / the sequence
    std::function<void (int)> onMoveSequence;   // Alt+Up/Down

private:
    PaulaProcessor& proc;
    PaulaProcessor::EditorState& st;

    int panDragTrack = -1;
    int loopDragFrom = -1;              // dragging over the line numbers
    juce::Rectangle<float> cornerRect() const;
    void askBlockNumber();
    int lineAtY (float y) const;
    juce::Rectangle<float> headerRect (int slot) const;
    juce::Rectangle<float> panRect (int slot) const;
    void setPanFromX (int track, float x);

    std::vector<prv::Cell> trackClip;
    int trackClipLines = 0;
    std::unique_ptr<prv::Block> blockClip;

    std::map<int, int> heldKeys;   // key code -> jam note
    float vu[prv::kMaxTracks] {};
    int lastDrawnLine = -1, lastDrawnBlock = -1;

    // geometry
    int visibleTracks() const;
    int trackChars() const;
    int numColumns() const;
    float helpHeight() const;
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
