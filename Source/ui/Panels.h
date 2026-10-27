// Paula's Revenge - side and bottom panels
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
// Scrollable list drawn with the pixel font (sequence + instrument lists)
class PixList : public juce::Component
{
public:
    std::function<int()> getNumRows;
    std::function<juce::String (int)> getRowText;
    std::function<bool (int)> isRowDim = [] (int) { return false; };
    std::function<void (int, const juce::MouseEvent&)> onClick;
    std::function<void (int)> onDoubleClick;
    int selected = 0;
    int playing = -1;   // row marked with a play arrow

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void ensureVisible (int row);
    float rowHeight() const { return Theme::px * 11; }

private:
    int scroll = 0;
    int rowAt (float y) const;
    int visibleRows() const { return std::max (1, (int) (getHeight() / rowHeight())); }
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
    NumberBox blockBox { "BLOCK", 0, 255, 0 };
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
    PixLabel  countLabel;
};

class WaveView : public juce::Component
{
public:
    explicit WaveView (PaulaProcessor& p) : proc (p) {}
    std::function<void()> changed;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
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

private:
    PaulaProcessor& proc;
    std::function<void()> changed;
    std::function<void (const juce::String&)> status;
    PixList list;
    PixButton loadBtn { "LOAD" }, clearBtn { "CLEAR" }, nameBtn { "NAME" };
    PixButton loopBtn { "LOOP", true };
    NumberBox vol { "VOL", 0, 64, 64 }, fine { "FINE", -8, 7, 0 }, trans { "TRNS", -24, 24, 0 };
    NumberBox loopStart { "LSTRT", 0, 1, 0 }, loopLen { "LLEN", 0, 1, 0 };
    WaveView wave;
    std::unique_ptr<juce::FileChooser> chooser;

    template <typename Fn> void editInstrument (Fn&& fn);
};

} // namespace prv::ui
