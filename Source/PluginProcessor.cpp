// Paula's Revenge - audio processor
#include "engine/BuiltinSynths.h"
#include "PluginProcessor.h"
#include "engine/StretchSync.h"
#include "Diagnostics.h"
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
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withInput ("CV In", juce::AudioChannelSet::discreteChannels (prv::kCvOutputs - 2), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("CV Out", juce::AudioChannelSet::discreteChannels (prv::kCvOutputs - 2), true))
{
    addStarterInstruments (song);
    song.chords = globalChords;
    fullVelocity = prv::prefs().getBoolValue ("fullVelocity", false);
    followClock = prv::prefs().getBoolValue ("followMidiClock", false);
    player.setSong (&song);
    player.mixer().pluginFactory = [this] (int slot, const prv::FxSlot& s, double sr) { return pluginHost.create (slot, s, sr); };
    pluginHost.onAsyncReady = [this] (int slot) { reloadMixerSlot (slot); };
    player.synths().factory = [this] (int i, const prv::Instrument& in, double sr)
    {
        if (in.isCvOut()) return std::unique_ptr<prv::PluginSynth> (std::make_unique<prv::CvOutSynth>());
        if (in.isBuiltin()) return prv::createBuiltinSynth (in, sr);
        return in.isMidiOut() ? midiHost.createSynth (in, sr) : pluginHost.createSynth (i, in, sr);
    };
}

std::function<std::unique_ptr<prv::PluginSynth> (int, const prv::Instrument&, double)> PaulaProcessor::synthFactory()
{
    // (renders leave MIDI-out instruments out)
    return [this] (int i, const prv::Instrument& in, double sr) -> std::unique_ptr<prv::PluginSynth>
    {
        if (in.isBuiltin()) return prv::createBuiltinSynth (in, sr);
        return in.isMidiOut() || in.isCvOut() ? nullptr : pluginHost.createSynth (i, in, sr);
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
        {
            if (auto* m = midiAt (i)) m->allNotesOff (0);
            if (auto* c = cvAt (i)) { c->allNotesOff (0); c->setTesting (false, 0); }   // (CV: gates low)
        }
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
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    // the input (SAMPLE): stereo, mono or none
    const auto in = layouts.getMainInputChannelSet();
    if (! (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono())) return false;
    // CV In / CV Out (an ES-9's other channels): off, or up to 14 more
    for (const auto* list : { &layouts.inputBuses, &layouts.outputBuses })
        for (int b = 1; b < list->size(); ++b)
            if ((*list)[b].size() > prv::kCvOutputs - 2) return false;
    return true;
}

void PaulaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    radio.prepare (sampleRate);   // (outside the lock: it may wait for its thread)
    const juce::SpinLock::ScopedLockType sl (lock);
    player.setSampleRate (sampleRate);
    lowEnd.prepare (sampleRate);
    scratchSize = std::max (samplesPerBlock, 512);
    scratchL.allocate ((size_t) scratchSize, true);
    inL.allocate ((size_t) scratchSize, true);
    inR.allocate ((size_t) scratchSize, true);
    scratchR.allocate ((size_t) scratchSize, true);
    radL.allocate ((size_t) scratchSize, true);
    radR.allocate ((size_t) scratchSize, true);
    allocateVintage (scratchSize);
    vintage.prepare (sampleRate, scratchSize);
    for (auto& b : cvBuf) b.allocate ((size_t) scratchSize, true);
    {
        const int maxD = (int) (sampleRate * 0.1) + 1;   // CV DELAY: up to 100 ms either way
        for (auto& d : cvDelayLine) d.setMax (maxD);
        musicDelayLine.setMax (maxD);
    }
}

void PaulaProcessor::allocateVintage (int n)
{
    vMonL.allocate ((size_t) n, true); vMonR.allocate ((size_t) n, true);
    vDecL.allocate ((size_t) n * 4 + 16, true); vDecR.allocate ((size_t) n * 4 + 16, true);   // (up to 48 kHz from 12 kHz)
}

void PaulaProcessor::setVintage (const prv::VintageSettings& v)
{
    withLock ([&] { vintage.prepare (currentRate > 0 ? currentRate : 48000.0, scratchSize); vintage.set (v); });
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

// STRETCH: keep each stretched instrument fitted to the song's tempo (the message thread, a few times a second).
// Rendering happens outside the lock; only the swap is inside. Returns what changed, for the status line.
juce::StringArray PaulaProcessor::refreshStretches()
{
    juce::StringArray said;
    const double bpm = song.tempo;
    const int spd = song.speed;
    for (int i = 0; i < prv::kMaxInstruments; ++i)
    {
        auto& in = song.instruments[(size_t) i];
        const bool want = prv::stretch::wanted (in);
        const auto hex = juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2);
        if (! want && ! in.stretchSrc.empty())
        {
            withLock ([&] { prv::stretch::restore (in); });
            said.add ("STRETCH OFF: INSTRUMENT " + hex + " IS ITS ORIGINAL AGAIN");
            continue;
        }
        if (! want) continue;
        if (prv::stretch::edited (in))
        {
            withLock ([&] { prv::stretch::bake (in); });
            said.add ("INSTRUMENT " + hex + " WAS EDITED: ITS STRETCH IS KEPT AS THE SAMPLE (STRETCH OFF)");
            continue;
        }
        if (in.stretchSrc.empty()) withLock ([&] { prv::stretch::begin (in); });
        if (! prv::stretch::needsRender (in, bpm, spd)) continue;
        // (the render reads the source, which only this thread changes)
        auto r = prv::stretch::make (in, bpm, spd);
        if (! r) continue;
        const double f = r->factor;
        withLock ([&] { prv::stretch::apply (in, std::move (*r)); });
        said.add ("STRETCH (" + juce::String (prv::stretch::modeName (in.stretchMode)) + "): INSTRUMENT " + hex + " FITTED TO " + juce::String (in.syncLines)
                  + " LINES AT " + juce::String ((int) bpm) + " BPM (x" + juce::String (f, 2) + (in.stretchSemis != 0 ? ", " + juce::String (in.stretchSemis > 0 ? "+" : "") + juce::String (in.stretchSemis) + " SEMITONES" : juce::String()) + ")");
    }
    return said;
}

void PaulaProcessor::replaceSong (prv::Song&& newSong)
{
    std::vector<std::unique_ptr<prv::Effect>> oldEffects;   // declared first: freed after the lock is released
    std::vector<std::unique_ptr<prv::PluginSynth>> oldSynths;
    if (! newSong.chordsSaved) { newSong.chords = globalChords; newSong.chordsSaved = true; }
    // STRETCH: the song keeps the originals; make the stretches for its tempo before it plays
    for (auto& in : newSong.instruments)
        if (prv::stretch::wanted (in) && in.stretchSrc.empty())
        {
            prv::stretch::begin (in);
            if (auto r = prv::stretch::make (in, newSong.tempo, newSong.speed)) prv::stretch::apply (in, std::move (*r));
            else prv::stretch::restore (in);
        }
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

bool PaulaProcessor::popMidiNote (InputNote& n)
{
    int got = 0;
    midiFifo.read (1).forEach ([&] (int idx) { n = midiNotes[idx]; got = 1; });
    if (got) logInput (n);
    return got != 0;
}

void PaulaProcessor::keyNoteInput (int note, bool on, int velocity)
{
    InputNote n;
    n.timeMs = juce::Time::getMillisecondCounterHiRes();
    n.note = note; n.on = on; n.velocity = velocity; n.fromMidi = false;
    n.instrument = editorState.instrument;
    withLock ([&] { n.pos = player.playPos(); });
    logInput (n);
}

void PaulaProcessor::logInput (const InputNote& n)
{
    if (n.on) lastPlayedNote = n.note;
    captureLog.push_back (n);
    while (! captureLog.empty() && n.timeMs - captureLog.front().timeMs > 120000.0) captureLog.pop_front();
    while (captureLog.size() > 20000) captureLog.pop_front();
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

void PaulaProcessor::pushSysex (const uint8_t* d, int n)
{
    // [length lo, length hi, bytes...]; a message that doesn't fit is dropped
    if (d == nullptr || n <= 0 || n > 60000 || sysexFifo.getFreeSpace() < n + 2) return;
    int k = 0;
    sysexFifo.write (n + 2).forEach ([&] (int idx) { sysexBuf[(size_t) idx] = k == 0 ? (uint8_t) (n & 255) : k == 1 ? (uint8_t) (n >> 8) : d[k - 2]; ++k; });
}

bool PaulaProcessor::readSysex (std::vector<uint8_t>& msg)
{
    if (sysexFifo.getNumReady() < 2) return false;
    int len = 0, k = 0;
    // the length (peeked), then the bytes
    int s1, n1, s2, n2;
    sysexFifo.prepareToRead (2, s1, n1, s2, n2);
    len = sysexBuf[(size_t) s1] | ((n1 > 1 ? sysexBuf[(size_t) s1 + 1] : sysexBuf[(size_t) s2]) << 8);
    if (sysexFifo.getNumReady() < len + 2) return false;
    msg.resize ((size_t) len);
    sysexFifo.read (len + 2).forEach ([&] (int idx) { if (k >= 2) msg[(size_t) (k - 2)] = sysexBuf[(size_t) idx]; ++k; });
    return true;
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
    // DIAGNOSTICS: how long this block takes against how long it lasts (and the wait for the lock)
    struct Timing
    {
        int64_t start = juce::Time::getHighResolutionTicks(), lockAsked = 0, lockGot = 0;
        int n; double rate;
        ~Timing()
        {
            const auto end = juce::Time::getHighResolutionTicks();
            const double tick = 1000.0 / (double) juce::Time::getHighResolutionTicksPerSecond();
            prv::Diagnostics::get().audioBlock ((double) (end - start) * tick, rate > 0 ? n * 1000.0 / rate : 0.0, (double) (lockGot - lockAsked) * tick);
        }
    } timing { .n = n, .rate = currentRate };

    if (n > scratchSize)
    {
        scratchSize = n;
        scratchL.allocate ((size_t) n, true);
        scratchR.allocate ((size_t) n, true);
        inL.allocate ((size_t) n, true);
        inR.allocate ((size_t) n, true);
        radL.allocate ((size_t) n, true);
        radR.allocate ((size_t) n, true);
        allocateVintage (n);
        for (auto& b : cvBuf) b.allocate ((size_t) n, true);
    }
    // the input (SAMPLE), before the buffer becomes the output: mono goes to both sides
    {
        const int ins = std::min (getMainBusNumInputChannels(), buffer.getNumChannels());
        inputChannels.store (ins, std::memory_order_relaxed);
        // AUTO-TUNE / the tuner: the input the modular comes back on
        const int tin = cvTuneInput.load (std::memory_order_relaxed) - 1;
        if (tin >= 0 && tin < std::min (getTotalNumInputChannels(), buffer.getNumChannels()))
        {
            const float* src = buffer.getReadPointer (tin);
            int w = tuneWrite.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) { tuneRing[(size_t) w] = src[i]; w = (w + 1) % kTuneRing; }
            tuneWrite.store (w, std::memory_order_release);
        }
        const float g = inputGain.load (std::memory_order_relaxed);
        if (ins >= 1) juce::FloatVectorOperations::multiply (inL.get(), buffer.getReadPointer (0), g, n);
        else juce::FloatVectorOperations::clear (inL.get(), n);
        if (ins >= 2) juce::FloatVectorOperations::multiply (inR.get(), buffer.getReadPointer (1), g, n);
        else juce::FloatVectorOperations::copy (inR.get(), inL.get(), n);
    }
    audioRunning.store (true, std::memory_order_relaxed);
    buffer.clear();

    midiHost.outputLatencyMs = 1000.0 * n / std::max (1.0, currentRate);

    timing.lockAsked = juce::Time::getHighResolutionTicks();
    const juce::SpinLock::ScopedLockType sl (lock);
    timing.lockGot = juce::Time::getHighResolutionTicks();
    player.synths().feedInput (inL.get(), inR.get(), n);   // (GRANULAR's live recording)

    // tell hosted plug-ins where the song is
    {
        auto& ph = pluginHost.playHead;
        const bool playing = player.isPlaying();
        if (playing && ! ph.playing.load()) ph.samples = 0;
        ph.playing = playing;
        ph.sampleRate = currentRate;
        if (playing) ph.samples = ph.samples.load() + n;
    }

    // CV out (an ES-9): the voltages go to the interface's outputs 3-16, when the song uses any
    bool cvUsed = false;
    for (const auto& c : song.cvSources) cvUsed = cvUsed || (c.kind != prv::CvKind::Off && c.out >= 3);
    for (const auto& in : song.instruments) cvUsed = cvUsed || in.isCvOut();
    const int devOuts = buffer.getNumChannels();
    if (cvUsed) for (int k = 2; k < prv::kCvOutputs; ++k) juce::FloatVectorOperations::clear (cvBuf[k].get(), n);
    for (int k = 0; k < prv::kCvOutputs; ++k) player.cv.out[k] = nullptr;
    cvActive.store (cvUsed && devOuts > 2, std::memory_order_relaxed);

    // render in slices between MIDI events so notes land sample-accurately
    int done = 0;
    auto renderTo = [&] (int end)
    {
        if (end > done)
        {
            if (cvUsed) for (int k = 2; k < prv::kCvOutputs; ++k) player.cv.out[k] = cvBuf[k].get() + done;
            player.render (scratchL.get() + done, scratchR.get() + done, end - done);
            done = end;
        }
    };
    // (the block started when it's heard; a note's time is its place in the block after that)
    const double blockMs = juce::Time::getMillisecondCounterHiRes();
    auto pushMidiInput = [&] (int note, bool on, int vel, int at)
    {
        InputNote in;
        in.timeMs = blockMs + 1000.0 * juce::jlimit (0, n, at) / std::max (1.0, currentRate);
        in.note = note; in.on = on; in.velocity = vel; in.fromMidi = true;
        in.instrument = jamInstrument.load();
        in.pos = player.playPos();   // (rendered up to this message already)
        midiFifo.write (1).forEach ([&] (int idx) { midiNotes[idx] = in; });
    };
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        if (msg.isSysEx()) { pushSysex (msg.getRawData(), msg.getRawDataSize()); continue; }
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
            const int vel = fullVelocity.load (std::memory_order_relaxed) ? 127 : (int) msg.getVelocity();
            player.jamNoteOn (note, jamInstrument.load(), vel, jamRangeStart.load(), jamRangeEnd.load(), jamRangeLoop.load());
            pushMidiInput (note, true, vel, meta.samplePosition);
        }
        else if (msg.isNoteOff())
        {
            const int note = juce::jlimit (1, (int) prv::kNoteMax, msg.getNoteNumber() - 23);
            player.jamNoteOff (note);
            pushMidiInput (note, false, 0, meta.samplePosition);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            player.jamAllOff();
        }
        // FOLLOW MIDI CLOCK: the tempo from its ticks; start, continue, stop and position too
        else if (followClock.load (std::memory_order_relaxed) && (msg.isMidiClock() || msg.isMidiStart() || msg.isMidiContinue() || msg.isMidiStop() || msg.isSongPositionPointer()))
        {
            const double t = (clockSamples + meta.samplePosition) / std::max (1.0, currentRate);
            if (msg.isMidiClock())
            {
                clockIn.tick (t);
                if (clockIn.locked (t)) { player.setBeatTempo (clockIn.bpm()); clockBpm.store ((float) clockIn.bpm(), std::memory_order_relaxed); }
            }
            else if (msg.isMidiStart()) { player.playSong (0, 0); sppPos = sppLine = 0; }
            else if (msg.isMidiContinue()) player.playSong (sppPos, sppLine);
            else if (msg.isMidiStop()) player.stop();
            else if (msg.isSongPositionPointer()) prv::songPositionAt (song, msg.getSongPositionPointerMidiBeat(), sppPos, sppLine);
        }
        else if (msg.isController())
        {
            lastCcNumber = msg.getControllerNumber();
            ++ccSerial;
        }
    }
    renderTo (n);
    lowEnd.process (scratchL.get(), scratchR.get(), n);
    clockSamples += n;
    if (followClock.load (std::memory_order_relaxed) && ! clockIn.locked (clockSamples / std::max (1.0, currentRate))) clockBpm.store (0.0f, std::memory_order_relaxed);
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

    // SAMPLE: the monitor (the input, heard through the app), then what the recorder takes -
    // the input, or everything the app plays (RESAMPLE)
    {
        // (AMIGA / S950: through the vintage sampler - heard like that, recorded at its rate and bits)
        const int source = recSource.load (std::memory_order_relaxed);
        const bool resampling = source == SourceResample;
        const bool old = vintage.active();
        if (source == SourceInput)
        {
            if (old)
            {
                const int k = vintage.process (inL.get(), inR.get(), n, vMonL.get(), vMonR.get(), vDecL.get(), vDecR.get());
                recorder.process (vDecL.get(), vDecR.get(), k, player.isPlaying());
            }
            else recorder.process (inL.get(), inR.get(), n, player.isPlaying());
        }
        if (monitorInput.load (std::memory_order_relaxed))
        {
            const bool through = old && ! resampling;
            juce::FloatVectorOperations::add (scratchL.get(), through ? vMonL.get() : inL.get(), n);
            juce::FloatVectorOperations::add (scratchR.get(), through ? vMonR.get() : inR.get(), n);
        }
        if (resampling)
        {
            if (old)
            {
                const int k = vintage.process (scratchL.get(), scratchR.get(), n, vMonL.get(), vMonR.get(), vDecL.get(), vDecR.get());
                recorder.process (vDecL.get(), vDecR.get(), k, player.isPlaying());
            }
            else recorder.process (scratchL.get(), scratchR.get(), n, player.isPlaying());
        }
        // INTERNET RADIO: added to what goes out (after RESAMPLE: it's not part of the music), and
        // recorded as it came in when SAMPLE's source is RADIO
        radio.process (scratchL.get(), scratchR.get(), radL.get(), radR.get(), n);
        if (source == SourceRadio)
        {
            if (old)
            {
                const int k = vintage.process (radL.get(), radR.get(), n, vMonL.get(), vMonR.get(), vDecL.get(), vDecR.get());
                recorder.process (vDecL.get(), vDecR.get(), k, player.isPlaying());
            }
            else recorder.process (radL.get(), radR.get(), n, player.isPlaying());
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
    // the voltages: outputs 3-16 (the device's channels, in order)
    // CV DELAY: the voltages later (positive), or the music on 1-2 later (negative), to line a modular up
    const int cvDelay = (int) std::lround (std::abs (song.cvDelayMs) * 0.001 * currentRate);
    if (cvUsed && song.cvDelayMs > 0)
        for (int k = 2; k + 1 < prv::kCvOutputs; k += 2) cvDelayLine[k / 2].process (cvBuf[k].get(), cvBuf[k + 1].get(), n, cvDelay);
    if (cvUsed && song.cvDelayMs < 0 && buffer.getNumChannels() >= 2)
        musicDelayLine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, cvDelay);
    if (cvUsed) for (int k = 2; k < std::min (devOuts, prv::kCvOutputs); ++k) buffer.copyFrom (k, 0, cvBuf[k].get(), n);
    for (int k = 0; k < prv::kCvOutputs; ++k) player.cv.out[k] = nullptr;
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
    st.setProperty ("skin", e.skin, nullptr);
    st.setProperty ("crt", e.crt, nullptr);
    st.setProperty ("scopes", e.scopes, nullptr);
    st.setProperty ("sidebarFold", e.sidebarFold, nullptr);
    st.setProperty ("sidebarTabs", e.sidebarTabs, nullptr);
    st.setProperty ("showLane", e.showLane, nullptr);
    st.setProperty ("showPads", e.showPads, nullptr);
    st.setProperty ("padBank", e.padBank, nullptr);
    st.setProperty ("spectrum", e.showSpectrum, nullptr);
    st.setProperty ("automation", e.showAutomation, nullptr);
    st.setProperty ("autoHeight", e.autoHeight, nullptr);
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
        o.model = (prv::PaulaOptions::Model) juce::jlimit (0, 5, (int) st.getProperty ("model", 0));
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
    e.skin = juce::jlimit (0, prv::ui::Theme::kSkins - 1, (int) st.getProperty ("skin", e.darkSkin ? 1 : 0));
    if (e.darkSkin != prv::ui::Theme::skinIsDark (e.skin)) e.skin = e.darkSkin ? 1 : 0;   // (an older DARK setting wins)
    e.crt = st.getProperty ("crt", false);
    e.scopes = st.getProperty ("scopes", true);
    e.sidebarFold = juce::jlimit (0, 7, (int) st.getProperty ("sidebarFold", 0));
    e.sidebarTabs = st.getProperty ("sidebarTabs", false);
    e.showLane = st.getProperty ("showLane", false);
    e.showPads = st.getProperty ("showPads", false);
    e.padBank = juce::jlimit (0, 3, (int) st.getProperty ("padBank", 0));
    e.showSpectrum = st.getProperty ("spectrum", true);
    e.showAutomation = st.getProperty ("automation", false);
    e.autoHeight = st.getProperty ("autoHeight", 0);
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

void PaulaProcessor::setFollowClock (bool on)
{
    followClock = on;
    prv::prefs().setValue ("followMidiClock", on);
    if (! on) withLock ([&] { player.setBeatTempo (0); clockIn.reset(); clockBpm = 0; });
}

void PaulaProcessor::setFullVelocity (bool on)
{
    fullVelocity = on;
    prv::prefs().setValue ("fullVelocity", on);
}

// ---------------------------------------------------------------------------
// AUTO-TUNE / the tuner
int PaulaProcessor::tuneSamples (std::vector<float>& out, int count) const
{
    count = juce::jlimit (0, kTuneRing, count);
    out.resize ((size_t) count);
    const int w = tuneWrite.load (std::memory_order_acquire);
    for (int i = 0; i < count; ++i) out[(size_t) i] = tuneRing[(size_t) ((w - count + i + kTuneRing) % kTuneRing)];
    return count;
}

prv::CvOutSynth* PaulaProcessor::cvAt (int i) const
{
    return dynamic_cast<prv::CvOutSynth*> (const_cast<PaulaProcessor*> (this)->player.synths().at (i));
}

void PaulaProcessor::setCvTest (int instrument, bool on, double volts)
{
    withLock ([&] { if (auto* c = cvAt (instrument)) c->setTesting (on, volts); });
}

