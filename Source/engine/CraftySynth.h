// Paula's Revenge - CRAFTY (v1.01): Twisted Genetics' Crafty Synth (TwistedGenetics/Crafty-Synth), built in - the
// wavetable monosynth after the Modal CRAFTsynth 2.0. Its own engine (third_party/crafty: the FM-1's synth, arp and
// tables) plays it at 44.1 kHz, resampled to the song's rate. The page: every sound control (0..127, as the
// CRAFTsynth's knobs, written its way), over one of its 64 factory presets (PRESET), which brings the mod matrix.
#pragma once

#include "PluginSynth.h"
#include <memory>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kCraftyKey = "SYN:CRAFTY";

const std::vector<SynthParam>& craftyParams();      // [0] PRESET, then the controls
int  craftyPresetCount();
const char* craftyPresetName (int i);
std::vector<float> craftyPreset (int i);

class CraftySynth : public PluginSynth
{
public:
    CraftySynth();
    ~CraftySynth() override;
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
    bool sounding() const;                           // (tests)

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace prv
