// Paula's Revenge - a plug-in instrument as the player sees it (plain C++)
#pragma once

#include "Song.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prv
{
// Events are timed by a sample offset into the next render() call.
class PluginSynth
{
public:
    virtual ~PluginSynth() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;   // may allocate
    virtual void noteOn (int midiNote, int velocity, int offset) = 0;
    virtual void noteOff (int midiNote, int offset) = 0;
    virtual void allNotesOff (int offset) = 0;
    virtual void pitchBend (int value14, int offset) = 0;          // 0..16383, 8192 = centre
    virtual void controller (int cc, int value, int offset) = 0;
    virtual void setParameter (int index, float value01) = 0;
    virtual void render (float* left, float* right, int numSamples) = 0;   // replaces the buffers
};

// The plug-in instruments of a song, one per instrument slot (made outside the audio lock)
class SynthRack
{
public:
    std::function<std::unique_ptr<PluginSynth> (int instrument, const Instrument&, double sampleRate)> factory;

    struct Pending { int instrument; std::string key; std::unique_ptr<PluginSynth> synth; };
    std::vector<Pending> plan (const Song&, double sampleRate, bool reloadAll = false) const;
    void apply (std::vector<Pending>&, std::vector<std::unique_ptr<PluginSynth>>& old);
    void syncNow (const Song& s, double sr) { std::vector<std::unique_ptr<PluginSynth>> old; auto p = plan (s, sr); apply (p, old); }
    void invalidate (int i) { if (i >= 0 && i < kMaxInstruments) keys[i] = "\x01"; }

    PluginSynth* at (int i) const { return i >= 0 && i < kMaxInstruments ? synths[i].get() : nullptr; }
    bool any() const { return count > 0; }
    void prepare (double sampleRate, int maxBlock) { for (auto& s : synths) if (s) s->prepare (sampleRate, maxBlock); }

private:
    std::unique_ptr<PluginSynth> synths[kMaxInstruments];
    std::string keys[kMaxInstruments];
    bool tried[kMaxInstruments] {};
    int count = 0;
};
} // namespace prv
