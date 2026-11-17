// Paula's Revenge - the TABLE page (v0.99): an instrument's TABLE, as on the M8 - 16 rows that step along
// with every note it plays. Each row: NOTE (semitones from the note played), VOL (00-40) and two commands
// (`>xy` loops back). SPEED: ticks a row. ON plays it with every note; `$xx` in the pattern picks one for a note.
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace prv::ui
{
class TablePage : public juce::Component
{
public:
    explicit TablePage (PaulaProcessor&);
    ~TablePage() override;
    std::function<void()> onClose, onChanged;
    std::function<void (const juce::String&)> onStatus;

    void opened();
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // (also for tests) the cursor: row 0-15, column 0 NOTE, 1 VOL, 2 FX1, 3 FX2
    int  row = 0, col = 0;
    int  instrument() const;
    void setOn (bool on);
    void setSpeed (int ticks);                // 0 = a row a line
    void setNote (int r, int semis);          // kTableNoNote clears it
    void setVol (int r, int vol);             // kTableNoVol clears it
    void setFx (int r, int which, uint8_t cmd, uint8_t param);
    void clearCell (int r, int c);
    void clearTable();
    void applyPreset (int i);
    static juce::StringArray presetNames();
    void play (bool on);
    bool typeKey (juce::juce_wchar ch);       // what a typed key does at the cursor (true: used)
    juce::Rectangle<float> cellRect (int r, int c) const;
    juce::String cellText (int r, int c) const;
    void showFxMenu (int r, int which);

private:
    PaulaProcessor& proc;
    PixButton insDown { "-" }, insUp { "+" }, onBtn { "ON", true }, speedDown { "-" }, speedUp { "+" };
    PixButton presetBtn { "PRESETS..." }, copyBtn { "COPY" }, pasteBtn { "PASTE" }, clearBtn { "CLEAR" };
    PixButton playBtn { juce::String::charToString ((juce::juce_wchar) 1) + " HOLD TO PLAY" }, closeBtn { "CLOSE" };
    juce::Rectangle<float> titleArea, insArea, speedArea, gridArea, helpArea, headArea;
    int  digit = 0;                // typing position in the cell (NOTE: tens/units; VOL: hi/lo; FX: command, hi, lo)
    bool negPending = false;       // NOTE: '-' typed before the number
    int  shownRow = -1;            // the row playing, as drawn
    InstrumentTable clip;          // COPY / PASTE
    bool hasClip = false;
    float textPx() const;
    float rowH() const;
    InstrumentTable& table();
    const InstrumentTable& table() const;
    template <typename Fn> void edit (const juce::String& what, Fn&& fn, bool merge = false);
    juce::String insName (int i) const;
    juce::String helpFor() const;
    int  playingRow() const;
    void moveCursor (int dr, int dc);
};
} // namespace prv::ui
