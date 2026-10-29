// Paula's Revenge - side and bottom panels
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "SeqEditor.h"

namespace prv::ui
{
// Scrollable list drawn with the pixel font (sequence + instrument lists)
class PixList : public juce::Component, private juce::Timer
{
    struct Ticker : juce::Timer { std::function<void()> fn; void timerCallback() override { if (fn) fn(); } } ticker;   // (scrolling names)
public:
    std::function<int()> getNumRows;
    std::function<juce::String (int)> getRowText;
    std::function<bool (int)> isRowDim = [] (int) { return false; };
    std::function<void (int, const juce::MouseEvent&)> onClick;
    std::function<void (int)> onDoubleClick;
    std::function<void (int from, int to)> onMove;   // set to allow drag-to-reorder
    // optional range highlighting: rows drawn as highlighted; whether a drag from a row
    // moves it (else the drag highlights a range, reported row by row)
    std::function<bool (int)> isRowInRange;
    std::function<bool (int)> dragMoves;
    std::function<void (int row)> onDragSelect;
    std::function<void (int row)> onHover;           // the row under the mouse (-1 = none)
    std::function<juce::Colour (int)> getRowStripe;   // a coloured stripe at the row's left edge (transparent = none)
    std::function<void()> onRelease;                  // the mouse let go (after a click on a row)
    std::function<void (int, float)> onDoubleClickAt; // a double-click and its x (if set, instead of onDoubleClick)
    float textX() const { return Theme::px * 8.0f; } // where the rows' text starts
    float rowY (int row) const { return (float) (row - scroll) * rowHeight(); }   // (may be off the list)
    int selected = 0;
    int playing = -1;   // row marked with a play arrow

    // optional: section dividers with a label on the right; clicks there go to onTagClick
    std::function<juce::String (int)> getRowTag;     // non-empty = a divider above this row
    std::function<bool (int)> isRowLooped;           // marked at the left edge
    std::function<void (int)> onTagClick;
    std::function<juce::String (int)> getRowName;    // a name shown after the row's text, before any section label
    float tagZoneX = -1;                             // x where the label zone starts (-1 = none)
    // NAME ZONE (the sequence): the row's name drawn from nameX to nameEndX (too long: it scrolls, on the
    // playing / selected / hovered row), then getRowNumber's text at numberX, then the section zone
    std::function<juce::String (int)> getRowNumber;
    float nameX = -1, nameEndX = -1, numberX = -1;
    float nameScroll (int row, const juce::String& name) const;   // how far (pixels) the name is scrolled now
    // where a drag moves rows: only from x < grabX (the position numbers) when set; a drag anywhere else
    // highlights. Clicks open the section label only on the label itself.
    float grabX = -1;
    bool  onTag (int row, float x) const;
    static double& testClockMs() { static double t = -1.0; return t; }   // tests: the time the scrolling uses (-1 = the real one)
    int  hoverRow = -1;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void ensureVisible (int row);
    void scrollBy (int rows);
    float rowHeight() const { return Theme::px * 11; }

private:
    int scroll = 0;
    WheelSteps wheel;
    EdgeScroller edgeScroll;
    float lastDragY = 0;
    int hoverTagRow = -1;
    int repeatDir = 0;            // arrow button held: -1 up, +1 down
    int dragRow = -1, dropRow = -1;
    bool reordering = false, rangeDragging = false;
    float thumbGrab = -1.0f;      // y offset inside the thumb while dragging it
    int rowAt (float y) const;
    int visibleRows() const { return std::max (1, (int) (getHeight() / rowHeight())); }
    int maxScroll() const;
    float barWidth() const { return Theme::px * 9; }
    juce::Rectangle<float> barArea() const { return getLocalBounds().toFloat().removeFromRight (barWidth()); }
    juce::Rectangle<float> upArrow() const { return barArea().removeFromTop (barWidth()); }
    juce::Rectangle<float> downArrow() const { return barArea().removeFromBottom (barWidth()); }
    juce::Rectangle<float> track() const { return barArea().reduced (0, barWidth()); }
    juce::Rectangle<float> thumb() const;
    void timerCallback() override;
};

class SequencePanel : public juce::Component
{
public:
    SequencePanel (PaulaProcessor&, std::function<void()> changed);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    SeqEditor& editor() { return ops; }
    std::function<void()> returnFocus;   // hand the keyboard back (to the pattern)

private:
    PaulaProcessor& proc;
    std::function<void()> changed;
    PixList list;
    PixButton insBtn { "ADD" }, newBtn { "NEW" }, delBtn { "DEL" };
    void insertPosition (bool newBlock);
    PixButton upBtn { juce::String::charToString ((juce::juce_wchar) 3) }, downBtn { juce::String::charToString ((juce::juce_wchar) 4) };
    PixButton cloneInPlaceBtn { "CLONE IN PLACE" }, arrangeBtn { "ARRANGE" };
    NumberBox blockBox { "BLOCK", 0, 255, 0 };
    PixButton loopSectBtn { "LOOP SECTION", true }, loopSelBtn { "LOOP SELECTION", true };
    PixButton cutBtn { "CUT" }, copyBtn { "COPY" }, pasteBtn { "PASTE" }, dupBtn { "DUPLICATE" };
    SeqEditor ops { proc };
public:
    std::function<void (const juce::String&)> pushStructUndo;
    std::function<void()> openArrange;
    std::function<void (const juce::String&)> onStatus;
    void moveSelected (int delta);
    void cloneInPlace();
    void combineSelection();                 // COMBINE: the highlighted positions' blocks into one new block
    void setLoopSelection (bool on);         // LOOP SELECTION: song play loops the highlighted positions
    PixList& listView() { return list; }     // (tests)
    SeqEditor& seqOps() { return ops; }      // (tests)
    void selectPosition (int row);   // same as clicking it in the list
    void nameBlockAt (int row);      // BLOCK NAME... for the block at that song position
};

// Add / rename / remove the section divider at sequence position `pos` (asks in a small window).
// `done` gets a status message when something changed.
void editSectionDialog (PaulaProcessor&, int pos, std::function<void (const juce::String&)> done);
void editBlockNameDialog (PaulaProcessor&, int block, std::function<void (const juce::String&)> done);   // the block's name (an undo step)
void setBlockName (PaulaProcessor&, int block, const juce::String& name);
// Point the player's section loop at the selected position's section (or turn it off)
void syncSectionLoop (PaulaProcessor&);

class BlockPanel : public juce::Component
{
public:
    BlockPanel (PaulaProcessor&, std::function<void()> changed);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PaulaProcessor& proc;
    std::function<void()> changed;
    NumberBox current { "BLOCK", 0, 255, 0 };
    NumberBox lines { "LINES", 1, 256, 64 };
    PixButton doubleBtn { "X2" }, halveBtn { "1/2" };
    PixButton newBtn { "NEW" }, cloneBtn { "CLONE" }, delBtn { "DEL" };
    PixButton numDownBtn { "NUM -" }, numUpBtn { "NUM +" };
    PixLabel  countLabel;
public:
    std::function<void (const juce::String&)> pushStructUndo;
    std::function<void()> onDouble, onHalve;
    std::function<void (bool cut)> onCopyBlock;   // right-click menu (the pattern's block clipboard)
    std::function<void()> onPasteBlock;
    void mouseDown (const juce::MouseEvent&) override;   // right-click: the block's menu
    void showBlockMenu();
};

class WaveView : public juce::Component
{
public:
    explicit WaveView (PaulaProcessor& p) : proc (p) {}
    std::function<void()> changed, openEditor, openSample;   // openSample: a click on an empty slot (SAMPLE)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    PaulaProcessor& proc;
    int dragging = 0; // 1 = loop start, 2 = loop end
    int sampleAt (float x) const;
};

class InstrumentPanel : public juce::Component
{
public:
    InstrumentPanel (PaulaProcessor&, std::function<void()> changed, std::function<void (const juce::String&)> status);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void loadSample();
    int  chopToPattern (int instrument);   // CHOP TO PATTERN (right-click an instrument); also tests
    std::function<void()> openBrowser, openSampleEditor, openParams, openSample;   // openSample: SAMPLE (record audio)

private:
    PaulaProcessor& proc;
    std::function<void()> changed;
    std::function<void (const juce::String&)> status;
    PixList list;
    PixButton loadBtn { "LOAD" }, editBtn { "EDIT" }, paramsBtn { "PARAMS" }, clearBtn { "CLEAR" }, nameBtn { "NAME" };
    PixButton loopBtn { "LOOP", true };
    NumberBox vol { "VOL", 0, 64, 64 }, fine { "FINE", -8, 7, 0 }, trans { "TRNS", -24, 24, 0 };
    NumberBox loopStart { "LSTRT", 0, 1, 0 }, loopLen { "LLEN", 0, 1, 0 };
    WaveView wave;
    std::unique_ptr<juce::FileChooser> chooser;

    template <typename Fn> void editInstrument (const juce::String& what, Fn&& fn);
public:
    void showInstrumentMenu (int row);   // right-click: COPY, PASTE, DUPLICATE, CLEAR, RENAME, LOAD, PLAY
private:
};

} // namespace prv::ui
