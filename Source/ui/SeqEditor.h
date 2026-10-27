// Paula's Revenge - editing a highlighted range of song positions (shared by the
// sequence panel and the arrange page: one selection, one clipboard)
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace prv::ui
{
class SeqEditor
{
public:
    explicit SeqEditor (PaulaProcessor& p) : proc (p) {}

    // called after every change (status may be empty)
    std::function<void (const juce::String&)> onDone;

    // the range is the anchor .. the current position (EditorState::seqAnchor / seqPos)
    int  selStart() const;
    int  selEnd() const;
    int  count() const { return selEnd() - selStart() + 1; }
    void select (int anchor, int cursor);
    void clamp();
    int  anchor() const;     // the fixed end of the range (= seqPos when there's no range)
    bool hasRange() const { return selStart() != selEnd(); }

    void copy();
    void cut();
    void paste();          // inserted after the range (or after the cursor)
    void duplicate();      // a copy straight after the range, which is then selected
    void remove();
    void moveBy (int delta);
    void moveTo (int insertBefore);
    void insertBlock (int block);
    void setBlock (int block);

private:
    PaulaProcessor& proc;
    void edit (std::function<void (std::vector<int>&)> fn, const juce::String& status,
               std::function<void (std::vector<SongSection>&, int)> secFn = nullptr);
    void done (const juce::String& s) { if (onDone) onDone (s); }
    static juce::String positions (int n) { return juce::String (n) + (n == 1 ? " POSITION" : " POSITIONS"); }
};
} // namespace prv::ui
