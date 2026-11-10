// Paula's Revenge - PHRASES on an instrument's PARAMS page: its list of phrases (up to 16), the
// keys, speed, loop and TRANSPOSE of the one picked, and that phrase written right here.
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "PatternEditor.h"

namespace prv::ui
{
class PhraseView : public juce::Component
{
public:
    explicit PhraseView (PaulaProcessor&);
    ~PhraseView() override;
    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onBack, onChanged;
    std::function<void (int block, int track)> onEditInPattern;

    void refresh();     // the current instrument's phrases into the controls
    void tick();        // the playing line

    // what the buttons do (tests call them too)
    int  selected() const { return sel; }
    void select (int index);
    bool addPhrase();
    bool clonePhrase();
    void deletePhrase();
    void setKeys (int lo, int hi);
    void setLines (int lines);
    void setLpb (int lpb);
    void setLoop (bool on);
    void setLoopRange (int start, int end);
    void setTranspose (bool on);
    void play (bool on);
    bool isPlaying() const;
    PatternEditor& pattern() { return phrasePattern; }   // (the phrase, written as the pattern is)
    PaulaProcessor::EditorState& patternState() { return phraseState; }
    int playLine() const;                                // the phrase's line playing (-1 = none)
    juce::Rectangle<float> listRow (int i) const;           // (tests) where phrase i is listed

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    PaulaProcessor& proc;
    int sel = 0, listTop = 0, shownInstrument = -1;
    PaulaProcessor::EditorState phraseState;             // (its own cursor; EDIT always on)
    PatternEditor phrasePattern { proc, phraseState, 0 };
    void syncPattern();                                  // the phrase picked into the editor
    void syncBack();                                     // its octave / step back to the pattern's
    int lastOctave = -1, lastStep = -1;
    PixButton addBtn { "ADD" }, cloneBtn { "CLONE" }, delBtn { "DEL" };
    NumberBox loBox { "LOW KEY", 1, 96, 1 }, hiBox { "HIGH KEY", 1, 96, 96 };
    NumberBox linesBox { "LINES", 1, kMaxLines, 16 }, lpbBox { "LPB", 1, 32, 4 };
    PixButton loopBtn { "LOOP", true }, transposeBtn { "TRANSPOSE", true };
    NumberBox startBox { "START", 0, kMaxLines - 1, 0 }, endBox { "END", 0, kMaxLines - 1, 15 };
    PixButton playBtn { juce::String::charToString ((juce::juce_wchar) 1) + " PLAY", true }, backBtn { "SOUND" };
    juce::Rectangle<float> listBox, listArea, setBox, gridBox;
    juce::Rectangle<int> phraseBounds;

    Instrument& inst();
    const Phrase* current() const;
    int  listRows() const;
    void phraseMenu (int index);
    template <typename Fn> void edit (const juce::String& what, Fn&& fn);
};
} // namespace prv::ui
