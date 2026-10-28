// Paula's Revenge - MIDI outputs for MIDI-out instruments
#include "MidiOutHost.h"

namespace prv
{
namespace
{
    // sends a block's messages through the device's own thread, at the time the audio is heard
    class DeviceSink : public MidiSink
    {
    public:
        DeviceSink (std::shared_ptr<juce::MidiOutput> o, MidiOutHost& h) : out (std::move (o)), host (h) { buffer.ensureSize (8192); }
        void send (const uint8_t* data, int length, int offset) override { buffer.addEvent (data, length, offset); }
        void flush (int, double sampleRate, double delayMs) override
        {
            if (buffer.isEmpty()) return;
            out->sendBlockOfMessages (buffer, juce::Time::getMillisecondCounterHiRes() + host.outputLatencyMs.load() + delayMs, sampleRate);
            buffer.clear();
        }
        void sendNow (const uint8_t* data, int length) override { out->sendMessageNow (juce::MidiMessage (data, length)); }

    private:
        std::shared_ptr<juce::MidiOutput> out;
        MidiOutHost& host;
        juce::MidiBuffer buffer;
    };
}

juce::StringArray MidiOutHost::deviceNames() const
{
    if (useFakeDevices) return fakeDevices;
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices()) names.add (d.name);
    return names;
}

std::shared_ptr<juce::MidiOutput> MidiOutHost::openDevice (const juce::String& name)
{
    if (auto it = openOutputs.find (name); it != openOutputs.end())
        if (auto o = it->second.lock()) return o;
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == name)
        {
            std::shared_ptr<juce::MidiOutput> o (juce::MidiOutput::openDevice (d.identifier));
            if (o == nullptr) return nullptr;
            o->startBackgroundThread();
            openOutputs[name] = o;
            return o;
        }
    return nullptr;
}

std::unique_ptr<PluginSynth> MidiOutHost::createSynth (const Instrument& in, double sampleRate)
{
    const juce::String name (in.midiDevice);
    std::unique_ptr<MidiSink> sink;
    if (sinkFactory) sink = sinkFactory (name);
    else if (auto o = openDevice (name)) sink = std::make_unique<DeviceSink> (std::move (o), *this);
    if (sink == nullptr)
    {
        errors[in.midiDevice] = name.isEmpty() ? juce::String ("NO MIDI OUTPUT CHOSEN") : "MIDI OUTPUT \"" + name.toUpperCase() + "\" ISN'T CONNECTED";
        return nullptr;
    }
    errors.erase (in.midiDevice);
    auto s = std::make_unique<MidiOutSynth> (std::move (sink));
    s->prepare (sampleRate, 512);
    s->update (in);
    return s;
}

juce::String MidiOutHost::lastError (const std::string& device) const
{
    auto it = errors.find (device);
    return it != errors.end() ? it->second : juce::String();
}
} // namespace prv
