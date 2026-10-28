// Paula's Revenge - spectrum strip under the pattern
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class Spectrum : public juce::Component
{
public:
    explicit Spectrum (PaulaProcessor& p);
    void tick();                 // ~30x a second
    void paint (juce::Graphics&) override;
    void feed (const float* samples, int n);   // used by tests
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onColourMenu;        // right-click: the spectrum's colour

private:
    static constexpr int kOrder = 11, kSize = 1 << kOrder, kBands = 48;
    PaulaProcessor& proc;
    juce::dsp::FFT fft { kOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ring = std::vector<float> (kSize, 0.0f);
    int ringPos = 0;
    std::vector<float> work = std::vector<float> (kSize * 2, 0.0f);
    float bands[kBands] {}, peaks[kBands] {};
    int peakHold[kBands] {};
    std::vector<float> incoming = std::vector<float> (8192, 0.0f);
    void analyse();
};
// Left/right output level meter: two bars with "L" and "R" so the balance is obvious
class OutputMeter : public juce::Component
{
public:
    explicit OutputMeter (PaulaProcessor& p) : proc (p) {}
    void tick();
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onColourMenu;        // right-click: the meters' colour
    void paint (juce::Graphics&) override;
private:
    PaulaProcessor& proc;
    float l = 0, r = 0, holdL = 0, holdR = 0;
    int holdCountL = 0, holdCountR = 0;
};
} // namespace prv::ui
