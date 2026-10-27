// Paula's Revenge - audio processor
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SongFile.h"

namespace
{
    // A few tiny looped chip waveforms so a new song makes sound straight away
    void addStarterInstruments (prv::Song& s)
    {
        auto make = [&] (int idx, const char* name, int len, auto fn, int vol)
        {
            auto& in = s.instruments[(size_t) idx];
            in.clear();
            in.name = name;
            in.data.resize ((size_t) len);
            for (int i = 0; i < len; ++i) in.data[(size_t) i] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (fn (i, len) * 30000.0));
            in.loopStart = 0; in.loopLength = len; in.volume = vol;
        };
        make (0, "CHIP SQUARE", 32, [] (int i, int n) { return i < n / 2 ? 1.0 : -1.0; }, 40);
        make (1, "CHIP SAW",    32, [] (int i, int n) { return 1.0 - 2.0 * i / (double) n; }, 44);
        make (2, "CHIP TRI",    32, [] (int i, int n) { const double p = i / (double) n; return p < 0.5 ? 4 * p - 1 : 3 - 4 * p; }, 56);
        make (3, "CHIP PULSE",  32, [] (int i, int n) { return i < n / 8 ? 1.0 : -1.0; }, 40);
        // short noise hit (one-shot)
        auto& nz = s.instruments[4];
        nz.clear(); nz.name = "NOISE HIT"; nz.volume = 48;
        juce::Random rng (7);
        nz.data.resize (3000);
        for (int i = 0; i < 3000; ++i)
            nz.data[(size_t) i] = (int16_t) ((rng.nextFloat() * 2.0f - 1.0f) * 30000.0f * std::pow (1.0f - i / 3000.0f, 2.0f));
        // sine-ish sub bass
        s.instruments[5].clear();
        make (5, "SUB BASS", 64, [] (int i, int n) { return std::sin (2.0 * juce::MathConstants<double>::pi * i / n); }, 64);
    }
}

PaulaProcessor::PaulaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addStarterInstruments (song);
    player.setSong (&song);
}

bool PaulaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void PaulaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    const juce::SpinLock::ScopedLockType sl (lock);
    player.setSampleRate (sampleRate);
    scratchSize = std::max (samplesPerBlock, 512);
    scratchL.allocate ((size_t) scratchSize, true);
    scratchR.allocate ((size_t) scratchSize, true);
}

void PaulaProcessor::replaceSong (prv::Song&& newSong)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    player.stop();
    player.jamAllOff();
    std::swap (song, newSong);   // the old song is freed by the caller's temporary, outside the audio path
    player.setSong (&song);
}

bool PaulaProcessor::popMidiNote (int& note)
{
    int got = 0;
    midiFifo.read (1).forEach ([&] (int idx) { note = midiNotes[idx]; got = 1; });
    return got != 0;
}

void PaulaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // host transport + tempo
    bool playing = false;
    double bpm = 0.0;
    if (! isRunningStandalone())
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                playing = pos->getIsPlaying();
                if (auto b = pos->getBpm()) bpm = *b;
            }
    hostIsPlaying = playing;
    hostBpm = bpm;

    if (n > scratchSize)
    {
        scratchSize = n;
        scratchL.allocate ((size_t) n, true);
        scratchR.allocate ((size_t) n, true);
    }

    const juce::SpinLock::ScopedLockType sl (lock);

    if (! isRunningStandalone() && followHostTransport.load())
    {
        if (playing && ! wasHostPlaying) player.playSong (0, 0);
        if (! playing && wasHostPlaying) player.stop();
    }
    wasHostPlaying = playing;
    player.setTempoOverride ((! isRunningStandalone() && followHostTempo.load() && bpm > 0) ? bpm : 0.0);

    // render in slices between MIDI events so notes land sample-accurately
    int done = 0;
    auto renderTo = [&] (int end)
    {
        if (end > done)
        {
            player.render (scratchL.get() + done, scratchR.get() + done, end - done);
            done = end;
        }
    };
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        if (msg.isNoteOn())
        {
            const int note = juce::jlimit (1, (int) prv::kNoteMax, msg.getNoteNumber() - 23); // MIDI 60 -> C-3
            player.jamNoteOn (note, jamInstrument.load(), msg.getVelocity());
            midiFifo.write (1).forEach ([&] (int idx) { midiNotes[idx] = note; });
        }
        else if (msg.isNoteOff())
        {
            player.jamNoteOff (juce::jlimit (1, (int) prv::kNoteMax, msg.getNoteNumber() - 23));
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            player.jamAllOff();
        }
    }
    renderTo (n);

    if (buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom (0, 0, scratchL.get(), n);
        buffer.copyFrom (1, 0, scratchR.get(), n);
    }
    else if (buffer.getNumChannels() == 1)
    {
        buffer.copyFrom (0, 0, scratchL.get(), n, 0.5f);
        buffer.addFrom (0, 0, scratchR.get(), n, 0.5f);
    }
}

juce::AudioProcessorEditor* PaulaProcessor::createEditor()
{
    return new PaulaEditor (*this);
}

void PaulaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree st ("PaulasRevenge");
    juce::MemoryBlock songData;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        songData = prv::writeSong (song);
    }
    st.setProperty ("song", songData, nullptr);
    const auto& o = player.options;
    st.setProperty ("model", (int) o.model, nullptr);
    st.setProperty ("zoh", o.zeroOrderHold, nullptr);
    st.setProperty ("eightBit", o.eightBit, nullptr);
    st.setProperty ("crunch", o.crunch8ch, nullptr);
    st.setProperty ("sep", o.stereoSep, nullptr);
    st.setProperty ("master", o.master, nullptr);
    st.setProperty ("hostTransport", followHostTransport.load(), nullptr);
    st.setProperty ("hostTempo", followHostTempo.load(), nullptr);
    const auto& e = editorState;
    st.setProperty ("instrument", e.instrument, nullptr);
    st.setProperty ("octave", e.octave, nullptr);
    st.setProperty ("step", e.step, nullptr);
    st.setProperty ("block", e.block, nullptr);
    st.setProperty ("seqPos", e.seqPos, nullptr);
    st.setProperty ("fourTrack", e.fourTrackView, nullptr);
    st.setProperty ("zoom", e.uiZoom, nullptr);
    st.setProperty ("follow", e.follow, nullptr);
    st.setProperty ("folder", e.lastFolder, nullptr);

    juce::MemoryOutputStream mos (destData, false);
    st.writeToStream (mos);
}

void PaulaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto st = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! st.isValid() || ! st.hasType ("PaulasRevenge")) return;

    if (auto* mb = st["song"].getBinaryData())
    {
        prv::Song s;
        juce::String err;
        if (prv::readSong (mb->getData(), mb->getSize(), s, err))
            replaceSong (std::move (s));
    }
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& o = player.options;
        o.model = (prv::PaulaOptions::Model) juce::jlimit (0, 2, (int) st.getProperty ("model", 0));
        o.zeroOrderHold = st.getProperty ("zoh", true);
        o.eightBit = st.getProperty ("eightBit", true);
        o.crunch8ch = st.getProperty ("crunch", true);
        o.stereoSep = st.getProperty ("sep", 0.75f);
        o.master = st.getProperty ("master", 1.0f);
    }
    followHostTransport = (bool) st.getProperty ("hostTransport", true);
    followHostTempo = (bool) st.getProperty ("hostTempo", true);
    auto& e = editorState;
    e.instrument = st.getProperty ("instrument", 0);
    e.octave = st.getProperty ("octave", 3);
    e.step = st.getProperty ("step", 1);
    e.block = st.getProperty ("block", 0);
    e.seqPos = st.getProperty ("seqPos", 0);
    e.fourTrackView = st.getProperty ("fourTrack", true);
    e.uiZoom = st.getProperty ("zoom", 0);
    e.follow = st.getProperty ("follow", true);
    e.lastFolder = st.getProperty ("folder", juce::String());
    jamInstrument = e.instrument;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PaulaProcessor();
}
