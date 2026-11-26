// Paula's Revenge - VISUALS: a show for the whole window, moving with the song's sound. Five scenes, each driven by the
// mix's spectrum (lows to highs) and the notes as they play:
//   PRISM    glowing bars in four colour lobes, thin wave strands woven through them
//   RAINBOW  a rainbow of thin bars round the middle, a stave of notes flowing along it (a note for each note played)
//   RIBBONS  glowing translucent ribbon waves, cyan to gold to red, with their reflection and drifting lights
//   PASTEL   rounded pastel bars on navy, mirrored below like a reflection in water
//   MESH     twisting ribbons of dashed lines on black, soft coloured lights behind them
// AUTO moves from one to the next every so often. For live sets, or a screen recording for a release.
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
    enum Scene { Auto = 0, Prism, Rainbow, Ribbons, Pastel, Mesh, Count };
    Scene scene = Auto;
    void setScene (Scene);
    Scene shownScene() const { return scene == Auto ? autoScene : scene; }
    void step (double seconds);            // the show moved on (the timer, or tests)
    float beatFlash() const { return flash; }
    float trackFlash (int tr) const { return tr >= 0 && tr < kMaxTracks ? trackHit[tr] : 0.0f; }
    bool controlsShown() const { return showControls; }
    PixButton sceneBtn[Count] { PixButton ("AUTO", true), PixButton ("PRISM", true), PixButton ("RAINBOW", true),
                                PixButton ("RIBBONS", true), PixButton ("PASTEL", true), PixButton ("MESH", true) };
    PixButton closeBtn { "CLOSE (ESC)" };

    static constexpr int kBands = 64;
    float band (int i) const { return lv[(size_t) juce::jlimit (0, kBands - 1, i)]; }

private:
    PaulaProcessor& proc;
    double t = 0;                          // seconds the show has run
    float flash = 0;                       // the beat: 1 on a hit, dying away
    float trackHit[kMaxTracks] {};
    int lastNotes[kMaxTracks] {};
    uint32_t seed = 1234567;
    double idle = 0;                       // seconds since the mouse moved (the buttons hide)
    bool showControls = true;
    Scene autoScene = Prism;
    double autoTime = 0;
    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }

    // the sound: the mix of every track's scope, its spectrum in kBands bands (smoothed), and a few summaries
    static constexpr int kOrder = 11, kSize = 1 << kOrder;
    juce::dsp::FFT fft { kOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> work = std::vector<float> (kSize * 2, 0.0f);
    std::array<float, kBands> lv {}, slow {};
    float bass = 0, mid = 0, high = 0, level = 0;
    void listen (double dt);
    float bandAt (float x01, bool smooth = false) const;   // 0..1 across the bands, between them

    // RAINBOW's notes riding the stave; RIBBONS' drifting lights
    struct Note { float x, pos, life; int kind; };
    std::vector<Note> notes;
    struct Light { float x, y, vx, vy, size, life, hue; };
    std::vector<Light> lights;
    double noteTimer = 0;

    void timerCallback() override;
    void paintPrism (juce::Graphics&, juce::Rectangle<float>);
    void paintRainbow (juce::Graphics&, juce::Rectangle<float>);
    void paintRibbons (juce::Graphics&, juce::Rectangle<float>);
    void paintPastel (juce::Graphics&, juce::Rectangle<float>);
    void paintMesh (juce::Graphics&, juce::Rectangle<float>);
};
} // namespace prv::ui
