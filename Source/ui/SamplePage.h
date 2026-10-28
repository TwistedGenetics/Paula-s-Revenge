// Paula's Revenge - SAMPLE: record audio (the input, or the app's own output) into the selected
// instrument. A meter, a trigger (MANUAL / THRESHOLD / WITH PLAYBACK), the take as a waveform,
// then KEEP (into the instrument, an undo step) or AGAIN.
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class SamplePage : public juce::Component
{
public:
    explicit SamplePage (PaulaProcessor&);
    ~SamplePage() override;

    std::function<void()> onClose, onChanged, onAudioSettings, onEdit;   // onEdit: the sample editor, after KEEP
    std::function<void (const juce::String&)> onStatus;

    void opened();
    void tick();                       // ~30 a second: the meter, the clock, the take when it's done
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // actions (buttons; tests call them too)
    void record();                     // RECORD: arm / start (again: STOP)
    void stop();
    bool keep();                       // the take into the instrument
    void again();                      // throw the take away and record again
    void setSource (int s);            // PaulaProcessor::SourceInput / SourceResample
    void setChannels (RecChannels c);
    void setTrigger (RecTrigger t);
    void setMonitor (bool on);
    void setAmigaRate (int index);     // 0 = none, 1-4 = 8363 / 16726 / 22050 / 28867
    void setThresholdDb (float db);
    void setGainDb (float db);
    bool hasTake() const { return ! takeL.empty(); }
    double takeSeconds() const { return takeRate > 0 ? (double) takeL.size() / takeRate : 0.0; }
    juce::String clockText() const;
    juce::String stateText() const;

    static constexpr double kMaxSeconds = 300.0;   // 5 minutes
    static const int kAmigaRates[5];

private:
    PaulaProcessor& proc;
    PixButton closeBtn { "CLOSE" }, settingsBtn { "AUDIO/MIDI SETTINGS..." };
    PixButton inputBtn { "INPUT", true }, resampleBtn { "RESAMPLE", true }, monitorBtn { "MONITOR", true };
    PixButton stereoBtn { "STEREO", true }, monoBtn { "MONO", true }, leftBtn { "LEFT", true }, rightBtn { "RIGHT", true };
    PixButton manualBtn { "MANUAL", true }, thresholdBtn { "THRESHOLD", true }, playbackBtn { "WITH PLAYBACK", true };
    PixButton recordBtn { "RECORD", true }, stopBtn { "STOP" };
    PixButton keepBtn { "KEEP" }, againBtn { "AGAIN" }, trimBtn { "TRIM SILENCE", true }, normBtn { "NORMALISE", true };
    std::vector<std::unique_ptr<PixButton>> rateBtns;
    PixSlider gainSlider { "INPUT GAIN" }, threshSlider { "THRESHOLD" };

    juce::Rectangle<int> titleArea, sourceLabel, channelsLabel, triggerLabel, rateLabel, meterArea, clockArea, waveArea, infoArea;
    float meterL = 0, meterR = 0, holdL = 0, holdR = 0;
    int holdCount = 0, clipCount = 0;
    std::vector<float> takeL, takeR, live;
    double takeRate = 0;
    bool takeHitLimit = false;
    RecChannels channels = RecChannels::Stereo;
    RecTrigger trigger = RecTrigger::Manual;
    int amigaIndex = 0;
    float thresholdDb = -30.0f, gainDb = 0.0f;

    void refreshButtons();
    void loadPrefs();
    void savePrefs();
    void status (const juce::String& m) { if (onStatus) onStatus (m); }
    void drawMeter (juce::Graphics&, juce::Rectangle<float>);
    void drawWave (juce::Graphics&, juce::Rectangle<float>);
};
} // namespace prv::ui
