#pragma once
/*
    The four LCD tabs:
      SAMPLE  waveform + slice badges, then the SLICE / ALL table
      SEQ     PATTERN / ORIGINAL, bars, pages, the KICK..PERC lanes, the FX row + the selected step's FX, memories
      FX      post-FX detail and every pad's setting
      SET     tempo, trigger, sound character, slicing, generate, MIDI
      AKAI    the Akaizer: AKAIZE render settings, and the cycle of the live AKAIZE pad and slice STRETCH
    Each is transparent and drawn on the LCD glass by the editor.
*/
#include "ChopUI/Widgets.h"
#include "ChoppageProcessor.h"
#include "ChopUI/ParamIds.h"

namespace ui
{
    // ---- A page of LCD rows bound to parameters ----------------------------------------------
    class ParamPage : public juce::Component
    {
    public:
        explicit ParamPage (ChoppageProcessor&);

        void addRow (const juce::String& group, const juce::String& title);
        void addText (const juce::String& t, int width = 0);
        void addValue (const char* paramId, const juce::String& caption, int width = 150, float fineStep = 0.01f);
        void addChoice (const char* paramId, juce::StringArray labels = {});
        void addBool (const char* paramId, const juce::String& on, const juce::String& off);
        void addAction (const juce::String& text, std::function<void()> fn, std::function<bool()> isOn = nullptr, int width = 0);
        /** A value that isn't a parameter: its text, drag / scroll (steps, fine), double-click. */
        void addCustom (std::function<juce::String()> text, std::function<void (float, bool)> drag, std::function<void()> reset, int width);
        void addRule();    // a divider line above the next row

        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh();

        static constexpr float kFont = 25.0f;
        int rowHeight = 38;
        int titleW = 170;    // 0: no title column

    protected:
        ChoppageProcessor& proc;
        Param param (const char* id) const { return { proc.apvts.getParameter (id) }; }

    private:
        struct Item { std::unique_ptr<LcdCell> cell; int width; };
        struct Row { juce::String group, title; std::vector<Item> items; bool ruleAbove = false; };
        std::vector<Row> rows;
        bool nextRule = false;
        static int textWidth (const juce::String& t) { return (int) (t.length() * kFont * 0.62f) + 18; }
    };

    // ---- SAMPLE -------------------------------------------------------------------------------------
    class WaveView final : public juce::Component
    {
    public:
        explicit WaveView (ChoppageProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        static constexpr int badgeRowH = 34;

        // ---- zoom: the part of the break shown (samples). Wheel = zoom round the mouse, shift / sideways wheel = scroll.
        void zoomBy (double factor, int anchorX = -1);   // < 1 in, > 1 out
        void scrollBy (double fractionOfView);          // - left, + right
        void zoomSlice();                               // the selected slice, with a little either side
        void zoomAll();
        bool zoomed() const;
        juce::String viewText() const;                  // "0.62 - 0.71 S  x12"
        std::function<void()> onViewChanged;
    private:
        int numSamples() const;
        bool followBreak();                              // a new break or file: back to the whole break (true if reset)
        void setView (double start, double length);
        double viewStart = 0.0, viewLen = 0.0;          // viewLen 0 = the whole break
        int viewSlot = -1, viewN = -1;                   // a new break or file shows it all again
        int xForSample (int s) const;
        int sampleForX (int x) const;
        int sliceAtX (int x) const;
        int markerNear (int x) const;
        void popup (int slice, int marker);
        ChoppageProcessor& proc;
        int dragMarker = -1, dragX = -1;
    };

    class SampleTab final : public juce::Component
    {
    public:
        explicit SampleTab (ChoppageProcessor&);
        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh();
    private:
        ChoppageProcessor& proc;
        WaveView wave;
        LcdCell sliceRow, allRow, viewInfo;
        std::vector<std::unique_ptr<LcdCell>> zoomKeys;
        std::vector<std::unique_ptr<LcdCell>> headers, cells;
    };

    // ---- SEQ ------------------------------------------------------------------------------------------
    class SeqGrid final : public juce::Component
    {
    public:
        explicit SeqGrid (ChoppageProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        int page = 0;
        int selStep = 0;                   // the step STEP FX / RATE edit (a click on any cell or the FX row picks it)
        std::function<void()> onSelect;
        static constexpr int laneW = 112;
        static constexpr int kLanes = 7;   // KICK SNARE GHOST HAT OPEN PERC, then the FX row
    private:
        int visibleSteps() const;
        juce::Rectangle<int> cell (int lane, int column) const;
        ChoppageProcessor& proc;
    };

    class SeqTab final : public juce::Component
    {
    public:
        explicit SeqTab (ChoppageProcessor&);
        void resized() override;
        void refresh();
    private:
        ChoppageProcessor& proc;
        SeqGrid grid;
        std::vector<std::unique_ptr<LcdCell>> tools, memories, stepFx;
        LcdCell memLabel, help;
    };

    // ---- DISK: the break browser, sorted by BPM -------------------------------------------------------
    class DiskTab final : public juce::Component
    {
    public:
        explicit DiskTab (ChoppageProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void refresh();
        std::function<void()> onChooseFolder, onSavePreset, onLoadPreset;
    private:
        int visibleRows() const;
        juce::Rectangle<int> listArea() const;
        ChoppageProcessor& proc;
        std::vector<bc::Session::BrowserEntry> entries;
        uint32_t seen = 0;
        int scroll = 0;
        static constexpr int rowH = 34;
        LcdCell folderCell, rescanCell, upCell, downCell, pathCell, saveCell, loadCell;
    };

    // ---- FX / SET -----------------------------------------------------------------------------------
    class FxTab final : public ParamPage { public: explicit FxTab (ChoppageProcessor&); };
    class SetTab final : public ParamPage { public: explicit SetTab (ChoppageProcessor&); };
    class AkaiTab final : public ParamPage { public: explicit AkaiTab (ChoppageProcessor&); };
}
