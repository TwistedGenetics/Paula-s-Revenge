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
} // namespace prv::ui
