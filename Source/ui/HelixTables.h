// Paula's Revenge - HELIX's wavetables on its page (v1.01): your wavetable folder (looked through with its sub-folders),
// the tables in it by group, and each table's frames for the display (built in the background, kept once made).
#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace prv::ui::helixtables
{
struct Entry
{
    juce::String name;    // "GROWL 01", "PIANO (CYCLES)"
    juce::String group;   // where it is under the folder: "" (the folder itself), "BASS", "BASS/REESE"
    juce::String id;      // "path:<wav>" or "dir:<folder of single cycles>"
};
juce::File folder();                                   // the wavetable folder (none yet: an empty File)
void setFolder (const juce::File&);                    // (and looks through it)
const std::vector<Entry>& library (bool rescan = false);
juce::StringArray groups();                            // the groups with tables in them, in order
juce::String nameOf (const juce::String& id);          // what to call a table: its file's or folder's name

struct Preview { int totalFrames = 0; std::vector<std::vector<float>> frames; };
// a table's frames to draw ("builtin:<name>" too); nullptr while it's being made (in the background)
std::shared_ptr<const Preview> preview (const juce::String& id);
bool newPreviews();                                    // true once after one was made (time to repaint)
} // namespace prv::ui::helixtables
