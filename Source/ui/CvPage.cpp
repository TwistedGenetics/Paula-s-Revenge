// Paula's Revenge - the CV OUT and CV SOURCES pages
#include "CvPage.h"
#include "ChoiceBox.h"
#include "../engine/SampleTools.h"
#include "../engine/CvOut.h"
#include "../Prefs.h"

namespace prv::ui
{
juce::String cvOutName (int o) { return o <= 0 ? juce::String ("OFF") : juce::String (o); }

namespace
{
    juce::String tenths (int v) { return juce::String (v / 10.0, 1) + "V"; }
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    // centred text that never runs off the sides: a big size drops to the normal one if it doesn't
    // fit, then the words wrap onto as many lines as it takes (each line centred). Returns the height used.
    float drawFitted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size, juce::Colour c)
    {
        const float px = Theme::px, room = area.getWidth() - px * 4;
        if (PixelFont::textWidth (text, size) > room) size = std::min (size, px);
        juce::StringArray lines;
        juce::String cur;
        for (auto& w : juce::StringArray::fromTokens (text, " ", ""))
        {
            const auto tryLine = cur.isEmpty() ? w : cur + " " + w;
            if (cur.isNotEmpty() && PixelFont::textWidth (tryLine, size) > room) { lines.add (cur); cur = w; }
            else cur = tryLine;
        }
        if (cur.isNotEmpty()) lines.add (cur);
        const float lh = size * 11.0f;
        float y = area.getY() + std::max (0.0f, (area.getHeight() - lh * lines.size()) * 0.5f);
        for (auto& l : lines) { PixelFont::drawCentred (g, l, { area.getX(), y, area.getWidth(), lh }, size, c); y += lh; }
        return lh * lines.size();
    }
    const char* kNoteNames[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    // a MIDI note as the tracker writes it (MIDI 60 = C-3)
    juce::String trackerNote (double midi)
    {
        const int n = (int) std::lround (midi);
        return juce::String (kNoteNames[((n % 12) + 12) % 12]) + juce::String ((int) std::floor (n / 12.0) - 2);
    }
}

// =========================================================================== CV OUT
CvOutPage::CvOutPage (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    Theme::markDarkPage (*this);

    for (auto* b : { &pitchBox, &gateBox, &velBox, &modBox })
    {
        b->labelChars = 10; b->valueChars = 3;
        b->format = [] (int v) { return cvOutName (v); };
    }
    pitchBox.onChange = [this] (int v) { edit ("CV PITCH OUT", [v] (Instrument& in) { in.cvPitchOut = v; }); };
    gateBox.onChange  = [this] (int v) { edit ("CV GATE OUT", [v] (Instrument& in) { in.cvGateOut = v; }); };
    velBox.onChange   = [this] (int v) { edit ("CV VELOCITY OUT", [v] (Instrument& in) { in.cvVelOut = v; }); };
    modBox.onChange   = [this] (int v) { edit ("CV MOD OUT", [v] (Instrument& in) { in.cvModOut = v; }); };
    scaleBox.labelChars = 11; scaleBox.valueChars = 5;
    scaleBox.format = [] (int v) { return tenths (v); };
    scaleBox.onChange = [this] (int v) { edit ("CV FULL SCALE", [v] (Instrument& in) { in.cvFullScale = v / 10.0f; }); };
    gateVBox.labelChars = 5; gateVBox.valueChars = 5;
    gateVBox.format = [] (int v) { return tenths (v); };
    gateVBox.onChange = [this] (int v) { edit ("CV GATE LEVEL", [v] (Instrument& in) { in.cvGateVolts = v / 10.0f; }); };
    octBox.labelChars = 7; octBox.valueChars = 2;
    octBox.format = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v); };
    octBox.onChange = [this] (int v) { edit ("CV OCTAVE", [v] (Instrument& in) { in.cvOctave = v; }); };
    bendBox.labelChars = 5; bendBox.valueChars = 2;
    bendBox.onChange = [this] (int v) { edit ("CV BEND RANGE", [v] (Instrument& in) { in.bendRange = v; }); };
    inputBox.labelChars = 6; inputBox.valueChars = 3;
    inputBox.format = [] (int v) { return v <= 0 ? juce::String ("OFF") : juce::String (v); };
    inputBox.onChange = [this] (int v) { proc.cvTuneInput = v; prv::prefs().setValue ("cvTuneInput", v); repaint(); };
    tunerBtn.onClick = [this] { setTuner (tunerBtn.getToggleState()); };
    calBtn.onClick = [this] { if (isCalibrating()) stopCalibration ("AUTO-TUNE STOPPED"); else calibrate(); };
    clearBtn.onClick = [this] { clearMap(); };
    sourcesBtn.onClick = [this] { if (onSources) onSources(); };
    chordsBtn.onClick = [this] { if (onChords) onChords(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    onBtn.onClick = [this]
    {
        const int i = proc.editorState.instrument;
        const bool off = proc.player.instrumentIsOn (i);
        proc.history.instrument (i, off ? "SWITCH OFF" : "SWITCH ON");
        proc.withLock ([&] { inst().switchedOff = off; proc.player.setInstrumentOn (i, -1); if (off) if (auto* c = proc.cvAt (i)) c->allNotesOff (0); });
        refresh();
    };
    sampleBtn.onClick = [this]
    {
        const int i = proc.editorState.instrument;
        setTuner (false);
        proc.history.instrument (i, "BACK TO A SAMPLE");
        proc.withLock ([&] { auto& in = inst(); in.plugin = in.cvOut = false; in.switchedOff = false; in.pluginKey.clear(); in.pluginName.clear(); });
        proc.updateMixerEffects();
        if (onChanged) onChanged();
        if (onClose) onClose();
    };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &pitchBox, &gateBox, &velBox, &modBox, &scaleBox, &gateVBox, &octBox, &bendBox, &inputBox,
                                &tunerBtn, &calBtn, &clearBtn, &sourcesBtn, &chordsBtn, &sampleBtn, &closeBtn, &onBtn })
        addAndMakeVisible (c);
    proc.cvTuneInput = juce::jlimit (0, kCvOutputs, prv::prefs().getIntValue ("cvTuneInput", 0));
}

CvOutPage::~CvOutPage() = default;

Instrument& CvOutPage::inst() { return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)]; }

template <typename Fn> void CvOutPage::edit (const juce::String& what, Fn&& fn)
{
    proc.history.instrument (proc.editorState.instrument, what, true);
    proc.withLock ([&] { fn (inst()); });
    if (tuning) hold (true, cvVoltsFor (inst(), 60));
    repaint();
}

void CvOutPage::opened()
{
    refresh();
    grabKeyboardFocus();
    if (onStatus)
        onStatus (proc.cvActive.load() || proc.getTotalNumOutputChannels() > 2
                  ? juce::String ("CV OUT: NOTES PLAY THE MODULAR - PITCH AND GATE ON THE INTERFACE'S OUTPUTS")
                  : juce::String ("CV OUT NEEDS A DC-COUPLED INTERFACE WITH MORE THAN 2 OUTPUTS (AN ES-9) - CHOOSE IT IN AUDIO/MIDI SETTINGS"));
}

void CvOutPage::refresh()
{
    const auto& in = inst();
    if (shown != proc.editorState.instrument && (tuning || isCalibrating())) { setTuner (false); stopCalibration ({}); }
    shown = proc.editorState.instrument;
    pitchBox.setValue (in.cvPitchOut); gateBox.setValue (in.cvGateOut); velBox.setValue (in.cvVelOut); modBox.setValue (in.cvModOut);
    scaleBox.setValue (juce::roundToInt (in.cvFullScale * 10)); gateVBox.setValue (juce::roundToInt (in.cvGateVolts * 10));
    octBox.setValue (in.cvOctave); bendBox.setValue (in.bendRange);
    inputBox.setValue (proc.cvTuneInput.load());
    tunerBtn.setToggleState (tuning, juce::dontSendNotification);
    calBtn.setButtonText (isCalibrating() ? "STOP" : "AUTO-TUNE");
    calBtn.setToggleState (isCalibrating(), juce::dontSendNotification);
    clearBtn.setEnabled (! in.cvMapNotes.empty());
    const bool on = proc.player.instrumentIsOn (proc.editorState.instrument);
    onBtn.setToggleState (on, juce::dontSendNotification);
    onBtn.setButtonText (on ? "ON" : "OFF");
    repaint();
}

void CvOutPage::visibilityChanged()
{
    if (! isVisible()) { if (isCalibrating()) stopCalibration ("AUTO-TUNE STOPPED"); setTuner (false); }
}

void CvOutPage::hold (bool on, double volts)
{
    proc.setCvTest (proc.editorState.instrument, on, volts);
}

double CvOutPage::measure()
{
    const double rate = proc.audioRate() > 0 ? proc.audioRate() : 48000.0;
    if (fakeOscillator)
    {
        // (tests) a modular that's slightly off 1 V / octave, sampled
        const auto* c = proc.cvAt (proc.editorState.instrument);
        const double hz = fakeOscillator (c != nullptr ? c->pitchVolts() : 0.0);
        std::vector<int16_t> d (8192);
        for (size_t i = 0; i < d.size(); ++i) d[i] = (int16_t) (12000 * std::sin (2 * 3.14159265358979 * hz * (double) i / rate));
        return detectPitch (d, 0, (int) d.size(), rate);
    }
    if (proc.cvTuneInput.load() <= 0) return 0;
    proc.tuneSamples (ring, 8192);
    std::vector<int16_t> d (ring.size());
    for (size_t i = 0; i < ring.size(); ++i) d[i] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (ring[i] * 32767.0f));
    return detectPitch (d, 0, (int) d.size(), rate);
}

void CvOutPage::setTuner (bool on)
{
    if (on && isCalibrating()) stopCalibration ({});
    tuning = on;
    hold (on, cvVoltsFor (inst(), 60));   // (C-3: what to tune the oscillator to)
    tunerBtn.setToggleState (on, juce::dontSendNotification);
    if (on && onStatus) onStatus (proc.cvTuneInput.load() > 0 ? "TUNER: C-3 IS ON THE PITCH OUTPUT - TUNE THE OSCILLATOR TO C, THEN AUTO-TUNE"
                                                              : "TUNER: SET INPUT TO THE INTERFACE INPUT THE OSCILLATOR COMES BACK ON");
    repaint();
}

void CvOutPage::calibrate()
{
    if (proc.cvTuneInput.load() <= 0 && ! fakeOscillator) { if (onStatus) onStatus ("AUTO-TUNE: SET INPUT FIRST (THE INTERFACE INPUT THE OSCILLATOR COMES BACK ON)"); return; }
    if (inst().cvPitchOut < 1) { if (onStatus) onStatus ("AUTO-TUNE: SET A PITCH OUT FIRST"); return; }
    tuning = false;
    calVolts.clear(); calHz.clear(); calReads.clear();
    calStep = 0; calWait = 0;
    refresh();
    if (onStatus) onStatus ("AUTO-TUNE: MEASURING - LEAVE THE OSCILLATOR ALONE (ABOUT 10 SECONDS)");
}

void CvOutPage::stopCalibration (const juce::String& why)
{
    if (calStep < 0) return;
    calStep = -1;
    hold (false, 0);
    refresh();
    if (why.isNotEmpty() && onStatus) onStatus (why);
}

void CvOutPage::clearMap()
{
    edit ("CLEAR CV MAP", [] (Instrument& in) { in.cvMapNotes.clear(); in.cvMapVolts.clear(); in.cvMapName.clear(); });
    refresh();
    if (onStatus) onStatus ("NO MAP: 1 V / OCTAVE, C-3 = 0 V");
}

void CvOutPage::tick()
{
    if (shown != proc.editorState.instrument) refresh();
    if (tuning)
    {
        static int div = 0;
        if (++div % 3 == 0) { lastHz = measure(); repaint (tunerArea.toNearestInt()); }
    }
    if (calStep >= 0)
    {
        // each voltage: 9 ticks to settle (~0.3 s), then 6 readings; the middle one is kept
        const int points = (int) std::lround ((kCalHigh - kCalLow) / kCalStep) + 1;
        const double v = kCalLow + calStep * kCalStep;
        if (calWait == 0) hold (true, v);
        ++calWait;
        if (calWait > 9) { const double hz = measure(); if (hz > 0) calReads.push_back (hz); }
        if (calWait >= 15)
        {
            if (! calReads.empty())
            {
                std::sort (calReads.begin(), calReads.end());
                calVolts.push_back (v);
                calHz.push_back (calReads[calReads.size() / 2]);
            }
            calReads.clear();
            calWait = 0;
            if (++calStep >= points)
            {
                hold (false, 0);
                calStep = -1;
                std::vector<float> mn, mv;
                if (solveCvMap (calVolts, calHz, mn, mv))
                {
                    const juce::String name (inst().name);
                    edit ("AUTO-TUNE", [&] (Instrument& in) { in.cvMapNotes = mn; in.cvMapVolts = mv; in.cvMapName = in.name; });
                    // (kept for this synth's name too)
                    juce::String keep;
                    for (size_t k = 0; k < mn.size(); ++k) keep << mn[k] << ":" << mv[k] << " ";
                    prv::prefs().setValue ("cvMap:" + name, keep.trim());
                    if (onStatus) onStatus ("AUTO-TUNE DONE: " + juce::String ((int) mn.size()) + " POINTS, " + trackerNote (mn.front()) + " TO " + trackerNote (mn.back())
                                            + " - NOTES NOW PLAY IN TUNE");
                }
                else if (onStatus) onStatus ("AUTO-TUNE: NOT ENOUGH CLEAR READINGS - CHECK THE INPUT AND THAT THE OSCILLATOR FOLLOWS THE PITCH CV");
                refresh();
                if (onChanged) onChanged();
            }
            else repaint();
        }
    }
}

void CvOutPage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    const int i = proc.editorState.instrument;
    const auto& in = inst();
    PixelFont::drawLeft (g, "INSTRUMENT " + hex2 (i + 1) + ": CV OUT TO A MODULAR", titleArea, px, pal.ink);
    // what it does, and whether the interface has the outputs
    {
        const int outs = proc.getTotalNumOutputChannels();
        juce::StringArray lines;
        lines.add ("NOTES: PITCH (1 V/OCT) " + cvOutName (in.cvPitchOut) + ", GATE " + cvOutName (in.cvGateOut)
                   + (in.cvVelOut > 0 ? ", VELOCITY " + cvOutName (in.cvVelOut) : juce::String())
                   + (in.cvModOut > 0 ? ", MOD (LXX) " + cvOutName (in.cvModOut) : juce::String()));
        lines.add (proc.cvActive.load() ? juce::String ("SENDING ON OUTPUTS 3-") + juce::String (std::min (outs, kCvOutputs)) + " (1-2: THE MUSIC)"
                                        : juce::String ("NEEDS A DC-COUPLED INTERFACE (ES-9)"));
        if (PixelFont::textWidth (lines[0], px) > infoArea.getWidth())   // (narrow: the short way)
            lines.set (0, "PITCH " + cvOutName (in.cvPitchOut) + ", GATE " + cvOutName (in.cvGateOut)
                          + (in.cvVelOut > 0 ? ", VEL " + cvOutName (in.cvVelOut) : juce::String())
                          + (in.cvModOut > 0 ? ", MOD " + cvOutName (in.cvModOut) : juce::String()));
        if (PixelFont::textWidth (lines[1], px) > infoArea.getWidth() && proc.cvActive.load())
            lines.set (1, "ON OUTPUTS 3-" + juce::String (std::min (outs, kCvOutputs)) + " (1-2: MUSIC)");
        auto r = infoArea;
        for (auto& l : lines) { auto row = r.removeFromTop (px * 11); PixelFont::drawLeft (g, l, row, px, pal.ink); }
    }
    // the tuner / AUTO-TUNE
    {
        g.setColour (Theme::skin.bg);
        g.fillRect (tunerArea);
        Theme::bevel (g, tunerArea, false, std::max (1.0f, px * 0.5f), pal);
        auto a = tunerArea.reduced (px * 3);
        if (calStep >= 0)
        {
            const int points = (int) std::lround ((kCalHigh - kCalLow) / kCalStep) + 1;
            const double v = kCalLow + calStep * kCalStep;
            drawFitted (g, "AUTO-TUNE " + juce::String (calStep + 1) + " OF " + juce::String (points) + ": " + juce::String (v, 2) + " V",
                        a.removeFromTop (px * 17), px * 1.5f, Theme::skin.text);
            const float f = (float) (calStep + 1) / points;
            auto bar = a.removeFromTop (px * 8).reduced (px * 4, 0);
            g.setColour (Theme::skin.dim); g.fillRect (bar);
            g.setColour (Theme::skin.track[2]); g.fillRect (bar.withWidth (bar.getWidth() * f));
            if (! calHz.empty()) drawFitted (g, "LAST: " + juce::String (calHz.back(), 1) + " HZ", a.removeFromTop (px * 12), px, Theme::skin.text);
        }
        else if (tuning)
        {
            if (lastHz > 0)
            {
                const double midi = 69.0 + 12.0 * std::log2 (lastHz / 440.0);
                const double near = std::round (midi);
                const int cents = (int) std::lround ((midi - near) * 100.0);
                drawFitted (g, trackerNote (near) + "  " + (cents > 0 ? "+" : "") + juce::String (cents) + " CENTS",
                            a.removeFromTop (px * 22), px * 2.0f, std::abs (cents) <= 3 ? Theme::skin.track[2] : Theme::skin.text);
                drawFitted (g, juce::String (lastHz, 1) + " HZ", a.removeFromTop (px * 11), px, Theme::skin.text);
                // the needle: -50..+50 cents
                auto bar = a.removeFromTop (px * 8).reduced (px * 4, 0);
                g.setColour (Theme::skin.dim); g.fillRect (bar);
                g.setColour (Theme::skin.text); g.fillRect (bar.getCentreX() - 1.0f, bar.getY(), 2.0f, bar.getHeight());
                const float x = bar.getCentreX() + bar.getWidth() * 0.5f * (float) juce::jlimit (-50, 50, cents) / 50.0f;
                g.setColour (std::abs (cents) <= 3 ? Theme::skin.track[2] : Theme::skin.hot);
                g.fillRect (x - px * 1.5f, bar.getY() - px, px * 3, bar.getHeight() + px * 2);
                a.removeFromTop (px * 3);
                drawFitted (g, "TUNE THE OSCILLATOR TO A C, THEN AUTO-TUNE", a, px, Theme::skin.text);
            }
            else drawFitted (g, proc.cvTuneInput.load() > 0 ? "LISTENING ON INPUT " + juce::String (proc.cvTuneInput.load()) + " - NO CLEAR PITCH YET"
                                                             : juce::String ("SET INPUT: WHERE THE OSCILLATOR COMES BACK IN"), a, px, Theme::skin.text);
        }
        else
        {
            const float used = drawFitted (g, in.cvMapNotes.empty() ? juce::String ("NO MAP: 1 V / OCTAVE, C-3 = 0 V")
                                                             : "MAP: " + juce::String ((int) in.cvMapNotes.size()) + " POINTS, " + trackerNote (in.cvMapNotes.front()) + " TO "
                                                               + trackerNote (in.cvMapNotes.back()) + (in.cvMapName.empty() ? juce::String() : " (" + juce::String (in.cvMapName) + ")"),
                                    a.withHeight (px * 11), px, Theme::skin.text);
            a.removeFromTop (std::max (px * 12, used + px));
            // the map: how far each measured point is from 1 V / octave
            if (in.cvMapNotes.size() >= 2 && a.getHeight() > px * 10)
            {
                auto gr = a.reduced (px * 4, px * 2);
                g.setColour (Theme::skin.dim); g.fillRect (gr.getX(), gr.getCentreY(), gr.getWidth(), 1.0f);
                const float n0 = in.cvMapNotes.front(), n1 = std::max (n0 + 1.0f, in.cvMapNotes.back());
                g.setColour (Theme::skin.track[2]);
                for (size_t k = 0; k < in.cvMapNotes.size(); ++k)
                {
                    const double ideal = (in.cvMapNotes[k] - 60.0) / 12.0;
                    const double cents = (in.cvMapVolts[k] - ideal) * 1200.0;
                    const float x = gr.getX() + gr.getWidth() * (in.cvMapNotes[k] - n0) / (n1 - n0);
                    const float y = gr.getCentreY() - gr.getHeight() * 0.5f * (float) juce::jlimit (-1.0, 1.0, cents / 100.0);
                    g.fillRect (x - px, y - px, px * 2, px * 2);
                }
                PixelFont::drawLeft (g, "+100 CENTS (OFF 1 V/OCT)", gr.withHeight (px * 9), px, Theme::skin.text);
            }
            else drawFitted (g, "TUNER, THEN AUTO-TUNE: NOTES PLAY IN TUNE EVEN ON A SYNTH THAT ISN'T EXACTLY 1 V / OCT", a, px, Theme::skin.text);
        }
    }
}

void CvOutPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds().reduced (gap);
    auto head = r.removeFromTop (rowH);
    closeBtn.setBounds (head.removeFromRight (charsW (5))); head.removeFromRight (gap);
    onBtn.setBounds (head.removeFromRight (charsW (4)));
    titleArea = head.toFloat();
    r.removeFromTop (gap);
    infoArea = r.removeFromTop (rowH * 2).toFloat();
    r.removeFromTop (gap);
    auto two = [&] (NumberBox& a, NumberBox& b)
    {
        auto row = r.removeFromTop (rowH); r.removeFromTop (gap);
        const int w = (row.getWidth() - gap * 3) / 2;
        a.setBounds (row.removeFromLeft (std::min (w, a.preferredWidth (px)))); row.removeFromLeft (gap * 3);
        b.setBounds (row.removeFromLeft (std::min (row.getWidth(), b.preferredWidth (px))));
    };
    two (pitchBox, gateBox);
    two (velBox, modBox);
    two (scaleBox, gateVBox);
    two (octBox, bendBox);
    r.removeFromTop (gap);
    {
        auto row = r.removeFromTop (rowH); r.removeFromTop (gap);
        inputBox.setBounds (row.removeFromLeft (inputBox.preferredWidth (px))); row.removeFromLeft (gap * 2);
        tunerBtn.setBounds (row.removeFromLeft (charsW (5))); row.removeFromLeft (gap);
        calBtn.setBounds (row.removeFromLeft (charsW (9))); row.removeFromLeft (gap);
        clearBtn.setBounds (row.removeFromLeft (std::min (row.getWidth(), charsW (9))));
    }
    auto bottom = r.removeFromBottom (rowH);
    r.removeFromBottom (gap);
    sampleBtn.setBounds (bottom.removeFromLeft (charsW (16))); bottom.removeFromLeft (gap);
    sourcesBtn.setBounds (bottom.removeFromLeft (charsW (13))); bottom.removeFromLeft (gap);
    chordsBtn.setBounds (bottom.getWidth() >= charsW (9) ? bottom.removeFromLeft (charsW (9)) : juce::Rectangle<int>());
    tunerArea = r.toFloat();
}

bool CvOutPage::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (isCalibrating()) { stopCalibration ("AUTO-TUNE STOPPED"); return true; }
        if (onClose) onClose();
        return true;
    }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
    // the computer keyboard plays the modular
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
        proc.keyNoteInput (note, true);
    }
    return true;
}

bool CvOutPage::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const int n = it->second;
            proc.withLock ([&] { proc.player.jamNoteOff (n); });
            proc.keyNoteInput (n, false);
            it = heldKeys.erase (it);
        }
        else ++it;
    }
    return false;
}

// =========================================================================== CV SOURCES
namespace
{
    const char* kKindNames[] = { "OFF", "CLOCK", "RESET", "RUN", "LFO", "ENVELOPE", "KNOB" };
    const char* kShapeNames[] = { "SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM" };
}

CvSourcesPage::CvSourcesPage (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    Theme::markDarkPage (*this);
    scaleBox.labelChars = 11; scaleBox.valueChars = 5;
    scaleBox.format = [] (int v) { return tenths (v); };
    scaleBox.onChange = [this] (int v)
    {
        proc.history.settings ("CV FULL SCALE", true);
        proc.withLock ([&] { proc.song.cvFullScale = v / 10.0f; });
    };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (scaleBox);
    addAndMakeVisible (closeBtn);
    refresh();
}

template <typename Fn> void CvSourcesPage::edit (int i, const juce::String& what, Fn&& fn)
{
    proc.history.settings ("CV " + juce::String (i + 1) + " " + what, true);
    proc.withLock ([&] { fn (proc.song.cvSources[i]); });
    repaint();
}

void CvSourcesPage::setKind (int i, CvKind k)
{
    edit (i, "KIND", [k] (CvSource& c)
    {
        const int out = c.out;
        c = CvSource {};
        c.kind = k; c.out = out;
        if (k == CvKind::Lfo) { c.volts = 2.0f; c.low = 0.0f; }
        if (k == CvKind::Env) { c.volts = 8.0f; }
        if (k == CvKind::Knob) { c.volts = 5.0f; c.low = 0.0f; }
    });
    refresh();
    resized();
    if (onStatus) onStatus ("CV " + juce::String (i + 1) + ": " + kKindNames[(int) k]);
}

void CvSourcesPage::setOut (int i, int o) { edit (i, "OUTPUT", [o] (CvSource& c) { c.out = o; }); }   // (no refresh: the box calling this stays)

void CvSourcesPage::chooseKind (int i)
{
    juce::Component::SafePointer<CvSourcesPage> safe (this);
    std::vector<MenuItem> items;
    for (int k = 0; k < (int) CvKind::Count; ++k)
        items.push_back ({ kKindNames[k], [safe, i, k] { if (safe != nullptr) safe->setKind (i, (CvKind) k); } });
    showMenu ("CV " + juce::String (i + 1) + ": WHAT IT SENDS", items);
}

void CvSourcesPage::chooseShape (int i)
{
    juce::Component::SafePointer<CvSourcesPage> safe (this);
    std::vector<MenuItem> items;
    for (int k = 0; k < 5; ++k)
        items.push_back ({ kShapeNames[k], [safe, i, k] { if (safe != nullptr) { safe->edit (i, "SHAPE", [k] (CvSource& c) { c.shape = k; }); safe->refresh(); } } });
    showMenu ("CV " + juce::String (i + 1) + ": LFO SHAPE", items);
}

void CvSourcesPage::refresh()
{
    scaleBox.setValue (juce::roundToInt (proc.song.cvFullScale * 10));
    for (int i = 0; i < kCvSources; ++i)
    {
        auto& row = rows[i];
        const auto& c = proc.song.cvSources[i];
        auto make = [&] (std::unique_ptr<NumberBox>& b, const juce::String& label, int lo, int hi, int v, std::function<juce::String (int)> fmt, std::function<void (int)> set, int lc, int vc)
        {
            b = std::make_unique<NumberBox> (label, lo, hi, v);
            b->labelChars = lc; b->valueChars = vc; b->compact = true;
            if (fmt) b->format = fmt;
            b->onChange = set;
            addAndMakeVisible (*b);
        };
        for (auto* p : { &row.out, &row.a, &row.b, &row.c, &row.d, &row.e, &row.f }) if (*p) { removeChildComponent (p->get()); p->reset(); }
        for (auto* p : { &row.kind, &row.shape, &row.knob }) if (*p) { removeChildComponent (p->get()); p->reset(); }
        row.kind = std::make_unique<PixButton> (kKindNames[(int) c.kind], c.kind != CvKind::Off);
        row.kind->setToggleState (c.kind != CvKind::Off, juce::dontSendNotification);
        row.kind->onClick = [this, i] { chooseKind (i); };
        addAndMakeVisible (*row.kind);
        if (c.kind == CvKind::Off) continue;
        auto tv = [] (int v) { return tenths (v); };
        make (row.out, "OUT", 0, kCvOutputs, c.out, [] (int v) { return cvOutName (v); }, [this, i] (int v) { setOut (i, v); }, 3, 3);
        switch (c.kind)
        {
            case CvKind::Clock:
                make (row.a, "PULSES/BEAT", 1, 24, c.ppqn, nullptr, [this, i] (int v) { edit (i, "CLOCK", [v] (CvSource& s) { s.ppqn = v; }); }, 11, 2);
                [[fallthrough]];
            case CvKind::Reset:
            case CvKind::Run:
                make (row.b, "LEVEL", 5, 120, juce::roundToInt (c.volts * 10), tv, [this, i] (int v) { edit (i, "LEVEL", [v] (CvSource& s) { s.volts = v / 10.0f; }); }, 5, 5);
                break;
            case CvKind::Lfo:
                row.shape = std::make_unique<PixButton> (kShapeNames[juce::jlimit (0, 4, c.shape)]);
                row.shape->onClick = [this, i] { chooseShape (i); };
                addAndMakeVisible (*row.shape);
                make (row.a, "BEATS", 1, 256, juce::roundToInt (c.beats * 4), [] (int v) { return v % 4 == 0 ? juce::String (v / 4) : juce::String (v / 4.0, 2); },
                      [this, i] (int v) { edit (i, "LFO RATE", [v] (CvSource& s) { s.beats = v / 4.0f; }); }, 5, 5);
                make (row.b, "DEPTH", 0, 100, juce::roundToInt (c.volts * 10), tv, [this, i] (int v) { edit (i, "LFO DEPTH", [v] (CvSource& s) { s.volts = v / 10.0f; }); }, 5, 5);
                make (row.c, "CENTRE", -100, 100, juce::roundToInt (c.low * 10), tv, [this, i] (int v) { edit (i, "LFO CENTRE", [v] (CvSource& s) { s.low = v / 10.0f; }); }, 6, 5);
                break;
            case CvKind::Env:
                make (row.a, "TRACK", 1, kMaxTracks, c.track + 1, nullptr, [this, i] (int v) { edit (i, "ENV TRACK", [v] (CvSource& s) { s.track = v - 1; }); }, 5, 1);
                make (row.b, "PEAK", 0, 100, juce::roundToInt (c.volts * 10), tv, [this, i] (int v) { edit (i, "ENV PEAK", [v] (CvSource& s) { s.volts = v / 10.0f; }); }, 4, 5);
                make (row.c, "A", 1, 5000, juce::roundToInt (c.attack * 1000), nullptr, [this, i] (int v) { edit (i, "ENV ATTACK", [v] (CvSource& s) { s.attack = v / 1000.0f; }); }, 1, 4);
                make (row.d, "D", 1, 5000, juce::roundToInt (c.decay * 1000), nullptr, [this, i] (int v) { edit (i, "ENV DECAY", [v] (CvSource& s) { s.decay = v / 1000.0f; }); }, 1, 4);
                make (row.e, "S", 0, 100, juce::roundToInt (c.sustain * 100), [] (int v) { return juce::String (v) + "%"; }, [this, i] (int v) { edit (i, "ENV SUSTAIN", [v] (CvSource& s) { s.sustain = v / 100.0f; }); }, 1, 4);
                make (row.f, "R", 1, 5000, juce::roundToInt (c.release * 1000), nullptr, [this, i] (int v) { edit (i, "ENV RELEASE", [v] (CvSource& s) { s.release = v / 1000.0f; }); }, 1, 4);
                break;
            case CvKind::Knob:
                make (row.a, "TRACK", 1, kMaxTracks, c.track + 1, nullptr, [this, i] (int v) { edit (i, "KNOB TRACK", [v] (CvSource& s) { s.track = v - 1; }); }, 5, 1);
                row.knob = std::make_unique<PixButton> (c.knob == 0 ? "AUTO A (VXX)" : "AUTO B (JXX)");
                row.knob->onClick = [this, i] { edit (i, "KNOB", [] (CvSource& s) { s.knob = 1 - s.knob; }); refresh(); };
                addAndMakeVisible (*row.knob);
                make (row.b, "BOTTOM", -100, 100, juce::roundToInt (c.low * 10), tv, [this, i] (int v) { edit (i, "KNOB BOTTOM", [v] (CvSource& s) { s.low = v / 10.0f; }); }, 6, 5);
                make (row.c, "TOP", -100, 100, juce::roundToInt (c.volts * 10), tv, [this, i] (int v) { edit (i, "KNOB TOP", [v] (CvSource& s) { s.volts = v / 10.0f; }); }, 3, 5);
                break;
            default: break;
        }
    }
    if (getWidth() > 0) resized();
    repaint();
}

void CvSourcesPage::layoutRow (int i, juce::Rectangle<int> r)
{
    const float px = Theme::px;
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto& row = rows[i];
    row.label = r.removeFromLeft (charsW (2)).toFloat();
    if (row.kind) { row.kind->setBounds (r.removeFromLeft (charsW (8))); r.removeFromLeft (gap); }
    auto put = [&] (juce::Component* c, int w) { if (c != nullptr && r.getWidth() > 0) { c->setBounds (r.removeFromLeft (std::min (w, r.getWidth()))); r.removeFromLeft (gap * 2); } };
    if (row.out) put (row.out.get(), row.out->preferredWidth (px));
    if (row.shape) put (row.shape.get(), charsW (8));
    if (row.knob) put (row.knob.get(), charsW (12));
    for (auto* b : { row.a.get(), row.b.get(), row.c.get(), row.d.get(), row.e.get(), row.f.get() })
        if (b != nullptr) put (b, b->preferredWidth (px));
}

void CvSourcesPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds().reduced (gap * 2);
    auto head = r.removeFromTop (rowH);
    closeBtn.setBounds (head.removeFromRight (charsW (5))); head.removeFromRight (gap * 2);
    scaleBox.setBounds (head.removeFromRight (scaleBox.preferredWidth (px)));
    titleArea = head.toFloat();
    r.removeFromTop (gap * 3);
    for (int i = 0; i < kCvSources; ++i) { layoutRow (i, r.removeFromTop (rowH)); r.removeFromTop (gap * 2); }
    helpArea = r.toFloat();
}

void CvSourcesPage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    PixelFont::drawLeft (g, "CV SOURCES: VOLTAGES THE SONG SENDS TO THE MODULAR", titleArea, px, pal.ink);
    for (int i = 0; i < kCvSources; ++i) PixelFont::drawLeft (g, juce::String (i + 1), rows[i].label, px, pal.ink);
    juce::StringArray help {
        "CLOCK: PULSES WHILE PLAYING (4 A BEAT = 16THS)",
        "RESET: A PULSE AS PLAY STARTS. RUN: HIGH WHILE PLAYING",
        "LFO: IN STEP WITH THE SONG (BEATS A CYCLE)",
        "ENVELOPE: EACH NOTE ON ITS TRACK, RELEASED BY OFF (A D R IN MS)",
        "KNOB: A TRACK'S VXX (AUTO A) OR JXX (AUTO B), YXX GLIDES",
        "OUTPUTS 3-16: THE INTERFACE'S (1-2 ARE THE MUSIC)" };
    auto a = helpArea.reduced (0, px * 2);
    for (auto& l : help) { if (a.getHeight() < px * 11) break; PixelFont::drawLeft (g, l, a.removeFromTop (px * 12), px, pal.ink); }
}

bool CvSourcesPage::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}
} // namespace prv::ui
