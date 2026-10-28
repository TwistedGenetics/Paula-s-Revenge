// Paula's Revenge - instrument parameters page (envelopes, filter, pan)
#include "ParamsPage.h"

namespace prv::ui
{
float ParamsPage::sliderToTime (float v)   { return 0.001f * std::pow (10000.0f, juce::jlimit (0.0f, 1.0f, v)); }   // 1 ms .. 10 s
float ParamsPage::timeToSlider (float s)   { return juce::jlimit (0.0f, 1.0f, std::log10 (std::max (0.001f, s) / 0.001f) / 4.0f); }
float ParamsPage::sliderToCutoff (float v) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, v)); }   // 20 Hz .. 20 kHz
float ParamsPage::cutoffToSlider (float hz) { return juce::jlimit (0.0f, 1.0f, std::log10 (std::max (20.0f, hz) / 20.0f) / 3.0f); }

namespace
{
    juce::String fmtTime (float v)
    {
        const float s = ParamsPage::sliderToTime (v);
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + "MS" : juce::String (s, 2) + "S";
    }
    juce::String fmtPct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
    juce::String fmtHz (float v)
    {
        const float hz = ParamsPage::sliderToCutoff (v);
        return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + "HZ" : juce::String (hz / 1000.0f, 1) + "K";
    }
    juce::String fmtOct (float v)
    {
        const float o = v * 16.0f - 8.0f;
        return (o >= 0 ? "+" : "") + juce::String (o, 1);
    }
    juce::String fmtPan (float v)
    {
        const int p = juce::roundToInt (v * 255.0f);
        if (std::abs (p - 128) <= 2) return "C";
        return juce::String (p < 128 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (p - 128) / 127.0 * 100.0));
    }
}

ParamsPage::ParamsPage (PaulaProcessor& p) : proc (p)
{
    fxRow.onOpen = [this] (int slot) { if (onOpenFx) onOpenFx (slot); };
    fxRow.onStatus = [this] (const juce::String& m) { if (onStatus) onStatus (m); };
    addAndMakeVisible (fxRow);
    Theme::markDarkPage (*this);   // (dark with light text in the dark skin)
    setWantsKeyboardFocus (true);
    setOpaque (true);

    for (auto* s : { &atk, &dec, &rel, &fAtk, &fDec, &fRel }) { s->format = fmtTime; s->labelChars = 4; }
    for (auto* s : { &sus, &fSus, &reso, &keyTrk }) { s->format = fmtPct; s->labelChars = 4; }
    cutoff.format = fmtHz; cutoff.labelChars = 4;
    envAmt.format = fmtOct; envAmt.labelChars = 4; envAmt.defaultValue = 0.5f;
    pan.format = fmtPan; pan.labelChars = 4; pan.defaultValue = 128.0f / 255.0f;
    for (auto* s : { &atk, &dec, &sus, &rel, &cutoff, &reso, &envAmt, &keyTrk, &fAtk, &fDec, &fSus, &fRel, &pan }) s->valueChars = 6.5f;
    atk.defaultValue = timeToSlider (0.005f); dec.defaultValue = timeToSlider (0.3f); sus.defaultValue = 1.0f; rel.defaultValue = timeToSlider (0.2f);
    fAtk.defaultValue = timeToSlider (0.005f); fDec.defaultValue = timeToSlider (0.3f); fSus.defaultValue = 0.0f; fRel.defaultValue = timeToSlider (0.2f);
    cutoff.defaultValue = 1.0f; reso.defaultValue = 0.0f; keyTrk.defaultValue = 0.0f;

    envOnBtn.onClick = [this] { const bool on = envOnBtn.getToggleState(); edit ([on] (Instrument& in) { in.volEnv.on = on; }); };
    atk.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.volEnv.attack  = sliderToTime (v); }); };
    dec.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.volEnv.decay   = sliderToTime (v); }); };
    sus.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.volEnv.sustain = v; }); };
    rel.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.volEnv.release = sliderToTime (v); }); };

    auto setType = [this] (FilterType t) { edit ([t] (Instrument& in) { in.filter = t; }); refresh(); };
    fOff.onClick = [setType] { setType (FilterType::Off); };
    fLp.onClick  = [setType] { setType (FilterType::LowPass); };
    fHp.onClick  = [setType] { setType (FilterType::HighPass); };
    fBp.onClick  = [setType] { setType (FilterType::BandPass); };
    cutoff.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.cutoff = sliderToCutoff (v); }); };
    reso.onChange   = [this] (float v) { edit ([v] (Instrument& in) { in.resonance = v; }); };
    envAmt.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.filterEnvAmount = v * 16.0f - 8.0f; }); };
    keyTrk.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.keyTrack = v; }); };
    fAtk.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.filterEnv.attack  = sliderToTime (v); }); };
    fDec.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.filterEnv.decay   = sliderToTime (v); }); };
    fSus.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.filterEnv.sustain = v; }); };
    fRel.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.filterEnv.release = sliderToTime (v); }); };

    amigaBtn.onClick = [this] { const bool on = amigaBtn.getToggleState(); edit ([on] (Instrument& in) { in.amigaPan = on; }); refresh(); };
    pan.onChange = [this] (float v) { edit ([v] (Instrument& in) { in.pan = juce::roundToInt (v * 255.0f); in.amigaPan = false; }); refresh(); };

    playBtn.onStateChange = [this]
    {
        const bool down = playBtn.isDown();
        if (down == playingHeld) return;
        playingHeld = down;
        const int note = proc.editorState.octave * 12 + 1;
        proc.withLock ([&] { if (down) proc.player.jamNoteOn (note, proc.editorState.instrument); else proc.player.jamNoteOff (note); });
        proc.keyNoteInput (note, down);
    };
    resetBtn.onClick = [this]
    {
        edit ([] (Instrument& in)
        {
            const Instrument d;
            in.volEnv = d.volEnv; in.filter = d.filter; in.cutoff = d.cutoff; in.resonance = d.resonance;
            in.filterEnv = d.filterEnv; in.filterEnvAmount = d.filterEnvAmount; in.keyTrack = d.keyTrack;
            in.amigaPan = d.amigaPan; in.pan = d.pan;
        });
        refresh();
    };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    pluginBtn.onClick = [this] { if (onUsePlugin) onUsePlugin(); };
    onBtn.onClick = [this] { toggleOn(); };
    learnA.onClick = [this] { armLearn (0); };
    learnB.onClick = [this] { armLearn (1); };
    trackBox.labelChars = 5; trackBox.valueChars = 1;
    trackBox.onChange = [this] (int) { learning = -1; refresh(); };

    for (auto* c : std::initializer_list<juce::Component*> { &envOnBtn, &atk, &dec, &sus, &rel, &fOff, &fLp, &fHp, &fBp, &cutoff, &reso, &envAmt, &keyTrk,
                                                             &fAtk, &fDec, &fSus, &fRel, &amigaBtn, &pan, &playBtn, &resetBtn, &closeBtn, &pluginBtn,
                                                             &onBtn, &learnA, &learnB, &trackBox })
        addAndMakeVisible (c);
    refresh();
}

ParamsPage::~ParamsPage()
{
    proc.withLock ([&] { proc.player.jamAllOff(); });
}

Instrument& ParamsPage::inst()
{
    return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
}

template <typename Fn> void ParamsPage::edit (Fn&& fn)
{
    proc.history.instrument (proc.editorState.instrument, "SOUND SHAPING", true);
    proc.withLock ([&] { fn (inst()); });
    repaint();
    if (onChanged) onChanged();
}

void ParamsPage::toggleOn()
{
    const int i = proc.editorState.instrument;
    if (learning >= 0)
    {
        // LEARN armed: the track's knob switches this instrument on and off
        const int k = learning, tr = trackBox.getValue() - 1;
        learning = -1;
        const juce::String name = (inst().name.empty() ? "INSTRUMENT " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2)
                                                      : juce::String (inst().name).toUpperCase()) + ": ON/OFF";
        proc.history.mixer ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A = " : " AUTO B = ") + name);
        proc.withLock ([&] { proc.song.mixer.trackAuto[tr][k] = AutoTarget { kAutoInstrumentBase + i, kAutoOnOff, name.toStdString() }; });
        if (onStatus) onStatus ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A (VXX) NOW SWITCHES " : " AUTO B (JXX) NOW SWITCHES ") + name + " (00 = OFF, 01-FF = ON)");
        refresh();
        return;
    }
    const bool off = ! inst().switchedOff;
    proc.history.instrument (i, off ? "INSTRUMENT OFF" : "INSTRUMENT ON");
    proc.withLock ([&] { inst().switchedOff = off; proc.player.setInstrumentOn (i, -1); });   // (the button wins over V / J switching)
    if (onStatus) onStatus (off ? "INSTRUMENT SWITCHED OFF - ITS NOTES ARE SILENT" : "INSTRUMENT SWITCHED ON");
    if (onChanged) onChanged();
    refresh();
}

void ParamsPage::armLearn (int knob)
{
    if (learning == knob) { learning = -1; if (onStatus) onStatus ("LEARN CANCELLED"); refresh(); return; }
    learning = knob;
    if (onStatus) onStatus ("NOW CLICK ON: TRACK " + juce::String (trackBox.getValue()) + (knob == 0 ? " AUTO A (VXX)" : " AUTO B (JXX)")
                            + " WILL SWITCH THIS INSTRUMENT ON AND OFF (LEARN AGAIN CANCELS)");
    refresh();
}

void ParamsPage::refresh()
{
    fxRow.refresh();
    if (shownInstrument != proc.editorState.instrument) { learning = -1; trackBox.setValue (proc.editorState.track + 1); }
    shownInstrument = proc.editorState.instrument;
    const auto& in = inst();
    const bool on = proc.player.instrumentIsOn (proc.editorState.instrument);
    onBtn.setToggleState (on, juce::dontSendNotification);
    onBtn.setButtonText (on ? "ON" : "OFF");
    learnA.setToggleState (learning == 0, juce::dontSendNotification);
    learnB.setToggleState (learning == 1, juce::dontSendNotification);
    envOnBtn.setToggleState (in.volEnv.on, juce::dontSendNotification);
    atk.setValue (timeToSlider (in.volEnv.attack)); dec.setValue (timeToSlider (in.volEnv.decay));
    sus.setValue (in.volEnv.sustain);               rel.setValue (timeToSlider (in.volEnv.release));
    for (auto* s : { &atk, &dec, &sus, &rel }) s->setAlpha (in.volEnv.on ? 1.0f : 0.45f);

    fOff.setToggleState (in.filter == FilterType::Off, juce::dontSendNotification);
    fLp.setToggleState (in.filter == FilterType::LowPass, juce::dontSendNotification);
    fHp.setToggleState (in.filter == FilterType::HighPass, juce::dontSendNotification);
    fBp.setToggleState (in.filter == FilterType::BandPass, juce::dontSendNotification);
    cutoff.setValue (cutoffToSlider (in.cutoff)); reso.setValue (in.resonance);
    envAmt.setValue ((in.filterEnvAmount + 8.0f) / 16.0f); keyTrk.setValue (in.keyTrack);
    fAtk.setValue (timeToSlider (in.filterEnv.attack)); fDec.setValue (timeToSlider (in.filterEnv.decay));
    fSus.setValue (in.filterEnv.sustain);               fRel.setValue (timeToSlider (in.filterEnv.release));
    const bool fOn = in.filter != FilterType::Off;
    for (auto* s : { &cutoff, &reso, &envAmt, &keyTrk, &fAtk, &fDec, &fSus, &fRel }) s->setAlpha (fOn ? 1.0f : 0.45f);

    amigaBtn.setToggleState (in.amigaPan, juce::dontSendNotification);
    pan.setValue (in.pan / 255.0f);
    pan.setAlpha (in.amigaPan ? 0.45f : 1.0f);
    repaint();
}

void ParamsPage::tick()
{
    if (shownInstrument != proc.editorState.instrument) refresh();
    // the ON button follows V / J switching while the song plays
    else if (onBtn.getToggleState() != proc.player.instrumentIsOn (proc.editorState.instrument)) refresh();
}

void ParamsPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    auto r = getLocalBounds().reduced (gap);

    titleArea = r.removeFromTop (rowH).toFloat();
    r.removeFromTop (gap);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    // bottom: ON, the track and LEARN A / B for switching it, CLOSE
    auto bottom = r.removeFromBottom (rowH);
    {
        closeBtn.setBounds (bottom.removeFromRight (charsW (5))); bottom.removeFromRight (gap * 2);
        onBtn.setBounds (bottom.removeFromLeft (charsW (4))); bottom.removeFromLeft (gap);
        trackBox.setBounds (bottom.removeFromLeft (trackBox.preferredWidth (px))); bottom.removeFromLeft (gap);
        learnA.setBounds (bottom.removeFromLeft (charsW (11))); bottom.removeFromLeft (gap);
        learnB.setBounds (bottom.removeFromLeft (std::min (bottom.getWidth(), charsW (11))));
    }
    r.removeFromBottom (gap);
    // play, reset, and a plug-in or MIDI out instead of the sample
    auto row2 = r.removeFromBottom (rowH);
    {
        playBtn.setBounds (row2.removeFromLeft (charsW (14))); row2.removeFromLeft (gap);
        resetBtn.setBounds (row2.removeFromLeft (charsW (9))); row2.removeFromLeft (gap);
        pluginBtn.setBounds (row2);
    }
    r.removeFromBottom (gap * 2);

    auto panRow = r.removeFromBottom (rowH + (int) (px * 14));
    panBox = panRow.toFloat();
    {
        auto inner = panRow.reduced (gap * 2).withTrimmedTop ((int) (px * 11));
        amigaBtn.setBounds (inner.removeFromLeft (inner.getWidth() / 3)); inner.removeFromLeft (gap * 3);
        pan.setBounds (inner);
    }
    r.removeFromBottom (gap * 2);

    const int colGap = gap * 3;
    auto left = r.removeFromLeft ((r.getWidth() - colGap) / 2);
    r.removeFromLeft (colGap);
    auto right = r;

    auto title = (int) (px * 11);
    auto stack = [&] (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps)
    {
        for (auto* c : comps) { c->setBounds (area.removeFromTop (rowH)); area.removeFromTop (gap); }
        return area;
    };

    // the instrument's own effects: under the volume envelope (its graph has the room)
    fxRow.setBounds (left.removeFromBottom (rowH));
    left.removeFromBottom (gap * 2);
    // volume envelope (left)
    envBox = left.toFloat();
    {
        auto in = left.reduced (gap * 2).withTrimmedTop (title);
        envOnBtn.setBounds (in.removeFromTop (rowH)); in.removeFromTop (gap * 2);
        in = stack (in, { &atk, &dec, &sus, &rel });
        envGraph = in.toFloat().reduced (0, (float) gap);
    }

    // filter + filter envelope (right), sized to their contents
    const int boxPad = gap * 4 + title;
    auto fTop = right.removeFromTop (boxPad + rowH * 5 + gap * 7);
    right.removeFromTop (gap * 2);
    filterBox = fTop.toFloat();
    {
        auto in = fTop.reduced (gap * 2).withTrimmedTop (title);
        auto types = in.removeFromTop (rowH); in.removeFromTop (gap * 2);
        const int w = (types.getWidth() - gap * 3) / 4;
        for (auto* b : { &fOff, &fLp, &fHp, &fBp }) { b->setBounds (types.removeFromLeft (w)); types.removeFromLeft (gap); }
        stack (in, { &cutoff, &reso, &envAmt, &keyTrk });
    }
    auto fBottom = right.removeFromTop (std::min (right.getHeight(), boxPad + rowH * 4 + gap * 4));
    fenvBox = fBottom.toFloat();
    {
        auto in = fBottom.reduced (gap * 2).withTrimmedTop (title);
        stack (in, { &fAtk, &fDec, &fSus, &fRel });
        fenvGraph = {};
    }
}

void ParamsPage::drawEnvelope (juce::Graphics& g, juce::Rectangle<float> area, const Envelope& e, bool active)
{
    if (area.getHeight() < 20) return;
    const auto& sk = Theme::skin;
    g.setColour (sk.bg);
    g.fillRect (area);
    const float total = e.attack + e.decay + 0.4f + e.release;
    auto x = [&] (float t) { return area.getX() + area.getWidth() * t / total; };
    auto y = [&] (float lv) { return area.getBottom() - 3.0f - (area.getHeight() - 6.0f) * lv; };
    juce::Path p;
    p.startNewSubPath (x (0), y (0));
    p.lineTo (x (e.attack), y (1));
    p.lineTo (x (e.attack + e.decay), y (e.sustain));
    p.lineTo (x (e.attack + e.decay + 0.4f), y (e.sustain));
    p.lineTo (x (total), y (0));
    g.setColour (active ? sk.track[2] : sk.dim);
    g.strokePath (p, juce::PathStrokeType (std::max (2.0f, Theme::px * 0.75f)));
    Theme::bevel (g, area, false, 1.0f, Theme::palFor (*this));
}

void ParamsPage::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    g.fillAll (Theme::palFor (*this).panel);
    const auto& in = inst();
    const juce::String idx = juce::String::toHexString (proc.editorState.instrument + 1).toUpperCase().paddedLeft ('0', 2);
    juce::String t = "INSTRUMENT " + idx + "  " + (in.name.empty() ? juce::String (in.hasData() ? "(NO NAME)" : "(EMPTY)") : juce::String (in.name));
    float tp = px;
    while (tp > 1.5f && PixelFont::textWidth (t, tp) > titleArea.getWidth()) tp -= 0.5f;
    PixelFont::drawLeft (g, t, titleArea, tp, Theme::palFor (*this).ink);

    Theme::panelBox (g, envBox, "VOLUME", Theme::palFor (*this));
    Theme::panelBox (g, filterBox, "FILTER", Theme::palFor (*this));
    Theme::panelBox (g, fenvBox, "FILTER ENVELOPE", Theme::palFor (*this));
    Theme::panelBox (g, panBox, "PAN", Theme::palFor (*this));
    drawEnvelope (g, envGraph, in.volEnv, in.volEnv.on);

    if (hasKeyboardFocus (true))
    {
        g.setColour (Theme::palFor (*this).ink);
        g.drawRect (getLocalBounds().toFloat(), std::max (2.0f, px * 0.5f));
    }
}

// play the instrument from the computer keyboard (same layout as the pattern)
bool ParamsPage::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;   // undo etc. go to the main window
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

bool ParamsPage::keyStateChanged (bool)
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
} // namespace prv::ui
