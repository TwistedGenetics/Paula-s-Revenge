// Paula's Revenge - the AKAI S1000 editor (see AkaiPanel.h)
#include "AkaiPanel.h"
#include "ChoiceBox.h"

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
        menuHook() ("WHICH S1000 PROGRAM?", names, [safe] (int a) { if (safe != nullptr && a >= 0) safe->pickProgram (a); });
    };
    progModeBtn.setClickingTogglesState (false);
    kgModeBtn.setClickingTogglesState (false);
    progModeBtn.onClick = [this] { editKeygroups (false); };
    kgModeBtn.onClick = [this] { editKeygroups (true); };
    kgBox.labelChars = 3; kgBox.valueChars = 5;
    kgBox.format = [this] (int v) { return juce::String (v) + "/" + juce::String (std::max (1, keygroupCount())); };
    kgBox.onChange = [this] (int v) { setKeygroup (v - 1); };
    for (auto* c : std::initializer_list<juce::Component*> { &getBtn, &programBtn, &progModeBtn, &kgModeBtn, &kgBox }) addAndMakeVisible (c);
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
    if (! sent) setStatus ("THE S1000'S MIDI OUTPUT ISN'T CONNECTED");
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
    setStatus (names.isEmpty() ? juce::String ("GET PROGRAMS: ASK THE S1000 WHAT'S IN IT")
                               : statusText);
}

void AkaiPanel::getPrograms()
{
    pending.clear(); waiting = false;
    setStatus ("ASKING THE S1000 FOR ITS PROGRAMS...");
    ask (akai::requestPrograms (cc()));
}

void AkaiPanel::pickProgram (int index)
{
    if (index < 0 || index >= names.size()) return;
    program = index;
    progBlock.clear(); kgBlocks.clear(); keygroup = 0;
    pending.clear(); waiting = false;
    programBtn.setButtonText ("PROGRAM: " + names[index]);
    setStatus ("READING PROGRAM " + names[index] + "...");
    ask (akai::requestProgram (cc(), index));
}

void AkaiPanel::received (const uint8_t* msg, int n)
{
    akai::Reply r;
    if (! akai::parse (msg, n, r)) return;
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
    if (waiting && now - askedAt > 2500.0)
    {
        waiting = false; pending.clear();
        setStatus ("NO ANSWER - SEE HELP: S1000 EDITOR");
    }
    if ((dirtyProg || dirtyKg) && ! waiting && now - lastSend >= 80.0)
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
    const bool loaded = progBlock.size() == (size_t) akai::kProgramBytes;
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
    // a zone: its sample, by name
    if (kgMode && keygroup < keygroupCount() && kgBlocks[(size_t) keygroup].size() == (size_t) akai::kKeygroupBytes && ps[(size_t) part].rfind ("ZONE ", 0) == 0)
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
