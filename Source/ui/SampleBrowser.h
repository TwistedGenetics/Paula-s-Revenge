// Paula's Revenge - sample browser that auditions files as you move through them
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "Panels.h"

namespace prv::ui
{
class SampleBrowser : public juce::Component
{
public:
    explicit SampleBrowser (PaulaProcessor&);
    ~SampleBrowser() override;

    std::function<void()> onLoaded;   // a sample went into the current instrument
    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;

    void opened();                    // called each time the browser is shown
    void tick();
    void setDirectory (const juce::File&);
    void selectRow (int row, bool audition);
    bool loadSelected();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    PaulaProcessor& proc;
    juce::File dir;
    struct Entry { juce::File file; bool isDir; juce::String label; };
    std::vector<Entry> entries;
    int selected = 0;

    PixList list;
    PixButton upBtn { juce::String::charToString ((juce::juce_wchar) 3) + " UP" }, homeBtn { "HOME" }, musicBtn { "MUSIC" }, desktopBtn { "DESKTOP" },
              downloadsBtn { "DOWNLOADS" }, docsBtn { "DOCUMENTS" }, volumesBtn { "DRIVES" }, finderBtn { "FINDER..." };
    PixButton playBtn { "PLAY" }, stopBtn { "STOP" }, autoBtn { "AUTO PLAY", true },
              loadBtn { "LOAD INTO 01" }, cancelBtn { "CLOSE" }, keepRateBtn { "KEEP RATE", true };
    PixSlider volSlider { "VOL" };

    juce::Rectangle<float> pathArea, infoArea, waveArea;
    juce::String info;
    std::vector<std::pair<float, float>> peaks;   // min/max per column of the auditioned file
    int auditionLength = 0;
    bool auditioning = false;
    juce::File auditionFile;
    std::unique_ptr<juce::FileChooser> chooser;

    void audition (const juce::File&);
    void stopAudition();
    void updateLoadLabel();
    static bool isAudioFile (const juce::File&);
};
} // namespace prv::ui
