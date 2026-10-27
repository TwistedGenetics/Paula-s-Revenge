// Paula's Revenge - spectrum strip under the pattern
#include "Spectrum.h"

namespace prv::ui
{
Spectrum::Spectrum (PaulaProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);
}

void Spectrum::feed (const float* samples, int n)
{
    for (int i = 0; i < n; ++i) { ring[(size_t) ringPos] = samples[i]; ringPos = (ringPos + 1) % kSize; }
}

void Spectrum::analyse()
{
    for (int i = 0; i < kSize; ++i) work[(size_t) i] = ring[(size_t) ((ringPos + i) % kSize)];
    std::fill (work.begin() + kSize, work.end(), 0.0f);
    window.multiplyWithWindowingTable (work.data(), (size_t) kSize);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);

    const double rate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const double lo = 40.0, hi = std::min (16000.0, rate * 0.45);
    for (int b = 0; b < kBands; ++b)
    {
        const double f1 = lo * std::pow (hi / lo, b / (double) kBands);
        const double f2 = lo * std::pow (hi / lo, (b + 1) / (double) kBands);
        const int i1 = juce::jlimit (1, kSize / 2 - 1, (int) (f1 * kSize / rate));
        const int i2 = juce::jlimit (i1 + 1, kSize / 2, (int) (f2 * kSize / rate) + 1);
        float m = 0;
        for (int i = i1; i < i2; ++i) m = std::max (m, work[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels (m / (kSize * 0.25f), -72.0f);
        // gentle upward tilt so the highs show, then -60..-6 dB fills the strip
        const float tilt = 3.0f * (float) std::log2 (std::max (1.0, f1 / 200.0));
        const float v = juce::jlimit (0.0f, 1.0f, (db + tilt + 60.0f) / 54.0f);
        bands[b] = std::max (v, bands[b] * 0.8f);
        if (bands[b] >= peaks[b]) { peaks[b] = bands[b]; peakHold[b] = 18; }
        else if (--peakHold[b] < 0) peaks[b] = std::max (0.0f, peaks[b] - 0.03f);
    }
}

void Spectrum::tick()
{
    const int got = proc.readScope (incoming.data(), (int) incoming.size());
    feed (incoming.data(), got);
    analyse();
    repaint();
}

void Spectrum::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    g.fillAll (sk.bg);

    auto inner = r.reduced (px * 2, px * 2);
    const float bw = inner.getWidth() / kBands;
    const float segH = std::max (2.0f, px * 2);
    const int segs = std::max (3, (int) (inner.getHeight() / segH));
    for (int b = 0; b < kBands; ++b)
    {
        const float x = inner.getX() + b * bw;
        const int lit = juce::roundToInt (bands[b] * segs);
        // bars pick up the track colours left to right so the strip ties in with the pattern
        const auto c = sk.track[(b * 8) / kBands];
        for (int k = 0; k < lit; ++k)
        {
            g.setColour (sk.dark ? c : sk.specBar);
            g.fillRect (x + 1.0f, inner.getBottom() - (k + 1) * segH + 1.0f, bw - 2.0f, segH - 1.0f);
        }
        const int pk = juce::roundToInt (peaks[b] * segs);
        if (pk > 0)
        {
            g.setColour (sk.hot);
            g.fillRect (x + 1.0f, inner.getBottom() - pk * segH + 1.0f, bw - 2.0f, segH - 1.0f);
        }
    }
    g.setColour (sk.dim);
    g.fillRect (inner.getX(), inner.getBottom(), inner.getWidth(), 1.0f);
    Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f));
}

void OutputMeter::tick()
{
    auto upd = [] (float in, float& v, float& hold, int& count)
    {
        v = std::max (in, v * 0.85f);
        if (v >= hold) { hold = v; count = 20; } else if (--count < 0) hold = std::max (0.0f, hold - 0.02f);
    };
    upd (proc.player.uiOutL.load(), l, holdL, holdCountL);
    upd (proc.player.uiOutR.load(), r, holdR, holdCountR);
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    auto area = getLocalBounds().toFloat();
    const float labelW = px * PixelFont::kCellW * 1.5f;
    auto toFrac = [] (float v) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -60.0f) + 48.0f) / 48.0f); };
    const float half = (area.getWidth() - px * 2) / 2.0f;
    int i = 0;
    for (auto [v, hold, name] : { std::tuple<float, float, const char*> { l, holdL, "L" }, { r, holdR, "R" } })
    {
        auto cell = juce::Rectangle<float> (area.getX() + (half + px * 2) * (float) i++, area.getY(), half, area.getHeight());
        PixelFont::drawLeft (g, name, cell.withWidth (labelW), px, Theme::ink);
        auto bar = cell.withTrimmedLeft (labelW).reduced (0, area.getHeight() * 0.22f);
        g.setColour (sk.bg);
        g.fillRect (bar);
        g.setColour (toFrac (v) > 0.92f ? sk.hot : sk.track[3]);
        g.fillRect (bar.withWidth (bar.getWidth() * toFrac (v)));
        g.setColour (sk.text);
        g.fillRect (bar.getX() + bar.getWidth() * toFrac (hold) - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        Theme::bevel (g, bar, false, 1.0f);
    }
}
} // namespace prv::ui
