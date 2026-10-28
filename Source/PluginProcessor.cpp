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
    song.chords = globalChords;
    player.setSong (&song);
    player.mixer().pluginFactory = [this] (int slot, const prv::FxSlot& s, double sr) { return pluginHost.create (slot, s, sr); };
    pluginHost.onAsyncReady = [this] (int slot) { reloadMixerSlot (slot); };
    player.synths().factory = [this] (int i, const prv::Instrument& in, double sr)
    {
        return in.isMidiOut() ? midiHost.createSynth (in, sr) : pluginHost.createSynth (i, in, sr);
    };
}

std::function<std::unique_ptr<prv::PluginSynth> (int, const prv::Instrument&, double)> PaulaProcessor::synthFactory()
{
    // (renders leave MIDI-out instruments out)
    return [this] (int i, const prv::Instrument& in, double sr) -> std::unique_ptr<prv::PluginSynth>
    {
        return in.isMidiOut() ? nullptr : pluginHost.createSynth (i, in, sr);
    };
}

prv::MidiOutSynth* PaulaProcessor::midiAt (int i) const
{
    return dynamic_cast<prv::MidiOutSynth*> (const_cast<PaulaProcessor*> (this)->player.synths().at (i));
}

juce::String PaulaProcessor::instrumentError (int i) const
{
    if (i < 0 || i >= prv::kMaxInstruments) return {};
    const auto& in = song.instruments[(size_t) i];
    return in.isMidiOut() ? midiHost.lastError (in.midiDevice) : pluginHost.lastError (in.pluginKey);
}

void PaulaProcessor::midiPanic()
{
    withLock ([&] {
        for (int i = 0; i < prv::kMaxInstruments; ++i)
            if (auto* m = midiAt (i)) m->allNotesOff (0);
    });
}

prv::PluginSynthImpl* PaulaProcessor::synthAt (int i) const
{
    return dynamic_cast<prv::PluginSynthImpl*> (const_cast<PaulaProcessor*> (this)->player.synths().at (i));
}

void PaulaProcessor::reloadInstrumentPlugin (int i)
{
    withLock ([&] { player.synths().invalidate (i); });
    updateMixerEffects();
}

std::function<std::unique_ptr<prv::Effect> (int, const prv::FxSlot&, double)> PaulaProcessor::pluginFactory()
{
    return [this] (int, const prv::FxSlot& s, double sr) { return pluginHost.create (-1, s, sr); };
}

prv::PluginEffect* PaulaProcessor::pluginAt (int slot) const
{
    return dynamic_cast<prv::PluginEffect*> (const_cast<PaulaProcessor*> (this)->player.mixer().effectAt (slot));
}

bool PaulaProcessor::pluginLoaded (int slot) const { return pluginAt (slot) != nullptr; }

void PaulaProcessor::capturePluginStates()
{
    for (int i = 0; i < prv::kTotalSlots; ++i)
    {
        if (song.mixer.slot (i).type != prv::FxType::Plugin) continue;
        if (auto* p = pluginAt (i))
        {
            auto st = p->state();
            withLock ([&] { song.mixer.slot (i).pluginState = std::move (st); });
        }
    }
    for (int i = 0; i < prv::kMaxInstruments; ++i)
    {
        if (! song.instruments[(size_t) i].isPlugin()) continue;
        if (auto* s = synthAt (i))
        {
            auto st = s->state();
            withLock ([&] { song.instruments[(size_t) i].pluginState = std::move (st); });
        }
    }
}

void PaulaProcessor::reloadMixerSlot (int slot)
{
    withLock ([&] { player.mixer().invalidate (slot); });
    updateMixerEffects();
}

bool PaulaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // stereo only: a mono layout would put everything on one side of some setups
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
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

void PaulaProcessor::updateMixerEffects()
{
    std::vector<std::unique_ptr<prv::Effect>> old;
    std::vector<std::unique_ptr<prv::PluginSynth>> oldSynths;
    auto changes = player.mixer().plan (song.mixer);   // (loads plug-ins: outside the lock)
    auto synthChanges = player.synths().plan (song, currentRate);
    if (changes.empty() && synthChanges.empty()) return;
    withLock ([&] { player.mixer().apply (changes, old); player.synths().apply (synthChanges, oldSynths); });
    // `old` is freed here, outside the audio lock
}

void PaulaProcessor::replaceSong (prv::Song&& newSong)
{
    std::vector<std::unique_ptr<prv::Effect>> oldEffects;   // declared first: freed after the lock is released
    std::vector<std::unique_ptr<prv::PluginSynth>> oldSynths;
    if (! newSong.chordsSaved) { newSong.chords = globalChords; newSong.chordsSaved = true; }
    // make the new song's effects (and load its plug-ins) before taking the lock
    auto changes = player.mixer().plan (newSong.mixer, true);
    auto synthChanges = player.synths().plan (newSong, currentRate, true);
    const juce::SpinLock::ScopedLockType sl (lock);
    player.stop();
    player.jamAllOff();
    std::swap (song, newSong);   // the old song is freed by the caller's temporary, outside the audio path
    player.setSong (&song);
    player.setLineLoop (-1, 0, 0);
    editorState.loopOn = false;
    editorState.loopBlock = -1;
    history.clear();
    player.mixer().reset();
    player.mixer().clearAutomation();
    player.resetSwitches();
    player.mixer().apply (changes, oldEffects);
    player.synths().apply (synthChanges, oldSynths);
}

juce::String PaulaProcessor::chordReport (const prv::Song& s) const
{
    if (s.chordsLegacy) return "CHORDS: OLD 0XY INTERVALS - CONVERT THEM ON THE CHORDS PAGE";
    if (! s.chordsSaved) return "CHORDS: THE GLOBAL SET";
    const auto diff = prv::differingSlots (s.chords, globalChords);
    if (diff.empty()) return "CHORDS: SAME AS THE GLOBAL SET";
    return "CHORDS: THE SONG'S OWN - SLOTS " + prv::slotList (diff, 6) + " DIFFER FROM THE GLOBAL SET";
}

bool PaulaProcessor::popMidiNote (int& note)
{
    int got = 0;
    midiFifo.read (1).forEach ([&] (int idx) { note = midiNotes[idx]; got = 1; });
    return got != 0;
}

std::unique_ptr<PaulaProcessor::PreviewAudio> PaulaProcessor::startPreview (std::unique_ptr<PreviewAudio> audio, int start, int end, bool loop,
                                                                            double pitch, bool playNow)
{
    if (audio == nullptr || audio->left.empty()) return stopPreview();
    const int len = (int) audio->left.size();
    if (! audio->right.empty()) audio->right.resize ((size_t) len, 0.0f);
    start = juce::jlimit (0, len - 1, start);
    end = (end < 0 || end > len) ? len : end;
    if (end <= start) end = len;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (preview, audio);
        prevPos = start; prevStart = start; prevEnd = end; prevLoop = loop; prevPlaying = playNow;
        prevPitch = juce::jlimit (0.01, 64.0, pitch);
    }
    previewPos = playNow ? start : -1;
    return audio;
}

void PaulaProcessor::restartPreview (int start, double pitch)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (preview == nullptr) return;
        const int len = (int) preview->left.size();
        prevStart = juce::jlimit (0, std::max (0, len - 1), start);
        prevPos = prevStart; prevEnd = len; prevLoop = false; prevPlaying = true;
        prevPitch = juce::jlimit (0.01, 64.0, pitch);
    }
    previewPos = start;
}

void PaulaProcessor::pausePreview()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        prevPlaying = false;
    }
    previewPos = -1;
}

std::unique_ptr<PaulaProcessor::PreviewAudio> PaulaProcessor::stopPreview()
{
    std::unique_ptr<PreviewAudio> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (preview, old);
        prevPlaying = false;
    }
    previewPos = -1;
    return old;
}

void PaulaProcessor::renderPreview (float* l, float* r, int n)
{
    if (! prevPlaying || preview == nullptr) return;
    const auto& a = *preview;
    const bool stereo = ! a.right.empty();
    const double inc = a.rate * prevPitch / std::max (1.0, currentRate);
    const float g = previewGain.load();
    for (int i = 0; i < n; ++i)
    {
        if (prevPos >= prevEnd)
        {
            if (prevLoop) prevPos = prevStart + std::fmod (prevPos - prevStart, (double) std::max (1, prevEnd - prevStart));
            else { prevPlaying = false; previewPos = -1; return; }
        }
        const int k = (int) prevPos;
        const float f = (float) (prevPos - k);
        const int k2 = std::min (k + 1, (int) a.left.size() - 1);
        const float sl = a.left[(size_t) k] + (a.left[(size_t) k2] - a.left[(size_t) k]) * f;
        const float sr = stereo ? a.right[(size_t) k] + (a.right[(size_t) k2] - a.right[(size_t) k]) * f : sl;
        l[i] += sl * g;
        r[i] += sr * g;
        prevPos += inc;
    }
    previewPos = (int) prevPos;
}

int PaulaProcessor::readScope (float* dest, int maxSamples)
{
    int got = 0;
    scopeFifo.read (std::min (maxSamples, scopeFifo.getNumReady())).forEach ([&] (int idx) { dest[got++] = scopeBuf[idx]; });
    return got;
}

void PaulaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    if (n > scratchSize)
    {
        scratchSize = n;
        scratchL.allocate ((size_t) n, true);
        scratchR.allocate ((size_t) n, true);
    }

    midiHost.outputLatencyMs = 1000.0 * n / std::max (1.0, currentRate);

    const juce::SpinLock::ScopedLockType sl (lock);

    // tell hosted plug-ins where the song is
    {
        auto& ph = pluginHost.playHead;
        const bool playing = player.isPlaying();
        if (playing && ! ph.playing.load()) ph.samples = 0;
        ph.playing = playing;
        ph.sampleRate = currentRate;
        if (playing) ph.samples = ph.samples.load() + n;
    }

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
        if (msg.isNoteOn() && midiToPreview.load() && preview != nullptr)
        {
            // sample browser: play the highlighted file at this note's pitch
            prevStart = 0; prevPos = 0; prevEnd = (int) preview->left.size(); prevLoop = false; prevPlaying = true;
            prevPitch = std::pow (2.0, (msg.getNoteNumber() - 60) / 12.0);
            previewPos = 0;
        }
        else if (msg.isNoteOn())
        {
            const int note = juce::jlimit (1, (int) prv::kNoteMax, msg.getNoteNumber() - 23); // MIDI 60 -> C-3
            player.jamNoteOn (note, jamInstrument.load(), msg.getVelocity(), jamRangeStart.load(), jamRangeEnd.load(), jamRangeLoop.load());
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
        else if (msg.isController())
        {
            lastCcNumber = msg.getControllerNumber();
            ++ccSerial;
        }
    }
    renderTo (n);
    renderPreview (scratchL.get(), scratchR.get(), n);

    {
        const int space = std::min (n, scopeFifo.getFreeSpace());
        const float* l = scratchL.get() + (n - space);
        const float* r = scratchR.get() + (n - space);
        int k = 0;
        scopeFifo.write (space).forEach ([&] (int idx) { scopeBuf[idx] = 0.5f * (l[k] + r[k]); ++k; });
    }

    // a short fade (switching the output device): no clicks
    {
        const float target = outputGainTarget.load (std::memory_order_relaxed);
        if (outputGain != target || target < 1.0f)
        {
            const float step = (float) (1.0 / (0.010 * std::max (1.0, currentRate)));   // 10 ms
            float* l = scratchL.get();
            float* r = scratchR.get();
            for (int i = 0; i < n; ++i)
            {
                outputGain = target > outputGain ? std::min (target, outputGain + step) : std::max (target, outputGain - step);
                l[i] *= outputGain; r[i] *= outputGain;
            }
        }
    }

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
    if (juce::MessageManager::existsAndIsCurrentThread()) capturePluginStates();
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
    st.setProperty ("sep3", o.stereoSep, nullptr);   // new key: the old 75% default isn't carried over
    st.setProperty ("master", o.master, nullptr);
    const auto& e = editorState;
    st.setProperty ("instrument", e.instrument, nullptr);
    st.setProperty ("octave", e.octave, nullptr);
    st.setProperty ("step", e.step, nullptr);
    st.setProperty ("block", e.block, nullptr);
    st.setProperty ("seqPos", e.seqPos, nullptr);
    st.setProperty ("fourTrack", e.fourTrackView, nullptr);
    st.setProperty ("dark", e.darkSkin, nullptr);
    st.setProperty ("spectrum", e.showSpectrum, nullptr);
    st.setProperty ("fx2", e.showFx2, nullptr);
    st.setProperty ("zoom", e.uiZoom, nullptr);
    st.setProperty ("follow", e.follow, nullptr);
    st.setProperty ("folder", e.lastFolder, nullptr);
    st.setProperty ("sampleFolder", e.sampleFolder, nullptr);
    st.setProperty ("file", e.currentFile, nullptr);

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
        o.model = (prv::PaulaOptions::Model) juce::jlimit (0, 4, (int) st.getProperty ("model", 0));
        o.zeroOrderHold = st.getProperty ("zoh", true);
        o.eightBit = st.getProperty ("eightBit", true);
        o.crunch8ch = st.getProperty ("crunch", true);
        o.stereoSep = st.getProperty ("sep3", 0.2f);
        o.master = st.getProperty ("master", 1.0f);
    }
    auto& e = editorState;
    e.instrument = st.getProperty ("instrument", 0);
    e.octave = st.getProperty ("octave", 3);
    e.step = st.getProperty ("step", 1);
    e.block = st.getProperty ("block", 0);
    e.seqPos = st.getProperty ("seqPos", 0);
    e.fourTrackView = st.getProperty ("fourTrack", true);
    e.darkSkin = st.getProperty ("dark", true);
    e.showSpectrum = st.getProperty ("spectrum", true);
    e.showFx2 = st.getProperty ("fx2", false);
    e.uiZoom = st.getProperty ("zoom", 0);
    e.follow = st.getProperty ("follow", true);
    e.lastFolder = st.getProperty ("folder", juce::String());
    e.sampleFolder = st.getProperty ("sampleFolder", juce::String());
    e.currentFile = st.getProperty ("file", juce::String());
    jamInstrument = e.instrument;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PaulaProcessor();
}
