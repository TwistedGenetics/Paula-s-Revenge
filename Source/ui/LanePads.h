#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
// THE LANE: the whole song along a strip over the pattern - one column per song position, a clip
// per track in its section's colour with its notes drawn small inside, the position being edited
// ringed and (playing) a playhead running through. Click a column to go to that position.
class ArrangeLane : public juce::Component
{
public:
    explicit ArrangeLane (PaulaProcessor&);
    std::function<void (int pos)> onPick;          // a column clicked: go to that song position
    int  preferredHeight() const;                  // (header and a row per track shown)
    int  columnAt (float x) const;                 // the song position under x (-1 = none)
    juce::Rectangle<float> columnRect (int pos) const;   // (empty when not shown)
    int  tracksShown() const;
    int  firstColumn() const { return first; }
    void tick();                                   // repaints when the playhead or position moved

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    PaulaProcessor& proc;
    int first = 0;            // the first position shown
    int lastKey = -1;
    float colW() const;
    float headerH() const;
    int visibleColumns() const;
    int currentPos() const;
    void keepVisible (int pos);
    juce::Colour sectionColour (int pos, int track) const;
    juce::String sectionNameAt (int pos) const;   // a section starting here ("" = none)
};

// THE PADS: sixteen pads, one instrument each (four banks: 1-16, 17-32, 33-48, 49-63). A pad plays
// its instrument at C of the octave (EDIT on: writes it at the cursor, as a key would) and lights
// whenever its instrument plays.
class PadPanel : public juce::Component, private juce::Timer
{
public:
    explicit PadPanel (PaulaProcessor&);
    ~PadPanel() override;
    std::function<void (int instrument)> onPadDown;
    std::function<void()> onPadUp;
    std::function<void()> onBank;                 // the bank changed
    juce::Rectangle<float> padRect (int i) const; // pad 0..15 of the bank shown
    int  padAt (juce::Point<float>) const;        // -1 = none
    int  bank() const;
    void setBank (int b);
    float flashOf (int i) const { return i >= 0 && i < 16 ? flash[i] : 0.0f; }
    int  heldPad() const { return held; }
    int  columns() const;                         // 4 (four by four) or 8 (two rows of eight, somewhere wide)
    int  preferredHeightFor (int width) const;    // (two rows of eight)

    PixButton bankDown { "-" }, bankUp { "+" };

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    PaulaProcessor& proc;
    float flash[16] {};
    int lastNotes[kMaxTracks] {};
    int held = -1;
    juce::Rectangle<float> padArea() const;
    float sideW() const;
    bool isStrip() const;
    float titleH() const;
    void timerCallback() override;
};
} // namespace prv::ui
