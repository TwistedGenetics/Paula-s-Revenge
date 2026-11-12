// Paula's Revenge - the AKAI S1000 / S950 editor (a MIDI-out instrument with GEAR: AKAI S1000 or AKAI S950). GET PROGRAMS asks the
// S1000 for its program names; picking one reads its common settings and every keygroup; then any setting of the
// program or a keygroup (filter, both envelopes, tuning, the four velocity zones...) is a bar by name. The S1000
// only takes whole blocks, so a change sends the program's (or the keygroup's) block back, at most every 80 ms.
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../engine/AkaiSysex.h"

namespace prv::ui
{
class AkaiPanel : public juce::Component
{
public:
    explicit AkaiPanel (PaulaProcessor&);
    std::function<void (const juce::String&)> onStatus;   // (the page's info line)

    void setInstrument (int i) { instrument = i; }
    void setModel (bool isS950);           // the S1000, or the S950 (S900) - which speak differently
    bool isS950() const { return s950; }
    void opened();
    void tick();                          // (the page's timer: replies, sends, time-outs)
    void resized() override;
    void paint (juce::Graphics&) override;

    // (also for tests)
    void getPrograms();
    void pickProgram (int index);
    void editKeygroups (bool kg);         // the PROGRAM's settings, or a KEYGROUP's
    void showPart (int part);
    void setKeygroup (int k);             // 0-based
    int  partCount() const { return (int) parts().size(); }
    int  rowCount() const { return (int) bars.size(); }
    juce::String rowName (int r) const { return r >= 0 && r < (int) rowFields.size() ? juce::String (rowFields[(size_t) r].name) : juce::String(); }
    void moveRow (int r, float v01) { if (r >= 0 && r < (int) bars.size()) bars[(size_t) r]->setValue (v01, true); }
    int  rowValue (int r) const;
    void received (const uint8_t* msg, int n);   // an S1000 message
    const juce::StringArray& programNames() const { return names; }
    int  keygroupCount() const { return (int) kgBlocks.size(); }
    bool ready() const { return progBlock.size() == (size_t) (s950 ? akai::s900::kProgramBytes : akai::kProgramBytes) && pending.empty(); }
    juce::String status() const { return statusText; }
    PixButton getBtn { "GET PROGRAMS" }, programBtn { "PROGRAM: -" }, progModeBtn { "PROGRAM", true }, kgModeBtn { "KEYGROUP", true };
    // SAMPLES OVER MIDI (v0.74): one of this song's samples into the sampler, or one of the sampler's into a free
    // instrument here
    PixButton sendSampleBtn { "SEND A SAMPLE..." }, getSampleBtn { "GET A SAMPLE..." };
    void sendSample (int songInstrument);      // (also for tests)
    void getSampleList();                      // then the menu of its samples
    void getSample (int index);                // (an index into sampleList())
    const juce::StringArray& sampleList() const { return sampleNames; }
    const akai::Transfer& transferState() const { return transfer; }
    int gotInto = -1;                          // (the instrument the last sample went into)
    NumberBox kgBox { "KG", 1, 1, 1 };

private:
    PaulaProcessor& proc;
    int instrument = 0;
    juce::StringArray names;
    int program = -1;
    bool s950 = false;
    std::vector<int> numbers;              // (the S950: each listed program's own number)
    std::vector<uint8_t> progBlock;
    std::vector<std::vector<uint8_t>> kgBlocks;
    int keygroup = 0;
    bool kgMode = false;
    int part = 0;
    std::vector<std::unique_ptr<PixButton>> partBtns;
    std::vector<std::unique_ptr<PixSlider>> bars;
    std::vector<akai::Field> rowFields;
    std::vector<std::vector<uint8_t>> pending;   // requests still to send (one at a time: each waits for its answer)
    bool waiting = false;
    double askedAt = 0;
    bool dirtyProg = false, dirtyKg = false;
    double lastSend = 0;
    juce::String statusText;
    akai::Transfer transfer;
    bool transferring = false, pickAfterList = false;
    double lastProgress = 0;
    juce::StringArray sampleNames;
    std::vector<int> sampleNumbers;
    void transferTick (double now);
    void transferDone();
    void updateSampleButtons();

    const std::vector<akai::Field>& fields() const
    {
        if (s950) return kgMode ? akai::s900::keygroupFields() : akai::s900::programFields();
        return kgMode ? akai::keygroupFields() : akai::programFields();
    }
    juce::String model() const { return s950 ? "S950" : "S1000"; }
    std::vector<std::string> parts() const { return akai::groupsOf (fields()); }
    int cc();
    void send (const std::vector<uint8_t>& msg);
    void ask (std::vector<uint8_t> msg);
    void askNext();
    void build();
    void refreshBars();
    void setStatus (const juce::String&);
};
} // namespace prv::ui
