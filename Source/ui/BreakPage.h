// Paula's Revenge - the BREAK page: a break, start to finish, in one place. Its waveform with the
// slices (each marked KICK / SNARE / HAT), slicing, BARS, MAKE BLOCK / CHOP HERE, and one-click
// re-edits and rebuilds of the chopped break - and 4 VARIATIONS into new blocks of the song.
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../engine/BreakTools.h"

namespace prv::ui
{
class BreakPage : public juce::Component
{
public:
    explicit BreakPage (PaulaProcessor&);
    std::function<void()> onClose, onChanged;
    std::function<void (const juce::String&)> onStatus;

    void opened();
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // (also for tests)
    int  instrument() const;
    int  bars = 1;                       // how many bars the break is (16 lines a bar)
    int  track() const { return proc.editorState.track; }
    const std::vector<HitKind>& kinds();
    void sliceAuto();
    void sliceEven();
    int  makeBlock();                    // a new block after the current position, the break chopped into it; its number
    int  chopHere();
    void reEdit (int which);             // 0 SHUFFLE, 1 GHOST ROLL, 2 HALF-TIME, 3 DOUBLE-TIME
    void rebuildAs (int i);              // CLASSIC '94, ROLLER, TWO-STEP, HALF-STEP
    int  variations();                   // 4 new blocks after the current one; how many
    void setKind (int slice, HitKind);   // (right-click a slice)
    juce::Rectangle<float> waveRect() const { return waveArea; }
    int  sliceAt (float x) const;
    // THE MARKERS (where each slice starts): drag one to move it, double-click the wave to add one,
    // double-click / Alt-click one (or pick it and press Delete) to take it away. The first stays at 0.
    int  markerAt (float x) const;       // the marker under x (within a few pixels; -1 = none)
    float markerX (int i) const;         // where marker i is drawn
    int  addMarker (int samplePos);      // its index (snapped to a zero crossing); -1 = not added
    bool deleteMarker (int i);
    bool moveMarker (int i, int samplePos);
    int  selectedMarker = -1;

    PixButton closeBtn { "CLOSE" }, insDown { "-" }, insUp { "+" };
    PixButton autoBtn { "AUTO" }, evenBtn { "EVEN 16" }, sensDown { "-" }, sensUp { "+" }, barsDown { "-" }, barsUp { "+" };
    PixButton trackDown { "-" }, trackUp { "+" }, makeBtn { "MAKE BLOCK" }, chopBtn { "CHOP HERE" }, playBtn { "PLAY" }, stopBtn { "STOP" };
    PixButton fitBtn { "FIT TO TEMPO", true }, keepBtn { "KEEP PITCH", true };
    PixButton shuffleBtn { "SHUFFLE" }, ghostBtn { "GHOST ROLL" }, halfBtn { "HALF-TIME" }, doubleBtn { "DOUBLE-TIME" };
    PixButton rebuildBtns[kRebuilds] { PixButton { "CLASSIC '94" }, PixButton { "ROLLER" }, PixButton { "TWO-STEP" }, PixButton { "HALF-STEP" } };
    PixButton variationsBtn { "4 VARIATIONS" }, fillBtn { "FILL", true };

private:
    PaulaProcessor& proc;
    int sens = 50;
    uint32_t shuffleSeed = 1;
    std::vector<HitKind> kindCache;
    std::vector<int> kindSlices;         // (the slices they were worked out for)
    int kindIns = -1;
    juce::Rectangle<float> titleArea, insArea, sliceLabel, sensArea, barsArea, waveArea, trackArea, reLabel, rbLabel, gridArea, gridTitle;
    void status (const juce::String& s) { if (onStatus) onStatus (s); }
    void changed() { repaint(); if (onChanged) onChanged(); }
    bool ensureSlices (int ins);
    int  dragMarker = -1;                // a marker being dragged
    bool dragMoved = false;
    juce::Rectangle<float> waveInner() const;   // where the wave is drawn (above the tabs)
    double xToSample (float x) const;
    void markersChanged (const juce::String& what);
    int  partLines() const;              // the lines the re-edits work on: the current block's
    void paintWave (juce::Graphics&);
    void paintGrid (juce::Graphics&);
    juce::String insName (int i) const;
};
} // namespace prv::ui
