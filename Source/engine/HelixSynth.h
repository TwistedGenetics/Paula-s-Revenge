// Paula's Revenge - HELIX (v1.01): Twisted Genetics' wavetable synth (TwistedGenetics/Twisted-Helix), built in.
// Its own engine (helix-core, in third_party) plays it; the page shows the controls that matter most - the two
// wavetable oscillators, sub and noise, both filters, the amp and filter envelopes, two LFOs and the eight macros -
// over one of its 54 factory presets (PRESET), which brings everything else: the mod matrix, the effects, the tables.
#pragma once

#include "PluginSynth.h"
#include <memory>
#include <string>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kHelixKey = "SYN:HELIX";
// (v1.01) the instrument's words (builtinText) naming OSC A / B's own wavetable: "path:<a WAV>" or "dir:<a folder of
// single-cycle WAVs>"; none = the built-in table its TABLE control picks
constexpr const char* kHelixTableA = "helix.tableA";
constexpr const char* kHelixTableB = "helix.tableB";

const std::vector<SynthParam>& helixParams();       // [0] PRESET (the factory preset under it), then the controls
int  helixPresetCount();
const char* helixPresetName (int i);
std::vector<float> helixPreset (int i);              // that factory preset, as the page's values

// (v1.01) a wavetable for drawing: "builtin:<name>", "path:<wav>" or "dir:<folder>" - up to maxFrames frames spread
// across it, each points samples long. Empty when it can't be read. Slow (it builds the table): not on the audio thread.
struct HelixTablePreview { int totalFrames = 0; std::vector<std::vector<float>> frames; };
HelixTablePreview helixTablePreview (const std::string& id, int maxFrames, int points);
const std::vector<std::string>& helixBuiltinTables();   // the built-in tables' names, as TABLE lists them

class HelixSynth : public PluginSynth
{
public:
    HelixSynth();
    ~HelixSynth() override;
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int midiNote, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int cc, int value, int offset) override;
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void updateSong (const Song&) override;
    void restoreParameter (int index) override;
    void setValues (const std::vector<float>& v);   // (tests)
    int  activeVoices() const;                       // (tests)
    void waitTables();                               // (tests: the tables loaded)

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace prv
