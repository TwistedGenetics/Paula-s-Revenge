// Paula's Revenge - bouncing a highlighted part of the pattern to audio (plain C++)
#pragma once

#include "Player.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prv
{
struct BounceRange { int block = 0, line0 = 0, line1 = 0, track0 = 0, track1 = 0; bool withFx = false; };

struct BounceResult
{
    std::vector<float> left, right;   // the audio (at the sample rate asked for)
    std::vector<int> instruments;     // the instruments played in the range (0-based) that are in the audio
    std::vector<int> leftOut;         // MIDI-out instruments in the range: hardware, so not in the audio
};

// the instruments whose notes are in the range (a note without an instrument number counts
// as the one last set on its track, earlier in the block)
std::vector<int> instrumentsInRange (const Song&, const BounceRange&);

// Plays just those lines of those tracks (from the range's first line, at the tempo and speed
// set there) and records them. Dry: the tracks before their mixer strips. With FX: after their
// strips (EQ, inserts, fader) plus what they send to SEND A / B through the returns (the master's
// inserts and the machine's output stage aren't in it). After the last line, plug-in notes and
// looped samples are let go and it records on until quiet (-80 dB for 0.25 s, at most 10 s).
bool bounceRange (const Song&, const PaulaOptions&, double sampleRate, const BounceRange&, BounceResult&,
                  std::function<std::unique_ptr<PluginSynth> (int, const Instrument&, double)> synthFactory,
                  std::string& error,
                  std::function<std::unique_ptr<Effect> (int, const FxSlot&, double)> pluginFactory = nullptr);
} // namespace prv
