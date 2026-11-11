// Paula's Revenge - LIVE MODE: play the song live. 16 big PADS launch blocks (in time, at the next bar, beat or
// line - each loops until another is launched) or play the instrument's slices; held FX on the whole mix (REPEAT,
// STUTTER, TAPE STOP, FILTER); and REC ARRANGE writes the blocks you launched, in order, into the song's sequence.
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../engine/LiveFx.h"

namespace prv::ui
{
class LivePage : public juce::Component
{
public:
    explicit LivePage (PaulaProcessor&);
    std::function<void()> onClose, onChanged;
    std::function<void (const juce::String&)> onStatus;

    void opened();
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;

    // (also for tests)
    enum PadMode { Blocks = 0, Slices };
    PadMode padMode = Blocks;
    int quant = 0;                         // 0 BAR (16 lines), 1 BEAT (4), 2 LINE, 3 NOW
    void padDown (int pad);
    void padUp (int pad);
    void fxDown (int k);                   // LiveFx::Kind
    void fxUp (int k);
    void setRecording (bool on);
    bool recording() const { return recOn; }
    const std::vector<int>& recorded() const { return recBlocks; }
    juce::Rectangle<float> padRect (int pad) const;
    int  padBlock (int pad) const;         // BLOCKS: the block a pad launches (-1: none)
    int  padNote (int pad) const;          // SLICES: the note it plays

    PixButton closeBtn { "CLOSE" }, blocksBtn { "BLOCKS", true }, slicesBtn { "SLICES", true }, quantBtn { "AT: NEXT BAR" },
              recBtn { "REC ARRANGE", true }, visualsBtn { "VISUALS" }, playBtn { "PLAY" }, stopBtn { "STOP" }, pageDown { "-" }, pageUp { "+" };
    static constexpr int kFx = 7;
    PixButton fxBtn[kFx] { PixButton ("REPEAT 1/4"), PixButton ("REPEAT 1/8"), PixButton ("REPEAT 1/16"), PixButton ("STUTTER 1/32"),
                           PixButton ("TAPE STOP"), PixButton ("FILTER DOWN"), PixButton ("FILTER UP") };
    int padPage = 0;                       // BLOCKS: pads show blocks 16 x padPage onwards

private:
    PaulaProcessor& proc;
    juce::Rectangle<float> titleArea, padArea, fxLabel, pageArea, hintArea;
    int downPad = -1, downNote = -1;
    std::map<int, int> keyPads, keyFx;     // key code -> pad / effect held on it
    bool recOn = false;
    std::vector<int> recBlocks;
    int lastBlock = -1, lastLine = -1;
    int blink = 0;
    void status (const juce::String& s) { if (onStatus) onStatus (s); }
    int padForKey (int keyCode) const;
    int fxForKey (int keyCode) const;
    void refreshButtons();
};
} // namespace prv::ui
