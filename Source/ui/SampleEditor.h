// Paula's Revenge - sample (instrument waveform) editor
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "../engine/TimeStretch.h"

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
    void mouseMove (const juce::MouseEvent&) override;
    bool keyStateChanged (bool) override;
    void focusLost (FocusChangeType) override;
    void visibilityChanged() override;

    // editing operations (also used by tests)
    enum class Op { Cut, Copy, Paste, Delete, Crop, Silence, Reverse, Normalise, FadeIn, FadeOut,
                    GainUp, GainDown, LoopToSel, ToMono };
    void apply (Op);
    void undo();
    void redo();
    void selectAll();
    void setSelection (int a, int b) { selA = a; selB = b; updateJamRange(); repaint(); }
    int selectionStart() const { return selA; }
    int selectionEnd() const { return selB; }
    void play (bool selectionOnly);
    void stop();

    // rate, resampling and time stretch (also used by tests)
    void setRate (double hz);
    void resampleTo (double hz, bool raw);
    std::vector<std::vector<float>> stretchedRegion (const StretchSettings&) const;   // selection or whole sample
    void applyStretch (const StretchSettings&);
    void previewStretch (const StretchSettings&);
    void showStretch (bool show);
    bool isStretchOpen() const { return stretchOpen; }

    // play the edited sample from the keyboard (also used by tests)
    void keyNoteOn (int note);
    void keyNoteOff (int note);
    void setSelKeys (bool on);
    void updateJamRange();

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
    bool pendingInside = false;       // clicked inside the selection, not dragged yet
    int  downSample = 0;
    std::map<int, int> heldKeys;      // key code -> note
    int  lastNote = 0;
    bool stretchOpen = false;

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
              undoBtn { "UNDO" }, redoBtn { "REDO" }, closeBtn { "CLOSE" },
              selKeysBtn { "SEL KEYS", true }, stretchBtn { "STRETCH" },
              presetBtn { "PRESET" }, rawBtn { "RAW", true }, resampleBtn { "RESAMPLE" };
    NumberBox rateBox { "RATE", 500, 96000, 16574 }, toBox { "TO", 500, 96000, 11025 };

    // time-stretch panel (replaces the button rows while open)
    PixButton stClassic { "CLASSIC", true }, stRevised { "REVISED", true }, stHard { "HARD", true }, stFit { "FIT TO LINES" },
              stPreview { "PREVIEW" }, stApply { "APPLY" }, stCancel { "CANCEL" };
    NumberBox stTime { "TIME %", 25, 2000, 100 }, stCycle { "CYCLE", 20, 2000, 1000 }, stLines { "LINES", 1, 256, 16 },
              stSemi { "SEMI", -36, 36, 0 }, stCents { "CENTS", -99, 99, 0 };
    StretchSettings stretchSettings() const;
    std::vector<juce::Component*> mainControls();
    std::vector<juce::Component*> stretchControls();
    juce::Rectangle<float> stretchInfoArea;

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
