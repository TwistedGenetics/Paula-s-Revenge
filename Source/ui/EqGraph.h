// Paula's Revenge - the channel EQ's graph: points you grab and move, double-click to add one,
// right-click for its type / RESET GAIN / REMOVE, and the channel's live spectrum behind the curve
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class EqGraph : public juce::Component
{
public:
    explicit EqGraph (PaulaProcessor&);
    ~EqGraph() override;

    int  track = 0;                 // the channel it edits
    int  selected = -1;             // the selected point (-1 = none)
    bool showSpectrum = true;
    std::function<void()> onChanged;                        // the EQ changed (controls follow)
    std::function<void (const juce::String&)> onStatus;

    void setTrack (int t);
    void tick();                    // ~30 a second: the spectrum
    void selectFirst();             // the first point (or none)

    // the points (also used by tests)
    int  addPointAt (juce::Point<float> where);             // as a double-click there; -1 when full
    void movePoint (int band, float hz, float db, bool merge);
    void setQ (int band, float q, bool merge);
    void setType (int band, EqType);
    void resetGain (int band);
    void removePoint (int band);
    void setOn (int band, bool on);
    juce::Point<float> pointPos (int band) const;           // where a point is drawn
    int  hitPoint (juce::Point<float>) const;               // the point there (-1 = none)
    float xToHz (float x) const;
    float yToDb (float y) const;
    float hzToX (double hz) const;
    float dbToY (double db) const;
    bool isDragging() const { return dragBand >= 0; }
    const std::vector<float>& spectrumColumns() const { return cols; }   // (tests)
    void feedSpectrum (const float* samples, int n);        // tests: as if the channel played this

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    static juce::String hzText (float hz);
    static juce::String dbText (float db);
    juce::String pointText (int band) const;    // "1.20 KHZ  +3.5 DB  Q 1.00"

private:
    PaulaProcessor& proc;
    juce::Rectangle<float> plot;    // inside the frame
    int dragBand = -1, hover = -1;
    bool dragMoved = false;
    float wheelAccum = 0;
    std::vector<float> cols;        // spectrum: dB per pixel column (smoothed)
    std::vector<float> tapBuf = std::vector<float> (4096, 0.0f), db;
    uint32_t lastCount = 0;

    ChannelEq& eq();
    const ChannelEq& eq() const;
    juce::String label (int band, const juce::String& what) const;
    void edited();
    void analyse (const float* samples, int n);
    void showPointMenu (int band);
};
} // namespace prv::ui
