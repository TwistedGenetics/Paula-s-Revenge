// Paula's Revenge - full-size song arrangement page (the sequence, big)
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class ArrangePage : public juce::Component
{
public:
    explicit ArrangePage (PaulaProcessor&);

    std::function<void()> onClose, onChanged, pushUndo, undo, redo;
    std::function<void (const juce::String&)> onStatus;

    void opened();
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // operations (also used by tests); they work on the selected range
    void select (int from, int to);
    void moveSelection (int delta);
    void moveSelectionTo (int insertBefore);
    void copySelection();
    void cutSelection();
    void paste();
    void repeatSelection();
    void deleteSelection();
    void insertBlock (int block);
    void setBlock (int block);
    int selStart() const { return std::min (anchor, cursor); }
    int selEnd() const { return std::max (anchor, cursor); }

private:
    PaulaProcessor& proc;
    int cursor = 0, anchor = 0, scroll = 0;
    int dragFrom = -1, dropAt = -1;
    bool dragging = false;
    std::vector<int> clip;

    PixButton upBtn { juce::String::charToString ((juce::juce_wchar) 3) + " MOVE" }, downBtn { juce::String::charToString ((juce::juce_wchar) 4) + " MOVE" },
              copyBtn { "COPY" }, cutBtn { "CUT" }, pasteBtn { "PASTE" }, repeatBtn { "REPEAT" },
              insertBtn { "INSERT" }, setBtn { "SET" }, deleteBtn { "DELETE" },
              undoBtn { "UNDO" }, redoBtn { "REDO" }, closeBtn { "CLOSE" };
    NumberBox blockBox { "BLOCK", 0, 255, 0 };

    juce::Rectangle<float> titleArea, listArea;
    float rowH() const { return Theme::px * 15; }
    int visibleRows() const { return std::max (1, (int) (listArea.getHeight() / rowH())); }
    int rowAt (float y) const;
    void ensureVisible();
    void changed (const juce::String& status);
    void edit (std::function<void (std::vector<int>&)> fn, const juce::String& status);
    void paintPreview (juce::Graphics&, juce::Rectangle<float>, int block);
};
} // namespace prv::ui
