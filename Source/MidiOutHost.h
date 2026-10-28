// Paula's Revenge - MIDI outputs for MIDI-out instruments (hardware synths, the MPC, ...)
#pragma once

#include <JuceHeader.h>
#include "engine/MidiOut.h"
#include <map>

namespace prv
{
class MidiOutHost
{
public:
    MidiOutHost() = default;

    // the MIDI outputs on this computer, by name (tests can set fakeDevices instead)
    juce::StringArray deviceNames() const;
    juce::StringArray fakeDevices;
    bool useFakeDevices = false;

    // a MIDI-out instrument's synth (message thread); null when its device isn't here
    std::unique_ptr<PluginSynth> createSynth (const Instrument&, double sampleRate);
    juce::String lastError (const std::string& device) const;

    // tests: where the messages go instead of a real device
    std::function<std::unique_ptr<MidiSink> (const juce::String& device)> sinkFactory;

    // how far ahead of being heard the audio is made (one audio block), so MIDI lines up
    std::atomic<double> outputLatencyMs { 0.0 };

private:
    std::map<juce::String, std::weak_ptr<juce::MidiOutput>> openOutputs;   // shared by instruments on one device
    std::map<std::string, juce::String> errors;
    std::shared_ptr<juce::MidiOutput> openDevice (const juce::String& name);
};
} // namespace prv
