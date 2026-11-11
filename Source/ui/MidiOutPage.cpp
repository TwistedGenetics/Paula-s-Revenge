// Paula's Revenge - a MIDI-out instrument's page
#include "MidiOutPage.h"
#include "ChoiceBox.h"

namespace prv::ui
{
namespace { const char* kKnobLetters[kInstrumentKnobs] = { "L", "Q", "H", "W" }; }

MidiOutPage::MidiOutPage (PaulaProcessor& p) : proc (p)
{
    Theme::markDarkPage (*this);   // (dark with light text in the dark skin)
    setWantsKeyboardFocus (true);
    setOpaque (true);

    list.getNumRows = [this] { return devices.size(); };
    list.getRowText = [this] (int r) { return devices[r].toUpperCase(); };
    list.onClick = [this] (int r, const juce::MouseEvent&) { list.selected = r; list.repaint(); };
    list.onDoubleClick = [this] (int r) { choose (r); };
    rescanBtn.onClick = [this] { refreshList(); if (onStatus) onStatus (juce::String (devices.size()) + " MIDI OUTPUTS FOUND"); };
    cancelBtn.onClick = [this] { if (inst().isMidiOut()) setBrowsing (false); else if (onClose) onClose(); };
    changeBtn.onClick = [this] { setBrowsing (true); };
    panicBtn.onClick = [this] { proc.midiPanic(); if (onStatus) onStatus ("PANIC: ALL NOTES OFF ON EVERY MIDI OUTPUT"); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    sampleBtn.onClick = [this]
    {
        const int i = proc.editorState.instrument;
        proc.history.instrument (i, "BACK TO A SAMPLE");
        proc.withLock ([&] { auto& in = inst(); in.plugin = in.midiOut = false; in.switchedOff = false; in.pluginKey.clear(); in.pluginName.clear(); });
        proc.updateMixerEffects();
        if (onChanged) onChanged();
        if (onClose) onClose();
    };

    chanBox.labelChars = 8; chanBox.valueChars = 2;
    chanBox.onChange = [this] (int v) { edit ("MIDI CHANNEL", [v] (Instrument& in) { in.midiChannel = v; }); };
    bendBox.labelChars = 5; bendBox.valueChars = 2;
    bendBox.onChange = [this] (int v) { edit ("PITCH BEND RANGE", [v] (Instrument& in) { in.bendRange = v; }); };
    delayBox.labelChars = 6; delayBox.valueChars = 7;
    delayBox.format = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v) + " MS"; };
    delayBox.onChange = [this] (int v) { edit ("MIDI DELAY", [v] (Instrument& in) { in.midiDelayMs = v; }); };
    chordsBtn.onClick = [this] { if (onChords) onChords(); };
    addChildComponent (chordsBtn);
    clockBtn.onClick = [this]
    {
        const bool on = clockBtn.getToggleState();
        edit (on ? "MIDI CLOCK ON" : "MIDI CLOCK OFF", [on] (Instrument& in) { in.midiClock = on; }, false);
        if (onStatus) onStatus (on ? "MIDI CLOCK, START AND STOP GO TO " + juce::String (inst().midiDevice).toUpperCase() + " - IT FOLLOWS THE SONG" : "NO MIDI CLOCK");
    };
    onBtn.onClick = [this] { toggleOn(); };
    trackBox.labelChars = 6; trackBox.valueChars = 1;
    trackBox.onChange = [this] (int) { learner.cancel(); refresh(); };
    learnA.onClick = [this] { learnTrackKnob (0); };
    learnB.onClick = [this] { learnTrackKnob (1); };

    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        ccBox[k] = std::make_unique<NumberBox> ("CC", 0, 127, 74);
        ccBox[k]->labelChars = 3; ccBox[k]->valueChars = 3;
        ccBox[k]->onChange = [this, k] (int v)
        {
            edit (juce::String ("KNOB ") + kKnobLetters[k] + " CC", [v, k] (Instrument& in) { in.midiCC[k] = v; });
            setKnobNames();
        };
        knobBar[k].labelChars = 5; knobBar[k].valueChars = 3.5f;
        knobBar[k].format = [] (float v) { return juce::String (juce::roundToInt (v * 127.0f)); };
        knobBar[k].defaultValue = 0.0f;
        knobBar[k].onChange = [this, k] (float v)
        {
            // sends the CC straight away (and REC AUTO can record it)
            const int i = proc.editorState.instrument;
            proc.withLock ([&] { if (auto* m = proc.midiAt (i)) { m->update (inst()); m->setParameterAt (k, v, 0); } });   // (with the latest settings: a knob just given a new control)
        };
        learnCcBtn[k].onClick = [this, k] { learnCc (k); };
        addChildComponent (*ccBox[k]);
        addChildComponent (knobBar[k]);
        addChildComponent (learnCcBtn[k]);
    }
    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        paramBtn[k] = std::make_unique<PixButton> ("BY NAME...");
        paramBtn[k]->onClick = [this, k] { pickParam (k); };
        addChildComponent (*paramBtn[k]);
    }
    gearBtn.onClick = [this]
    {
        if (! menuHook()) return;
        juce::StringArray names { "NONE (CC NUMBERS)" };
        for (int g = 1; g <= gearCount(); ++g) names.add (gearName (g));
        juce::Component::SafePointer<MidiOutPage> safe (this);
        menuHook() ("WHAT'S ON THE OTHER END?", names, [safe] (int a) { if (safe != nullptr && a >= 0) safe->setGear (a); });
    };
    controlsBtn.setClickingTogglesState (false);
    controlsBtn.onClick = [this] { openControls (! controlsOpen); };
    addChildComponent (gearBtn);
    addChildComponent (controlsBtn);
    for (auto* c : std::initializer_list<juce::Component*> { &list, &rescanBtn, &cancelBtn, &changeBtn, &sampleBtn, &panicBtn, &closeBtn,
                                                             &chanBox, &bendBox, &delayBox, &trackBox, &onBtn, &clockBtn, &learnA, &learnB })
        addChildComponent (c);
}

MidiOutPage::~MidiOutPage()
{
    for (const auto& kv : heldKeys) { const int n = kv.second; proc.withLock ([&] { proc.player.jamNoteOff (n); }); }
}

Instrument& MidiOutPage::inst()
{
    return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
}

template <typename Fn> void MidiOutPage::edit (const juce::String& what, Fn&& fn, bool merge)
{
    proc.history.instrument (proc.editorState.instrument, what, merge);
    proc.withLock ([&] { fn (inst()); });
    repaint();
}

void MidiOutPage::setKnobNames()
{
    const int i = proc.editorState.instrument;
    proc.withLock ([&]
    {
        auto& in = inst();
        for (int k = 0; k < kInstrumentKnobs; ++k)
            in.knobs[k] = AutoTarget { kAutoInstrumentBase + i, k, (juce::String (in.midiDevice).toUpperCase() + ": " + knobTargetName (k)).toStdString() };
    });
    repaint();
}

void MidiOutPage::opened (bool browse)
{
    shown = proc.editorState.instrument;
    trackBox.setValue (proc.editorState.track + 1);
    learner.cancel(); ccLearn = -1;
    setBrowsing (browse || ! inst().isMidiOut());
    grabKeyboardFocus();
}

void MidiOutPage::setBrowsing (bool b)
{
    browsing = b;
    learner.cancel(); ccLearn = -1;
    if (browsing) refreshList();
    refresh();
}

void MidiOutPage::refreshList()
{
    devices = proc.midiHost.deviceNames();
    list.selected = juce::jmax (0, devices.indexOf (juce::String (inst().midiDevice)));
    list.ensureVisible (list.selected);
    list.repaint();
}

void MidiOutPage::choose (int row)
{
    if (row < 0 || row >= devices.size()) return;
    const auto name = devices[row];
    const int i = proc.editorState.instrument;
    const bool was = inst().isMidiOut();
    proc.history.instrument (i, "MIDI OUT " + name.toUpperCase());
    proc.withLock ([&]
    {
        auto& in = inst();
        if (! was)
        {
            // a fresh MIDI-out instrument (the sample goes)
            in = Instrument {};
            in.name = name.toUpperCase().substring (0, 22).toStdString();
        }
        in.plugin = true; in.midiOut = true;
        in.midiDevice = name.toStdString();
        in.pluginKey = "MIDI:" + in.midiDevice;
        in.pluginName = name.toUpperCase().toStdString();
    });
    setKnobNames();
    proc.reloadInstrumentPlugin (i);   // (also tries again when the same device was missing)
    browsing = false;
    refresh();
    if (onChanged) onChanged();
    if (onStatus)
        onStatus (proc.pluginLoadedInstrument (i) ? "PLAYS " + name.toUpperCase() + " ON CHANNEL " + juce::String (inst().midiChannel) + " - THE KEYS PLAY IT TOO"
                                                  : proc.instrumentError (i));
}

void MidiOutPage::learnCc (int k)
{
    if (ccLearn == k) { ccLearn = -1; refresh(); if (onStatus) onStatus ("LEARN CANCELLED"); return; }
    learner.cancel();
    ccLearn = k;
    ccSerialAtArm = proc.ccSerial.load();
    if (onStatus) onStatus (juce::String ("NOW MOVE A KNOB ON YOUR MIDI CONTROLLER: ITS CC GOES ON KNOB ") + kKnobLetters[k] + " (LEARN CC AGAIN CANCELS)");
    refresh();
}

void MidiOutPage::learnTrackKnob (int k)
{
    if (learner.armed && learner.what == 10 + k) { learner.cancel(); refresh(); if (onStatus) onStatus ("LEARN CANCELLED"); return; }
    ccLearn = -1;
    learner.arm (10 + k, [this] { std::vector<float> v; for (auto& b : knobBar) v.push_back (b.value); return v; });
    if (onStatus) onStatus ("NOW MOVE A SEND BAR FOR TRACK " + juce::String (trackBox.getValue()) + (k == 0 ? " AUTO A (V)" : " AUTO B (J)") + ", OR CLICK ON TO SWITCH IT ON/OFF");
    refresh();
}

void MidiOutPage::learned (int param)
{
    const int i = proc.editorState.instrument;
    const juce::String name = juce::String (inst().midiDevice).toUpperCase() + ": "
                            + (param == kAutoOnOff ? juce::String ("ON/OFF") : knobTargetName (juce::jlimit (0, 3, param)));
    const int k = learner.what - 10, tr = trackBox.getValue() - 1;
    proc.history.mixer ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A = " : " AUTO B = ") + name);
    proc.withLock ([&] { proc.song.mixer.trackAuto[tr][k] = AutoTarget { kAutoInstrumentBase + i, param, name.toStdString() }; });
    if (onStatus) onStatus ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A (VXX) NOW MOVES " : " AUTO B (JXX) NOW MOVES ") + name
                            + (param == kAutoOnOff ? juce::String (" (00 = OFF, 01-FF = ON)") : juce::String()));
    learner.cancel();
    refresh();
}

void MidiOutPage::toggleOn()
{
    if (learner.armed) { learned (kAutoOnOff); return; }
    const int i = proc.editorState.instrument;
    const bool off = ! inst().switchedOff;
    proc.history.instrument (i, off ? "INSTRUMENT OFF" : "INSTRUMENT ON");
    proc.withLock ([&] { inst().switchedOff = off; proc.player.setInstrumentOn (i, -1); });
    if (onStatus) onStatus (off ? "INSTRUMENT SWITCHED OFF - IT SENDS NO NOTES" : "INSTRUMENT SWITCHED ON");
    refresh();
}

void MidiOutPage::refresh()
{
    const bool midi = inst().isMidiOut() && ! browsing;
    list.setVisible (browsing);
    rescanBtn.setVisible (browsing);
    cancelBtn.setVisible (browsing);
    for (auto* c : std::initializer_list<juce::Component*> { &changeBtn, &sampleBtn, &panicBtn, &chanBox, &bendBox, &delayBox, &trackBox,
                                                             &onBtn, &clockBtn, &learnA, &learnB, &chordsBtn })
        c->setVisible (midi);
    const int gr = midi ? inst().midiGear : 0;
    if (gr == 0) controlsOpen = false;
    const bool knobs = midi && ! controlsOpen;
    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        const bool named = gr != 0 && inst().midiCC[k] >= kGearKnobBase;
        ccBox[k]->setVisible (knobs && ! named); knobBar[k].setVisible (knobs); learnCcBtn[k].setVisible (knobs && ! named);
        paramBtn[k]->setVisible (knobs && gr != 0);
        paramBtn[k]->setButtonText (named ? knobTargetName (k) : juce::String ("BY NAME..."));
    }
    for (auto* c : std::initializer_list<juce::Component*> { &trackBox, &learnA, &learnB }) c->setVisible (knobs);
    for (auto* c : std::initializer_list<juce::Component*> { &chanBox, &bendBox, &delayBox, &clockBtn, &onBtn }) c->setVisible (midi && ! controlsOpen);   // (ALL CONTROLS: the room for them)
    gearBtn.setVisible (midi);
    gearBtn.setButtonText ("GEAR: " + juce::String (gearName (gr)));
    controlsBtn.setVisible (midi && gr != 0);
    controlsBtn.setToggleState (controlsOpen, juce::dontSendNotification);
    for (auto& b : groupBtns) b->setVisible (midi && controlsOpen);
    for (auto& b : ctlBars) b->setVisible (midi && controlsOpen);
    closeBtn.setVisible (true);
    if (midi)
    {
        const auto& in = inst();
        chanBox.setValue (in.midiChannel);
        bendBox.setValue (in.bendRange);
        delayBox.setValue (in.midiDelayMs);
        clockBtn.setToggleState (in.midiClock, juce::dontSendNotification);
        const bool on = proc.player.instrumentIsOn (proc.editorState.instrument);
        onBtn.setToggleState (on, juce::dontSendNotification);
        onBtn.setButtonText (on ? "ON" : "OFF");
        auto* m = proc.midiAt (proc.editorState.instrument);
        for (int k = 0; k < kInstrumentKnobs; ++k)
        {
            if (in.midiCC[k] < kGearKnobBase) ccBox[k]->setValue (in.midiCC[k]);
            if (m != nullptr && m->knobValue (k) >= 0) knobBar[k].setValue (m->knobValue (k));
            learnCcBtn[k].setToggleState (ccLearn == k, juce::dontSendNotification);
        }
        learnA.setToggleState (learner.armed && learner.what == 10, juce::dontSendNotification);
        learnB.setToggleState (learner.armed && learner.what == 11, juce::dontSendNotification);
        // older songs / first open: knobs L, Q, H, W point at their CCs
        bool named = true;
        for (const auto& t : in.knobs) named = named && t.param >= 0;
        if (! named) setKnobNames();
    }
    resized();
    repaint();
}

void MidiOutPage::tick()
{
    if (proc.editorState.instrument != shown)
    {
        shown = proc.editorState.instrument;
        learner.cancel(); ccLearn = -1;
        if (! inst().isMidiOut() && ! browsing) { if (onClose) onClose(); return; }
        refresh();
    }
    if (! browsing && inst().isMidiOut())
    {
        if (onBtn.getToggleState() != proc.player.instrumentIsOn (proc.editorState.instrument)) refresh();
        // the send bars follow what the pattern sends
        if (auto* m = proc.midiAt (proc.editorState.instrument))
            for (int k = 0; k < kInstrumentKnobs; ++k)
                if (m->knobValue (k) >= 0 && std::abs (m->knobValue (k) - knobBar[k].value) > 0.004f && ! learner.armed) knobBar[k].setValue (m->knobValue (k));
    }
    // LEARN CC: the next CC that comes in from a MIDI controller
    if (ccLearn >= 0 && proc.ccSerial.load() != ccSerialAtArm)
    {
        const int k = ccLearn, cc = juce::jlimit (0, 127, proc.lastCcNumber.load());
        ccLearn = -1;
        edit (juce::String ("KNOB ") + kKnobLetters[k] + " CC", [cc, k] (Instrument& in) { in.midiCC[k] = cc; }, false);
        setKnobNames();
        if (onStatus) onStatus (juce::String ("KNOB ") + kKnobLetters[k] + " NOW SENDS CC " + juce::String (cc) + " - USE " + kKnobLetters[k] + "XX ON THIS INSTRUMENT'S NOTES");
        refresh();
    }
    const int moved = learner.poll();
    if (moved >= 0) learned (moved);
}

bool MidiOutPage::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (learner.armed || ccLearn >= 0) { learner.cancel(); ccLearn = -1; refresh(); return true; }
        if (browsing && inst().isMidiOut()) { setBrowsing (false); return true; }
        if (onClose) onClose();
        return true;
    }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
    if (browsing)
    {
        const int code = k.getKeyCode();
        if (code == juce::KeyPress::returnKey) { choose (list.selected); return true; }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            list.selected = juce::jlimit (0, std::max (0, devices.size() - 1), list.selected + (code == juce::KeyPress::upKey ? -1 : 1));
            list.ensureVisible (list.selected);
            return true;
        }
    }
    // the computer keyboard plays it
    static const char* lower = "zsxdcvgbhnjm,l.;/";
    static const char* upper = "q2w3er5t6y7ui9o0p";
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    int note = -1;
    for (int i = 0; lower[i]; ++i) if (ch == (juce::juce_wchar) lower[i]) note = proc.editorState.octave * 12 + i + 1;
    for (int i = 0; upper[i]; ++i) if (ch == (juce::juce_wchar) upper[i]) note = (proc.editorState.octave + 1) * 12 + i + 1;
    if (note <= 0) return false;
    note = juce::jlimit (1, (int) kNoteMax, note);
    if (heldKeys.count (k.getKeyCode()) == 0)
    {
        heldKeys[k.getKeyCode()] = note;
        proc.withLock ([&] { proc.player.jamNoteOn (note, proc.editorState.instrument); });
        proc.keyNoteInput (note, true);   // (CAPTURE hears it)
    }
    return true;
}

bool MidiOutPage::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const int n = it->second;
            proc.withLock ([&] { proc.player.jamNoteOff (n); });
            proc.keyNoteInput (n, false);   // (CAPTURE hears it)
            it = heldKeys.erase (it);
        }
        else ++it;
    }
    return false;
}

// ---- GEAR
juce::String MidiOutPage::knobTargetName (int k)
{
    const auto& in = inst();
    const int c = in.midiCC[juce::jlimit (0, kInstrumentKnobs - 1, k)];
    if (c >= kGearKnobBase)
        if (const auto* map = gearMap (in.midiGear))
            if (c - kGearKnobBase < (int) map->params.size()) return juce::String (map->params[(size_t) (c - kGearKnobBase)].label());
    return "CC " + juce::String (std::min (c, 127));
}

void MidiOutPage::setGear (int id)
{
    id = juce::jlimit (0, gearCount(), id);
    // the knobs: that synth's usual four (NONE: back to CC numbers)
    int want[kInstrumentKnobs] { 74, 71, 73, 10 };
    if (const auto* map = gearMap (id))
    {
        const char* picks[3][kInstrumentKnobs] = {
            { "FILTER: CUTOFF", "FILTER: RESONANCE", "FILTER EG: INTENSITY", "AMP EG: RELEASE" },
            { "FILTER: CUTOFF", "FILTER: RESONANCE", "OSC: A", "OSC: B" },
            { "FILTER: CUTOFF", "ROLLERS: BLEND", "ROLLERS: DETUNE", "ROLLERS: PITCH DROP" } };
        for (int k = 0; k < kInstrumentKnobs; ++k)
            for (size_t n = 0; n < map->params.size(); ++n)
                if (id <= 3 && map->params[n].label() == picks[id - 1][k]) want[k] = kGearKnobBase + (int) n;
    }
    edit ("GEAR: " + juce::String (gearName (id)), [id, &want] (Instrument& in)
    {
        in.midiGear = id;
        for (int k = 0; k < kInstrumentKnobs; ++k) in.midiCC[k] = want[k];
    }, false);
    ctlValues.clear();
    controlsGroup = 0;
    setKnobNames();
    if (controlsOpen) buildControls();
    refresh();
    if (onStatus)
        onStatus (id == 0 ? juce::String ("NO GEAR: THE KNOBS SEND CC NUMBERS")
                          : "GEAR: " + juce::String (gearName (id)) + " - KNOBS L Q H W: " + knobTargetName (0) + ", " + knobTargetName (1) + ", "
                            + knobTargetName (2) + ", " + knobTargetName (3));
}

void MidiOutPage::setKnobParam (int k, int param)
{
    if (k < 0 || k >= kInstrumentKnobs) return;
    const auto* map = gearMap (inst().midiGear);
    static const int plain[kInstrumentKnobs] { 74, 71, 73, 10 };
    if (! (map != nullptr && param < (int) map->params.size())) param = -1;
    const int v = param >= 0 ? kGearKnobBase + param : inst().midiCC[k] < kGearKnobBase ? inst().midiCC[k] : plain[k];
    edit (juce::String ("KNOB ") + kKnobLetters[k] + " = " + (param >= 0 && map != nullptr ? juce::String (map->params[(size_t) param].label()) : juce::String ("A CC")),
          [k, v] (Instrument& in) { in.midiCC[k] = v; }, false);
    setKnobNames();
    refresh();
    if (onStatus) onStatus (juce::String ("KNOB ") + kKnobLetters[k] + " NOW SENDS " + knobTargetName (k) + " - USE " + kKnobLetters[k] + "XX ON THIS INSTRUMENT'S NOTES");
}

void MidiOutPage::pickParam (int k)
{
    const auto* map = gearMap (inst().midiGear);
    if (map == nullptr || ! menuHook()) return;
    const auto groups = map->groups();
    juce::StringArray names;
    for (const auto& gname : groups) names.add (gname);
    names.add ("A CC NUMBER INSTEAD");
    juce::Component::SafePointer<MidiOutPage> safe (this);
    menuHook() (juce::String ("KNOB ") + kKnobLetters[k] + ": WHICH PART?", names, [safe, k, groups] (int a)
    {
        if (safe == nullptr || a < 0) return;
        if (a >= (int) groups.size()) { safe->setKnobParam (k, -1); return; }
        const auto* m = gearMap (safe->inst().midiGear);
        if (m == nullptr) return;
        juce::StringArray ps; std::vector<int> idx;
        for (size_t n = 0; n < m->params.size(); ++n)
            if (m->params[n].group == groups[(size_t) a]) { ps.add (juce::String (m->params[n].name) + "  (" + m->params[n].where() + ")"); idx.push_back ((int) n); }
        menuHook() (juce::String ("KNOB ") + kKnobLetters[k] + ": " + groups[(size_t) a], ps, [safe, k, idx] (int b)
        {
            if (safe != nullptr && b >= 0 && b < (int) idx.size()) safe->setKnobParam (k, idx[(size_t) b]);
        });
    });
}

void MidiOutPage::openControls (bool open)
{
    controlsOpen = open && inst().midiGear != 0;
    learner.cancel(); ccLearn = -1;
    if (controlsOpen) buildControls();
    refresh();
}

void MidiOutPage::showGroup (int g)
{
    controlsGroup = g;
    buildControls();
    refresh();
}

void MidiOutPage::buildControls()
{
    for (auto& b : groupBtns) removeChildComponent (b.get());
    for (auto& b : ctlBars) removeChildComponent (b.get());
    groupBtns.clear(); ctlBars.clear(); ctlParams.clear(); ctlNames.clear(); ctlWhere.clear();
    const auto* map = gearMap (inst().midiGear);
    if (map == nullptr) return;
    const auto groups = map->groups();
    controlsGroup = juce::jlimit (0, std::max (0, (int) groups.size() - 1), controlsGroup);
    for (int gi = 0; gi < (int) groups.size(); ++gi)
    {
        auto b = std::make_unique<PixButton> (juce::String (groups[(size_t) gi]), true);
        b->setClickingTogglesState (false);
        b->setToggleState (gi == controlsGroup, juce::dontSendNotification);
        b->onClick = [this, gi] { showGroup (gi); };
        addChildComponent (*b);
        groupBtns.push_back (std::move (b));
    }
    int longest = 4;
    for (const auto& p : map->params) if (p.group == groups[(size_t) controlsGroup]) longest = std::max (longest, (int) p.name.size());
    for (size_t n = 0; n < map->params.size(); ++n)
    {
        const auto& p = map->params[n];
        if (p.group != groups[(size_t) controlsGroup]) continue;
        auto b = std::make_unique<PixSlider> (juce::String (p.name));
        b->labelChars = longest + 1; b->valueChars = 5.5f;
        const int steps = p.steps;
        b->format = [steps] (float v) { return steps >= 2 ? juce::String (juce::roundToInt (v * (steps - 1)) + 1) + "/" + juce::String (steps) : juce::String (juce::roundToInt (v * 127.0f)); };
        b->defaultValue = 0.5f;
        const auto it = ctlValues.find ((int) n);
        b->value = it != ctlValues.end() ? it->second : (steps >= 2 ? 0.0f : 0.5f);
        const int param = (int) n;
        b->onChange = [this, param] (float v)
        {
            ctlValues[param] = v;
            const int i = proc.editorState.instrument;
            proc.withLock ([&] { if (auto* m = proc.midiAt (i)) { m->update (inst()); m->sendGear (param, v, 0); } });
        };
        addChildComponent (*b);
        ctlNames.add (juce::String (p.name));
        ctlParams.push_back (param);
        ctlBars.push_back (std::move (b));
    }
}

void MidiOutPage::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    g.fillAll (Theme::palFor (*this).panel);
    const int i = proc.editorState.instrument;
    const juce::String title = "INSTRUMENT " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2)
                             + (browsing ? juce::String (": PICK A MIDI OUTPUT") : juce::String (": MIDI OUT"));
    PixelFont::drawLeft (g, title, titleArea, px, Theme::palFor (*this).ink);
    auto fit = [px] (juce::String t, float w) { const int n = std::max (4, (int) (w / (px * PixelFont::kCellW))); return t.length() > n ? t.substring (0, n - 2) + ".." : t; };

    if (browsing)
    {
        const juce::String t = devices.isEmpty() ? juce::String ("NO MIDI OUTPUTS - CONNECT ONE, THEN RESCAN")
                                                 : juce::String (devices.size()) + " OUTPUTS - DOUBLE-CLICK ONE";
        PixelFont::drawLeft (g, fit (t, infoArea.getWidth()), infoArea, px, Theme::palFor (*this).ink);
        return;
    }
    const auto& in = inst();
    const bool loaded = proc.pluginLoadedInstrument (i);
    juce::StringArray lines;
    lines.add (loaded ? "PLAYS " + juce::String (in.midiDevice).toUpperCase() + " ON CHANNEL " + juce::String (in.midiChannel)
                      : "!" + juce::String (in.midiDevice).toUpperCase() + " ISN'T CONNECTED");
    lines.add (! loaded ? juce::String ("SILENT, SETTINGS KEPT - TRY CHANGE DEVICE...")
               : ! proc.player.instrumentIsOn (i) ? juce::String ("SWITCHED OFF: SENDS NO NOTES UNTIL IT IS ON AGAIN")
               : juce::String ("NOT IN RENDERS - STOP TWICE FOR PANIC"));
    if (in.midiGear != 0)
        if (const auto* map = gearMap (in.midiGear))
            lines.add (controlsOpen ? juce::String ("MOVE ONE: IT'S SENT STRAIGHT AWAY") : juce::String (map->note));
    auto r = infoArea;
    for (auto& l : lines) { auto row = r.removeFromTop (px * 11); PixelFont::drawLeft (g, fit (l, row.getWidth()), row, px, Theme::palFor (*this).ink); }

    if (controlsOpen)
    {
        for (size_t r = 0; r < ctlWhere.size() && r < ctlParams.size(); ++r)
            if (const auto* map = gearMap (in.midiGear))
                PixelFont::drawLeft (g, juce::String (map->params[(size_t) ctlParams[r]].where()), ctlWhere[r], px, Theme::palFor (*this).ink);
        return;
    }
    for (int k = 0; k < kInstrumentKnobs; ++k)
        PixelFont::drawLeft (g, (in.midiGear != 0 ? juce::String() : juce::String ("KNOB ")) + kKnobLetters[k] + "XX", knobLabel[k], px, Theme::palFor (*this).ink);
    const int tr = trackBox.getValue() - 1;
    const auto& a = proc.song.mixer.trackAuto[tr][0];
    const auto& b = proc.song.mixer.trackAuto[tr][1];
    const int room = ((int) (trackArea.getWidth() / (px * PixelFont::kCellW)) - 21) / 2;
    const juce::String ta = "TRACK " + juce::String (tr + 1) + ":  A = " + (a.valid() ? shortTargetName (a.name, room) : juce::String ("-"))
                          + "   B = " + (b.valid() ? shortTargetName (b.name, room) : juce::String ("-"));
    PixelFont::drawLeft (g, fit (ta, trackArea.getWidth()), trackArea, px, Theme::palFor (*this).ink);
}

void MidiOutPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds().reduced (gap);
    auto head = r.removeFromTop (rowH);
    closeBtn.setBounds (head.removeFromRight (charsW (5)));
    titleArea = head.toFloat();
    r.removeFromTop (gap);

    if (browsing)
    {
        auto bottom = r.removeFromBottom (rowH);
        rescanBtn.setBounds (bottom.removeFromLeft (charsW (6))); bottom.removeFromLeft (gap);
        cancelBtn.setBounds (bottom.removeFromRight (charsW (6))); bottom.removeFromRight (gap);
        infoArea = bottom.toFloat();
        r.removeFromBottom (gap);
        list.setBounds (r);
        return;
    }
    {
        // the bottom row: back to a sample, and the chord table (for CHORD)
        auto row = r.removeFromBottom (rowH);
        sampleBtn.setBounds (row.removeFromLeft (charsW (16))); row.removeFromLeft (gap * 3);
        chordsBtn.setBounds (row.getWidth() >= charsW (9) ? row.removeFromLeft (charsW (9)) : juce::Rectangle<int>());
    }
    r.removeFromBottom (gap);
    infoArea = r.removeFromTop (rowH * (inst().midiGear != 0 ? 3 : 2)).toFloat();
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        changeBtn.setBounds (row.removeFromLeft (charsW (16))); row.removeFromLeft (gap * 3);
        panicBtn.setBounds (row.removeFromLeft (charsW (5)));
    }
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        gearBtn.setBounds (row.removeFromLeft (charsW (26))); row.removeFromLeft (gap);
        controlsBtn.setBounds (controlsBtn.isVisible() ? row.removeFromLeft (charsW (15)) : juce::Rectangle<int>());
    }
    if (! controlsOpen)
    {
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        onBtn.setBounds (row.removeFromRight (charsW (4))); row.removeFromRight (gap);
        chanBox.setBounds (row.removeFromLeft (chanBox.preferredWidth (px))); row.removeFromLeft (gap * 3);
        bendBox.setBounds (row.removeFromLeft (std::min (row.getWidth(), bendBox.preferredWidth (px))));
    }
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        delayBox.setBounds (row.removeFromLeft (delayBox.preferredWidth (px))); row.removeFromLeft (gap * 3);
        clockBtn.setBounds (row.removeFromLeft (std::min (row.getWidth(), charsW (15))));
    }
    }
    r.removeFromTop (gap * 3);
    if (controlsOpen)
    {
        // the groups as tabs (as many rows as they need), then the group's controls
        auto row = r.removeFromTop (rowH);
        for (auto& gb : groupBtns)
        {
            const int w = charsW (gb->getButtonText().length() + 1);
            if (w > row.getWidth() && row.getWidth() < r.getWidth()) { r.removeFromTop (gap); row = r.removeFromTop (rowH); }
            gb->setBounds (row.removeFromLeft (std::min (w, row.getWidth()))); row.removeFromLeft (gap);
        }
        r.removeFromTop (gap * 3);
        ctlWhere.clear();
        // (one column, or two when they don't fit)
        const int fits = std::max (1, (r.getHeight() + gap) / (rowH + gap));
        const int cols = (int) ctlBars.size() > fits ? 2 : 1;
        const int perCol = (int) (ctlBars.size() + (size_t) cols - 1) / cols;
        const int colW = (r.getWidth() - gap * 4 * (cols - 1)) / cols;
        for (int n = 0; n < (int) ctlBars.size(); ++n)
        {
            const int c = n / perCol, row = n % perCol;
            juce::Rectangle<int> cr (r.getX() + c * (colW + gap * 4), r.getY() + row * (rowH + gap), colW, rowH);
            if (cols == 1) { ctlWhere.push_back (cr.removeFromRight (charsW (11)).toFloat()); cr.removeFromRight (gap * 2); }
            ctlBars[(size_t) n]->setBounds (cr);
        }
        return;
    }
    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        auto row = r.removeFromTop (rowH); r.removeFromTop (gap);
        const bool named = inst().midiGear != 0 && inst().midiCC[k] >= kGearKnobBase;
        knobLabel[k] = row.removeFromLeft (charsW (inst().midiGear != 0 ? 3 : 8)).toFloat(); row.removeFromLeft (gap);
        knobBar[k].setLabel (named ? juce::String() : juce::String ("SEND"));
        knobBar[k].labelChars = named ? 0 : 5;
        if (named) { paramBtn[k]->setBounds (row.removeFromLeft (charsW (20))); row.removeFromLeft (gap * 2); }
        else
        {
            ccBox[k]->setBounds (row.removeFromLeft (ccBox[k]->preferredWidth (px))); row.removeFromLeft (gap);
            if (inst().midiGear != 0) { paramBtn[k]->setBounds (row.removeFromLeft (charsW (11))); row.removeFromLeft (gap * 2); }
            else row.removeFromLeft (gap);
        }
        if (! named) { learnCcBtn[k].setBounds (row.removeFromRight (charsW (8))); row.removeFromRight (gap * 2); }
        knobBar[k].setBounds (row);
    }
    r.removeFromTop (gap * 2);
    trackArea = r.removeFromTop (rowH).toFloat();
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        trackBox.setBounds (row.removeFromLeft (trackBox.preferredWidth (px))); row.removeFromLeft (gap * 2);
        learnA.setBounds (row.removeFromLeft (charsW (16))); row.removeFromLeft (gap);
        learnB.setBounds (row.removeFromLeft (std::min (row.getWidth(), charsW (16))));
    }
}
} // namespace prv::ui
