// Paula's Revenge - sample (instrument waveform) editor
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class SampleEditor : public juce::Component
{
public:
    explicit SampleEditor (PaulaProcessor&);

    std::function<void()> onChanged;   // instrument data changed
    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;

    void tick();                       // ~30 Hz: playhead
    void instrumentSelected();         // the current instrument changed
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // editing operations (also used by tests)
    enum class Op { Cut, Copy, Paste, Delete, Crop, Silence, Reverse, Normalise, FadeIn, FadeOut,
                    GainUp, GainDown, LoopToSel, ToMono };
    void apply (Op);
    void undo();
    void redo();
    void selectAll();
    void setSelection (int a, int b) { selA = a; selB = b; repaint(); }
    void play (bool selectionOnly);
    void stop();

private:
    PaulaProcessor& proc;
    int instrIndex = -1;

    double viewStart = 0, viewLen = 1;
    int selA = 0, selB = 0;           // selB > selA means a selection
    int cursor = 0;
    int dragAnchor = 0;
    bool dragging = false;
    bool drawMode = false;
    bool previewing = false;
    int lastDrawSample = -1, lastDrawValue = 0, drawLane = 0;
    int dragOverview = 0;

    std::vector<Instrument> undoStack, redoStack;
    struct Clip { std::vector<int16_t> left, right; bool empty() const { return left.empty(); } };
    static Clip clipboard;

    PixButton playBtn { "PLAY" }, playSelBtn { juce::String::charToString ((juce::juce_wchar) 1) + " SEL" }, loopBtn { "LOOP", true }, stopBtn { "STOP" },
              zoomInBtn { "ZOOM +" }, zoomOutBtn { "ZOOM -" }, showAllBtn { "FIT ALL" }, zoomSelBtn { "FIT SEL" },
              selAllBtn { "SEL ALL" }, drawBtn { "DRAW", true },
              cutBtn { "CUT" }, copyBtn { "COPY" }, pasteBtn { "PASTE" }, deleteBtn { "DELETE" }, cropBtn { "CROP" },
              silenceBtn { "SILENCE" }, reverseBtn { "REVERSE" }, normBtn { "MAX VOL" },
              fadeInBtn { "FADE IN" }, fadeOutBtn { "FADE OUT" },
              gainDownBtn { "VOL -" }, gainUpBtn { "VOL +" }, loopSelBtn { "SET LOOP" }, monoBtn { "TO MONO" },
              undoBtn { "UNDO" }, redoBtn { "REDO" }, closeBtn { "CLOSE" };

    juce::Rectangle<float> headerArea, waveArea, overviewArea;

    const Instrument& inst() const;
    int length() const;
    bool hasSel() const { return selB > selA; }
    void clampView();
    void zoom (double factor, double aroundSample);
    double xToSample (float x) const;
    float sampleToX (double s) const;
    void commit (Instrument&& edited, const juce::String& what);
    void pushUndo();
    void drawAt (const juce::MouseEvent&);
    void paintLane (juce::Graphics&, juce::Rectangle<float> lane, const std::vector<int16_t>& data, juce::Colour col);
    juce::String fmt (int samples) const;
};
} // namespace prv::ui
