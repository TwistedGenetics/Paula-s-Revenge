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
    void mouseDrag (const juce::MouseEvent&) override;
    bool keyStateChanged (bool) override;
    void visibilityChanged() override;
    void playFrom (int sample);               // also used by tests
    void playNote (int note);                 // tracker note number, C-3 = original pitch
    int  loadedLength() const { return auditionLength; }
    const juce::File& currentDirectory() const { return dir; }

    // favourite folders FAV 1-4 (remembered on this computer); also used by tests
    static constexpr int kFavourites = 4;
    juce::File favourite (int i) const;
    void setFavourite (int i, const juce::File& folder);   // an empty File clears it
    void favouriteClicked (int i, juce::ModifierKeys);     // click = go (or set if empty), Shift = set, Alt = clear

private:
    PaulaProcessor& proc;
    juce::File dir;
    struct Entry { juce::File file; bool isDir; juce::String label; };
    std::vector<Entry> entries;
    int selected = 0;

    PixList list;
    PixButton upBtn { juce::String::charToString ((juce::juce_wchar) 3) + " UP" }, homeBtn { "HOME" }, musicBtn { "MUSIC" }, desktopBtn { "DESKTOP" },
              downloadsBtn { "DOWNLOADS" }, docsBtn { "DOCUMENTS" }, volumesBtn { "DRIVES" }, finderBtn { "FINDER..." };
    PixButton favBtn[kFavourites] { PixButton ("FAV 1"), PixButton ("FAV 2"), PixButton ("FAV 3"), PixButton ("FAV 4") };
    void updateFavButtons();
    void chooseFavourite (int i);
    PixButton playBtn { "PLAY" }, stopBtn { "STOP" }, autoBtn { "AUTO PLAY", true },
              loadBtn { "LOAD INTO 01" }, cancelBtn { "CLOSE" };
    PixSlider volSlider { "VOL" };

    juce::Rectangle<float> pathArea, infoArea, waveArea;
    juce::String info;
    std::vector<std::pair<float, float>> peaks;   // min/max per column of the auditioned file
    int auditionLength = 0;
    bool auditioning = false;
    juce::File auditionFile;
    std::unique_ptr<juce::FileChooser> chooser;

    void audition (const juce::File&, bool playNow);
    void stopAudition();
    int  noteForKey (const juce::KeyPress&) const;
    std::map<int, int> heldKeys;
    double auditionRate = 44100.0;
    void updateLoadLabel();
    static bool isAudioFile (const juce::File&);
};
} // namespace prv::ui
