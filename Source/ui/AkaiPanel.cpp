// Paula's Revenge - the AKAI S1000 editor (see AkaiPanel.h)
#include "AkaiPanel.h"
#include "ChoiceBox.h"
#include "../engine/Recorder.h"

namespace prv::ui
{
AkaiPanel::AkaiPanel (PaulaProcessor& p) : proc (p)
{
    getBtn.onClick = [this] { getPrograms(); };
    programBtn.onClick = [this]
    {
        if (names.isEmpty()) { getPrograms(); return; }
        if (! menuHook()) return;
        juce::Component::SafePointer<AkaiPanel> safe (this);
        menuHook() ("WHICH " + model() + " PROGRAM?", names, [safe] (int a) { if (safe != nullptr && a >= 0) safe->pickProgram (a); });
    };
    progModeBtn.setClickingTogglesState (false);
    kgModeBtn.setClickingTogglesState (false);
    progModeBtn.onClick = [this] { editKeygroups (false); };
    kgModeBtn.onClick = [this] { editKeygroups (true); };
    kgBox.labelChars = 3; kgBox.valueChars = 5;
    kgBox.format = [this] (int v) { return juce::String (v) + "/" + juce::String (std::max (1, keygroupCount())); };
    kgBox.onChange = [this] (int v) { setKeygroup (v - 1); };
    sendSampleBtn.onClick = [this]
    {
        if (transferring) { transfer.cancel(); transferDone(); return; }
        // which of this song's samples?
        juce::StringArray items; std::vector<int> which;
        for (int i = 0; i < kMaxInstruments; ++i)
        {
            const auto& in = proc.song.instruments[(size_t) i];
            if (! in.hasData() || in.isPlugin()) continue;
            const double secs = (double) in.data.size() / std::max (1.0, in.rate);
            const int words = (int) std::min<double> ((double) in.data.size(), in.data.size() * std::min (1.0, 48000.0 / std::max (1.0, in.rate)));
            items.add (juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2) + " " + juce::String (in.name).toUpperCase().substring (0, 16)
                       + " (" + juce::String (secs, 1) + " S - ABOUT " + juce::String ((int) std::ceil (akai::secondsOnTheWire (s950, words))) + " S OVER MIDI)");
            which.push_back (i);
        }
        if (items.isEmpty()) { setStatus ("NO SAMPLES IN THIS SONG TO SEND"); return; }
        if (! menuHook()) return;
        juce::Component::SafePointer<AkaiPanel> safe (this);
        menuHook() ("SEND WHICH SAMPLE TO THE " + model() + "? (IT GOES IN AS A NEW SAMPLE)", items, [safe, which] (int a)
        {
            if (safe != nullptr && a >= 0 && a < (int) which.size()) safe->sendSample (which[(size_t) a]);
        });
    };
    getSampleBtn.onClick = [this]
    {
        if (transferring) { transfer.cancel(); transferDone(); return; }
        getSampleList();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &getBtn, &programBtn, &progModeBtn, &kgModeBtn, &kgBox, &sendSampleBtn, &getSampleBtn }) addAndMakeVisible (c);
    build();
}

void AkaiPanel::setModel (bool isS950)
{
    if (isS950 == s950) return;
    s950 = isS950;
    names.clear(); numbers.clear(); progBlock.clear(); kgBlocks.clear();
    program = -1; keygroup = 0; part = 0; pending.clear(); waiting = false;
    programBtn.setButtonText ("PROGRAM: -");
    transfer = akai::Transfer {}; transferring = false; pickAfterList = false;
    sampleNames.clear(); sampleNumbers.clear();
    updateSampleButtons();
    build();
}

int AkaiPanel::cc()
{
    // the S1000's exclusive channel: this instrument's CHANNEL (1 = exclusive channel 0)
    return juce::jlimit (0, 15, proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, instrument)].midiChannel - 1);
}

void AkaiPanel::setStatus (const juce::String& s)
{
    statusText = s;
    if (onStatus) onStatus (s);
}

void AkaiPanel::send (const std::vector<uint8_t>& msg)
{
    bool sent = false;
    proc.withLock ([&]
    {
        if (auto* m = proc.midiAt (instrument))
            if (auto* out = m->output()) { out->sendNow (msg.data(), (int) msg.size()); sent = true; }
    });
    if (! sent) setStatus ("THE " + model() + "'S MIDI OUTPUT ISN'T CONNECTED");
}

void AkaiPanel::ask (std::vector<uint8_t> msg)
{
    pending.push_back (std::move (msg));
    if (! waiting) askNext();
}

void AkaiPanel::askNext()
{
    if (pending.empty()) { waiting = false; return; }
    auto msg = pending.front();
    pending.erase (pending.begin());
    waiting = true;
    askedAt = juce::Time::getMillisecondCounterHiRes();
    send (msg);
}

void AkaiPanel::opened()
{
    build();
    setStatus (names.isEmpty() ? "GET PROGRAMS: ASK THE " + model() + " WHAT'S IN IT"
                               : statusText);
}

void AkaiPanel::getPrograms()
{
    pending.clear(); waiting = false; pickAfterList = false;
    setStatus ("ASKING THE " + model() + " FOR ITS PROGRAMS...");
    ask (s950 ? akai::s900::requestCatalog (cc()) : akai::requestPrograms (cc()));
}

void AkaiPanel::pickProgram (int index)
{
    if (index < 0 || index >= names.size()) return;
    program = index;
    progBlock.clear(); kgBlocks.clear(); keygroup = 0;
    pending.clear(); waiting = false;
    programBtn.setButtonText ("PROGRAM: " + names[index]);
    setStatus ("READING PROGRAM " + names[index] + "...");
    ask (s950 ? akai::s900::requestProgram (cc(), index < (int) numbers.size() ? numbers[(size_t) index] : index)
              : akai::requestProgram (cc(), index));
}

// ---- samples over MIDI
void AkaiPanel::sendSample (int i)
{
    if (i < 0 || i >= kMaxInstruments) return;
    const auto& in = proc.song.instruments[(size_t) i];
    if (! in.hasData() || in.isPlugin()) return;
    akai::Sample smp;
    smp.name = in.name.empty() ? "SAMPLE " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2).toStdString() : in.name;
    // mono (a stereo sample: both sides mixed), at its own rate - above 48 kHz it's brought down to 44.1
    std::vector<float> f (in.data.size());
    const bool stereo = in.dataR.size() == in.data.size();
    for (size_t k = 0; k < f.size(); ++k) f[k] = stereo ? 0.5f * ((float) in.data[k] + (float) in.dataR[k]) : (float) in.data[k];
    double rate = std::max (1000.0, in.rate);
    double scale = 1.0;
    if (rate > 48000.0) { f = prv::resample (f, rate, 44100.0); scale = 44100.0 / rate; rate = 44100.0; }
    smp.rate = (int) std::lround (rate);
    smp.data.resize (f.size());
    for (size_t k = 0; k < f.size(); ++k) smp.data[k] = (int16_t) juce::jlimit (-32768.0f, 32767.0f, std::round (f[k]));
    if (in.loopLength >= 4)
    {
        smp.loopStart = (int) std::lround (in.loopStart * scale); smp.loopLength = (int) std::lround (in.loopLength * scale);
        smp.loopLength = std::min (smp.loopLength, (int) smp.data.size() - smp.loopStart);
        smp.pingPong = in.pingPong;
    }
    if (s950)
    {
        if ((int) smp.data.size() > akai::s900::kMaxWords) { setStatus ("TOO LONG FOR THE S950 (" + juce::String (akai::s900::kMaxWords / std::max (1, smp.rate)) + " S AT MOST AT THIS RATE)"); return; }
        if ((int) smp.data.size() < akai::s900::kMinWords) smp.data.resize ((size_t) akai::s900::kMinWords, 0);
    }
    pending.clear(); waiting = false;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (s950) transfer.sendS950 (cc(), smp, now); else transfer.sendS1000 (cc(), smp, now);
    transferring = true; gotInto = -1;
    setStatus (juce::String (transfer.status));
    updateSampleButtons();
    transferTick (now);
}

void AkaiPanel::getSampleList()
{
    pending.clear(); waiting = false;
    pickAfterList = true;
    setStatus ("ASKING THE " + model() + " FOR ITS SAMPLES...");
    ask (s950 ? akai::s900::requestCatalog (cc()) : akai::requestSamples (cc()));
}

void AkaiPanel::getSample (int index)
{
    if (index < 0 || index >= sampleNames.size()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    pending.clear(); waiting = false;
    if (s950) transfer.getS950 (cc(), index < (int) sampleNumbers.size() ? sampleNumbers[(size_t) index] : index, sampleNames[index].toStdString(), now);
    else transfer.getS1000 (cc(), index, now);
    transferring = true; gotInto = -1;
    setStatus (juce::String (transfer.status));
    updateSampleButtons();
    transferTick (now);
}

void AkaiPanel::updateSampleButtons()
{
    sendSampleBtn.setButtonText (transferring ? "STOP" : "SEND A SAMPLE...");
    getSampleBtn.setButtonText (transferring ? "STOP" : "GET A SAMPLE...");
    getSampleBtn.setVisible (! transferring);
}

void AkaiPanel::transferTick (double now)
{
    for (auto& m : transfer.poll (now)) send (m);
    if (transfer.state == akai::Transfer::State::Working)
    {
        if (now - lastProgress > 250.0)
        {
            lastProgress = now;
            setStatus (juce::String (transfer.status) + (transfer.progress > 0 ? " " + juce::String ((int) (transfer.progress * 100.0f)) + "%" : juce::String()));
        }
        return;
    }
    transferDone();
}

void AkaiPanel::transferDone()
{
    if (! transferring) return;
    transferring = false;
    juce::String msg (transfer.status);
    if (transfer.state == akai::Transfer::State::Done && ! transfer.result.data.empty() && msg.startsWith ("GOT "))
    {
        // into the first free instrument (no sample, not a synth or MIDI out)
        int slot = -1;
        for (int i = 0; i < kMaxInstruments && slot < 0; ++i)
        {
            const auto& in = proc.song.instruments[(size_t) i];
            if (! in.hasData() && ! in.isPlugin()) slot = i;
        }
        if (slot < 0) msg = "GOT IT, BUT EVERY INSTRUMENT IS IN USE - FREE ONE AND GET IT AGAIN";
        else
        {
            const auto& smp = transfer.result;
            proc.history.instrument (slot, "GET SAMPLE FROM THE " + model());
            proc.withLock ([&]
            {
                auto& in = proc.song.instruments[(size_t) slot];
                in.name = juce::String (smp.name).substring (0, 22).toStdString();
                in.data = smp.data; in.dataR.clear();
                in.rate = smp.rate; in.natRate = 0;
                in.loopStart = smp.loopLength >= 4 ? smp.loopStart : 0;
                in.loopLength = smp.loopLength >= 4 ? smp.loopLength : 0;
                in.pingPong = smp.pingPong;
                in.playStart = in.playEnd = 0;
            });
            gotInto = slot;
            msg += " - IT'S INSTRUMENT " + juce::String::toHexString (slot + 1).toUpperCase().paddedLeft ('0', 2);
        }
    }
    setStatus (msg);
    updateSampleButtons();
}

void AkaiPanel::received (const uint8_t* msg, int n)
{
    if (transferring) { transfer.received (msg, n, juce::Time::getMillisecondCounterHiRes()); return; }
    akai::Reply r;
    if (s950)
    {
        // the S950 (S900): the catalogue, or a whole program with its keygroups
        if (! akai::s900::parse (msg, n, r)) return;
        if (r.op == akai::s900::CAT && pickAfterList)
        {
            pickAfterList = false;
            sampleNames.clear(); sampleNumbers = r.sampleNumbers;
            for (const auto& x : r.sampleNames) sampleNames.add (juce::String (x));
            askNext();
            if (sampleNames.isEmpty()) { setStatus ("THE S950 HAS NO SAMPLES"); return; }
            setStatus (juce::String (sampleNames.size()) + " SAMPLES IN THE S950");
            if (menuHook())
            {
                juce::Component::SafePointer<AkaiPanel> safe (this);
                menuHook() ("GET WHICH SAMPLE FROM THE S950? (IT GOES INTO THE FIRST FREE INSTRUMENT)", sampleNames, [safe] (int a) { if (safe != nullptr && a >= 0) safe->getSample (a); });
            }
            return;
        }
        if (r.op == akai::s900::CAT)
        {
            names.clear(); numbers = r.numbers;
            for (const auto& x : r.names) names.add (juce::String (x));
            setStatus (juce::String (names.size()) + " PROGRAMS IN THE S950 - PICK ONE");
            programBtn.setButtonText (names.isEmpty() ? juce::String ("PROGRAM: -") : juce::String ("PROGRAM: PICK ONE"));
            askNext();
            resized();
        }
        else if (r.op == akai::s900::PRGM && program >= 0 && program < (int) numbers.size() && r.program == numbers[(size_t) program])
        {
            progBlock = r.block; kgBlocks = r.keygroups;
            keygroup = juce::jlimit (0, std::max (0, keygroupCount() - 1), keygroup);
            askNext();
            kgBox.setRange (1, std::max (1, keygroupCount()));
            kgBox.setValue (keygroup + 1);
            setStatus (names[program] + ": " + juce::String (keygroupCount()) + " KEYGROUPS - EDITS GO STRAIGHT IN");
            build();
        }
        return;
    }
    if (! akai::parse (msg, n, r)) return;
    if (r.op == akai::SLIST && pickAfterList)
    {
        pickAfterList = false;
        sampleNames.clear();
        for (const auto& x : r.sampleNames) sampleNames.add (juce::String (x));
        askNext();
        if (sampleNames.isEmpty()) { setStatus ("THE S1000 HAS NO SAMPLES"); return; }
        setStatus (juce::String (sampleNames.size()) + " SAMPLES IN THE S1000");
        if (menuHook())
        {
            juce::Component::SafePointer<AkaiPanel> safe (this);
            menuHook() ("GET WHICH SAMPLE FROM THE S1000? (IT GOES INTO THE FIRST FREE INSTRUMENT)", sampleNames, [safe] (int a) { if (safe != nullptr && a >= 0) safe->getSample (a); });
        }
        return;
    }
    if (r.op == akai::PLIST)
    {
        names.clear();
        for (const auto& s : r.names) names.add (juce::String (s));
        setStatus (juce::String (names.size()) + " PROGRAMS IN THE S1000 - PICK ONE");
        programBtn.setButtonText (names.isEmpty() ? juce::String ("PROGRAM: -") : juce::String ("PROGRAM: PICK ONE"));
        askNext();
        resized();
    }
    else if (r.op == akai::PDATA && r.program == program)
    {
        progBlock = r.block;
        const int groups = juce::jlimit (1, 99, (int) progBlock[42]);
        kgBlocks.assign ((size_t) groups, {});
        for (int k = 0; k < groups; ++k) pending.push_back (akai::requestKeygroup (cc(), program, k));
        setStatus ("READING " + juce::String (groups) + " KEYGROUPS...");
        askNext();
    }
    else if (r.op == akai::KDATA && r.program == program && r.keygroup >= 0 && r.keygroup < (int) kgBlocks.size())
    {
        kgBlocks[(size_t) r.keygroup] = r.block;
        askNext();
        if (pending.empty())
        {
            kgBox.setRange (1, keygroupCount());
            kgBox.setValue (keygroup + 1);
            setStatus (names[program] + ": " + juce::String (keygroupCount()) + " KEYGROUPS - EDITS GO STRAIGHT IN");
            build();
        }
    }
    else if (r.op == akai::REPLY)
    {
        if (r.replyCode != 0) setStatus ("THE S1000 SAID NO (ERROR " + juce::String (r.replyCode) + ")");
        if (waiting) askNext();
    }
}

void AkaiPanel::tick()
{
    std::vector<uint8_t> msg;
    while (proc.readSysex (msg)) received (msg.data(), (int) msg.size());
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (transferring) { transferTick (now); return; }
    if (waiting && now - askedAt > 2500.0)
    {
        waiting = false; pending.clear(); pickAfterList = false;
        setStatus ("NO ANSWER - SEE HELP: " + model() + " EDITOR");
    }
    if (s950 && (dirtyProg || dirtyKg) && ! waiting && now - lastSend >= 150.0)
    {
        // the S950 takes a program whole: its own settings and every keygroup (it becomes the one selected)
        if (progBlock.size() == (size_t) akai::s900::kProgramBytes && program < (int) numbers.size())
            send (akai::s900::programMessage (cc(), numbers[(size_t) program], progBlock, kgBlocks));
        dirtyProg = dirtyKg = false;
        lastSend = now;
    }
    if (! s950 && (dirtyProg || dirtyKg) && ! waiting && now - lastSend >= 80.0)
    {
        if (dirtyProg && progBlock.size() == (size_t) akai::kProgramBytes) send (akai::programMessage (cc(), program, progBlock));
        if (dirtyKg && keygroup < keygroupCount() && kgBlocks[(size_t) keygroup].size() == (size_t) akai::kKeygroupBytes)
            send (akai::keygroupMessage (cc(), program, keygroup, kgBlocks[(size_t) keygroup]));
        dirtyProg = dirtyKg = false;
        lastSend = now;
    }
}

void AkaiPanel::editKeygroups (bool kg)
{
    kgMode = kg;
    part = 0;
    build();
}

void AkaiPanel::showPart (int p)
{
    part = juce::jlimit (0, std::max (0, partCount() - 1), p);
    build();
}

void AkaiPanel::setKeygroup (int k)
{
    keygroup = juce::jlimit (0, std::max (0, keygroupCount() - 1), k);
    kgBox.setValue (keygroup + 1);
    if (kgMode) build();
}

int AkaiPanel::rowValue (int r) const
{
    if (r < 0 || r >= (int) rowFields.size()) return 0;
    const auto& block = kgMode ? (keygroup < keygroupCount() ? kgBlocks[(size_t) keygroup] : progBlock) : progBlock;
    return akai::get (block, rowFields[(size_t) r]);
}

void AkaiPanel::build()
{
    for (auto& b : partBtns) removeChildComponent (b.get());
    for (auto& b : bars) removeChildComponent (b.get());
    partBtns.clear(); bars.clear(); rowFields.clear();
    progModeBtn.setToggleState (! kgMode, juce::dontSendNotification);
    kgModeBtn.setToggleState (kgMode, juce::dontSendNotification);
    kgBox.setVisible (kgMode);
    const auto ps = parts();
    part = juce::jlimit (0, std::max (0, (int) ps.size() - 1), part);
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        auto b = std::make_unique<PixButton> (juce::String (ps[(size_t) i]), true);
        b->setClickingTogglesState (false);
        b->setToggleState (i == part, juce::dontSendNotification);
        b->onClick = [this, i] { showPart (i); };
        addAndMakeVisible (*b);
        partBtns.push_back (std::move (b));
    }
    int longest = 4;
    for (const auto& x : fields()) if (x.group == ps[(size_t) part]) longest = std::max (longest, (int) x.name.size());
    const bool loaded = progBlock.size() == (size_t) (s950 ? akai::s900::kProgramBytes : akai::kProgramBytes);
    for (const auto& x : fields())
    {
        if (x.group != ps[(size_t) part]) continue;
        auto b = std::make_unique<PixSlider> (juce::String (x.name));
        b->labelChars = longest + 1; b->valueChars = 12.5f;
        const akai::Field fx = x;
        b->format = [fx, loaded] (float v) { return loaded ? juce::String (fx.text (fx.lo + juce::roundToInt (v * (fx.hi - fx.lo)))) : juce::String ("-"); };
        b->onChange = [this, fx] (float v)
        {
            const int value = fx.lo + juce::roundToInt (v * (fx.hi - fx.lo));
            if (kgMode)
            {
                if (keygroup >= keygroupCount() || kgBlocks[(size_t) keygroup].empty()) return;
                akai::set (kgBlocks[(size_t) keygroup], fx, value); dirtyKg = true;
            }
            else
            {
                if (progBlock.empty()) return;
                akai::set (progBlock, fx, value); dirtyProg = true;
            }
        };
        b->setEnabled (loaded);
        addAndMakeVisible (*b);
        rowFields.push_back (x);
        bars.push_back (std::move (b));
    }
    refreshBars();
    resized();
    repaint();
    // (the S950: the keygroup's soft / loud sample, by name)
    if (s950 && kgMode && keygroup < keygroupCount() && kgBlocks[(size_t) keygroup].size() == (size_t) akai::s900::kKeygroupBytes)
    {
        const auto& kb = kgBlocks[(size_t) keygroup];
        setStatus ("KG " + juce::String (keygroup + 1) + " SOFT/LOUD: " + juce::String (akai::s900::name (kb, akai::s900::kSoftName))
                   + " / " + juce::String (akai::s900::name (kb, akai::s900::kLoudName)));
    }
    // a zone: its sample, by name
    if (! s950 && kgMode && keygroup < keygroupCount() && kgBlocks[(size_t) keygroup].size() == (size_t) akai::kKeygroupBytes && ps[(size_t) part].rfind ("ZONE ", 0) == 0)
    {
        const int z = ps[(size_t) part].back() - '1';
        const auto sample = akai::fromAkai (kgBlocks[(size_t) keygroup].data() + 34 + 24 * z, 12);
        setStatus ("KEYGROUP " + juce::String (keygroup + 1) + " " + juce::String (ps[(size_t) part]) + ": " + (sample.empty() ? juce::String ("NO SAMPLE") : juce::String (sample)));
    }
}

void AkaiPanel::refreshBars()
{
    for (int r = 0; r < (int) bars.size(); ++r)
    {
        const auto& x = rowFields[(size_t) r];
        const int v = rowValue (r);
        bars[(size_t) r]->setValue (x.hi > x.lo ? (float) (v - x.lo) / (float) (x.hi - x.lo) : 0.0f, false);
        bars[(size_t) r]->repaint();
    }
}

void AkaiPanel::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds();
    {
        auto row = r.removeFromTop (rowH);
        getBtn.setBounds (row.removeFromLeft (charsW (13))); row.removeFromLeft (gap);
        programBtn.setBounds (row.removeFromLeft (std::min (row.getWidth(), charsW (22))));
    }
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        sendSampleBtn.setBounds (row.removeFromLeft (charsW (16))); row.removeFromLeft (gap);
        getSampleBtn.setBounds (row.removeFromLeft (std::min (row.getWidth(), charsW (15))));
    }
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        progModeBtn.setBounds (row.removeFromLeft (charsW (8))); row.removeFromLeft (gap);
        kgModeBtn.setBounds (row.removeFromLeft (charsW (9))); row.removeFromLeft (gap * 3);
        kgBox.setBounds (row.removeFromLeft (std::min (row.getWidth(), kgBox.preferredWidth (px))));
    }
    r.removeFromTop (gap * 2);
    auto row = r.removeFromTop (rowH);
    for (auto& b : partBtns)
    {
        const int w = charsW (b->getButtonText().length() + 1);
        if (w > row.getWidth() && row.getWidth() < r.getWidth()) { r.removeFromTop (gap); row = r.removeFromTop (rowH); }
        b->setBounds (row.removeFromLeft (std::min (w, row.getWidth()))); row.removeFromLeft (gap);
    }
    r.removeFromTop (gap * 3);
    for (auto& b : bars) { b->setBounds (r.removeFromTop (rowH)); r.removeFromTop (gap); }
}

void AkaiPanel::paint (juce::Graphics&) {}
} // namespace prv::ui
