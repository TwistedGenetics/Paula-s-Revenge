// Paula's Revenge - full-size song arrangement page (the sequence, big)
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "SeqEditor.h"

namespace prv::ui
{
class ArrangePage : public juce::Component
{
public:
    explicit ArrangePage (PaulaProcessor&);

    std::function<void()> onClose, onChanged;
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
    void showPositionMenu (int row);   // the right-click menu
    int selStart() const { return ops.selStart(); }
    int selEnd() const { return ops.selEnd(); }

private:
    PaulaProcessor& proc;
    SeqEditor ops { proc };
    int scroll = 0;
    bool selecting = false;           // dragging out a highlight (not moving one)
    int cursorPos() const { return proc.editorState.seqPos; }
    int dragFrom = -1, dropAt = -1;
    bool dragging = false;
    WheelSteps wheel;
    EdgeScroller edgeScroll;
    float lastDragY = 0;

    PixButton upBtn { juce::String::charToString ((juce::juce_wchar) 3) + " MOVE" }, downBtn { juce::String::charToString ((juce::juce_wchar) 4) + " MOVE" },
              copyBtn { "COPY" }, cutBtn { "CUT" }, pasteBtn { "PASTE" }, repeatBtn { "DUPLICATE" },
              insertBtn { "INSERT" }, setBtn { "SET" }, deleteBtn { "DELETE" },
              undoBtn { "UNDO" }, redoBtn { "REDO" }, closeBtn { "CLOSE" },
              sectionBtn { "SECTION" }, loopSectBtn { "LOOP SECTION", true };
    NumberBox blockBox { "BLOCK", 0, 255, 0 };

    juce::Rectangle<float> titleArea, listArea;
    float rowH() const { return Theme::px * 15; }
    int visibleRows() const { return std::max (1, (int) (listArea.getHeight() / rowH())); }
    int rowAt (float y) const;
    void ensureVisible();
    void changed (const juce::String& status);
    // fn changes the sequence; secFn (optional) moves the section dividers to match
    float tagX() const;   // where section labels start in a row
    void paintPreview (juce::Graphics&, juce::Rectangle<float>, int block);
};
} // namespace prv::ui
