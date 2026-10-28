// Paula's Revenge - a plug-in instrument's page
#include "SynthPage.h"

namespace prv::ui
{
namespace { const char* kKnobLetters[kInstrumentKnobs] = { "L", "Q", "H", "W" }; }

SynthPage::SynthPage (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    list.getNumRows = [this] { return (int) choices.size(); };
    list.getRowText = [this] (int r)
    {
        const auto& d = choices[(size_t) r];
        return d.name.toUpperCase() + "  (" + d.manufacturerName.toUpperCase() + ", " + (d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName.toUpperCase()) + ")";
    };
    list.onClick = [this] (int r, const juce::MouseEvent&) { list.selected = r; list.repaint(); };
    list.onDoubleClick = [this] (int r) { choose (r); };
    rescanBtn.onClick = [this] { proc.pluginHost.startScan(); };
    cancelBtn.onClick = [this] { if (inst().isPlugin()) setBrowsing (false); else if (onClose) onClose(); };

    windowBtn.onClick = [this] { openWindow(); };
    changeBtn.onClick = [this] { setBrowsing (true); };
    sampleBtn.onClick = [this]
    {
        // back to an ordinary (empty) sample instrument
        const int i = proc.editorState.instrument;
        proc.history.instrument (i, "BACK TO A SAMPLE");
        proc.withLock ([&] { auto& in = inst(); in.plugin = false; in.switchedOff = false; in.pluginKey.clear(); in.pluginName.clear(); in.pluginState.clear(); });
        proc.updateMixerEffects();
        if (onChanged) onChanged();
        if (onClose) onClose();
    };
    closeBtn.onClick = [this] { if (onClose) onClose(); };

    outBox.labelChars = 7; outBox.valueChars = 6;
    outBox.format = [] (int v) { return v == 0 ? juce::String ("AUTO") : "TRACK " + juce::String (v); };
    outBox.onChange = [this] (int v)
    {
        proc.history.instrument (proc.editorState.instrument, "PLUG-IN OUTPUT", true);
        proc.withLock ([&] { inst().pluginOutput = v - 1; });
        repaint();
    };
    bendBox.labelChars = 5; bendBox.valueChars = 2;
    bendBox.onChange = [this] (int v)
    {
        proc.history.instrument (proc.editorState.instrument, "PITCH BEND RANGE", true);
        proc.withLock ([&] { inst().bendRange = v; });
    };
    trackBox.labelChars = 6; trackBox.valueChars = 1;
    trackBox.onChange = [this] (int) { repaint(); };

    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        learnK[k].onClick = [this, k] { learnKnob (k); };
        clearK[k].onClick = [this, k]
        {
            proc.history.instrument (proc.editorState.instrument, juce::String ("CLEAR KNOB ") + kKnobLetters[k]);
            proc.withLock ([&] { inst().knobs[k] = AutoTarget {}; });
            repaint();
        };
    }
    onBtn.onClick = [this] { toggleOn(); };
    chordsBtn.onClick = [this] { if (onChords) onChords(); };
    addChildComponent (chordsBtn);
    chordBtn.onClick = [this]
    {
        const bool on = chordBtn.getToggleState();
        proc.history.instrument (proc.editorState.instrument, on ? "CHORD ON" : "CHORD OFF");
        proc.withLock ([&] { inst().chord = on; });
        if (onStatus) onStatus (on ? "CHORD ON: 0XY ON A NOTE'S LINE PLAYS CHORD SLOT XY, HELD (RIGHT-CLICK IT IN THE PATTERN FOR THE LIST)" : "CHORD OFF: 0XY IS AN ARPEGGIO");
    };
    learnA.onClick = [this] { learnTrackKnob (0); };
    learnB.onClick = [this] { learnTrackKnob (1); };

    for (auto* c : std::initializer_list<juce::Component*> { &list, &rescanBtn, &cancelBtn, &windowBtn, &changeBtn, &sampleBtn, &closeBtn,
                                                             &outBox, &bendBox, &trackBox, &learnA, &learnB, &onBtn, &chordBtn })
        addChildComponent (c);
    for (int k = 0; k < kInstrumentKnobs; ++k) { addChildComponent (learnK[k]); addChildComponent (clearK[k]); }
}

SynthPage::~SynthPage()
{
    for (const auto& kv : heldKeys) { const int n = kv.second; proc.withLock ([&] { proc.player.jamNoteOff (n); }); }
}

Instrument& SynthPage::inst()
{
    return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
}

void SynthPage::opened (bool browse)
{
    shown = proc.editorState.instrument;
    trackBox.setValue (proc.editorState.track + 1);
    learner.cancel();
    setBrowsing (browse || ! inst().isPlugin());
    grabKeyboardFocus();
}

void SynthPage::setBrowsing (bool b)
{
    browsing = b;
    learner.cancel();
    if (browsing)
    {
        refreshList();
        if (choices.empty() && ! proc.pluginHost.isScanning()) proc.pluginHost.startScan();
    }
    refresh();
}

void SynthPage::refreshList()
{
    choices = proc.pluginHost.instruments();
    list.selected = 0;
    for (int i = 0; i < (int) choices.size(); ++i)
        if (prv::PluginHost::keyFor (choices[(size_t) i]) == inst().pluginKey) list.selected = i;
    list.ensureVisible (list.selected);
    list.repaint();
}

void SynthPage::choose (int row)
{
    if (row < 0 || row >= (int) choices.size()) return;
    const auto d = choices[(size_t) row];
    const int i = proc.editorState.instrument;
    proc.capturePluginStates();
    proc.history.instrument (i, "PLUG-IN INSTRUMENT " + d.name.toUpperCase());
    proc.withLock ([&]
    {
        auto& in = inst();
        const auto keepName = in.name;
        in = Instrument {};
        in.plugin = true;
        in.pluginKey = prv::PluginHost::keyFor (d);
        in.pluginName = d.name.toUpperCase().toStdString();
        in.name = in.pluginName.substr (0, 22);
        in.switchedOff = false;
        juce::ignoreUnused (keepName);
    });
    proc.updateMixerEffects();
    browsing = false;
    refresh();
    if (onChanged) onChanged();
    if (onStatus)
        onStatus (proc.pluginLoadedInstrument (i) ? juce::String (inst().pluginName) + " LOADED - PLAY IT WITH THE KEYS, OR OPEN ITS WINDOW"
                                                  : juce::String (inst().pluginName) + ": " + proc.pluginHost.lastError (inst().pluginKey));
}

std::vector<float> SynthPage::readParams()
{
    if (auto* s = proc.synthAt (proc.editorState.instrument)) return prv::PluginHost::parameterValues (s->instance());
    return {};
}

void SynthPage::learnKnob (int k)
{
    if (! proc.pluginLoadedInstrument (proc.editorState.instrument)) { if (onStatus) onStatus ("THE PLUG-IN ISN'T LOADED"); return; }
    learner.arm (k, [this] { return readParams(); });
    openWindow();
    if (onStatus) onStatus (juce::String ("NOW MOVE A CONTROL IN THE PLUG-IN'S WINDOW FOR KNOB ") + kKnobLetters[k] + ", OR CLICK ON TO SWITCH IT ON/OFF (LEARN AGAIN STOPS)");
    repaint();
}

void SynthPage::learnTrackKnob (int k)
{
    if (! proc.pluginLoadedInstrument (proc.editorState.instrument)) { if (onStatus) onStatus ("THE PLUG-IN ISN'T LOADED"); return; }
    learner.arm (10 + k, [this] { return readParams(); });
    openWindow();
    if (onStatus) onStatus ("NOW MOVE A CONTROL IN THE PLUG-IN'S WINDOW FOR TRACK " + juce::String (trackBox.getValue()) + (k == 0 ? " AUTO A (V)" : " AUTO B (J)") + ", OR CLICK ON TO SWITCH IT ON/OFF");
    repaint();
}

void SynthPage::openWindow()
{
    const int i = proc.editorState.instrument;
    if (auto* s = proc.synthAt (i))
        proc.pluginHost.openEditor (s->instance(), "INSTRUMENT " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2) + ": " + juce::String (inst().pluginName));
    else if (onStatus) onStatus ("THE PLUG-IN ISN'T LOADED: " + proc.pluginHost.lastError (inst().pluginKey));
}

void SynthPage::refresh()
{
    const bool plug = inst().isPlugin() && ! browsing;
    list.setVisible (browsing);
    rescanBtn.setVisible (browsing);
    cancelBtn.setVisible (browsing);
    for (auto* c : std::initializer_list<juce::Component*> { &windowBtn, &changeBtn, &sampleBtn, &outBox, &bendBox, &trackBox, &learnA, &learnB, &onBtn, &chordBtn, &chordsBtn })
        c->setVisible (plug);
    for (int k = 0; k < kInstrumentKnobs; ++k) { learnK[k].setVisible (plug); clearK[k].setVisible (plug); }
    closeBtn.setVisible (true);
    if (plug)
    {
        outBox.setValue (inst().pluginOutput + 1);
        bendBox.setValue (inst().bendRange);
        const bool on = proc.player.instrumentIsOn (proc.editorState.instrument);
        onBtn.setToggleState (on, juce::dontSendNotification);
        onBtn.setButtonText (on ? "ON" : "OFF");
        chordBtn.setToggleState (inst().chord, juce::dontSendNotification);
        const bool loaded = proc.pluginLoadedInstrument (proc.editorState.instrument);
        windowBtn.setEnabled (loaded);
        for (auto& b : learnK) b.setEnabled (loaded);
        learnA.setEnabled (loaded); learnB.setEnabled (loaded);
        for (int k = 0; k < kInstrumentKnobs; ++k) learnK[k].setToggleState (learner.armed && learner.what == k, juce::dontSendNotification);
        learnA.setToggleState (learner.armed && learner.what == 10, juce::dontSendNotification);
        learnB.setToggleState (learner.armed && learner.what == 11, juce::dontSendNotification);
    }
    resized();
    repaint();
}

void SynthPage::tick()
{
    if (proc.editorState.instrument != shown)
    {
        shown = proc.editorState.instrument;
        learner.cancel();
        if (! inst().isPlugin() && ! browsing) { if (onClose) onClose(); return; }
        refresh();
    }
    if (browsing)
    {
        const auto s = proc.pluginHost.scanStatus();
        if (s != lastScan) { lastScan = s; repaint(); }
        if ((int) proc.pluginHost.instruments().size() != (int) choices.size()) refreshList();
    }
    // the ON button follows V / J switching while the song plays
    if (! browsing && inst().isPlugin() && onBtn.getToggleState() != proc.player.instrumentIsOn (proc.editorState.instrument)) refresh();
    // LEARN: the first control that moves
    const int moved = learner.poll();
    if (moved >= 0) learned (moved);
}

void SynthPage::learned (int param)
{
    const int i = proc.editorState.instrument;
    auto* s = proc.synthAt (i);
    const juce::String name = juce::String (inst().pluginName) + ": "
                            + (param == kAutoOnOff ? juce::String ("ON/OFF")
                               : s ? prv::PluginHost::parameterName (s->instance(), param) : juce::String ("#") + juce::String (param));
    const juce::String how = param == kAutoOnOff ? " (00 = OFF, 01-FF = ON)" : "";
    const AutoTarget t { kAutoInstrumentBase + i, param, name.toStdString() };
    if (learner.what < 10)
    {
        const int k = learner.what;
        proc.history.instrument (i, juce::String ("KNOB ") + kKnobLetters[k] + " = " + name);
        proc.withLock ([&] { inst().knobs[k] = t; });
        if (onStatus) onStatus (juce::String ("KNOB ") + kKnobLetters[k] + " NOW MOVES " + name + " - USE " + kKnobLetters[k] + "XX ON THIS INSTRUMENT'S NOTES" + how);
    }
    else
    {
        const int k = learner.what - 10, tr = trackBox.getValue() - 1;
        proc.history.mixer ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A = " : " AUTO B = ") + name);
        proc.withLock ([&] { proc.song.mixer.trackAuto[tr][k] = t; });
        if (onStatus) onStatus ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A (VXX) NOW MOVES " : " AUTO B (JXX) NOW MOVES ") + name + how);
    }
    learner.cancel();
    refresh();
}

void SynthPage::toggleOn()
{
    // with LEARN armed, ON is learned: the knob then switches the instrument on and off
    if (learner.armed) { learned (kAutoOnOff); return; }
    const int i = proc.editorState.instrument;
    const bool off = ! inst().switchedOff;
    proc.history.instrument (i, off ? "INSTRUMENT OFF" : "INSTRUMENT ON");
    proc.withLock ([&] { inst().switchedOff = off; proc.player.setInstrumentOn (i, -1); });   // (the button wins over V / J switching)
    if (onStatus) onStatus (off ? "INSTRUMENT SWITCHED OFF - ITS NOTES ARE SILENT" : "INSTRUMENT SWITCHED ON");
    refresh();
}

bool SynthPage::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (learner.armed) { learner.cancel(); refresh(); return true; }
        if (browsing && inst().isPlugin()) { setBrowsing (false); return true; }
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
            list.selected = juce::jlimit (0, std::max (0, (int) choices.size() - 1), list.selected + (code == juce::KeyPress::upKey ? -1 : 1));
            list.ensureVisible (list.selected);
            return true;
        }
    }
    // the computer keyboard plays the instrument
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
    }
    return true;
}

bool SynthPage::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const int n = it->second;
            proc.withLock ([&] { proc.player.jamNoteOff (n); });
            it = heldKeys.erase (it);
        }
        else ++it;
    }
    return false;
}

void SynthPage::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    g.fillAll (Theme::panel);
    const int i = proc.editorState.instrument;
    const juce::String title = "INSTRUMENT " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2)
                             + (browsing ? juce::String (": PICK A VST3 / AU INSTRUMENT") : juce::String (": PLUG-IN INSTRUMENT"));
    PixelFont::drawLeft (g, title, titleArea, px, Theme::ink);
    auto fit = [px] (juce::String t, float w) { const int n = std::max (4, (int) (w / (px * PixelFont::kCellW))); return t.length() > n ? t.substring (0, n - 2) + ".." : t; };

    if (browsing)
    {
        const auto s = proc.pluginHost.scanStatus();
        const juce::String t = proc.pluginHost.isScanning() ? s : juce::String ((int) choices.size()) + " INSTRUMENTS - DOUBLE-CLICK ONE TO LOAD IT";
        PixelFont::drawLeft (g, fit (t, infoArea.getWidth()), infoArea, px, Theme::ink);
        return;
    }
    const auto& in = inst();
    juce::PluginDescription d;
    const bool known = proc.pluginHost.findDescription (in.pluginKey, d);
    const bool loaded = proc.pluginLoadedInstrument (i);
    juce::StringArray lines;
    lines.add (juce::String (in.pluginName) + (known ? "  BY " + d.manufacturerName.toUpperCase() + " (" + (d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName.toUpperCase()) + ")" : juce::String()));
    lines.add (loaded && ! proc.player.instrumentIsOn (i) ? juce::String ("SWITCHED OFF: SILENT UNTIL IT IS ON AGAIN") :
               loaded ? "PLAYS INTO TRACK " + juce::String (proc.player.synthOutputTrack (i) + 1) + "'S MIXER STRIP" + (in.pluginOutput < 0 ? " (AUTO)" : "")
                      : "MISSING: " + proc.pluginHost.lastError (in.pluginKey) + " - SILENT, SETTINGS KEPT");
    auto r = infoArea;
    for (auto& l : lines) { auto row = r.removeFromTop (px * 11); PixelFont::drawLeft (g, fit (l, row.getWidth()), row, px, Theme::ink); }

    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        const auto& t = in.knobs[k];
        const juce::String txt = juce::String ("KNOB ") + kKnobLetters[k] + "XX: " + (t.param >= 0 ? juce::String (t.name) : juce::String ("NOT SET"));
        PixelFont::drawLeft (g, fit (txt, knobArea[k].getWidth()), knobArea[k], px, t.param >= 0 ? Theme::ink : Theme::faint);
    }
    const int tr = trackBox.getValue() - 1;
    juce::String ta = "TRACK " + juce::String (tr + 1) + ":  A = ";
    const auto& a = proc.song.mixer.trackAuto[tr][0];
    const auto& b = proc.song.mixer.trackAuto[tr][1];
    const int room = ((int) (trackArea.getWidth() / (px * PixelFont::kCellW)) - 21) / 2;
    ta += (a.valid() ? shortTargetName (a.name, room) : juce::String ("-")) + "   B = " + (b.valid() ? shortTargetName (b.name, room) : juce::String ("-"));
    PixelFont::drawLeft (g, fit (ta, trackArea.getWidth()), trackArea, px, Theme::ink);
}

void SynthPage::resized()
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
    infoArea = r.removeFromTop (rowH * 2).toFloat();
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        windowBtn.setBounds (row.removeFromLeft (charsW (19))); row.removeFromLeft (gap);
        changeBtn.setBounds (row.removeFromLeft (charsW (17)));
    }
    {
        // the bottom row: back to a sample, and the chord table (for CHORD)
        auto row = r.removeFromBottom (rowH);
        sampleBtn.setBounds (row.removeFromLeft (charsW (16))); row.removeFromLeft (gap * 3);
        chordsBtn.setBounds (row.getWidth() >= charsW (9) ? row.removeFromLeft (charsW (9)) : juce::Rectangle<int>());
    }
    r.removeFromBottom (gap);
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH);
        outBox.setBounds (row.removeFromLeft (std::min (row.getWidth() / 2, outBox.preferredWidth (px) + charsW (2)))); row.removeFromLeft (gap * 3);
        onBtn.setBounds (row.removeFromRight (charsW (4))); row.removeFromRight (gap);
        chordBtn.setBounds (row.removeFromRight (charsW (5))); row.removeFromRight (gap * 3);
        bendBox.setBounds (row.removeFromLeft (std::min (row.getWidth(), bendBox.preferredWidth (px))));
    }
    r.removeFromTop (gap * 3);
    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        auto row = r.removeFromTop (rowH); r.removeFromTop (gap);
        clearK[k].setBounds (row.removeFromRight (charsW (5))); row.removeFromRight (gap);
        learnK[k].setBounds (row.removeFromRight (charsW (5))); row.removeFromRight (gap);
        knobArea[k] = row.toFloat();
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
