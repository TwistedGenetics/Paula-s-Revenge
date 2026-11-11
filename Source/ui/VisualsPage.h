// Paula's Revenge - VISUALS: a demoscene show for the whole window, in time with the song. Copper bars, a
// starfield, a sine scroller, each track's oscilloscope - the kicks and snares push them about (every note flashes
// its track, and the stars rush forward on the beat). For live sets, or a screen recording for a release.
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace prv::ui
{
class VisualsPage : public juce::Component, private juce::Timer
{
public:
    explicit VisualsPage (PaulaProcessor&);
    ~VisualsPage() override;
    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;

    void opened();
    void closed();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // (also for tests)
    enum Scene { All = 0, Copper, Stars, Scopes, Count };
    Scene scene = All;
    void setScene (Scene);
    void step (double seconds);            // the show moved on (the timer, or tests)
    float beatFlash() const { return flash; }
    float trackFlash (int t) const { return t >= 0 && t < kMaxTracks ? trackHit[t] : 0.0f; }
    bool controlsShown() const { return showControls; }
    PixButton sceneBtn[Count] { PixButton ("ALL", true), PixButton ("COPPER", true), PixButton ("STARS", true), PixButton ("SCOPES", true) };
    PixButton closeBtn { "CLOSE (ESC)" };

private:
    PaulaProcessor& proc;
    double t = 0;                          // seconds the show has run
    double scroll = 0;                     // the scroller's position (pixels)
    float flash = 0;                       // the beat: 1 on a hit, dying away
    float trackHit[kMaxTracks] {};
    int lastNotes[kMaxTracks] {};
    float speed = 1;                       // the stars' speed (pushed up by hits)
    struct Star { float x, y, z; };
    std::vector<Star> stars;
    uint32_t seed = 1234567;
    double idle = 0;                       // seconds since the mouse moved (the buttons hide)
    bool showControls = true;
    juce::String scrollText;
    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }
    void timerCallback() override;
    void paintCopper (juce::Graphics&, juce::Rectangle<float>);
    void paintStars (juce::Graphics&, juce::Rectangle<float>);
    void paintScopes (juce::Graphics&, juce::Rectangle<float>);
    void paintScroller (juce::Graphics&, juce::Rectangle<float>);
};
} // namespace prv::ui
