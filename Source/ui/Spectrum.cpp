// Paula's Revenge - spectrum strip under the pattern
#include "Spectrum.h"
#include "MeterColours.h"
#include "Analyser.h"

namespace prv::ui
{
Spectrum::Spectrum (PaulaProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (true, false);   // (right-click: its colour)
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
        if (i2 - i1 <= 1)
        {
            // narrower than a bin (the lows): read between the bins at the band's middle, so the line stays smooth
            const double fb = std::sqrt (f1 * f2) * kSize / rate;
            const int b0 = juce::jlimit (1, kSize / 2 - 2, (int) fb);
            const float t = (float) (fb - b0);
            m = work[(size_t) b0] * (1.0f - t) + work[(size_t) b0 + 1] * t;
        }
        else
            for (int i = i1; i < i2; ++i) m = std::max (m, work[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels (m / (kSize * 0.25f), -72.0f);
        // gentle upward tilt so the highs show, then -60..-6 dB fills the strip
        const float tilt = 3.0f * (float) std::log2 (std::max (1.0, f1 / 200.0));
        const float v = juce::jlimit (0.0f, 1.0f, (db + tilt + 60.0f) / 54.0f);
        bands[b] = std::max (v, bands[b] * 0.86f);   // (up at once, down smoothly)
        if (bands[b] >= peaks[b]) { peaks[b] = bands[b]; peakHold[b] = 18; }
        else if (--peakHold[b] < 0) peaks[b] = std::max (0.0f, peaks[b] - 0.03f);
    }
}

void Spectrum::setInstrument (int ins)
{
    if (ins == onlyInstrument) return;
    auto& tap = proc.player.insTap;
    if (onlyInstrument >= 0 && tap.source.load() == onlyInstrument) tap.source.store (-1);
    onlyInstrument = ins;
    if (ins >= 0) tap.source.store (ins);
    lastTapCount = 0;
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (std::begin (bands), std::end (bands), 0.0f);
    std::fill (std::begin (peaks), std::end (peaks), 0.0f);
    repaint();
}

void Spectrum::tick()
{
    const int got = proc.readScope (incoming.data(), (int) incoming.size());   // (always drained)
    if (onlyInstrument >= 0)
    {
        auto& tap = proc.player.insTap;
        tap.source.store (onlyInstrument);
        const uint32_t c = tap.count();
        if (c == lastTapCount) { repaint(); return; }   // (stopped: it stays as it was)
        lastTapCount = c;
        const int n = tap.read (incoming.data(), kSize);
        feed (incoming.data(), n);
    }
    else feed (incoming.data(), got);
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
    // a faint grid: 100 Hz, 1 kHz, 10 kHz stronger, the steps between them lighter; levels across
    {
        const double rate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const double lo = 40.0, hi = std::min (16000.0, rate * 0.45);
        auto xOf = [&] (double hz) { return inner.getX() + inner.getWidth() * (float) (std::log (hz / lo) / std::log (hi / lo)); };
        for (double hz : { 50.0, 200.0, 500.0, 2000.0, 5000.0 })
        { g.setColour (sk.rule.withAlpha (0.35f)); g.fillRect (xOf (hz), inner.getY(), 1.0f, inner.getHeight()); }
        for (double hz : { 100.0, 1000.0, 10000.0 })
        { g.setColour (sk.rule.withAlpha (0.7f)); g.fillRect (xOf (hz), inner.getY(), 1.0f, inner.getHeight()); }
        for (int k = 1; k < 4; ++k)
        { g.setColour (sk.rule.withAlpha (0.25f)); g.fillRect (inner.getX(), inner.getY() + inner.getHeight() * (float) k / 4.0f, inner.getWidth(), 1.0f); }
        if (inner.getHeight() >= PixelFont::textHeight (px) * 4)
            for (auto [hz, t] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1K" }, { 10000.0, "10K" } })
                PixelFont::draw (g, t, xOf (hz) + px * 2, inner.getBottom() - PixelFont::textHeight (px) - px, px, sk.dim);
    }
    // the analyser: a smooth line, the fill fading under it, the peak hold above (in the spectrum's colour, or RAINBOW)
    {
        const std::vector<float> lv (std::begin (bands), std::end (bands)), pk (std::begin (peaks), std::end (peaks));
        drawAnalyser (g, inner, lv, pk, [] (float x01) { return meters::spectrum (juce::roundToInt (x01 * (kBands - 1)), kBands); });
    }
    g.setColour (sk.dim);
    g.fillRect (inner.getX(), inner.getBottom(), inner.getWidth(), 1.0f);
    if (overlay) overlay (g, inner);
    if (onlyInstrument >= 0)
    {
        const juce::String t = "INSTRUMENT " + juce::String::toHexString (onlyInstrument + 1).toUpperCase().paddedLeft ('0', 2) + " ONLY";
        auto box = juce::Rectangle<float> (PixelFont::textWidth (t, px) + px * 4, px * 10).withPosition (inner.getX(), inner.getY());
        g.setColour (sk.bg);
        g.fillRect (box);
        PixelFont::draw (g, t, box.getX() + px * 2, box.getY() + px * 1.5f, px, sk.text);
    }
    Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f));
}

void OutputMeter::tick()
{
    auto upd = [] (float in, float& v, float& hold, int& count)
    {
        v = std::max (in, v * 0.922f);
        if (v >= hold) { hold = v; count = 40; } else if (--count < 0) hold = std::max (0.0f, hold - 0.01f);
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
        g.setColour (toFrac (v) > 0.92f ? sk.hot : meters::vu (3));
        g.fillRect (bar.withWidth (bar.getWidth() * toFrac (v)));
        g.setColour (sk.text);
        g.fillRect (bar.getX() + bar.getWidth() * toFrac (hold) - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        Theme::bevel (g, bar, false, 1.0f);
    }
}
void Spectrum::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onColourMenu) onColourMenu();
}

void OutputMeter::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onColourMenu) onColourMenu();
}
} // namespace prv::ui
