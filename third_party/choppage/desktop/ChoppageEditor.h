#pragma once
/*
    ChoppageEditor — BR3AK's arrangement drawn as an Akai S950 front panel.

      header   TWISTED GENETICS · CHOPPAGE model plate · SAMPLE / SEQ / FX / SET / DISK keys (red LEDs)
      left     SAMPLE RATE fader (44.1 kHz top, 4.0 kHz bottom)
      centre   the blue LCD: ▶ · break picker · LOAD · DRAG · BPM / the 8 break slots / the tab / help line
               PATTERN and FX dice with AMOUNT knobs, then the 8 FX pads
      right    LEVEL meter and VOL
      bottom   floppy slot (drop breaks / click to load) and FILTER · DISTORTION · CRUSH · REVERB

    Everything is laid out at designW x designH and scaled as a whole, so the
    window resizes without the layout changing.
*/
#include "ChoppageProcessor.h"
#include "ChopUI/Tabs.h"

class ChoppageEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             public juce::DragAndDropContainer,
                             private juce::Timer
{
public:
    static constexpr int designW = 1600, designH = 1100;

    explicit ChoppageEditor (ChoppageProcessor&);
    ~ChoppageEditor() override;

    void resized() override;
    void showTab (int);

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    bool keyPressed (const juce::KeyPress&) override;   // Cmd+Z UNDO, Shift+Cmd+Z / Cmd+Y REDO

private:
    class Panel;
    void timerCallback() override;
    void chooseFile();
    void chooseFolder();
    void choosePreset (bool save);

    ChoppageProcessor& proc;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    uint32_t seenVersion = 0;
};
