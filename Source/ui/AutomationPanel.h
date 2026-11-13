// Paula's Revenge - graphical AUTOMATION: under the pattern (in place of the spectrum), the current
// track's controls on the left and the selected one's envelope for the current block on the right,
// with the play line going across it. Points, lines or curves; double-click adds or removes a point,
// drag moves one; DRAW paints, LINE draws a straight ramp; SNAP to lines or beats; COPY / PASTE.
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "Panels.h"

namespace prv::ui
{
class AutomationPanel : public juce::Component
{
public:
    explicit AutomationPanel (PaulaProcessor&);

    std::function<void (const juce::String&)> onStatus;
    std::function<void (int height)> onResize;   // the top edge dragged: the panel's new height

    enum class Tool { Edit, Draw, Line };
    enum class Snap { Off, Lines, Beats };
    struct Target { int target = -1, param = -1; juce::String name, shortName; };

    void refresh();                 // the block, the track or the song changed
    void tick();                    // the play line
    int  block() const;
    const std::vector<Target>& targets() const { return list; }   // what the list shows now
    int  selectedTarget() const { return selRow; }
    void selectTarget (int row);
    bool show (int target, int param);
    juce::String selectedName() const { return selRow >= 0 && selRow < (int) list.size() ? list[(size_t) selRow].name : juce::String(); }
    juce::String selectedShortName() const { return selRow >= 0 && selRow < (int) list.size() ? list[(size_t) selRow].shortName : juce::String(); }   // (as the list shows it)
    const AutoLane* selectedLane() const { return lane(); }   // (tests)   // (v0.86) pick this control's envelope (false: not in the list)
    void setTool (Tool t);
    void setSnap (Snap s);
    void setShape (AutoShape);
    void setQuery (const juce::String& q);
    void setAutomatedOnly (bool);
    void copyLane();
    void pasteLane();
    void clearLane();
    const AutoLane* lane() const;   // the selected target's envelope in this block (null = none)
    // the graph's coordinates (tests): a position and value as a point on the graph, and back
    juce::Point<float> toScreen (double pos, float value) const;
    double posAt (float x) const;
    float  valueAt (float y) const;
    juce::Rectangle<int> graphBounds() const { return graph.toNearestInt(); }
    static constexpr int kGrab = 6;   // the top edge's grab height (in px units)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    PaulaProcessor& proc;
    PixList targetList;
    PixButton onlyBtn { "AUTOMATED ONLY", true };
    PixButton shapeBtn[3] { PixButton ("POINTS", true), PixButton ("LINES", true), PixButton ("CURVES", true) };
    PixButton drawBtn { "DRAW", true }, lineBtn { "LINE", true }, snapBtn { "SNAP: LINES" };
    PixButton copyBtn { "COPY" }, pasteBtn { "PASTE" }, clearBtn { "CLEAR" };
    std::vector<Target> list;
    int selRow = 0;
    juce::String query;
    bool automatedOnly = false;
    Tool tool = Tool::Edit;
    Snap snap = Snap::Lines;
    AutoShape newShape = AutoShape::Lines;
    juce::Rectangle<float> graph, header, searchArea;
    juce::Point<float> readoutAt;
    int shownBlock = -1, shownTrack = -1;
    float playPos = -1;
    // a drag in progress
    enum class Drag { None, Point, Bend, Paint, Line, Resize } drag = Drag::None;
    int dragPoint = -1, selPoint = -1;
    double lineFromPos = 0; float lineFromValue = 0;
    juce::Point<float> lineTo;
    int resizeFrom = 0;
    double paintLastPos = -1; float paintLastValue = 0;   // DRAW: where the last mouse move painted
    juce::String readoutText;
    static std::vector<AutoPoint> clip;   // COPY / PASTE (between blocks and controls)

    void rebuildList();
    juce::String valueText (float v) const;
    double snapPos (double pos) const;
    int  pointNear (juce::Point<float>) const;
    AutoLane& laneForEdit (const juce::String& what, bool merge);   // makes it if needed (an undo step)
    void tidy (AutoLane&);
    int  lines() const;
};
} // namespace prv::ui
