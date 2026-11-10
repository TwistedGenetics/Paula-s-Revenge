// Paula's Revenge - PHRASES on an instrument's PARAMS page: its list of phrases (up to 16), the
// keys, speed, loop and TRANSPOSE of the one picked, and that phrase written right here.
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
// the phrase itself: one track of lines - NOTE INSTRUMENT FX1 FX2 - written like the pattern
class PhraseGrid : public juce::Component
{
public:
    explicit PhraseGrid (PaulaProcessor&);
    std::function<int()> phraseIndex;                       // which of the instrument's phrases (-1 = none)
    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onEdited;

    int line = 0, column = 0;                               // the cursor (columns: 0 note, 1-2 instrument, 3-5 FX1, 6-8 FX2)
    int playLine = -1;                                      // the line playing (-1 = none)
    void setCursor (int l, int c);
    bool typeKey (const juce::KeyPress&);                   // (keyPressed; tests call it too)
    juce::String rowText (int l) const;                     // "000 C-3 -- C20 ---"
    int  linesShown() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress& k) override { return typeKey (k); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    PaulaProcessor& proc;
    int top = 0;
    const Phrase* phrase() const;
    Block* block() const;
    float rowH() const;
    float charW() const;
    void keepVisible();
    void write (const juce::String& what, std::function<void (Cell&)> fn, bool advance = true);
};

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
    PhraseGrid& grid() { return phraseGrid; }
    juce::Rectangle<float> listRow (int i) const;           // (tests) where phrase i is listed

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    PaulaProcessor& proc;
    int sel = 0, listTop = 0, shownInstrument = -1;
    PhraseGrid phraseGrid { proc };
    PixButton addBtn { "ADD" }, cloneBtn { "CLONE" }, delBtn { "DEL" };
    NumberBox loBox { "LOW KEY", 1, 96, 1 }, hiBox { "HIGH KEY", 1, 96, 96 };
    NumberBox linesBox { "LINES", 1, kMaxLines, 16 }, lpbBox { "LPB", 1, 32, 4 };
    PixButton loopBtn { "LOOP", true }, transposeBtn { "TRANSPOSE", true };
    NumberBox startBox { "START", 0, kMaxLines - 1, 0 }, endBox { "END", 0, kMaxLines - 1, 15 };
    PixButton playBtn { juce::String::charToString ((juce::juce_wchar) 1) + " PLAY", true }, backBtn { "SOUND" };
    juce::Rectangle<float> listBox, listArea, setBox, gridBox;

    Instrument& inst();
    const Phrase* current() const;
    int  listRows() const;
    void phraseMenu (int index);
    template <typename Fn> void edit (const juce::String& what, Fn&& fn);
};
} // namespace prv::ui
