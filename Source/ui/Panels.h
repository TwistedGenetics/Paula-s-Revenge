// Paula's Revenge - side and bottom panels
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
// Scrollable list drawn with the pixel font (sequence + instrument lists)
class PixList : public juce::Component, private juce::Timer
{
public:
    std::function<int()> getNumRows;
    std::function<juce::String (int)> getRowText;
    std::function<bool (int)> isRowDim = [] (int) { return false; };
    std::function<void (int, const juce::MouseEvent&)> onClick;
    std::function<void (int)> onDoubleClick;
    std::function<void (int from, int to)> onMove;   // set to allow drag-to-reorder
    int selected = 0;
    int playing = -1;   // row marked with a play arrow

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void ensureVisible (int row);
    void scrollBy (int rows);
    float rowHeight() const { return Theme::px * 11; }

private:
    int scroll = 0;
    int repeatDir = 0;            // arrow button held: -1 up, +1 down
    int dragRow = -1, dropRow = -1;
    bool reordering = false;
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

private:
    PaulaProcessor& proc;
    std::function<void()> changed;
    PixList list;
    PixButton insBtn { "INS" }, delBtn { "DEL" };
    PixButton upBtn { juce::String::charToString ((juce::juce_wchar) 3) }, downBtn { juce::String::charToString ((juce::juce_wchar) 4) };
    PixButton cloneInPlaceBtn { "CLONE IN PLACE" };
    NumberBox blockBox { "BLOCK", 0, 255, 0 };
public:
    std::function<void()> pushStructUndo;
    void moveSelected (int delta);
    void cloneInPlace();
};

class BlockPanel : public juce::Component
{
public:
    BlockPanel (PaulaProcessor&, std::function<void()> changed, std::function<void (int)> pushUndo);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PaulaProcessor& proc;
    std::function<void()> changed;
    std::function<void (int)> pushUndo;
    NumberBox current { "BLOCK", 0, 255, 0 };
    NumberBox lines { "LINES", 1, 256, 64 };
    PixButton newBtn { "NEW" }, cloneBtn { "CLONE" }, delBtn { "DEL" };
    PixButton numDownBtn { "NUM -" }, numUpBtn { "NUM +" };
    PixLabel  countLabel;
public:
    std::function<void()> pushStructUndo;
};

class WaveView : public juce::Component
{
public:
    explicit WaveView (PaulaProcessor& p) : proc (p) {}
    std::function<void()> changed, openEditor;
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
    std::function<void()> openBrowser, openSampleEditor, openParams;

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

    template <typename Fn> void editInstrument (Fn&& fn);
};

} // namespace prv::ui
