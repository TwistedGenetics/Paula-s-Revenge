// Paula's Revenge - main window
#include "PluginEditor.h"
#include "engine/Crunch.h"
#include "Diagnostics.h"
#include "ui/ChoiceBox.h"
#include "ui/MeterColours.h"
#include "engine/Capture.h"
#include "engine/Groove.h"
#include "SongFile.h"
#include "engine/Bounce.h"
#include "engine/ModuleIO.h"
#include "Prefs.h"

using namespace prv::ui;

namespace { const juce::String kHelpText = "SPACE=PLAY+REC  RETURN=PLAY  ESC=EDIT"; }

PaulaEditor::PaulaEditor (PaulaProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      pattern (p), lane (p), pads (p),
      sequence (p, [this] { refreshAll(); }),
      blockPanel (p, [this] { refreshAll(); }),
      instruments (p, [this] { refreshAll(); }, [this] (const juce::String& s) { setStatus (s); }),
      spectrum (p), sampleEditor (p), browser (p), params (p), synthPage (p), midiPage (p), arrange (p), mixer (p), chords (p), samplePage (p), cvPage (p), cvSources (p), outMeter (p)
{
    auto& st = proc.editorState;
    Theme::setSkinId ((Theme::SkinId) juce::jlimit (0, Theme::kSkins - 1, st.skin));
    Theme::crt = st.crt;
    // DARK: click flips WORKBENCH / DARK; right-click (or the View menu) has every SKIN and CRT
    skinBtn.setClickingTogglesState (false);
    skinBtn.onClick = [this, &st] { setSkin (st.skin == 0 ? 1 : 0); };
    skinBtn.onRightClick = [this] { showSkinMenu(); };
    specBtn.onClick = [this, &st] { st.showSpectrum = specBtn.getToggleState(); resized(); };
    autoBtn.onClick = [this, &st]
    {
        st.showAutomation = autoBtn.getToggleState();
        if (st.showAutomation && st.view != View::Pattern) setView (View::Pattern);
        automation.refresh();
        resized();
        setStatus (st.showAutomation ? "AUTOMATION: PICK A CONTROL ON THE LEFT, DOUBLE-CLICK THE GRAPH TO ADD POINTS (DRAG ITS TOP EDGE TO RESIZE)" : "AUTOMATION HIDDEN (THE ENVELOPES STILL PLAY)");
    };
    automation.onStatus = [this] (const juce::String& t) { setStatus (t); };
    automation.onResize = [this, &st] (int h) { st.autoHeight = juce::jmax ((int) (prv::ui::Theme::px * 60), h); resized(); };

    // ---- title bar
    newBtn.onClick  = [this] { newSong(); };
    loadBtn.onClick = [this] { loadSong(); };
    saveBtn.onClick = [this] { saveSong(); };
    saveAsBtn.onClick = [this] { saveSongAs(); };
    undoBtn.onClick = [this] { if (! proc.history.undo()) setStatus ("NOTHING TO UNDO"); };
    redoBtn.onClick = [this] { if (! proc.history.redo()) setStatus ("NOTHING TO REDO"); };
    proc.history.onRestored = [this] (const juce::String& what) { historyRestored (what); };
    fullVelBtn.onClick = [this] { setFullVelocity (fullVelBtn.getToggleState()); };
    noteOffsBtn.onClick = [this] { setNoteOffs (noteOffsBtn.getToggleState()); };
    fullVelBtn.setToggleState (proc.fullVelocity.load(), juce::dontSendNotification);
    noteOffsBtn.setToggleState (prv::ui::PatternEditor::recordNoteOffs(), juce::dontSendNotification);
    helpBtn.onClick = [this] { toggleHelp(); };
    help.onClose = [this] { setView (helpReturn == View::Help ? View::Pattern : helpReturn); };
    help.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (help);
    // (Space while the song plays: it stops, and EDIT goes off as Space switched it on)
    chords.onStopSong = [this] { pattern.stopPlayback(); proc.editorState.editMode = false; refreshAll(); setStatus ("STOPPED"); };
    sampleEditor.onStopSong = [this] { pattern.stopPlayback(); proc.editorState.editMode = false; refreshAll(); setStatus ("STOPPED"); };
    chords.onClose = [this] { setView (chordsReturn == View::Chords || chordsReturn == View::Help ? View::Pattern : chordsReturn); };
    chords.onStatus = [this] (const juce::String& m) { setStatus (m); };
    chords.onChanged = [this] { refreshAll(); };
    pattern.onInputNote = [this] (int note, bool on)
    {
        if (! (chords.isVisible() && chords.isRecording())) return false;
        chords.inputNote (note, on);
        return true;
    };
    addChildComponent (chords);
    // SAMPLE: record audio into the instrument
    samplePage.onClose = [this] { setView (sampleReturn == View::Sample || sampleReturn == View::Help ? View::Pattern : sampleReturn); };
    samplePage.onChanged = [this] { refreshAll(); };
    samplePage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    samplePage.onAudioSettings = [this] { if (openAudioSettings) openAudioSettings(); else setStatus ("AUDIO/MIDI SETTINGS: IN THE APP MENU"); };
    samplePage.onEdit = [this] { setView (View::SampleEdit); };
    addChildComponent (samplePage);
    // CV out to a modular (ES-9)
    cvPage.onClose = [this] { setView (View::Pattern); };
    enginePage.onClose = [this] { setView (View::Pattern); };
    enginePage.onChanged = [this] { instruments.refresh(); };
    enginePage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    enginePage.onLoadSample = [this] { openLoad (0); setStatus ("PICK A SAMPLE FOR GRANULAR - DOUBLE-CLICK OR LOAD INTO LOADS IT"); };
    enginePage.onOptions = [this] { setView (View::Synth); };
    addChildComponent (enginePage);
    synthPage.onOpenBuiltin = [this] { setView (View::Engine); };
    cvPage.onChanged = [this] { refreshAll(); };
    cvPage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    cvPage.onSources = [this] { setView (View::CvSources); };
    cvPage.onChords = [this] { openChords(); };
    addChildComponent (cvPage);
    cvSources.onClose = [this] { setView (proc.song.instruments[(size_t) proc.editorState.instrument].isCvOut() ? View::CvOut : View::Pattern); };
    cvSources.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (cvSources);
    // an instrument's own effects: edited on the mixer's effect page, BACK returns here
    auto openInstrumentFx = [this] (int slot)
    {
        fxReturn = proc.editorState.view;
        setView (View::Mixer);
        mixer.openEffect (slot);
    };
    params.onOpenFx = openInstrumentFx;
    // its own EQ and the 3 effects together (the EQ button on the FX row)
    auto openEqFx = [this]
    {
        fxReturn = proc.editorState.view;
        setView (View::Mixer);
        mixer.openInstrument (proc.editorState.instrument);
    };
    params.onOpenEqFx = openEqFx;
    instruments.openEqFx = openEqFx;   // (EQ/FX on the instruments panel)
    instruments.openPhrase = [this] (int k) { setView (View::Params); params.showPhrases (true); params.phraseView().select (k); };
    breakPage.onOpenPhrase = [this] (int k) { setView (View::Params); params.showPhrases (true); params.phraseView().select (k); };
    synthPage.onOpenEqFx = openEqFx;
    // a VST3 / AU instrument's filter and envelopes: the PARAMS page, BACK TO THE PLUG-IN returns
    synthPage.onOpenShape = [this] { paramsForPlugin = proc.editorState.instrument; setView (View::Params); };
    params.onBackToPlugin = [this] { paramsForPlugin = -1; setView (View::Synth); };
    params.onFitStretch = [this] (int lines) { setView (View::SampleEdit); sampleEditor.openFit (lines); };
    synthPage.onOpenFx = openInstrumentFx;
    mixer.onBackFromInstrument = [this] { setView (fxReturn == View::Mixer ? View::Params : fxReturn); };
    instruments.openSample = [this] { openSample(); };
    pattern.onEditChords = [this] (int slot) { openChords (slot); };
    for (auto& a : recSeenTrack) for (auto& v : a) v = -1.0f;
    for (auto& a : recSeenInst) for (auto& v : a) v = -1.0f;
    for (auto& a : recLastTrack) for (auto& v : a) v = -1;
    for (auto& a : recLastInst) for (auto& v : a) v = -1;
    // INTERNET RADIO (a page of its own; it keeps playing on the others)
    radioBtn.setClickingTogglesState (false);
    radioBtn.onClick = [this]
    {
        if (proc.editorState.view == View::Radio) { radioPage.onClose(); return; }
        radioReturn = proc.editorState.view;
        setView (View::Radio);
    };
    radioPage.onClose = [this] { setView (radioReturn == View::Radio ? View::Pattern : radioReturn); };
    radioPage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    radioPage.onGrabbed = [this] (int) { refreshAll(); setView (View::SampleEdit); };
    addChildComponent (radioPage);
    // the BREAK page
    addChildComponent (breakPage);
    breakPage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    breakPage.onChanged = [this] { refreshAll(); };
    breakPage.onClose = [this] { setView (breakReturn == View::Break ? View::Pattern : breakReturn); };
    instruments.openBreak = [this] { openBreak(); };
    // LIVE MODE and VISUALS
    addChildComponent (livePage);
    livePage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    livePage.onChanged = [this] { refreshAll(); };
    livePage.onClose = [this] { setView (liveReturn == View::Live ? View::Pattern : liveReturn); };
    livePage.visualsBtn.onClick = [this] { openVisuals(); };
    addChildComponent (visualsPage);
    visualsPage.onClose = [this] { setView (visualsReturn == View::Visuals ? View::Pattern : visualsReturn); };
    liveBtn.setClickingTogglesState (false);
    liveBtn.onClick = [this] { openLive(); };
    grooveBtn.setClickingTogglesState (false);
    grooveBtn.onClick = [this] { showGrooveMenu(); };
    recAutoBtn.onClick = [this, &st]
    {
        st.recAuto = recAutoBtn.getToggleState();
        setStatus (st.recAuto ? "REC AUTO ON: WITH EDIT ON AND THE SONG PLAYING, MOVING A LEARNED CONTROL WRITES ITS V / J / L / Q / H / W"
                              : "REC AUTO OFF");
        pattern.grabKeyboardFocus();
    };
    zoomOut.onClick = [this, &st] { st.uiZoom = std::max (-3, st.uiZoom - 1); resized(); repaint(); };
    zoomIn.onClick  = [this, &st] { st.uiZoom = std::min (4, st.uiZoom + 1); resized(); repaint(); };

    // ---- transport
    // SONG / BLOCK: play - and back to the main page (the pattern) from whatever page is open
    playSongBtn.onClick  = [this, &st] { if (st.view != View::Pattern) setView (View::Pattern); proc.withLock ([&] { proc.player.playSong (st.seqPos, 0); }); refreshAll(); };
    playBlockBtn.onClick = [this, &st] { if (st.view != View::Pattern) setView (View::Pattern); proc.withLock ([&] { proc.player.playBlock (st.block, 0); }); refreshAll(); };
    stopBtn.onClick      = [this]
    {
        // STOP while already stopped: panic (all notes off on every MIDI output)
        if (! proc.player.isPlaying()) { proc.midiPanic(); setStatus ("PANIC: ALL NOTES OFF ON EVERY MIDI OUTPUT"); }
        pattern.stopPlayback(); refreshAll();
    };
    editBtn.onClick      = [this, &st] { st.editMode = editBtn.getToggleState(); pattern.grabKeyboardFocus(); refreshAll(); };
    captureBtn.onClick   = [this] { captureNow(); };
    // right-click EDIT: recording options and CAPTURE
    editBtn.onRightClick = [this, &st]
    {
        juce::Component::SafePointer<PaulaEditor> safe (this);
        choice.show (*this, "RECORDING", { st.recNoteOffs ? "STEP ENTRY NOTE-OFFS: ON (TURN OFF)" : "STEP ENTRY NOTE-OFFS: OFF (TURN ON)",
                                           prv::ui::PatternEditor::detectChords() ? "DETECT CHORDS: ON (TURN OFF)" : "DETECT CHORDS: OFF (TURN ON)",
                                           "CAPTURE WHAT I PLAYED (CMD+R)",
                                           prv::ui::PatternEditor::recordNoteOffs() ? "NOTE-OFFS: ON (TURN OFF - NO OFFS RECORDED AT ALL)" : "NOTE-OFFS: OFF (TURN ON)",
                                           proc.fullVelocity.load() ? "FULL VEL: ON (TURN OFF - USE THE KEYBOARD'S VELOCITY)" : "FULL VEL: OFF (TURN ON - EVERY NOTE AT 127)",
                                           prv::ui::PatternEditor::followInstrument() ? "FOLLOW INSTRUMENT: ON (TURN OFF)" : "FOLLOW INSTRUMENT: OFF (TURN ON)" },
                     [safe] (int a)
        {
            if (safe == nullptr) return;
            auto& s2 = safe->proc.editorState;
            if (a == 0)
            {
                s2.recNoteOffs = ! s2.recNoteOffs;
                safe->setStatus (s2.recNoteOffs ? "STEP ENTRY: LETTING GO OF A KEY WRITES OFF (PLUG-IN / MIDI-OUT INSTRUMENTS)"
                                                : "STEP ENTRY: NO NOTE-OFFS (WHILE PLAYING THEY'RE STILL RECORDED)");
            }
            else if (a == 1)
            {
                const bool on = ! prv::ui::PatternEditor::detectChords();
                prv::ui::PatternEditor::setDetectChords (on);
                safe->setStatus (on ? "DETECT CHORDS: NOTES PLAYED TOGETHER THAT MATCH THE CHORD TABLE GO IN AS ONE NOTE + #XY"
                                    : "DETECT CHORDS OFF: EVERY NOTE GOES IN AS PLAYED");
            }
            else if (a == 2) safe->captureNow();
            else if (a == 3) safe->setNoteOffs (! prv::ui::PatternEditor::recordNoteOffs());
            else if (a == 4) safe->setFullVelocity (! safe->proc.fullVelocity.load());
            else if (a == 5) safe->setFollowInstrument (! prv::ui::PatternEditor::followInstrument());
        });
    };
    loopBtn.onClick      = [this] { pattern.setLoopOn (loopBtn.getToggleState()); pattern.grabKeyboardFocus(); refreshAll(); };
    followBtn.onClick    = [this, &st] { st.follow = followBtn.getToggleState(); refreshAll(); };
    panelsBtn.onClick    = [this] { showPanels = panelsBtn.getToggleState(); resized(); };
    for (auto* b : { &tempoBox, &speedBox, &octBox, &stepBox }) b->compact = true;
    tempoBox.onChange = [this] (int v) { proc.history.settings ("BPM", true); proc.withLock ([&] { proc.song.tempo = v; }); };
    speedBox.onChange = [this] (int v) { proc.history.settings ("SPEED", true); proc.withLock ([&] { proc.song.speed = v; }); };
    octBox.onChange   = [&st] (int v) { st.octave = v; };
    stepBox.onChange  = [&st] (int v) { st.step = v; };
    octBox.labelChars = 4; stepBox.labelChars = 5; tempoBox.labelChars = 4; speedBox.labelChars = 4;
    speedBox.valueChars = 2; octBox.valueChars = 1; stepBox.valueChars = 2;

    // ---- paula strip
    auto setModel = [this] (prv::PaulaOptions::Model m)
    {
        proc.withLock ([&] { proc.player.options.model = m; });
        refreshAll();
    };
    a500Btn.onClick  = [this, setModel] { setModel (prv::PaulaOptions::Model::A500);
                                          setStatus ("A500 / A600 / A2000: PAULA, 4.4 KHZ OUTPUT FILTER"); };
    a1200Btn.onClick = [this, setModel] { setModel (prv::PaulaOptions::Model::A1200);
                                          setStatus ("A1200: PAULA, BRIGHT OUTPUT (-3 DB AROUND 14 KHZ)"); };
    a4000Btn.onClick = [this, setModel] { setModel (prv::PaulaOptions::Model::A4000);
                                          setStatus ("A4000: PAULA, 4.6 KHZ OUTPUT FILTER (MEASURED)"); };
    cleanBtn.onClick = [setModel] { setModel (prv::PaulaOptions::Model::Clean); };
    s950Btn.onClick  = [this, setModel] { setModel (prv::PaulaOptions::Model::S950);
                                          setStatus ("AKAI S950: 12-BIT, VARIABLE-RATE PLAYBACK, 6-POLE SWITCHED-CAP FILTER"); };
    s1000Btn.onClick = [this, setModel] { setModel (prv::PaulaOptions::Model::S1000);
                                          setStatus ("AKAI S1000: 16-BIT, INTERPOLATED PLAYBACK, CLEAN 44.1K OUTPUT FILTER"); };
    steppedBtn.onClick  = [this] { applyOptions(); };
    eightBitBtn.onClick = [this] { applyOptions(); };
    crunchBtn.onClick   = [this] { applyOptions(); };
    ledBtn.onClick = [this] { proc.history.settings ("LED FILTER"); proc.withLock ([&] { proc.song.ledFilterAtStart = ledBtn.getToggleState(); proc.player.setLed (ledBtn.getToggleState()); }); };
    sepSlider.defaultValue = 0.2f;
    sepSlider.labelChars = volSlider.labelChars = 4;
    volSlider.defaultValue = 0.5f;
    volSlider.format = [] (float v) { return juce::String (juce::roundToInt (v * 200)) + "%"; };
    sepSlider.onChange = [this] (float) { applyOptions(); };
    volSlider.onChange = [this] (float) { applyOptions(); };
    // the master VOL: a compact box by the L / R meter (0-200 %, 100 = as it is)
    volBox.compact = true; volBox.labelChars = 3; volBox.valueChars = 3;
    volBox.onChange = [this] (int v) { volSlider.setValue (v / 200.0f); applyOptions(); setStatus ("MASTER VOLUME " + juce::String (v) + "% (AFTER EVERYTHING, BEFORE THE OUTPUT)"); };
    // SEP (stereo separation) lives in a menu now: right-click a machine button, or the View menu
    for (auto* b : { &a500Btn, &a1200Btn, &a4000Btn, &cleanBtn, &s950Btn, &s1000Btn }) b->onRightClick = [this] { showSeparationMenu(); };

    // ---- song strip
    viewBtn.onClick = [this, &st] { st.fourTrackView = viewBtn.getToggleState(); refreshAll(); };

    pattern.onStateChanged = [this] { refreshAll(); };
    pattern.onBounce = [this] { askBounce(); };
    params.onEditPhrase = [this] (int b, int t)
    {
        auto& st = proc.editorState;
        st.block = proc.song.safeBlock (b);
        st.track = juce::jlimit (0, proc.song.numTracks - 1, t);
        st.line = 0;
        setView (View::Pattern);
        refreshAll();
        setStatus ("THE PHRASE: BLOCK " + juce::String (st.block).paddedLeft ('0', 3) + " TRACK " + juce::String (st.track + 1) + " - WRITE IT FROM C-3 (THE NOTE PLAYED MOVES IT)");
    };
    pattern.onResample = [this] { resampleChop(); };
    addChildComponent (lane);
    addChildComponent (pads);
    lane.onPick = [this] (int pos) { sequence.selectPosition (pos); };
    pads.onPadDown = [this] (int ins)
    {
        auto& es = proc.editorState;
        if (es.instrument != ins) { es.instrument = ins; proc.jamInstrument = ins; refreshAll(); }
        pattern.padDown();
        setStatus ("PAD " + juce::String (ins % 16 + 1) + ": INSTRUMENT " + juce::String (ins + 1).paddedLeft ('0', 2)
                   + (es.editMode ? " - WRITTEN AT THE CURSOR" : " - EDIT ON WRITES IT AT THE CURSOR"));
    };
    pads.onPadUp = [this] { pattern.padUp(); };
    pads.onBank = [this] { setStatus ("PADS BANK " + juce::String::charToString ((juce::juce_wchar) ('A' + proc.editorState.padBank))
                                      + ": INSTRUMENTS " + juce::String (proc.editorState.padBank * 16 + 1) + "-" + juce::String (std::min (prv::kMaxInstruments, proc.editorState.padBank * 16 + 16))); };
    pattern.onFollowInstrument = [this] (int ins)
    {
        if (proc.editorState.instrument == ins) return;
        proc.editorState.instrument = ins;
        proc.jamInstrument = ins;
        refreshAll();
    };
    pattern.onSelectionMenu = [this] (int line, int track, int group)
    {
        juce::Component::SafePointer<PaulaEditor> safe (this);
        choice.show (*this, "THE HIGHLIGHTED NOTES", { "BOUNCE: NO FX", "BOUNCE: INSTRUMENT FX ONLY", "BOUNCE: MIXER FX ONLY", "BOUNCE: ALL FX", "EFFECT COMMANDS...",
                                                        "REVERSE THE LINES", "STUTTER (THE FIRST QUARTER, OVER AND OVER)", "RATCHET (ROLLS THAT SPEED UP)", "RE-SEQUENCE THE SLICES (SHUFFLE)",
                                                        "CONDITION (WHICH PASSES PLAY)..." },
                     [safe, line, track, group] (int a)
        {
            if (safe == nullptr) return;
            if (a >= 0 && a <= 3) { if (safe->bounceSelection (a)) safe->askAfterBounce (safe->proc.editorState.instrument); }
            else if (a == 4) safe->pattern.openCommandMenu (line, track, group);
            else if (a >= 5 && a <= 8) { safe->pattern.breakTool (a - 5); safe->refreshAll(); }
            else if (a == 9) safe->showConditionMenu();
        });
    };
    pattern.onStatus = [this] (const juce::String& m) { setStatus (m); };
    blockPanel.onCopyBlock = [this] (bool cut) { pattern.copyBlock (cut); };
    blockPanel.onPasteBlock = [this] { pattern.pasteBlock(); };
    prv::ui::menuHook() = [this] (const juce::String& t, const juce::StringArray& a, std::function<void (int)> done) { choice.show (*this, t, a, std::move (done)); };
    pattern.onVuMenu = [this] { showMeterColourMenu (false); };
    outMeter.onColourMenu = [this] { showMeterColourMenu (false); };
    mixer.onMeterColourMenu = [this] { showMeterColourMenu (false); };
    spectrum.onColourMenu = [this] { showMeterColourMenu (true); };
    pattern.onSpectrumMenu = [this] { showMeterColourMenu (true); };

    // PARAMS: a plug-in instrument has its own page
    instruments.openParams = [this]
    {
        const auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
        setView (in.isMidiOut() ? View::MidiOut : in.isCvOut() ? View::CvOut : in.isPlugin() ? View::Synth : View::Params);
    };
    sequence.openArrange = [this] { setView (View::Arrange); };
    sequence.onStatus = [this] (const juce::String& m) { setStatus (m); };
    sequence.returnFocus = [this]
    {
        switch (proc.editorState.view)
        {
            case View::SampleEdit: sampleEditor.grabKeyboardFocus(); break;
            case View::Browser:    browser.grabKeyboardFocus(); break;
            case View::Params:     params.grabKeyboardFocus(); break;
            case View::Synth:      synthPage.grabKeyboardFocus(); break;
            case View::MidiOut:    midiPage.grabKeyboardFocus(); break;
            case View::Arrange:    arrange.grabKeyboardFocus(); break;
            case View::Mixer:      mixer.grabKeyboardFocus(); break;
            case View::Help:       help.grabKeyboardFocus(); break;
            case View::Chords:     chords.grabKeyboardFocus(); break;
            case View::Sample:     samplePage.grabKeyboardFocus(); break;
            case View::CvOut:      cvPage.grabKeyboardFocus(); break;
            case View::CvSources:  cvSources.grabKeyboardFocus(); break;
            case View::Radio:      radioPage.grabKeyboardFocus(); break;
            case View::Break:      breakPage.grabKeyboardFocus(); break;
            case View::Live:       livePage.grabKeyboardFocus(); break;
            case View::Visuals:    visualsPage.grabKeyboardFocus(); break;
            case View::Pattern:    pattern.grabKeyboardFocus(); break;
        }
    };
    arrange.onClose = [this] { setView (View::Pattern); };
    arrange.onChanged = [this] { sequence.refresh(); blockPanel.refresh(); };
    arrange.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (arrange);
    mixer.onClose = [this] { setView (View::Pattern); };
    mixer.onStatus = [this] (const juce::String& m) { setStatus (m); };
    mixer.onPlayStop = [this] (bool song) { pattern.togglePlayStop (song, true); refreshAll(); };
    mixer.onOctave = [this] { refreshAll(); mixer.repaint(); };
    addChildComponent (mixer);
    mixerBtn.onClick = [this] { setView (proc.editorState.view == View::Mixer ? View::Pattern : View::Mixer); };
    fx2Btn.onClick = [this, &st] { st.showFx2 = fx2Btn.getToggleState(); if (! st.showFx2) st.column = std::min (st.column, 5); refreshAll(); };
    params.onClose = [this] { setView (View::Pattern); };
    params.onUsePlugin = [this]
    {
        juce::Component::SafePointer<PaulaEditor> safe (this);
        choice.show (*this, "PLAY THIS INSTRUMENT WITH", { "A VST3 / AU INSTRUMENT", "MIDI OUT TO HARDWARE", "CV OUT TO A MODULAR (ES-9)" }, [safe] (int a)
        {
            if (safe == nullptr || a < 0) return;
            if (a == 0) { safe->setView (View::Synth); safe->synthPage.opened (true); }
            else if (a == 1) { safe->setView (View::MidiOut); safe->midiPage.opened (true); }
            else safe->makeCvOut();
        });
    };
    params.onStatus = [this] (const juce::String& m) { setStatus (m); };
    midiPage.onClose = [this] { setView (View::Pattern); };
    midiPage.onChords = [this] { openChords(); };
    synthPage.onChords = [this] { openChords(); };
    midiPage.onChanged = [this] { refreshAll(); };
    midiPage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (midiPage);
    synthPage.onClose = [this] { setView (View::Pattern); };
    synthPage.onChanged = [this] { refreshAll(); };
    synthPage.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (synthPage);
    params.onChanged = [this] { instruments.refresh(); };
    addChildComponent (params);
    sequence.pushStructUndo = [this] (const juce::String& w) { pattern.pushStructureUndo (w); };
    blockPanel.pushStructUndo = [this] (const juce::String& w) { pattern.pushStructureUndo (w); };
    blockPanel.onDouble = [this] { pattern.doubleBlock(); };
    blockPanel.onHalve = [this] { pattern.halveBlock(); };
    pattern.onMoveSequence = [this] (int d) { sequence.moveSelected (d); };
    instruments.openBrowser = [this] { setView (View::Browser); };
    instruments.openSource = [this] (int kind) { openLoad (kind); };
    for (int k = 0; k < 5; ++k)
    {
        static const int kindOfTab[5] = { 0, 4, 1, 2, 3 };   // SAMPLES, SYNTHS, VST / AU, MIDI OUT, CV OUT
        loadTab[k].setClickingTogglesState (false);
        loadTab[k].onClick = [this, k] { openLoad (kindOfTab[k]); };
        addChildComponent (loadTab[k]);
    }
    instruments.openSampleEditor = [this]
    {
        // EDIT on a plug-in instrument opens the plug-in's own window
        const auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
        if (in.isMidiOut()) setView (View::MidiOut);
        else if (in.isCvOut()) setView (View::CvOut);
        else if (in.isBuiltin()) setView (View::Engine);
        else if (in.isPlugin()) synthPage.openWindow();
        else setView (View::SampleEdit);
    };
    sampleEditor.onChanged = [this] { instruments.refresh(); };
    sampleEditor.onClose = [this] { setView (View::Pattern); };
    sampleEditor.onStatus = [this] (const juce::String& m) { setStatus (m); };
    sampleEditor.onStems = [this] { splitStems(); };
    prv::ui::Theme::markDarkPage (stemsPanel);
    addChildComponent (stemsPanel);
    browser.onLoaded = [this] { refreshAll(); setView (returnView); };
    browser.onClose = [this] { setView (returnView); };
    browser.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (sampleEditor);
    addChildComponent (browser);
    addChildComponent (automation);
    addChildComponent (crtOverlay);
    addChildComponent (dragReadout);
    // SMARTER LAYOUT: the sidebar panels fold to their titles
    for (auto* f : { &seqFold, &blockFold, &instFold }) { addChildComponent (*f); f->onRightClick = [this] { showSidebarMenu(); }; }
    seqFold.onClick   = [this] { foldPanel (1, false); };
    blockFold.onClick = [this] { foldPanel (2, false); };
    instFold.onClick  = [this] { foldPanel (4, false); };
    titleClicks.ed = this;
    sequence.onShowAll = [this] (bool on) { setSequenceShowAll (on); };
    sequence.addMouseListener (&titleClicks, false);
    blockPanel.addMouseListener (&titleClicks, false);
    instruments.addMouseListener (&titleClicks, false);
    // PICTURES: icons beside the labels (when there's room for both at full size)
    using Icon = prv::ui::PixButton::Icon;
    helpBtn.icon = Icon::Help; newBtn.icon = Icon::Page; loadBtn.icon = Icon::Folder; saveBtn.icon = Icon::Disk; saveAsBtn.icon = Icon::Disk;
    undoBtn.icon = Icon::Undo; redoBtn.icon = Icon::Redo; mixerBtn.icon = Icon::Mixer; specBtn.icon = Icon::Spectrum;
    radioBtn.icon = Icon::Radio; autoBtn.icon = Icon::Curve;
    prv::ui::DragReadout::instance = &dragReadout;

    for (auto* c : std::initializer_list<juce::Component*> {
            &helpBtn, &newBtn, &loadBtn, &saveBtn, &saveAsBtn, &undoBtn, &redoBtn, &fullVelBtn, &noteOffsBtn, &zoomOut, &zoomIn, &outMeter,
            &playSongBtn, &playBlockBtn, &stopBtn, &editBtn, &captureBtn, &loopBtn, &followBtn, &panelsBtn, &tempoBox, &speedBox, &octBox, &stepBox, &posLabel,
            &a500Btn, &a1200Btn, &a4000Btn, &cleanBtn, &s950Btn, &s1000Btn, &steppedBtn, &eightBitBtn, &crunchBtn, &ledBtn, &ledLabel, &volBox,
            &viewBtn, &mixerBtn, &statusLabel,
            &skinBtn, &specBtn, &fx2Btn, &recAutoBtn, &grooveBtn, &radioBtn, &autoBtn, &liveBtn, &spectrum,
            &pattern, &sequence, &blockPanel, &instruments })
        addAndMakeVisible (c);

    panelsBtn.setToggleState (true, juce::dontSendNotification);
    setStatus (kHelpText);

    setResizable (true, true);
    setResizeLimits (1000, 640, 8000, 5000);   // (the transport row at full size)
    setSize (1440, 900);
    refreshAll();
    setView (st.view == View::Browser ? View::Pattern : st.view);
    startTimerHz (60);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PaulaEditor> (this)] { if (safe) safe->pattern.grabKeyboardFocus(); });

    // with a plug-in's window in front, the computer keyboard still plays (and edits) here
    proc.pluginHost.onWindowKey = [this] (const juce::KeyPress& k)
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey || k.getKeyCode() == juce::KeyPress::tabKey) return false;
        return keyPressed (k);
    };
    proc.pluginHost.onWindowKeyState = [this] (bool down) { keyStateFromElsewhere (down); };
}

void PaulaEditor::keyStateFromElsewhere (bool down)
{
    switch (proc.editorState.view)
    {
        case View::SampleEdit: sampleEditor.keyStateChanged (down); break;
        case View::Browser:    browser.keyStateChanged (down); break;
        case View::Engine:     enginePage.keyStateChanged (down); break;
        case View::Params:     params.keyStateChanged (down); break;
        case View::Synth:      synthPage.keyStateChanged (down); break;
        case View::MidiOut:    midiPage.keyStateChanged (down); break;
        case View::Mixer:      mixer.keyStateChanged (down); break;
        case View::Arrange:    break;
        default:               pattern.keyStateChanged (down); break;
    }
}

PaulaEditor::~PaulaEditor()
{
    prv::ui::menuHook() = nullptr;
    if (prv::ui::DragReadout::instance == &dragReadout) prv::ui::DragReadout::instance = nullptr;
    sequence.removeMouseListener (&titleClicks);
    blockPanel.removeMouseListener (&titleClicks);
    instruments.removeMouseListener (&titleClicks);
    proc.pluginHost.onWindowKey = nullptr;
    proc.pluginHost.onWindowKeyState = nullptr;
    stopTimer();
    proc.history.onRestored = nullptr;
    waitForSave();   // (a save still being written finishes first)
}

void PaulaEditor::historyRestored (const juce::String& what)
{
    sampleEditor.stop();
    sampleEditor.instrumentSelected();
    sampleEditor.repaint();
    pattern.syncLoop();
    if (params.isVisible()) params.refresh();
    if (synthPage.isVisible()) synthPage.refresh();
    if (arrange.isVisible()) arrange.opened();
    if (mixer.isVisible()) mixer.refresh();
    setStatus (what);
    refreshAll();
    repaint();
}

void PaulaEditor::setView (View v)
{
    auto& st = proc.editorState;
    if (v == View::Browser && st.view != View::Browser) returnView = st.view;
    if (st.view == View::SampleEdit && v != View::SampleEdit) sampleEditor.stop();
    if (st.view == View::Params && v != View::Params) proc.withLock ([&] { proc.player.jamAllOff(); });
    if (st.view == View::Synth && v != View::Synth) proc.withLock ([&] { proc.player.jamAllOff(); });
    if (st.view == View::MidiOut && v != View::MidiOut) proc.withLock ([&] { proc.player.jamAllOff(); });
    if (st.view == View::CvOut && v != View::CvOut) proc.withLock ([&] { proc.player.jamAllOff(); });
    auto wide = [] (View w) { return w == View::Mixer || w == View::Help || w == View::Chords || w == View::CvSources || w == View::Radio || w == View::Break || w == View::Live; };
    if (st.view == View::Visuals && v != View::Visuals) visualsPage.closed();
    const bool layoutChange = wide (st.view) != wide (v);
    st.view = v;
    if (layoutChange) resized();
    pattern.setVisible (v == View::Pattern);
    sampleEditor.setVisible (v == View::SampleEdit);
    browser.setVisible (v == View::Browser);
    params.setVisible (v == View::Params);
    synthPage.setVisible (v == View::Synth);
    midiPage.setVisible (v == View::MidiOut);
    arrange.setVisible (v == View::Arrange);
    mixer.setVisible (v == View::Mixer);
    help.setVisible (v == View::Help);
    chords.setVisible (v == View::Chords);
    samplePage.setVisible (v == View::Sample);
    cvPage.setVisible (v == View::CvOut);
    enginePage.setVisible (v == View::Engine);
    cvSources.setVisible (v == View::CvSources);
    radioPage.setVisible (v == View::Radio);
    breakPage.setVisible (v == View::Break);
    livePage.setVisible (v == View::Live);
    visualsPage.setVisible (v == View::Visuals);
    if (v == View::Visuals) visualsPage.toFront (false);
    liveBtn.setToggleState (v == View::Live, juce::dontSendNotification);
    radioBtn.setToggleState (v == View::Radio || proc.radio.state() != prv::RadioPlayer::State::Stopped, juce::dontSendNotification);
    helpBtn.setToggleState (v == View::Help, juce::dontSendNotification);
    mixerBtn.setToggleState (v == View::Mixer, juce::dontSendNotification);
    if (v == View::Params) { params.refresh(); params.grabKeyboardFocus(); }
    else if (v == View::Synth) synthPage.opened (false);
    else if (v == View::MidiOut) midiPage.opened (false);
    else if (v == View::Arrange) arrange.opened();
    else if (v == View::SampleEdit) { sampleEditor.instrumentSelected(); sampleEditor.grabKeyboardFocus(); }
    else if (v == View::Browser) browser.opened();
    else if (v == View::Mixer) mixer.opened();
    else if (v == View::Help) help.opened();
    else if (v == View::Chords) chords.opened();
    else if (v == View::Sample) samplePage.opened();
    else if (v == View::CvOut) cvPage.opened();
    else if (v == View::Engine) { enginePage.refresh(); enginePage.grabKeyboardFocus(); }
    else if (v == View::CvSources) cvSources.opened();
    else if (v == View::Radio) radioPage.opened();
    else if (v == View::Break) breakPage.opened();
    else if (v == View::Live) livePage.opened();
    else if (v == View::Visuals) visualsPage.opened();
    else pattern.grabKeyboardFocus();
    refreshAll();
}

// ---------------------------------------------------------------------------
void PaulaEditor::applyOptions()
{
    proc.withLock ([&] {
        auto& o = proc.player.options;
        if (! o.isAkai())   // the Akai models show their own fixed settings on these buttons
        {
            o.zeroOrderHold = steppedBtn.getToggleState();
            o.eightBit = eightBitBtn.getToggleState();
            if (o.isAmiga()) o.crunch8ch = crunchBtn.getToggleState();
        }
        o.stereoSep = sepSlider.value;
        o.master = volSlider.value * 2.0f;
    });
}

void PaulaEditor::refreshAll()
{
    auto& st = proc.editorState;
    {
        static const int tabOfKind[5] = { 0, 2, 3, 4, 1 };
        if (loadTabsShown() != tabsLaidOut || (tabsLaidOut && ! loadTab[tabOfKind[loadTabShown()]].getToggleState())) resized();   // (LOAD's tabs came or went)
    }
    const auto& song = proc.song;
    st.block = song.safeBlock (st.block);
    st.line = juce::jlimit (0, song.blocks[(size_t) st.block].lines - 1, st.line);
    st.track = juce::jlimit (0, song.numTracks - 1, st.track);
    // block play: picking another block carries playback over to it, in time
    proc.withLock ([&] { proc.player.switchBlock (st.block); });
    if (chords.isVisible()) chords.refresh();

    const auto& o = proc.player.options;
    a500Btn.setToggleState (o.model == prv::PaulaOptions::Model::A500, juce::dontSendNotification);
    a1200Btn.setToggleState (o.model == prv::PaulaOptions::Model::A1200, juce::dontSendNotification);
    a4000Btn.setToggleState (o.model == prv::PaulaOptions::Model::A4000, juce::dontSendNotification);
    cleanBtn.setToggleState (o.model == prv::PaulaOptions::Model::Clean, juce::dontSendNotification);
    s950Btn.setToggleState (o.model == prv::PaulaOptions::Model::S950, juce::dontSendNotification);
    s1000Btn.setToggleState (o.model == prv::PaulaOptions::Model::S1000, juce::dontSendNotification);
    // the Akai models set these themselves; show what they do
    const bool akai = o.isAkai();
    steppedBtn.setToggleState (o.stepped(), juce::dontSendNotification);
    eightBitBtn.setToggleState (akai ? false : o.eightBit, juce::dontSendNotification);
    crunchBtn.setToggleState (o.isAmiga() && o.crunch8ch, juce::dontSendNotification);
    steppedBtn.setEnabled (! akai);
    eightBitBtn.setEnabled (! akai);
    crunchBtn.setEnabled (o.isAmiga());   // 8CH GRIT: the Amiga models
    ledBtn.setEnabled (! akai);
    ledBtn.setToggleState (song.ledFilterAtStart, juce::dontSendNotification);
    sepSlider.setValue (o.stereoSep);
    volSlider.setValue (o.master * 0.5f);
    volBox.setValue (juce::roundToInt (o.master * 100.0f));

    editBtn.setToggleState (st.editMode, juce::dontSendNotification);
    loopBtn.setToggleState (st.loopOn, juce::dontSendNotification);
    undoBtn.setEnabled (proc.history.canUndo());
    redoBtn.setEnabled (proc.history.canRedo());
    followBtn.setToggleState (st.follow, juce::dontSendNotification);
    tempoBox.setValue (song.tempo);
    speedBox.setValue (song.speed);
    octBox.setValue (st.octave);
    stepBox.setValue (st.step);

    viewBtn.setToggleState (st.fourTrackView, juce::dontSendNotification);
    if (st.darkSkin != prv::ui::Theme::skinIsDark (st.skin)) st.skin = st.darkSkin ? 1 : 0;   // (something set DARK directly)
    skinBtn.setToggleState (st.skin != 0, juce::dontSendNotification);
    if (sequence.wholeBtn.getToggleState() && seqSpread != (sequence.visibleListRows() < (int) proc.song.sequence.size() || seqSpread))
        resized();   // (SHOW ALL: the song grew past the sidebar)
    // (WORKBENCH and DARK: the button says DARK, off or on; the long names short; the rest by name)
    skinBtn.setButtonText (st.skin == 2 ? "KS 1.3" : st.skin == 3 ? "HI CON" : st.skin <= 1 ? "DARK"
                           : prv::ui::Theme::skinName ((prv::ui::Theme::SkinId) juce::jlimit (0, prv::ui::Theme::kSkins - 1, st.skin)));
    specBtn.setToggleState (st.showSpectrum, juce::dontSendNotification);
    autoBtn.setToggleState (st.showAutomation, juce::dontSendNotification);
    automation.refresh();
    fx2Btn.setToggleState (st.showFx2, juce::dontSendNotification);
    recAutoBtn.setToggleState (st.recAuto, juce::dontSendNotification);
    grooveBtn.setToggleState (proc.song.grooveKind != 0, juce::dontSendNotification);
    if (! st.showFx2) st.column = std::min (st.column, 5);

    proc.jamInstrument = st.instrument;
    sequence.refresh();
    blockPanel.refresh();
    instruments.refresh();
    pattern.repaint();
    if (arrange.isVisible()) arrange.repaint();
    repaint (getLocalBounds().removeFromTop ((int) (prv::ui::Theme::px * 14)));
}

void PaulaEditor::setStatus (const juce::String& s)
{
    statusText = s.toUpperCase();
    statusLabel.setText (statusText);
    statusTimer = 30 * 12;
}

void PaulaEditor::timerCallback()
{
    // FILL: while F2 is held (or latched with Shift+F2, or the BREAK page's FILL)
    {
        const bool held = hasKeyboardFocus (true) && juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::F2Key);
        const bool want = fillLatched || held || breakPage.fillBtn.getToggleState();
        if (proc.player.fill.load() != want)
        {
            proc.player.fill.store (want);
            pattern.repaint();
            if (held) setStatus ("FILL - ?01 STEPS PLAY, ?02 STEPS DON'T (SHIFT+F2 LATCHES IT)");
        }
    }
    // LIVE FEEDBACK at 60 frames a second: the pattern (play line, meters, scopes, note flashes) and the output meter
    pattern.tick();
    outMeter.tick();
    if (mixer.isVisible()) mixer.tickMeters();
    if ((++frame & 1) != 0) return;   // everything else, 30 times a second as before
    if (lane.isVisible()) { lane.tick(); if ((frame & 15) == 0) lane.repaint(); }   // (and now and then: notes typed in show up)
    if ((frame & 15) == 0)
    {
        // STRETCH: refit stretched instruments when the tempo or LINES changed (about 4 times a second)
        const auto said = proc.refreshStretches();
        if (! said.isEmpty()) { setStatus (said[said.size() - 1]); instruments.repaint(); if (params.isVisible()) params.refresh(); }
    }
    prv::Diagnostics::get().tick();   // (slow audio blocks into the log; a stall of this window too)
    {
        // the radio's changes, and it running dry (the network too slow), into the log
        const int rs = (int) proc.radio.state();
        const auto dry = proc.radio.underruns();
        if (rs != lastRadioState)
        {
            const auto s = proc.radio.station();
            const auto why = proc.radio.problem();
            prv::Diagnostics::get().log ("RADIO " + prv::RadioPlayer::stateName ((prv::RadioPlayer::State) rs) + ": " + s.name + " (" + s.format() + ") " + s.url
                                         + (why.isNotEmpty() ? " - " + why : juce::String()));
            lastRadioState = rs;
        }
        if (dry > lastRadioDry)
            prv::Diagnostics::get().log ("RADIO RAN DRY (THE STATION / NETWORK FELL BEHIND): " + proc.radio.station().name + ", " + juce::String (dry) + " TIMES SO FAR");
        lastRadioDry = dry;
    }
    // the pattern gets the keyboard back when nothing else has it (after a dialog, a plug-in
    // window, the app coming to the front...), so typing always lands in the pattern
    if (proc.editorState.view == View::Pattern && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0
        && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        if ((f == nullptr || f == this) && isShowing() && getPeer() != nullptr && getPeer()->isFocused()) pattern.grabKeyboardFocus();
    }
    if (automation.isVisible()) automation.tick();
    if (proc.followClock.load())
    {
        // the tempo it's following, when it changes by a whole BPM
        const float b = proc.clockBpm.load();
        if (std::abs (b - shownClockBpm) >= 1.0f || (b == 0) != (shownClockBpm == 0))
        {
            shownClockBpm = b;
            setStatus (b > 0 ? "MIDI CLOCK: " + juce::String (b, 1) + " BPM" : juce::String ("MIDI CLOCK: WAITING FOR A CLOCK ON THE MIDI INPUT"));
        }
    }
    if (spectrum.isVisible())
    {
        // at instrument level (its PARAMS or plug-in page) the strip shows just that instrument
        const auto v = proc.editorState.view;
        spectrum.setInstrument (v == View::Params || v == View::Synth ? proc.editorState.instrument : -1);
        // PARAMS: the strip shows the filter's curve over the instrument's sound too
        if (v == View::Params && ! params.phrasesShown())
        {
            if (! spectrum.overlay)
                spectrum.overlay = [this] (juce::Graphics& g, juce::Rectangle<float> r)
                {
                    if (proc.editorState.view != View::Params || params.phrasesShown()) return;
                    prv::ui::FilterScope::drawCurve (g, r, proc.song.instruments[(size_t) juce::jlimit (0, prv::kMaxInstruments - 1, proc.editorState.instrument)], proc.getSampleRate());
                };
        }
        else spectrum.overlay = nullptr;
        spectrum.tick();
    }
    else spectrum.setInstrument (-1);
    if (sampleEditor.isVisible()) sampleEditor.tick();
    if (browser.isVisible()) browser.tick();
    if (params.isVisible()) params.tick();
    if (synthPage.isVisible()) synthPage.tick();
    // SAMPLE: its meter, and a take finishing even while another page is showing
    if (samplePage.isVisible() || proc.recorder.state() != prv::TakeRecorder::State::Idle) samplePage.tick();
    if (midiPage.isVisible()) midiPage.tick();
    if (cvPage.isVisible()) cvPage.tick();
    if (enginePage.isVisible()) enginePage.tick();
    if (radioPage.isVisible()) radioPage.tick();
    if (breakPage.isVisible()) breakPage.tick();
    if (livePage.isVisible()) livePage.tick();
    if (stemsPanel.isVisible()) stemsPanel.repaint();
    radioBtn.setToggleState (proc.editorState.view == View::Radio || proc.radio.state() != prv::RadioPlayer::State::Stopped, juce::dontSendNotification);
    // the PARAMS page follows the instrument: a plug-in or MIDI-out instrument shows its own page
    {
        const auto& cur = proc.song.instruments[(size_t) proc.editorState.instrument];
        const auto view = proc.editorState.view;
        if ((view == View::Params || view == View::Synth || view == View::CvOut) && cur.isMidiOut()) setView (View::MidiOut);
        else if ((view == View::Params || view == View::Synth || view == View::MidiOut) && cur.isCvOut()) setView (View::CvOut);
        else if ((view == View::Params || view == View::MidiOut || view == View::CvOut) && cur.isPlugin() && ! cur.isMidiOut() && ! cur.isCvOut()
                 && ! (view == View::Params && paramsForPlugin == proc.editorState.instrument)) setView (View::Synth);   // (unless its filter / envelopes asked for)
        else if (view == View::CvOut && ! cur.isCvOut()) setView (View::Params);
    }
    recordAutomation();
    if (arrange.isVisible()) arrange.tick();
    if (mixer.isVisible()) mixer.tick();
    if (autosave != nullptr) autosave->tick();
    auto& st = proc.editorState;
    const bool playing = proc.player.isPlaying();
    const int seqNow = proc.player.uiSeqPos.load();
    if (playing != lastPlaying || (playing && seqNow != lastSeq))
    {
        const bool stopped = lastPlaying && ! playing;
        lastPlaying = playing; lastSeq = seqNow;
        if (stopped) refreshAll();
        sequence.refresh();
        blockPanel.refresh();
    }

    const juce::String pos = "POS " + juce::String (st.seqPos).paddedLeft ('0', 3)
                           + "  BLK " + juce::String (st.block).paddedLeft ('0', 3)
                           + "  LINE " + juce::String (st.line).paddedLeft ('0', 3)
                           + "  INS " + juce::String::toHexString (st.instrument + 1).toUpperCase().paddedLeft ('0', 2);
    posLabel.setText (pos);

    // while playing, the tempo/speed boxes and the LED button show the live values
    if (playing)
    {
        tempoBox.setValue (proc.player.uiTempo.load());
        speedBox.setValue (proc.player.uiSpeed.load());
        ledBtn.setToggleState (proc.player.uiLed.load(), juce::dontSendNotification);
    }


    const bool dirty = proc.history.isDirty();
    if (dirty != lastDirty)
    {
        lastDirty = dirty;
        saveBtn.setButtonText (dirty ? "SAVE*" : "SAVE");   // unsaved changes
        repaint (getLocalBounds().removeFromTop ((int) (prv::ui::Theme::px * 14)));
    }
    if (undoBtn.isEnabled() != proc.history.canUndo()) undoBtn.setEnabled (proc.history.canUndo());
    if (redoBtn.isEnabled() != proc.history.canRedo()) redoBtn.setEnabled (proc.history.canRedo());

    if (statusTimer > 0 && --statusTimer == 0)
        statusLabel.setText (kHelpText);
}

// ---------------------------------------------------------------------------
void PaulaEditor::paint (juce::Graphics& g)
{
    using prv::ui::Theme;
    using prv::ui::PixelFont;
    Theme::paintBackground (g, getLocalBounds());
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();

    // title bar: horizontal rules with the name knocked out (drawn in black on white; on a wash, straight on it)
    auto title = r.removeFromTop (px * 14);
    if (! Theme::skin.wash)
    {
        g.setColour (Theme::panel);
        g.fillRect (title);
        Theme::bevel (g, title.reduced (1.0f), true, std::max (1.0f, px * 0.5f));
    }
    const float tpx = px * 1.5f;
    const juce::String name = "PAULA'S REVENGE";
    const float nameW = PixelFont::textWidth (name, tpx);
    auto nameArea = title.withTrimmedLeft (px * 4).withWidth (nameW + px * 2);
    if (nameArea.getRight() + px * 2 < helpBtn.getX())   // only when it fits (the window title has it anyway)
        PixelFont::drawLeft (g, name, nameArea, tpx, Theme::ink);
    else nameArea = nameArea.withWidth (0);
    auto rules = title.withLeft (nameArea.getRight() + px * 3).withRight (helpBtn.getX() - px * 4.0f).reduced (0, px * 4);
    g.setColour (Theme::ink);
    for (float y = rules.getY(); y < rules.getBottom(); y += px * 2)
        g.fillRect (rules.getX(), y, std::max (0.0f, rules.getWidth()), std::max (1.0f, px * 0.5f));
    const juce::String songName = "\"" + juce::String (proc.song.name) + "\"" + (proc.history.isDirty() ? " *" : "");
    const float snW = PixelFont::textWidth (songName, px) + px * 4;
    if (rules.getWidth() > snW + px * 8)
    {
        auto sn = rules.withSizeKeepingCentre (snW, rules.getHeight() + px * 2);
        g.setColour (Theme::panel);
        g.fillRect (sn);
        PixelFont::drawCentred (g, songName, sn, px, Theme::ink);
    }
}

void PaulaEditor::resized()
{
    using prv::ui::Theme;
    auto& st = proc.editorState;
    const float base = std::min ((float) getHeight() / 300.0f, (float) getWidth() / 480.0f);
    // (text and lines snap to whole screen pixels: crisp at any size)
    {
        float scale = 1.0f;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) scale = (float) d->scale;
        scale = std::max (1.0f, std::round (scale));
        Theme::deviceScale = scale;
        prv::ui::PixelFont::snapScale = scale;
    }
    Theme::px = juce::jlimit (1.5f, 5.0f, std::floor (base * 2.0f) / 2.0f + st.uiZoom * 0.5f);
    crtOverlay.setBounds (getLocalBounds());
    crtOverlay.setVisible (Theme::crt);
    crtOverlay.toFront (false);
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };

    auto r = getLocalBounds();

    // title bar buttons (right side)
    auto title = r.removeFromTop ((int) (px * 14)).reduced (gap, (int) (px * 1.5f));
    // the wide gaps between the groups close up when the window is narrow, so every label stays full size
    const int buttonsW = 2 * (charsW (2) + gap) + charsW (7) + charsW (9) + charsW (8) + 2 * charsW (4) + charsW (7)
                       + charsW (5) + charsW (4) + charsW (3) + charsW (4) + gap * 7;
    const int wide = buttonsW + gap * 3 * 4 <= title.getWidth() ? gap * 3 : gap;
    // PICTURES: the spare room left of the buttons buys icons, most useful first (labels never shrink for them)
    std::map<prv::ui::PixButton*, int> plus;
    {
        int spare = title.getWidth() - buttonsW - wide * 4 - charsW (4);   // (keep some of the title's rules)
        const int iconW = (int) std::ceil (px * 9);
        for (auto* b : { &helpBtn, &loadBtn, &saveBtn, &newBtn, &undoBtn, &redoBtn, &saveAsBtn })
            if (spare >= iconW) { plus[b] = iconW; spare -= iconW; }
    }
    auto withIcon = [&plus] (prv::ui::PixButton& b, int w) { auto it = plus.find (&b); return w + (it != plus.end() ? it->second : 0); };
    zoomIn.setBounds (title.removeFromRight (charsW (2) + gap)); title.removeFromRight (gap);
    zoomOut.setBounds (title.removeFromRight (charsW (2) + gap)); title.removeFromRight (wide);
    captureBtn.setBounds (title.removeFromRight (charsW (7))); title.removeFromRight (gap);
    noteOffsBtn.setBounds (title.removeFromRight (charsW (9))); title.removeFromRight (gap);
    fullVelBtn.setBounds (title.removeFromRight (charsW (8))); title.removeFromRight (wide);
    redoBtn.setBounds (title.removeFromRight (withIcon (redoBtn, charsW (4)))); title.removeFromRight (gap);
    undoBtn.setBounds (title.removeFromRight (withIcon (undoBtn, charsW (4)))); title.removeFromRight (wide);
    saveAsBtn.setBounds (title.removeFromRight (withIcon (saveAsBtn, charsW (7)))); title.removeFromRight (gap);
    saveBtn.setBounds (title.removeFromRight (withIcon (saveBtn, charsW (5)))); title.removeFromRight (gap);
    loadBtn.setBounds (title.removeFromRight (withIcon (loadBtn, charsW (4)))); title.removeFromRight (gap);
    newBtn.setBounds (title.removeFromRight (withIcon (newBtn, charsW (3)))); title.removeFromRight (wide);
    helpBtn.setBounds (title.removeFromRight (withIcon (helpBtn, charsW (4))));

    r.removeFromTop (gap);

    // transport row
    // (FOLLOW and MIXER after STEP; the wider gaps close up when the window is narrow, labels stay full size)
    auto tr = r.removeFromTop (rowH).reduced (std::max (1, gap / 2), 0);
    auto tW = [px] (int chars) { return (int) std::ceil (px * 6 * chars - px + 7.0f); };   // (the text at full size, a little room)
    const int fixedW = tW (6) + tW (7) + tW (6) + tW (6) + tW (4) + tW (6) + tW (5)
                     + tempoBox.preferredWidth (px) + speedBox.preferredWidth (px) + octBox.preferredWidth (px) + stepBox.preferredWidth (px);
    const int tg = std::max (2, (int) px);   // between the buttons
    const int spare = tr.getWidth() - fixedW - tg * 6;
    const int bigGap = juce::jlimit (tg, gap * 3, spare / 5);
    playSongBtn.setBounds (tr.removeFromLeft (tW (6))); tr.removeFromLeft (tg);
    playBlockBtn.setBounds (tr.removeFromLeft (tW (7))); tr.removeFromLeft (tg);
    stopBtn.setBounds (tr.removeFromLeft (tW (6))); tr.removeFromLeft (tg);
    editBtn.setBounds (tr.removeFromLeft (tW (6))); tr.removeFromLeft (tg);
    loopBtn.setBounds (tr.removeFromLeft (tW (4))); tr.removeFromLeft (bigGap);
    tempoBox.setBounds (tr.removeFromLeft (tempoBox.preferredWidth (px))); tr.removeFromLeft (bigGap);
    speedBox.setBounds (tr.removeFromLeft (speedBox.preferredWidth (px))); tr.removeFromLeft (bigGap);
    octBox.setBounds (tr.removeFromLeft (octBox.preferredWidth (px))); tr.removeFromLeft (bigGap);
    stepBox.setBounds (tr.removeFromLeft (stepBox.preferredWidth (px))); tr.removeFromLeft (bigGap);
    followBtn.setBounds (tr.removeFromLeft (tW (6))); tr.removeFromLeft (tg);
    mixerBtn.setBounds (tr.removeFromLeft (tW (5)));
    posLabel.setVisible (false);

    r.removeFromTop (gap);

    // bottom strips
    auto status = r.removeFromBottom (rowH).reduced (gap, 0);
    r.removeFromBottom (gap);
    auto paula = r.removeFromBottom (rowH).reduced (gap, 0);    // models, options, meter
    r.removeFromBottom (gap);

    viewBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    panelsBtn.setBounds (status.removeFromLeft (charsW (6))); status.removeFromLeft (gap);
    skinBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    specBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    fx2Btn.setBounds (status.removeFromLeft (charsW (3))); status.removeFromLeft (gap);
    recAutoBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    grooveBtn.setBounds (status.removeFromLeft (charsW (6))); status.removeFromLeft (gap);
    radioBtn.setBounds (status.removeFromLeft (charsW (5))); status.removeFromLeft (gap);
    autoBtn.setBounds (status.removeFromLeft (charsW (10))); status.removeFromLeft (gap);
    liveBtn.setBounds (status.removeFromLeft (charsW (4))); status.removeFromLeft (gap);
    status.removeFromLeft (gap * 2);
    ledLabel.setVisible (false);
    statusLabel.setBounds (status);

    a500Btn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    a1200Btn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    a4000Btn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    cleanBtn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    s950Btn.setBounds (paula.removeFromLeft (charsW (4))); paula.removeFromLeft (gap);
    s1000Btn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap * 3);
    steppedBtn.setBounds (paula.removeFromLeft (charsW (7))); paula.removeFromLeft (gap);
    eightBitBtn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    crunchBtn.setBounds (paula.removeFromLeft (charsW (8))); paula.removeFromLeft (gap);
    ledBtn.setBounds (paula.removeFromLeft (charsW (4))); paula.removeFromLeft (gap * 3);
    // the master VOL beside the L / R meter (SEP: right-click a machine button)
    {
        auto meterArea = paula.withTrimmedLeft (std::max (0, paula.getWidth() - charsW (34)));
        volBox.setBounds (meterArea.removeFromLeft (std::min (volBox.preferredWidth (px), meterArea.getWidth() - charsW (6)))); meterArea.removeFromLeft (gap * 2);
        outMeter.setBounds (meterArea);
    }

    // main area
    auto main = r.reduced (gap, 0);
    // the mixer uses the whole width
    // the mixer and HELP use the whole width
    if (st.view == View::Mixer) mixer.setBounds (main);
    help.setBounds (main);
    chords.setBounds (main);
    cvSources.setBounds (main);
    radioPage.setBounds (main);
    breakPage.setBounds (main);
    livePage.setBounds (main);
    visualsPage.setBounds (getLocalBounds());   // (VISUALS: the whole window)
    const bool wideView = st.view == View::Mixer || st.view == View::Help || st.view == View::Chords || st.view == View::CvSources || st.view == View::Radio || st.view == View::Break || st.view == View::Live;
    const bool panels = showPanels && ! wideView;
    // SMARTER LAYOUT: a folded panel is just its title bar; the open ones share the room
    const int fold = st.sidebarFold;
    sequence.wholeBtn.setToggleState ((fold & 7) == 6, juce::dontSendNotification);
    sequence.setVisible (panels && (fold & 1) == 0);
    blockPanel.setVisible (panels && (fold & 2) == 0);
    instruments.setVisible (panels && (fold & 4) == 0);
    seqFold.setVisible (panels && (fold & 1) != 0);
    blockFold.setVisible (panels && (fold & 2) != 0);
    instFold.setVisible (panels && (fold & 4) != 0);
    // PADS (the pattern page): four by four beside the pattern when there's no sidebar, otherwise two rows
    // of eight under it in the spectrum's place - either way the pattern keeps its width and its text size
    const bool padsShown = st.showPads && st.view == View::Pattern;
    pads.setVisible (padsShown);
    padsBeside = padsShown && ! panels;
    if (panels)
    {
        auto left = main.removeFromLeft (std::min (charsW (32), main.getWidth() / 2));
        main.removeFromLeft (gap * 2);
        const int foldH = (int) (prv::ui::PixelFont::textHeight (px) + px * 3);
        const int blockH = (int) (px * 14) + rowH * 3 + gap * 3;
        const bool seqOpen = (fold & 1) == 0, blockOpen = (fold & 2) == 0, instOpen = (fold & 4) == 0;
        // what's left for the two lists once the block panel and the folded titles have theirs
        int room = left.getHeight() - gap * 2 - (blockOpen ? blockH : foldH) - (seqOpen ? 0 : foldH) - (instOpen ? 0 : foldH);
        // (a fourth row of sequence buttons: LOOP SELECTION / LOOP SECTION, taken from the instrument list)
        const int seqH = ! seqOpen ? foldH
                       : ! instOpen ? room
                       : std::max (rowH * 9, (int) ((left.getHeight() - rowH - gap) * 0.40f) + rowH + gap);
        (seqOpen ? (juce::Component&) sequence : (juce::Component&) seqFold).setBounds (left.removeFromTop (seqH));
        left.removeFromTop (gap);
        (blockOpen ? (juce::Component&) blockPanel : (juce::Component&) blockFold).setBounds (left.removeFromTop (blockOpen ? blockH : foldH));
        left.removeFromTop (gap);
        if (instOpen) instruments.setBounds (left);
        else instFold.setBounds (left.removeFromTop (foldH));
        juce::ignoreUnused (room);
        // SHOW ALL: when the song is longer than the sidebar shows, the sequence spreads across the main area
        // too, in columns (the pattern is back when SHOW ALL is off)
        sequence.layoutList();
        if (sequence.wholeBtn.getToggleState() && sequence.visibleListRows() < (int) proc.song.sequence.size())
        {
            // (the whole of the side and the main area: the folded titles wait under it until SHOW ALL is off)
            sequence.setBounds (sequence.getBounds().withRight (main.getRight()).withBottom (std::max (main.getBottom(), sequence.getBottom())));
            blockFold.setVisible (false);
            instFold.setVisible (false);
            pads.setVisible (false);
            sequence.layoutList();
            sequence.toFront (false);
            crtOverlay.toFront (false);
            seqSpread = true;
        }
        else seqSpread = false;
    }
    if (padsShown && ! padsBeside)
    {
        pads.setBounds (main.removeFromBottom (std::min (pads.preferredHeightFor (main.getWidth()), (int) (main.getHeight() * 0.3f))));
        main.removeFromBottom (gap);
    }
    // AUTOMATION under the pattern takes the spectrum's place (and as much room as it's dragged to)
    const bool autoShown = st.showAutomation && st.view == View::Pattern;
    automation.setVisible (autoShown);
    spectrum.setVisible (st.showSpectrum && ! wideView && ! autoShown && ! (padsShown && ! padsBeside));
    if (autoShown)
    {
        const int h = juce::jlimit ((int) (px * 60), std::max ((int) (px * 60), main.getHeight() - (int) (px * 80)), st.autoHeight > 0 ? st.autoHeight : (int) (main.getHeight() * 0.42f));
        automation.setBounds (main.removeFromBottom (h));
        main.removeFromBottom (gap);
    }
    else if (spectrum.isVisible())
    {
        spectrum.setBounds (main.removeFromBottom ((int) (px * 22)));
        main.removeFromBottom (gap);
    }
    // THE LANE over the pattern, the PADS beside it (the pattern page only)
    const bool laneShown = st.showLane && st.view == View::Pattern;
    lane.setVisible (laneShown);
    if (padsBeside)
    {
        // (no sidebar: four by four beside the pattern)
        pads.setBounds (main.removeFromRight (std::min (charsW (24), main.getWidth() / 3)));
        main.removeFromRight (gap);
    }
    if (laneShown)
    {
        lane.setBounds (main.removeFromTop (std::min (lane.preferredHeight(), (int) (main.getHeight() * 0.35f))));
        main.removeFromTop (gap);
    }
    pattern.setBounds (main);
    sampleEditor.setBounds (main);
    params.setBounds (main);
    enginePage.setBounds (main);
    {
        // LOAD's tabs over the sample browser, the plug-in list and the MIDI outputs: SAMPLES, VST / AU, MIDI OUT, CV OUT
        const int lit = loadTabShown();
        tabsLaidOut = lit >= 0;
        auto tabbed = main;
        auto row = tabbed.removeFromTop (rowH); tabbed.removeFromTop (gap);
        const int w = (row.getWidth() - gap * 4) / 5;
        static const int tabOfKind[5] = { 0, 2, 3, 4, 1 };
        for (int k = 0; k < 5; ++k)
        {
            loadTab[k].setBounds (k < 4 ? row.removeFromLeft (w) : row); row.removeFromLeft (gap);
            loadTab[k].setVisible (tabsLaidOut);
            loadTab[k].setToggleState (lit >= 0 && k == tabOfKind[lit], juce::dontSendNotification);
        }
        browser.setBounds (tabbed);
        synthPage.setBounds (tabsLaidOut && lit == 1 ? tabbed : main);
        midiPage.setBounds (tabsLaidOut && lit == 2 ? tabbed : main);
    }
    arrange.setBounds (main);
    samplePage.setBounds (main);
    cvPage.setBounds (main);
    {
        // STEMS' progress: a box across the middle of the page (the rest carries on around it)
        const int h = (int) (px * 12 * 4 + px * 30);
        stemsPanel.setBounds (main.withSizeKeepingCentre (std::min (main.getWidth(), charsW (52)), std::min (main.getHeight(), h)));
    }
}

bool PaulaEditor::loadTabsShown() const { return loadTabShown() >= 0; }

int PaulaEditor::loadTabShown() const
{
    const auto v = proc.editorState.view;
    if (v == View::Browser) return 0;
    if (v == View::Synth && synthPage.isBrowsing()) return 1;
    if (v == View::MidiOut && midiPage.isBrowsing()) return 2;
    return -1;
}

void PaulaEditor::openLoad (int kind)
{
    const int i = proc.editorState.instrument;
    const auto hex = juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2);
    if (kind == 4)
    {
        // SYNTHS: the built-in ones (it turns the instrument into one, so it asks)
        juce::Component::SafePointer<PaulaEditor> safe (this);
        const auto& in = proc.song.instruments[(size_t) i];
        const bool has = in.hasData() || in.isPlugin();
        choice.show (*this, "INSTRUMENT " + hex + (has ? ": PLAY A BUILT-IN SYNTH INSTEAD? (UNDO BRINGS IT BACK)" : ": A BUILT-IN SYNTH"),
                     { "SUB - A CLICK-FREE SUB BASS (MONO, GLIDE, 808 DROP, DRIVE)",
                       "REESE - DETUNED SAWS THAT PHASE, OR A HOOVER (FILTER, WOBBLE)",
                       "GRANULAR - GRAINS OF WAVETABLES, A SAMPLE OR THE INPUT (FREEZE, LFOS)", "CANCEL" }, [safe] (int a)
        {
            if (safe != nullptr && a == 0) safe->makeBuiltinSynth (prv::kSubKey);
            if (safe != nullptr && a == 1) safe->makeBuiltinSynth (prv::kReeseKey);
            if (safe != nullptr && a == 2) safe->makeBuiltinSynth (prv::kGrainKey);
        });
        return;
    }
    if (kind == 0) setView (View::Browser);
    else if (kind == 1) { setView (View::Synth); synthPage.opened (true); setStatus ("INSTRUMENT " + hex + ": DOUBLE-CLICK A VST3 / AU INSTRUMENT TO LOAD IT (TYPE TO SEARCH)"); }
    else if (kind == 2) { setView (View::MidiOut); midiPage.opened (true); setStatus ("INSTRUMENT " + hex + ": DOUBLE-CLICK A MIDI OUTPUT TO PLAY YOUR HARDWARE"); }
    else
    {
        // CV OUT has no list: it turns the instrument into one straight away, so it asks
        juce::Component::SafePointer<PaulaEditor> safe (this);
        choice.show (*this, "INSTRUMENT " + hex + ": CV OUT TO A MODULAR (ES-9)?", { "YES, PLAY A MODULAR", "NO" }, [safe] (int a)
        {
            if (safe != nullptr && a == 0) safe->makeCvOut();
        });
        return;
    }
    refreshAll();
}

void PaulaEditor::makeBuiltinSynth (const std::string& key)
{
    const int i = proc.editorState.instrument;
    proc.history.instrument (i, key == prv::kSubKey ? "SUB SYNTH" : key == prv::kReeseKey ? "REESE SYNTH" : "GRANULAR SYNTH");
    proc.withLock ([&] {
        proc.player.jamAllOff();
        prv::makeBuiltin (proc.song.instruments[(size_t) i], key);
        for (auto& sl : proc.song.mixer.inst[i]) sl = prv::FxSlot {};
    });
    proc.updateMixerEffects();
    refreshAll();
    setView (View::Engine);
    setStatus ("INSTRUMENT " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2) + " IS A " + juce::String (key == prv::kSubKey ? "SUB" : key == prv::kReeseKey ? "REESE" : "GRANULAR")
               + " - PLAY IT WITH THE NOTE KEYS; RIGHT-CLICK A CONTROL TO MAKE IT A KNOB (LXX...)");
}

void PaulaEditor::makeCvOut()
{
    // this instrument plays a modular through the interface's outputs
    const int i = proc.editorState.instrument;
    proc.history.instrument (i, "CV OUT");
    proc.withLock ([&] {
        auto& in = proc.song.instruments[(size_t) i];
        in.plugin = true; in.cvOut = true; in.midiOut = false; in.pluginKey = "CV:"; in.pluginName = "CV OUT";
        if (in.name.empty() || in.hasData()) in.name = "CV OUT";
        in.bendRange = 12;
    });
    proc.updateMixerEffects();
    refreshAll();
    setView (View::CvOut);
}

void PaulaEditor::openChords (int slot)
{
    auto& st = proc.editorState;
    if (st.view != View::Chords) chordsReturn = st.view == View::Browser ? View::Pattern : st.view;
    setView (View::Chords);
    if (slot > 0) chords.opened (slot);
}

void PaulaEditor::toggleSample()
{
    if (proc.editorState.view == View::Sample) samplePage.onClose();
    else openSample();
}

void PaulaEditor::openSample()
{
    auto& st = proc.editorState;
    if (st.view != View::Sample) sampleReturn = st.view == View::Browser ? View::Pattern : st.view;
    setView (View::Sample);
}

void PaulaEditor::setFullVelocity (bool on)
{
    proc.setFullVelocity (on);
    fullVelBtn.setToggleState (on, juce::dontSendNotification);
    setStatus (on ? "FULL VEL: EVERY NOTE YOU PLAY GOES IN AT FULL VELOCITY (127)" : "FULL VEL OFF: NOTES USE THE KEYBOARD'S VELOCITY");
}

void PaulaEditor::showSeparationMenu()
{
    // SEP: how far the Amiga layout spreads the tracks (1, 4, 5, 8 left; 2, 3, 6, 7 right)
    juce::Component::SafePointer<PaulaEditor> safe (this);
    std::vector<prv::ui::MenuItem> items;
    const int now = juce::roundToInt (sepSlider.value * 100.0f);
    for (int v : { 0, 10, 20, 35, 50, 75, 100 })
        items.push_back ({ juce::String (v == now ? "> " : "  ") + juce::String (v) + "%" + (v == 0 ? " (MONO)" : v == 20 ? " (DEFAULT)" : v == 100 ? " (HARD AMIGA)" : ""),
                           [safe, v] { if (safe) safe->setSeparation (v / 100.0f); } });
    prv::ui::showMenu ("STEREO SEPARATION (SEP)", items);
}

void PaulaEditor::showGrooveMenu()
{
    // GROOVE: the song's feel (notes on some lines a little late as it plays), then how much
    juce::Component::SafePointer<PaulaEditor> safe (this);
    juce::StringArray kinds;
    const int now = proc.song.grooveKind;
    for (int k = 0; k < prv::kGrooveKinds; ++k) kinds.add (juce::String (k == now ? "> " : "  ") + prv::grooveName ((prv::GrooveKind) k));
    choice.show (*this, "GROOVE (THE SONG'S FEEL)", kinds, [safe] (int k)
    {
        if (safe == nullptr || k < 0) return;
        if (k == 0) { safe->setGroove (0, safe->proc.song.grooveAmount); return; }
        juce::StringArray amounts;
        for (int a : { 25, 50, 66, 75, 100 }) amounts.add (juce::String (a) + "%" + (a == safe->proc.song.grooveAmount ? " (NOW)" : ""));
        safe->choice.show (*safe, juce::String ("HOW MUCH ") + prv::grooveName ((prv::GrooveKind) k) + "?", amounts, [safe, k] (int a)
        {
            static const int pct[] = { 25, 50, 66, 75, 100 };
            if (safe != nullptr && a >= 0 && a < 5) safe->setGroove (k, pct[a]);
        });
    });
}

void PaulaEditor::setGroove (int kind, int amount)
{
    proc.history.settings ("GROOVE");
    proc.withLock ([&] { proc.song.grooveKind = (uint8_t) juce::jlimit (0, prv::kGrooveKinds - 1, kind); proc.song.grooveAmount = juce::jlimit (0, 100, amount); });
    refreshAll();
    setStatus (kind == 0 ? juce::String ("GROOVE OFF: EVERY NOTE ON ITS LINE")
                         : "GROOVE: " + juce::String (prv::grooveName ((prv::GrooveKind) kind)) + " " + juce::String (amount) + "% (AS IT PLAYS; THE PATTERN ISN'T CHANGED)");
}

void PaulaEditor::setSeparation (float v)
{
    sepSlider.setValue (juce::jlimit (0.0f, 1.0f, v));
    applyOptions();
    setStatus ("STEREO SEPARATION " + juce::String (juce::roundToInt (sepSlider.value * 100.0f)) + "%: HOW FAR TRACKS 1, 4, 5, 8 SIT LEFT AND 2, 3, 6, 7 RIGHT");
}

void PaulaEditor::setFollowInstrument (bool on)
{
    prv::ui::PatternEditor::setFollowInstrument (on);
    setStatus (on ? "FOLLOW INSTRUMENT: CLICKING A NOTE PICKS ITS INSTRUMENT IN THE LIST"
                  : "FOLLOW INSTRUMENT OFF: CLICKING A NOTE LEAVES THE INSTRUMENT AS IT IS");
}

void PaulaEditor::setNoteOffs (bool on)
{
    prv::ui::PatternEditor::setRecordNoteOffs (on);
    noteOffsBtn.setToggleState (on, juce::dontSendNotification);
    setStatus (on ? "NOTE-OFFS: LETTING GO OF A KEY WRITES OFF (NOT WHEN THE NEXT NOTE STARTS ON THE SAME LINE)"
                  : "NOTE-OFFS OFF: NO OFFS ARE RECORDED (LIVE, STEP ENTRY OR CAPTURE)");
}

int PaulaEditor::captureNow()
{
    // CAPTURE: the latest take of what was played (MIDI keyboard, laptop keys) into the pattern
    auto& st = proc.editorState;
    { PaulaProcessor::InputNote in; while (proc.popMidiNote (in)) {} }   // (anything still on its way)
    std::vector<prv::CapturedNote> log;
    for (const auto& n : proc.captureLog)
    {
        prv::CapturedNote c;
        c.timeMs = n.timeMs; c.note = n.note; c.velocity = n.velocity; c.on = n.on; c.instrument = n.instrument;
        c.playing = n.pos.playing; c.seqPos = n.pos.seqPos; c.block = n.pos.block; c.line = n.pos.line; c.speed = n.pos.speed; c.pass = n.pos.pass;
        log.push_back (c);
    }
    if (log.empty()) { setStatus ("CAPTURE: NOTHING PLAYED YET - PLAY SOMETHING, THEN CAPTURE"); return 0; }
    std::vector<bool> offs ((size_t) prv::kMaxInstruments, false);
    for (int i = 0; i < prv::kMaxInstruments; ++i) offs[(size_t) i] = proc.song.instruments[(size_t) i].isPlugin();
    const auto res = prv::placeCapture (proc.song, log, st.block, st.line, st.track, proc.song.tempo, proc.song.speed, offs,
                                        prv::ui::PatternEditor::detectChords(), prv::ui::PatternEditor::recordNoteOffs());
    int notes = 0;
    for (const auto& p : res.places) if (p.note != prv::kNoteOff) ++notes;
    if (res.places.empty())
    {
        setStatus ("CAPTURE: NO ROOM - " + juce::String (res.skipped) + " NOTES HAD NO FREE CELL");
        return 0;
    }
    proc.history.structure ("CAPTURE " + juce::String (notes) + " NOTES");
    proc.withLock ([&]
    {
        for (const auto& p : res.places)
        {
            auto& c = proc.song.blocks[(size_t) p.block].at (p.line, p.track);
            c.note = p.note; c.instr = p.instr;
            if (p.chordSlot != 0)
            {
                if (c.fx1Empty()) { c.cmd = prv::kCmdChord; c.param = p.chordSlot; }
                else if (c.fx2Empty()) { c.cmd2 = prv::kCmdChord; c.param2 = p.chordSlot; }
            }
            if (p.micro != 0)
            {
                const uint8_t m = prv::cmdLetter ('M');
                if (c.fx1Empty()) { c.cmd = m; c.param = p.micro; }
                else if (c.fx2Empty()) { c.cmd2 = m; c.param2 = p.micro; }
            }
        }
    });
    proc.captureLog.clear();
    st.block = res.firstBlock; st.line = res.firstLine;
    const int singles = notes - res.chords;
    juce::String what;
    if (singles > 0 || res.chords == 0) what << singles << (singles == 1 ? " NOTE" : " NOTES");
    if (res.chords > 0) what << (what.isEmpty() ? "" : " AND ") << res.chords << (res.chords == 1 ? " CHORD (#)" : " CHORDS (#)");
    setStatus ("CAPTURED " + what + " INTO BLOCK " + juce::String (res.firstBlock).paddedLeft ('0', 3)
               + " FROM LINE " + juce::String (res.firstLine).paddedLeft ('0', 3) + (res.played ? " (WHERE THEY WERE PLAYED)" : " (FROM THE CURSOR)")
               + (res.skipped > 0 ? " - " + juce::String (res.skipped) + " SKIPPED (NO FREE CELL)" : juce::String()));
    refreshAll();
    repaint();
    return notes;
}

void PaulaEditor::showMeterColourMenu (bool forSpectrum)
{
    // one colour for every level meter (pattern VU bars, L/R, mixer), another for the spectrum
    namespace meters = prv::ui::meters;
    juce::Component::SafePointer<PaulaEditor> safe (this);
    choice.show (*this, forSpectrum ? "SPECTRUM COLOUR" : "VU METER COLOUR", meters::names (forSpectrum), [safe, forSpectrum] (int a)
    {
        if (safe == nullptr || a < 0) return;
        if (forSpectrum) meters::setSpectrumChoice (a); else meters::setVuChoice (a);
        safe->setStatus ((forSpectrum ? "SPECTRUM: " : "VU METERS: ") + meters::names (forSpectrum)[a]
                         + (forSpectrum ? " - RIGHT-CLICK THE METERS FOR THEIR COLOUR" : " - RIGHT-CLICK THE SPECTRUM FOR ITS COLOUR"));
        safe->repaint();
    });
}

void PaulaEditor::toggleHelp()
{
    auto& st = proc.editorState;
    if (st.view == View::Help) { setView (helpReturn == View::Help ? View::Pattern : helpReturn); return; }
    helpReturn = st.view == View::Browser ? View::Pattern : st.view;
    setView (View::Help);
}

bool PaulaEditor::keyPressed (const juce::KeyPress& k)
{
    // F2: FILL (held - the timer watches it); Shift+F2 latches it
    if (k.getKeyCode() == juce::KeyPress::F2Key)
    {
        if (k.getModifiers().isShiftDown()) setFillLatch (! fillLatched);
        return true;
    }
    // Cmd+K: the command palette
    if (k.getModifiers().isCommandDown() && ! k.getModifiers().isShiftDown() && (k.getKeyCode() == 'K' || k.getKeyCode() == 'k'))
    {
        showPalette();
        return true;
    }
    // HELP: F1 or Cmd+/ (Cmd+?)
    if (k.getKeyCode() == juce::KeyPress::F1Key
        || (k.getModifiers().isCommandDown() && (k.getKeyCode() == '/' || k.getKeyCode() == '?' || k.getTextCharacter() == '/' || k.getTextCharacter() == '?')))
    {
        toggleHelp();
        return true;
    }
    // [ ] octave down / up on every page that plays notes (and with a plug-in's window in front);
    // the pattern and the sample browser do their own, HELP's search keeps its typing
    {
        const auto mm = k.getModifiers();
        const auto tc = k.getTextCharacter();
        const auto v = proc.editorState.view;
        if (! mm.isCommandDown() && ! mm.isCtrlDown() && ! mm.isAltDown() && (tc == '[' || tc == ']')
            && v != View::Pattern && v != View::Browser && v != View::Help)
        {
            auto& st = proc.editorState;
            st.octave = juce::jlimit (0, 6, st.octave + (tc == ']' ? 1 : -1));
            octBox.setValue (st.octave);
            setStatus ("OCTAVE " + juce::String (st.octave) + " ([ DOWN, ] UP)");
            return true;
        }
    }
    // Space on every page: the song / block plays or stops (and any sample preview stops);
    // the pattern, HELP's search, the sample editor and CHORDS do their own first
    {
        const auto mm = k.getModifiers();
        const auto v = proc.editorState.view;
        const bool space = k.getKeyCode() == juce::KeyPress::spaceKey, ret = k.getKeyCode() == juce::KeyPress::returnKey;
        if ((space || ret) && ! mm.isCommandDown() && ! mm.isCtrlDown() && ! mm.isAltDown()
            && v != View::Pattern && v != View::Help)
        {
            // Space: play and record (EDIT on) / stop (EDIT off); Return: play / stop only
            const bool previewOnly = ! proc.player.isPlaying() && proc.previewPos.load() >= 0;
            sampleEditor.stop();
            proc.stopPreview();
            if (! previewOnly) pattern.togglePlayStop (mm.isShiftDown(), space);
            refreshAll();
            setStatus (proc.player.isPlaying() ? (space ? "PLAYING, RECORDING (SPACE STOPS IT)" : "PLAYING (RETURN STOPS IT)") : "STOPPED");
            return true;
        }
    }
    // window-wide shortcuts (pages pass on keys they don't use)
    const auto m = k.getModifiers();
    if (m.isCommandDown())
    {
        const auto ch = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode());
        if (ch == 's') { if (m.isShiftDown()) saveSongAs(); else saveSong(); return true; }
        if (ch == 'o' && ! m.isShiftDown()) { loadSong(); return true; }
        if (ch == 'z') { if (m.isShiftDown()) redoBtn.onClick(); else undoBtn.onClick(); return true; }
        if (ch == 'y') { redoBtn.onClick(); return true; }
        if (ch == '2') { mixerBtn.onClick(); return true; }
        if (ch == '1') { setView (View::Pattern); return true; }
       #if JUCE_MAC
        const bool fullScreenKeys = m.isCtrlDown();   // (Ctrl+Cmd+F stays full screen)
       #else
        const bool fullScreenKeys = false;
       #endif
        if (ch == 'f' && ! fullScreenKeys)
        {
            auto& st = proc.editorState;
            st.follow = ! st.follow;
            followBtn.setToggleState (st.follow, juce::dontSendNotification);
            setStatus (st.follow ? "FOLLOW ON - THE VIEW MOVES WITH PLAYBACK (CMD+F)" : "FOLLOW OFF (CMD+F)");
            refreshAll();
            return true;
        }
        if (ch == '3') { if (proc.editorState.view == View::Chords) chords.onClose(); else openChords(); return true; }
        if (ch == '4') { toggleSample(); return true; }
        if (ch == '5') { radioBtn.onClick(); return true; }   // INTERNET RADIO (again to go back)
        if (ch == '6') { openBreak(); return true; }          // the BREAK page (again to go back)
        if (ch == '7') { openLive(); return true; }           // LIVE MODE (again to go back)
        if (ch == '8') { openVisuals(); return true; }        // VISUALS (again to go back)
        if (ch == 'r' && ! m.isShiftDown()) { captureNow(); return true; }
    }
    // keys that land on the window (e.g. after clicking a button) go to the active view
    switch (proc.editorState.view)
    {
        case View::SampleEdit: return sampleEditor.keyPressed (k);
        case View::Browser:    return browser.keyPressed (k);
        case View::Engine:     return enginePage.keyPressed (k);
        case View::Params:     return params.keyPressed (k);
        case View::Synth:      return synthPage.keyPressed (k);
        case View::MidiOut:    return midiPage.keyPressed (k);
        case View::Arrange:    return arrange.keyPressed (k);
        case View::Mixer:      return mixer.keyPressed (k);
        case View::Help:       return help.keyPressed (k);
        case View::Chords:     return chords.keyPressed (k);
        case View::Sample:     return samplePage.keyPressed (k);
        case View::CvOut:      return cvPage.keyPressed (k);
        case View::CvSources:  return cvSources.keyPressed (k);
        case View::Radio:      return radioPage.keyPressed (k);
        case View::Break:      return breakPage.keyPressed (k) || pattern.keyPressed (k);
        case View::Live:       return livePage.keyPressed (k);
        case View::Visuals:    return visualsPage.keyPressed (k);
        default:               return pattern.keyPressed (k);
    }
}

// ---------------------------------------------------------------------------
void PaulaEditor::newSong()
{
    juce::Component::SafePointer<PaulaEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New song",
        "Clear the current song? Unsaved changes will be lost.", "Clear", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe] (int r)
        {
            if (r != 1 || safe == nullptr) return;
            prv::Song s;
            // keep the instruments so a fresh tune can start straight away
            s.instruments = safe->proc.song.instruments;
            s.chordsSaved = false;   // (the global chord set)
            safe->proc.replaceSong (std::move (s));
            auto& st = safe->proc.editorState;
            st.block = 0; st.line = 0; st.seqPos = 0; st.track = 0;
            st.currentFile = {};
            safe->setStatus ("NEW SONG - INSTRUMENTS KEPT (CLEAR THEM IN THE INSTRUMENT PANEL)");
            safe->refreshAll();
            safe->repaint();
        }));
}

juce::String PaulaEditor::songNameFor (const juce::File& f)
{
    return f.getFileNameWithoutExtension().toUpperCase().substring (0, 40);
}

void PaulaEditor::loadSong()
{
    auto& st = proc.editorState;
    juce::File start = prv::startFolder (st.lastFolder, "songFolder");
    chooser = std::make_unique<juce::FileChooser> ("Load a song or module", start, "*.prv;*.mod;*.med;*.mmd0;*.mmd1;*");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto file = fc.getResult();
        if (! file.existsAsFile()) return;
        safe->loadSongFrom (file);
    });
}

bool PaulaEditor::loadSongFrom (const juce::File& file)
{
    waitForSave();
    prv::Diagnostics::get().activity ("LOADING " + file.getFileName().toUpperCase());
    prv::rememberFolder (proc.editorState.lastFolder, "songFolder", file.getParentDirectory());
    prv::Song s;
    juce::String err;
    juce::StringArray notes;
    if (! prv::loadSongFile (file, s, err, notes))
    {
        // (a shared folder may still be bringing it over from the other computer)
        const auto shared = sharedFolder();
        const bool syncing = shared != juce::File() && file.isAChildOf (shared);
        prv::Diagnostics::get().log ("LOAD FAILED: " + file.getFullPathName() + ": " + err);
        setStatus ("ERROR: " + err + (syncing ? " - IF IT WAS JUST SAVED ON THE OTHER COMPUTER, IT MAY STILL BE SYNCING: TRY AGAIN IN A MOMENT" : juce::String()));
        return false;
    }
    if (s.chordsLegacy || s.chordsSaved) notes.add (proc.chordReport (s));
    if (file.hasFileExtension ("prv")) s.name = songNameFor (file).toStdString();   // (a MOD keeps its own name)
    proc.replaceSong (std::move (s));
    auto& st2 = proc.editorState;
    st2.block = proc.song.safeBlock (proc.song.sequence[0]); st2.line = 0; st2.seqPos = 0; st2.track = 0;
    st2.currentFile = file.hasFileExtension ("prv") ? file.getFullPathName() : juce::String();
    knownFileTime = file.getLastModificationTime();
    prv::Diagnostics::get().log ("LOADED " + file.getFullPathName() + " (" + juce::String (file.getSize() / 1048576.0, 1) + " MB)");
    setStatus ("LOADED " + file.getFileName() + (notes.isEmpty() ? juce::String() : "  -  " + notes.joinIntoString ("  ")));
    refreshAll();
    repaint();
    return true;
}

void PaulaEditor::startAutosave()
{
    autosave = std::make_unique<prv::Autosave> (proc);
    if (! autosave->startSession()) return;
    // the last session didn't end cleanly: offer its autosave back
    const auto when = autosave->savedAt();
    const auto from = prv::Autosave::savedFrom();
    const juce::String name = from.isNotEmpty() ? juce::File (from).getFileNameWithoutExtension().toUpperCase() : juce::String ("AN UNSAVED SONG");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    juce::Timer::callAfterDelay (500, [safe, when, name]
    {
        if (safe == nullptr) return;
        safe->choice.show (*safe, "PAULA'S REVENGE DIDN'T CLOSE PROPERLY LAST TIME",
                           { "RECOVER " + name + " (AUTOSAVED " + when.formatted ("%H:%M").toUpperCase() + ", " + when.formatted ("%d %b").toUpperCase() + ")",
                             "START AS NORMAL (THE AUTOSAVE IS KEPT UNTIL THE NEXT ONE)" },
                           [safe] (int a) { if (safe != nullptr && a == 0) safe->recoverAutosave(); });
    });
}

void PaulaEditor::endAutosave()
{
    if (autosave != nullptr) autosave->waitForIt();
    prv::Autosave::endSession();
}

bool PaulaEditor::recoverAutosave()
{
    const auto file = prv::Autosave::songFile();
    if (! file.existsAsFile()) { setStatus ("NO AUTOSAVE TO RECOVER"); return false; }
    const auto from = prv::Autosave::savedFrom();
    const auto when = file.getLastModificationTime();
    if (! loadSongFrom (file)) return false;
    // back as the song it was: its own file (if it had one), with changes not saved there yet
    auto& st = proc.editorState;
    st.currentFile = from;
    proc.withLock ([&] { proc.song.name = from.isNotEmpty() ? songNameFor (juce::File (from)).toStdString() : std::string(); });
    proc.history.markChanged();
    if (autosave != nullptr) autosave->tick (true);   // (and that's the new autosave)
    setStatus ("RECOVERED THE AUTOSAVE FROM " + when.formatted ("%H:%M").toUpperCase() + " - SAVE IT TO KEEP IT");
    refreshAll();
    return true;
}

bool PaulaEditor::writeSongTo (const juce::File& file, bool checkChanged)
{
    // changed by another computer (a shared folder) since this app last read or wrote it?
    const bool ours = proc.editorState.currentFile == file.getFullPathName();
    if (checkChanged && file.existsAsFile() && (! ours || knownFileTime == juce::Time()
                                                 || file.getLastModificationTime() > knownFileTime + juce::RelativeTime::seconds (2)))
    {
        if (ours)
        {
            juce::Component::SafePointer<PaulaEditor> safe (this);
            const auto copy = file.getParentDirectory().getNonexistentChildFile (file.getFileNameWithoutExtension() + " (" + juce::SystemStats::getComputerName() + ")", ".prv", false);
            choice.show (*this, file.getFileName().toUpperCase() + " WAS CHANGED SINCE YOU OPENED IT (ON ANOTHER COMPUTER?)",
                         { "SAVE A COPY: " + copy.getFileName().toUpperCase(), "REPLACE IT WITH THIS ONE" },
                         [safe, file, copy] (int a)
                         {
                             if (safe == nullptr || a < 0) return;
                             safe->startSaving (a == 0 ? copy : file);
                         });
            return true;
        }
    }
    startSaving (file);
    return true;
}

void PaulaEditor::startSaving (const juce::File& file)
{
    waitForSave();
    // the song is called what its file is called
    proc.withLock ([&] { proc.song.name = songNameFor (file).toStdString(); });
    proc.capturePluginStates();
    // a copy, taken here without the lock: this is the only thread that changes the song, and
    // the music keeps playing; packing and writing it happen in the background
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto copy = std::make_shared<prv::Song> (proc.song);
    const auto id = proc.history.changeId();
    saving = true;
    lastSaveOk = false;
    prv::Diagnostics::get().activity ("SAVING " + file.getFileName().toUpperCase());
    setStatus ("SAVING " + file.getFileName().toUpperCase() + "...");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    saveThread = std::thread ([copy, file, safe, id, t0]
    {
        const auto data = prv::writeSong (*copy);
        // written next to it, then put in its place: never half a song (and a synced folder
        // never sends half of one to the other computer)
        const auto tmp = file.getSiblingFile ("." + file.getFileNameWithoutExtension() + ".saving");
        bool ok = tmp.replaceWithData (data.getData(), data.getSize());
        if (ok) ok = tmp.moveFileTo (file);
        if (! ok) tmp.deleteFile();
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        if (auto* ed = safe.getComponent())   // (the window joins this thread before it goes)
        {
            std::lock_guard<std::mutex> l (ed->saveMutex);
            ed->pendingSave = SaveResult { ok, file, id, ms, data.getSize() };
        }
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->finishPendingSave(); });
    });
}

void PaulaEditor::finishPendingSave()
{
    std::optional<SaveResult> r;
    {
        std::lock_guard<std::mutex> l (saveMutex);
        r.swap (pendingSave);
    }
    if (r) saveFinished (r->ok, r->file, r->id, r->ms, r->bytes);
}

void PaulaEditor::waitForSave()
{
    if (saveThread.joinable()) saveThread.join();
}

void PaulaEditor::saveFinished (bool ok, const juce::File& file, uint64_t changeId, double ms, size_t bytes)
{
    saving = false;
    lastSaveOk = ok;
    prv::Diagnostics::get().log ((ok ? "SAVED " : "SAVE FAILED: ") + file.getFullPathName() + " (" + juce::String (bytes / 1048576.0, 1) + " MB, "
                                  + juce::String (juce::roundToInt (ms)) + " MS)");
    if (! ok)
    {
        setStatus ("ERROR: COULDN'T WRITE " + file.getFullPathName());
        return;
    }
    auto& st = proc.editorState;
    st.currentFile = file.getFullPathName();
    knownFileTime = file.getLastModificationTime();
    prv::rememberFolder (st.lastFolder, "songFolder", file.getParentDirectory());
    proc.history.markSavedAt (changeId);
    if (proc.song.name != songNameFor (file).toStdString()) proc.withLock ([&] { proc.song.name = songNameFor (file).toStdString(); });
    const auto shared = sharedSongsFolder();
    setStatus ("SAVED " + file.getFileName() + (shared != juce::File() && file.isAChildOf (shared) ? " IN THE SHARED FOLDER" : " IN " + file.getParentDirectory().getFullPathName()));
    refreshAll();
    repaint();
}

bool PaulaEditor::saveSongDirect()
{
    const auto& path = proc.editorState.currentFile;
    if (path.isEmpty() || ! juce::File::isAbsolutePath (path)) return false;
    const juce::File file (path);
    if (! file.getParentDirectory().isDirectory()) return false;
    return writeSongTo (file);
}

void PaulaEditor::saveSong()
{
    // straight to the song's file; the first save asks for a name
    if (! saveSongDirect()) saveSongAs();
}

// ---------------------------------------------------------------------------- SHARED FOLDER
juce::File PaulaEditor::sharedFolder()
{
    const auto p = prv::prefs().getValue ("sharedFolder");
    return p.isNotEmpty() && juce::File::isAbsolutePath (p) ? juce::File (p) : juce::File();
}

juce::File PaulaEditor::sharedSongsFolder()
{
    const auto f = sharedFolder();
    return f == juce::File() ? juce::File() : f.getChildFile ("Paula's Revenge").getChildFile ("Songs");
}

juce::Array<juce::File> PaulaEditor::sharedFolderCandidates()
{
    juce::Array<juce::File> out;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto add = [&] (const juce::File& f) { if (f.isDirectory() && ! out.contains (f)) out.add (f); };
    // Dropbox says where it is in its info.json
    for (auto info : { home.getChildFile (".dropbox/info.json"),
                       juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Dropbox/info.json"),
                       juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getSiblingFile ("Local").getChildFile ("Dropbox/info.json") })
    {
        const auto v = juce::JSON::parse (info);
        for (auto kind : { "personal", "business" })
            if (v[kind].isObject()) add (juce::File (v[kind]["path"].toString()));
    }
    add (home.getChildFile ("Dropbox"));
    // macOS File Provider folders (Dropbox, OneDrive, Google Drive) and iCloud Drive
    const auto cloud = home.getChildFile ("Library/CloudStorage");
    if (cloud.isDirectory())
        for (const auto& f : cloud.findChildFiles (juce::File::findDirectories, false))
        {
            const auto n = f.getFileName();
            if (n.startsWith ("Dropbox") || n.startsWith ("OneDrive")) add (f);
            if (n.startsWith ("GoogleDrive")) add (f.getChildFile ("My Drive"));
        }
    add (home.getChildFile ("Library/Mobile Documents/com~apple~CloudDocs"));
    // Windows
    for (const auto& f : home.findChildFiles (juce::File::findDirectories, false, "OneDrive*")) add (f);
    add (home.getChildFile ("iCloudDrive"));
    return out;
}

bool PaulaEditor::setSharedFolder (const juce::File& f)
{
    if (f == juce::File())
    {
        prv::prefs().removeValue ("sharedFolder");
        prv::prefs().saveIfNeeded();
        setStatus ("NO SHARED FOLDER: SONGS SAVE WHERE YOU PUT THEM");
        return true;
    }
    if (! f.isDirectory()) { setStatus ("ERROR: " + f.getFullPathName() + " ISN'T A FOLDER"); return false; }
    prv::prefs().setValue ("sharedFolder", f.getFullPathName());
    prv::prefs().saveIfNeeded();
    const auto songs = sharedSongsFolder();
    songs.createDirectory();
    prv::Diagnostics::get().log ("SHARED FOLDER: " + songs.getFullPathName());
    setStatus ("SHARED FOLDER: " + songs.getFullPathName() + " - SET THE SAME ONE ON YOUR OTHER COMPUTER");
    return songs.isDirectory();
}

void PaulaEditor::chooseSharedFolder()
{
    const auto found = sharedFolderCandidates();
    juce::StringArray answers;
    auto nameFor = [] (const juce::File& f)
    {
        const auto p = f.getFullPathName();
        if (p.contains ("com~apple~CloudDocs") || p.endsWithIgnoreCase ("iCloudDrive")) return juce::String ("ICLOUD DRIVE");
        if (p.containsIgnoreCase ("Dropbox")) return "DROPBOX (" + f.getFileName().toUpperCase() + ")";
        if (p.containsIgnoreCase ("OneDrive")) return "ONEDRIVE (" + f.getFileName().toUpperCase() + ")";
        if (p.containsIgnoreCase ("GoogleDrive")) return juce::String ("GOOGLE DRIVE");
        return f.getFileName().toUpperCase();
    };
    for (auto& f : found) answers.add ("USE " + nameFor (f));
    answers.add ("CHOOSE ANOTHER FOLDER...");
    if (sharedFolder() != juce::File()) answers.add ("STOP USING A SHARED FOLDER");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    const auto cur = sharedFolder();
    choice.show (*this, cur == juce::File() ? juce::String ("SHARE SONGS BETWEEN COMPUTERS: PICK THE SYNCED FOLDER")
                                            : "SHARED FOLDER NOW: " + cur.getFullPathName().toUpperCase(),
                 answers, [safe, found] (int a)
    {
        if (safe == nullptr || a < 0) return;
        if (a < found.size()) { safe->setSharedFolder (found[a]); return; }
        if (a == found.size())
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("The synced folder (Dropbox, iCloud Drive, OneDrive...)",
                                                                 juce::File::getSpecialLocation (juce::File::userHomeDirectory));
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult().isDirectory()) safe->setSharedFolder (fc.getResult());
            });
            return;
        }
        safe->setSharedFolder ({});
    });
}

void PaulaEditor::openShared()
{
    const auto songs = sharedSongsFolder();
    if (songs == juce::File()) { chooseSharedFolder(); return; }
    songs.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Open a shared song", songs, "*.prv");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().existsAsFile()) safe->loadSongFrom (fc.getResult());
    });
}

void PaulaEditor::saveToShared()
{
    const auto songs = sharedSongsFolder();
    if (songs == juce::File()) { chooseSharedFolder(); return; }
    songs.createDirectory();
    const juce::File cur (juce::File::isAbsolutePath (proc.editorState.currentFile) ? proc.editorState.currentFile : juce::String());
    if (cur != juce::File() && cur.isAChildOf (songs)) { writeSongTo (cur); return; }
    const juce::String base = cur != juce::File() ? cur.getFileNameWithoutExtension()
                            : juce::File::createLegalFileName (juce::String (proc.song.name).toLowerCase());
    auto target = songs.getChildFile ((base.isNotEmpty() ? base : juce::String ("untitled")) + ".prv");
    if (target.existsAsFile())
    {
        juce::Component::SafePointer<PaulaEditor> safe (this);
        const auto copy = songs.getNonexistentChildFile (target.getFileNameWithoutExtension(), ".prv", false);
        choice.show (*this, target.getFileName().toUpperCase() + " IS ALREADY IN THE SHARED FOLDER",
                     { "SAVE AS " + copy.getFileName().toUpperCase(), "REPLACE IT" },
                     [safe, target, copy] (int a) { if (safe != nullptr && a >= 0) safe->writeSongTo (a == 0 ? copy : target, false); });
        return;
    }
    writeSongTo (target, false);
}

// ---------------------------------------------------------------------------- STEMS
void PaulaEditor::splitStems()
{
    const int i = proc.editorState.instrument;
    const auto& in = proc.song.instruments[(size_t) i];
    if (! in.hasData()) { setStatus ("THIS INSTRUMENT HAS NO SAMPLE TO SPLIT"); return; }
    if (stemJob.running()) { setStatus ("STEMS ARE ALREADY BEING MADE - WAIT, OR CANCEL THEM"); return; }
    if (! prv::stems::onnxAvailable()) { setStatus ("STEMS AREN'T BUILT INTO THIS VERSION"); return; }
    const double secs = (double) in.data.size() / std::max (1.0, in.rate);
    auto dl = [] (prv::stems::Quality q)
    {
        const auto b = prv::StemsJob::downloadSize (q);
        return b > 0 ? " - DOWNLOADS " + juce::String (juce::roundToInt (b / 1048576.0)) + " MB ONCE" : juce::String();
    };
    juce::Component::SafePointer<PaulaEditor> safe (this);
    choice.show (*this, "SPLIT " + juce::String (in.name).toUpperCase() + " (" + juce::String (secs, 1) + " S) INTO DRUMS, BASS, OTHER, VOCALS",
                 { "BEST: FOUR FINE-TUNED MODELS, SLOWER" + dl (prv::stems::Quality::Best),
                   "FAST: ONE MODEL, FOR A QUICK LISTEN" + dl (prv::stems::Quality::Fast) },
                 [safe] (int a) { if (safe != nullptr && a >= 0) safe->startStems (a == 0 ? prv::stems::Quality::Best : prv::stems::Quality::Fast); });
}

bool PaulaEditor::startStems (prv::stems::Quality q)
{
    const int i = proc.editorState.instrument;
    const auto& in = proc.song.instruments[(size_t) i];
    if (! in.hasData() || stemJob.running()) return false;
    std::vector<float> L (in.data.size()), R;
    for (size_t k = 0; k < L.size(); ++k) L[k] = in.data[k] / 32768.0f;
    if (in.isStereo()) { R.resize (in.dataR.size()); for (size_t k = 0; k < R.size(); ++k) R[k] = in.dataR[k] / 32768.0f; }
    const juce::String name (in.name);
    juce::Component::SafePointer<PaulaEditor> safe (this);
    stemsPanel.title = [q, name] { return "STEMS (" + juce::String (q == prv::stems::Quality::Best ? "BEST" : "FAST") + "): " + name.toUpperCase(); };
    stemsPanel.stage = [this] { return stemJob.stage(); };
    stemsPanel.progress = [this] { return stemJob.progress(); };
    stemsPanel.left = [this]
    {
        const double s = stemJob.secondsLeft();
        if (s < 0) return juce::String ("WORKING IT OUT...");
        if (s < 10) return juce::String ("A FEW SECONDS LEFT");
        return s < 90 ? "ABOUT " + juce::String (juce::roundToInt (s)) + " SECONDS LEFT" : "ABOUT " + juce::String (juce::roundToInt (s / 60)) + " MINUTES LEFT";
    };
    stemsPanel.cancel.onClick = [this] { stemJob.cancel(); setStatus ("CANCELLING THE STEMS..."); };
    prv::Diagnostics::get().activity ("MAKING STEMS");
    const bool ok = stemJob.start (std::move (L), std::move (R), in.rate, q, [safe, i] (bool done, juce::String err, std::shared_ptr<prv::stems::StemResult> r)
    {
        if (safe != nullptr) safe->stemsFinished (done, err, r, i);
    });
    stemsPanel.setVisible (ok);
    stemsPanel.toFront (false);
    resized();
    setStatus ("SPLITTING INTO STEMS - THE MUSIC AND THE PAGES CARRY ON MEANWHILE");
    return ok;
}

void PaulaEditor::stemsFinished (bool ok, const juce::String& error, std::shared_ptr<prv::stems::StemResult> r, int from)
{
    stemsPanel.setVisible (false);
    if (! ok || r == nullptr) { setStatus (error == "CANCELLED" ? juce::String ("STEMS CANCELLED") : "STEMS: " + error); return; }
    // four new instruments, in the first empty slots
    const auto base = juce::String (proc.song.instruments[(size_t) from].name).toUpperCase().substring (0, 15).trimEnd();
    juce::Array<int> made;
    for (int s = 0; s < prv::stems::kSources; ++s)
    {
        int idx = -1;
        for (int k = 0; k < prv::kMaxInstruments && idx < 0; ++k)
        {
            const auto& c = proc.song.instruments[(size_t) k];
            if (! c.hasData() && ! c.isPlugin()) idx = k;
        }
        if (idx < 0) break;
        prv::TakeOptions o;
        o.channels = prv::RecChannels::Stereo;
        o.trimSilence = false;
        o.normalise = false;
        o.name = (base + " " + prv::stems::sourceName (s)).trim().toStdString();
        prv::Instrument in;
        // (a stem that's silence still becomes an instrument, so the four line up)
        auto& Ls = r->left[s];
        auto& Rs = r->right[s];
        if (! prv::takeToInstrument (Ls, Rs, r->rate, o, in)) { in = prv::Instrument {}; in.name = o.name; }
        proc.history.instrument (idx, "STEMS: " + juce::String (prv::stems::sourceName (s)));
        proc.withLock ([&] { proc.player.jamAllOff(); std::swap (proc.song.instruments[(size_t) idx], in); });
        made.add (idx);
    }
    if (made.isEmpty()) { setStatus ("NO EMPTY INSTRUMENTS FOR THE STEMS - CLEAR FOUR, THEN SPLIT AGAIN"); return; }
    proc.editorState.instrument = made[0];
    juce::StringArray where;
    for (int k = 0; k < made.size(); ++k) where.add (juce::String::toHexString (made[k] + 1).toUpperCase().paddedLeft ('0', 2) + " " + prv::stems::sourceName (k));
    setStatus ("STEMS: " + where.joinIntoString (", ") + (made.size() < prv::stems::kSources ? " (NO ROOM FOR THE REST)" : juce::String()));
    refreshAll();
    sampleEditor.instrumentSelected();
}

void PaulaEditor::StemsPanel::paint (juce::Graphics& g)
{
    const float px = prv::ui::Theme::px;
    const auto& pal = prv::ui::Theme::palFor (*this);
    const auto& sk = prv::ui::Theme::skin;
    auto r = getLocalBounds().toFloat();
    g.setColour (pal.panel);
    g.fillRect (r);
    prv::ui::Theme::bevel (g, r, true, std::max (1.0f, px), pal);
    r = r.reduced (px * 6);
    const float lh = px * 12;
    auto line = [&] (const juce::String& t, juce::Colour c)
    {
        const int n = std::max (4, (int) (r.getWidth() / (px * prv::ui::PixelFont::kCellW)));
        prv::ui::PixelFont::drawLeft (g, t.length() > n ? t.substring (0, n - 2) + ".." : t, r.removeFromTop (lh), px, c);
    };
    line (title ? title() : juce::String(), pal.ink);
    line (stage ? stage() : juce::String(), pal.ink);
    r.removeFromTop (px * 3);
    auto bar = r.removeFromTop (px * 12);
    g.setColour (sk.bg);
    g.fillRect (bar);
    const double p = progress ? juce::jlimit (0.0, 1.0, progress()) : 0.0;
    g.setColour (sk.track[2].withAlpha (0.55f));
    g.fillRect (bar.reduced (px).withWidth ((bar.getWidth() - px * 2) * (float) p));
    prv::ui::Theme::bevel (g, bar, false, std::max (1.0f, px * 0.5f), pal);
    prv::ui::PixelFont::drawCentred (g, juce::String (juce::roundToInt (p * 100)) + "%", bar, px, sk.text);
    r.removeFromTop (px * 3);
    line (left ? left() : juce::String(), pal.ink);
}

void PaulaEditor::StemsPanel::resized()
{
    const float px = prv::ui::Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 6));
    cancel.setBounds (r.removeFromBottom ((int) (px * 11)).removeFromRight ((int) (px * 6 * 6 + px * 4)));
}

void PaulaEditor::saveDiagnostics()
{
    const auto f = prv::Diagnostics::get().saveReport();
    f.revealToUser();
    setStatus ("DIAGNOSTICS REPORT ON THE DESKTOP: " + f.getFileName().toUpperCase() + " - SEND IT OVER");
}

void PaulaEditor::saveSongAs()
{
    auto& st = proc.editorState;
    const juce::File current (juce::File::isAbsolutePath (st.currentFile) ? st.currentFile : juce::String());
    juce::File start = current != juce::File() ? current
                     : sharedSongsFolder().isDirectory() ? sharedSongsFolder().getChildFile (juce::File::createLegalFileName (juce::String (proc.song.name).toLowerCase()) + ".prv")
                     : prv::startFolder (st.lastFolder, "songFolder")
                           .getChildFile (juce::File::createLegalFileName (juce::String (proc.song.name).toLowerCase()) + ".prv");
    chooser = std::make_unique<juce::FileChooser> ("Save song as", start, "*.prv");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension ("prv")) file = file.withFileExtension ("prv");
        safe->writeSongTo (file, false);   // (the window has already asked about replacing it)
    });
}

void PaulaEditor::exportMod()
{
    auto& st = proc.editorState;
    juce::File start = prv::startFolder (st.lastFolder, "songFolder")
                           .getChildFile (juce::File::createLegalFileName (juce::String (proc.song.name).toLowerCase()) + ".mod");
    chooser = std::make_unique<juce::FileChooser> ("Save as MOD", start, "*.mod");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension ("mod")) file = file.withFileExtension ("mod");
        auto& self = *safe;
        std::vector<uint8_t> bytes;
        prv::IOResult res;
        self.proc.withLock ([&] { res = prv::saveMOD (self.proc.song, bytes); });
        juce::StringArray notes;
        for (auto& w : res.warnings) notes.add (w);
        if (file.replaceWithData (bytes.data(), bytes.size()))
            self.setStatus ("SAVED " + file.getFileName() + (notes.isEmpty() ? juce::String() : "  -  " + notes.joinIntoString ("  ")));
        else self.setStatus ("ERROR: COULDN'T WRITE " + file.getFileName());
    });
}

void PaulaEditor::exportWav()
{
    juce::Component::SafePointer<PaulaEditor> safe (this);
    choice.show (*this, "RENDER WAV", { "SONG (ONE WAV FILE)", "STEMS (A WAV PER TRACK)" }, [safe] (int a)
    {
        if (safe != nullptr && a >= 0) safe->exportWav (a == 1);
    });
}

void PaulaEditor::exportWav (bool stems)
{
    auto& st = proc.editorState;
    juce::File start = prv::startFolder (st.lastFolder, "songFolder")
                           .getChildFile (juce::File::createLegalFileName (juce::String (proc.song.name).toLowerCase()) + ".wav");
    chooser = std::make_unique<juce::FileChooser> (stems ? "Render stems (a folder is made next to this name)" : "Render song to WAV", start, "*.wav");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, stems] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension ("wav")) file = file.withFileExtension ("wav");
        safe->renderTo (file, stems);
    });
}

void PaulaEditor::renderTo (const juce::File& file, bool stems)
{
    prv::Song copy;
    prv::PaulaOptions opts;
    proc.capturePluginStates();   // plug-ins render with their current settings
    proc.withLock ([&] { copy = proc.song; opts = proc.player.options; });
    juce::String err;
    juce::StringArray leftOut, cvFiles;
    const double rate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    if (prv::renderSongToWav (copy, opts, rate, file, err, proc.pluginFactory(), proc.synthFactory(), stems, &leftOut, &cvFiles))
    {
        juce::String m = stems ? "RENDERED STEMS INTO " + prv::stemFolderFor (file).getFileName() : "RENDERED " + file.getFileName();
        if (! cvFiles.isEmpty()) m << " + " << cvFiles.size() << " CV FILE" << (cvFiles.size() == 1 ? "" : "S") << " (CV OUT 3...)";
        if (! leftOut.isEmpty()) m << " - LEFT OUT (MIDI OUT, HARDWARE): " << leftOut.joinIntoString (", ");
        setStatus (m);
    }
    else setStatus ("ERROR: " + err);
}

// ---------------------------------------------------------------------------
// BOUNCE: the highlighted lines and tracks become a new sample instrument (the
// instruments that played them, and the pattern, stay as they are)
void PaulaEditor::askBounce()
{
    juce::Component::SafePointer<PaulaEditor> safe (this);
    choice.show (*this, "BOUNCE THE HIGHLIGHTED NOTES",
                 { "NO FX (DRY)", "INSTRUMENT FX ONLY (THE INSTRUMENTS' OWN INSERTS)", "MIXER FX ONLY (EQ, INSERTS, FADER, SENDS)", "ALL FX (INSTRUMENTS AND MIXER)",
                   "RESAMPLE: ALL FX, CHOPPED A SLICE A BEAT, IN TIME" }, [safe] (int a)
    {
        if (safe == nullptr || a < 0) return;
        if (a == 4) safe->resampleChop();
        else if (safe->bounceSelection (a)) safe->askAfterBounce (safe->proc.editorState.instrument);
    });
}

// RESAMPLE: what to do with what was just bounced - slice it, make it GRANULAR, crunch it, reverse it
void PaulaEditor::askAfterBounce (int slot)
{
    juce::Component::SafePointer<PaulaEditor> safe (this);
    choice.show (*this, "BOUNCED TO INSTRUMENT " + juce::String::toHexString (slot + 1).toUpperCase().paddedLeft ('0', 2) + " - AND THEN?",
                 { "KEEP IT AS IT IS", "SLICE IT ON THE BREAK PAGE (IT FINDS THE HITS)", "MAKE IT A GRANULAR (ITS GRAINS COME FROM IT)",
                   "CRUNCH: AMIGA 8-BIT", "CRUNCH: SP-1200 (GRITTY)", "CRUNCH: MPC60 (WARM)", "REVERSE IT", "STRETCH IT TO FIT A NUMBER OF LINES" }, [safe, slot] (int a)
    {
        if (safe != nullptr && a > 0) safe->afterBounce (slot, a);
    });
}

bool PaulaEditor::afterBounce (int slot, int what)
{
    if (slot < 0 || slot >= prv::kMaxInstruments || ! proc.song.instruments[(size_t) slot].hasData()) return false;
    auto& st = proc.editorState;
    st.instrument = slot;
    const juce::String num = juce::String::toHexString (slot + 1).toUpperCase().paddedLeft ('0', 2);
    if (what == 1)
    {
        openBreak();
        breakPage.sliceAuto();
        return true;
    }
    if (what == 2)
    {
        proc.history.instrument (slot, "MAKE IT GRANULAR");
        proc.withLock ([&]
        {
            auto& in = proc.song.instruments[(size_t) slot];
            auto data = std::move (in.data); auto dataR = std::move (in.dataR);
            const double rate = in.rate, nat = in.natRate;
            const auto name = in.name;
            prv::makeBuiltin (in, prv::kGrainKey);
            in.data = std::move (data); in.dataR = std::move (dataR); in.rate = rate; in.natRate = nat;
            in.name = ("GRAIN " + juce::String (name).fromFirstOccurrenceOf ("BOUNCE", false, false).trim()).substring (0, 22).toStdString();
            auto v = prv::readBuiltinState (in);
            v[prv::grain::Source] = 1; v[prv::grain::SampleSlot] = (float) (slot + 1);
            prv::writeBuiltinState (in, v);
        });
        proc.updateMixerEffects();
        setView (View::Engine);
        setStatus ("INSTRUMENT " + num + " IS NOW A GRANULAR PLAYING THE BOUNCE - PLAY IT, SCAN IT, FREEZE IT");
        refreshAll();
        return true;
    }
    if (what >= 3 && what <= 5)
    {
        const auto style = (prv::CrunchStyle) (what - 3);
        proc.history.instrument (slot, juce::String ("CRUNCH: ") + prv::crunchName (style));
        bool ok = false;
        proc.withLock ([&] { ok = prv::crunchInstrument (proc.song.instruments[(size_t) slot], style); });
        setStatus (ok ? "INSTRUMENT " + num + " CRUNCHED: " + prv::crunchName (style) + " (" + juce::String ((int) proc.song.instruments[(size_t) slot].rate) + " HZ) - SAME PITCH, OLD-SAMPLER GRIT"
                      : juce::String ("NOTHING TO CRUNCH"));
        refreshAll();
        return ok;
    }
    if (what == 6)
    {
        proc.history.instrument (slot, "REVERSE IT");
        proc.withLock ([&]
        {
            auto& in = proc.song.instruments[(size_t) slot];
            std::reverse (in.data.begin(), in.data.end());
            std::reverse (in.dataR.begin(), in.dataR.end());
        });
        setStatus ("INSTRUMENT " + num + " REVERSED");
        refreshAll();
        return true;
    }
    if (what == 7)
    {
        // the stretch, set up to fit the nearest whole bar (16 lines) - change LINES and APPLY
        const auto& in = proc.song.instruments[(size_t) slot];
        const double secs = (double) in.data.size() / std::max (1.0, in.rate);
        const double lineSecs = proc.song.speed * 2.5 / std::max (1.0, (double) proc.song.tempo);
        const int lines = juce::jlimit (1, 256, std::max (4, (int) std::lround (secs / lineSecs / 4.0) * 4));
        setView (View::SampleEdit);
        sampleEditor.openFit (lines);
        return true;
    }
    return false;
}

bool PaulaEditor::bounceSelection (int fx)
{
    fx = juce::jlimit (0, 3, fx);
    const bool instFx = fx == BounceInstrumentFx || fx == BounceAllFx, mixFx = fx == BounceMixerFx || fx == BounceAllFx;
    prv::BounceRange r;
    if (! pattern.selectionArea (r.block, r.line0, r.line1, r.track0, r.track1))
    {
        setStatus ("HIGHLIGHT SOME NOTES FIRST (DRAG OVER THEM)");
        return false;
    }
    r.withFx = mixFx;
    r.instrumentFx = instFx;
    // the first empty instrument slot
    int slot = -1;
    for (int i = 0; i < prv::kMaxInstruments && slot < 0; ++i)
    {
        const auto& in = proc.song.instruments[(size_t) i];
        if (! in.hasData() && ! in.isPlugin()) slot = i;
    }
    if (slot < 0) { setStatus ("CAN'T BOUNCE: ALL 63 INSTRUMENTS ARE IN USE"); return false; }

    prv::Song copy;
    prv::PaulaOptions opts;
    proc.capturePluginStates();
    proc.withLock ([&] { copy = proc.song; opts = proc.player.options; });
    prv::BounceResult res;
    std::string err;
    const double rate = 44100.0;
    if (! prv::bounceRange (copy, opts, rate, r, res, proc.synthFactory(), err, proc.pluginFactory()))
    {
        setStatus ("CAN'T BOUNCE: " + juce::String (err));
        return false;
    }

    prv::Instrument made;
    static const char* prefix[4] = { "BOUNCE T", "BOUNCE+IFX T", "BOUNCE+MFX T", "BOUNCE+FX T" };
    const juce::String name = juce::String (prefix[fx]) + juce::String (r.track0 + 1)
                            + (r.track1 > r.track0 ? "-" + juce::String (r.track1 + 1) : juce::String())
                            + " " + juce::String (r.line0).paddedLeft ('0', 3) + "-" + juce::String (r.line1).paddedLeft ('0', 3);
    made.name = name.substring (0, 22).toStdString();
    made.rate = rate;
    made.volume = 64;
    made.data.resize (res.left.size());
    made.dataR.resize (res.right.size());
    bool stereo = false;
    for (size_t k = 0; k < res.left.size(); ++k)
    {
        made.data[k]  = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (res.left[k] * 32767.0f));
        made.dataR[k] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (res.right[k] * 32767.0f));
        stereo = stereo || made.data[k] != made.dataR[k];
    }
    if (! stereo) made.dataR.clear();

    proc.history.instrument (slot, "BOUNCE " + name);
    proc.withLock ([&] { proc.song.instruments[(size_t) slot] = std::move (made); });
    proc.editorState.instrument = slot;
    juce::String m = "BOUNCED TO INSTRUMENT " + juce::String::toHexString (slot + 1).toUpperCase().paddedLeft ('0', 2)
                   + " (" + juce::String (res.left.size() / rate, 2) + " S" + (fx == BounceAllFx ? ", ALL FX" : fx == BounceMixerFx ? ", MIXER FX" : fx == BounceInstrumentFx ? ", INSTRUMENT FX" : ", DRY") + ") - C-3 PLAYS IT AS RECORDED";
    if (! res.leftOut.empty()) m << " - MIDI OUT ISN'T IN IT (HARDWARE)";
    setStatus (m);
    refreshAll();
    return true;
}

void PaulaEditor::setSkin (int id)
{
    auto& st = proc.editorState;
    st.skin = juce::jlimit (0, prv::ui::Theme::kSkins - 1, id);
    st.darkSkin = prv::ui::Theme::skinIsDark (st.skin);
    prv::ui::Theme::setSkinId ((prv::ui::Theme::SkinId) st.skin);
    refreshAll();
    resized();
    repaint();
    setStatus ("SKIN: " + prv::ui::Theme::skinName ((prv::ui::Theme::SkinId) st.skin) + " - RIGHT-CLICK FOR MORE");
}

// ---- SMARTER LAYOUT: the sidebar's panels fold to their titles
void PaulaEditor::TitleClicks::mouseDown (const juce::MouseEvent& e)
{
    if (ed == nullptr || e.mods.isPopupMenu()) return;
    const float titleH = prv::ui::PixelFont::textHeight (prv::ui::Theme::px) + prv::ui::Theme::px * 2;
    if (e.position.y > titleH) return;
    const int bit = e.eventComponent == &ed->sequence ? 1 : e.eventComponent == &ed->blockPanel ? 2 : e.eventComponent == &ed->instruments ? 4 : 0;
    if (bit == 0) return;
    juce::Component::SafePointer<PaulaEditor> safe (ed);
    juce::MessageManager::callAsync ([safe, bit] { if (safe != nullptr && ! safe->sidebarTabs()) safe->foldPanel (bit, true); });   // (after this click is done with the panel)
}

void PaulaEditor::foldPanel (int bit, bool folded)
{
    auto& st = proc.editorState;
    if (st.sidebarTabs) st.sidebarFold = folded ? st.sidebarFold : (7 & ~bit);   // TABS: this one open, the others folded
    else st.sidebarFold = folded ? (st.sidebarFold | bit) : (st.sidebarFold & ~bit);
    resized();
    repaint();
    const juce::String name = bit == 1 ? "SEQUENCE" : bit == 2 ? "BLOCK" : "INSTRUMENTS";
    setStatus (name + (folded ? " FOLDED - CLICK ITS TITLE TO OPEN IT (RIGHT-CLICK: TABS)" : " OPEN - CLICK ITS TITLE TO FOLD IT"));
}

void PaulaEditor::setSequenceShowAll (bool on)
{
    auto& st = proc.editorState;
    if (on == sequenceShowAll()) return;
    if (on) { foldBeforeShowAll = st.sidebarFold; st.sidebarFold = 6; }
    else st.sidebarFold = (foldBeforeShowAll & 6) == 6 || (foldBeforeShowAll & 1) != 0 ? (st.sidebarTabs ? 3 : 0) : foldBeforeShowAll;
    resized();
    repaint();
    setStatus (on ? "SEQUENCE: SHOW ALL - " + juce::String ((int) proc.song.sequence.size()) + " POSITIONS (CLICK SHOW ALL AGAIN FOR THE OTHER PANELS)"
                  : "SEQUENCE: THE OTHER PANELS ARE BACK");
}

void PaulaEditor::setSidebarTabs (bool on)
{
    auto& st = proc.editorState;
    st.sidebarTabs = on;
    if (on)
    {
        // keep the instruments open (or whichever is open now)
        const int open = (st.sidebarFold & 4) == 0 ? 4 : (st.sidebarFold & 1) == 0 ? 1 : (st.sidebarFold & 2) == 0 ? 2 : 4;
        st.sidebarFold = 7 & ~open;
    }
    else st.sidebarFold = 0;
    resized();
    repaint();
    setStatus (on ? "SIDEBAR TABS: ONE PANEL AT A TIME - CLICK A TITLE TO SWITCH" : "SIDEBAR: ALL PANELS OPEN");
}

void PaulaEditor::setLane (bool on)
{
    proc.editorState.showLane = on;
    resized();
    repaint();
    setStatus (on ? "THE LANE: THE WHOLE SONG AS CLIPS OVER THE PATTERN - CLICK ONE TO GO THERE" : "THE LANE: OFF");
}

void PaulaEditor::setPads (bool on)
{
    proc.editorState.showPads = on;
    resized();
    repaint();
    setStatus (on ? "PADS: CLICK ONE TO PLAY ITS INSTRUMENT (EDIT ON: WRITES IT) - + / - FOR THE NEXT 16" : "PADS: OFF");
}

void PaulaEditor::showSidebarMenu()
{
    auto& st = proc.editorState;
    juce::Component::SafePointer<PaulaEditor> safe (this);
    prv::ui::showMenu ("SIDEBAR", {
        { st.sidebarTabs ? "TABS: OFF (ALL PANELS)" : "TABS: ONE PANEL AT A TIME", [safe] { if (safe) safe->setSidebarTabs (! safe->sidebarTabs()); } },
        { "OPEN ALL PANELS", [safe] { if (safe) { safe->proc.editorState.sidebarTabs = false; safe->proc.editorState.sidebarFold = 0; safe->resized(); safe->repaint(); } } },
        { st.showLane ? "THE LANE: OFF" : "THE LANE: THE SONG OVER THE PATTERN", [safe] { if (safe) safe->setLane (! safe->laneOn()); } },
        { st.showPads ? "PADS: OFF" : "PADS: 16 INSTRUMENT PADS", [safe] { if (safe) safe->setPads (! safe->padsOn()); } },
    });
}

// ---- Cmd+K: everything the app can do, by name
std::vector<prv::ui::PaletteItem> PaulaEditor::paletteItems()
{
    std::vector<prv::ui::PaletteItem> out;
    juce::Component::SafePointer<PaulaEditor> safe (this);
    // every button on screen (by its label)
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* ch : c.getChildren())
        {
            if (! ch->isVisible() || ch == &palette) continue;
            if (auto* b = dynamic_cast<prv::ui::PixButton*> (ch); b != nullptr && b->isEnabled())
            {
                const auto t = b->getButtonText().replaceCharacter ('\n', ' ').trim();
                bool word = false;
                for (auto ch2 : t) if (juce::CharacterFunctions::isLetter (ch2)) word = true;
                if (t.length() >= 2 && word)
                {
                    juce::Component::SafePointer<prv::ui::PixButton> sb (b);
                    out.push_back ({ t.toUpperCase(), {}, "BUTTON", [sb] { if (sb != nullptr) sb->triggerClick(); } });
                }
            }
            walk (*ch);
        }
    };
    walk (*this);
    // the pages
    using V = View;
    const std::pair<const char*, V> pages[] = { { "PAGE: PATTERN", V::Pattern }, { "PAGE: MIXER", V::Mixer }, { "PAGE: ARRANGE", V::Arrange },
                                                { "PAGE: CHORDS", V::Chords }, { "PAGE: RADIO", V::Radio }, { "PAGE: INSTRUMENT PARAMS", V::Params },
                                                { "PAGE: SAMPLE EDITOR", V::SampleEdit }, { "PAGE: SAMPLE (RECORD)", V::Sample } };
    for (auto& [n, v] : pages) { const auto vv = v; out.push_back ({ n, {}, "PAGE", [safe, vv] { if (safe) safe->setView (vv); } }); }
    out.push_back ({ "PAGE: HELP", "F1", "PAGE", [safe] { if (safe) safe->toggleHelp(); } });
    out.push_back ({ "PAGE: BREAK", "CMD+6", "PAGE", [safe] { if (safe) safe->openBreak(); } });
    out.push_back ({ "FILL: LATCH ON / OFF", "SHIFT+F2", "PLAY", [safe] { if (safe) safe->setFillLatch (! safe->fillLatched); } });
    out.push_back ({ "CONDITION ON THE HIGHLIGHTED NOTES...", {}, "EDIT", [safe] { if (safe) safe->showConditionMenu(); } });
    // the look and the layout
    for (int i = 0; i < prv::ui::Theme::kSkins; ++i)
        out.push_back ({ "SKIN: " + prv::ui::Theme::skinName ((prv::ui::Theme::SkinId) i), {}, "VIEW", [safe, i] { if (safe) safe->setSkin (i); } });
    out.push_back ({ "CRT SCANLINES ON / OFF", {}, "VIEW", [safe] { if (safe) safe->setCrt (! safe->crtOn()); } });
    out.push_back ({ "TRACK SCOPES ON / OFF", {}, "VIEW", [safe] { if (safe) safe->setScopes (! safe->scopesOn()); } });
    out.push_back ({ "THE LANE (SONG CLIPS OVER THE PATTERN) ON / OFF", {}, "VIEW", [safe] { if (safe) safe->setLane (! safe->laneOn()); } });
    out.push_back ({ "PADS ON / OFF", {}, "VIEW", [safe] { if (safe) safe->setPads (! safe->padsOn()); } });
    out.push_back ({ "SIDEBAR TABS ON / OFF", {}, "VIEW", [safe] { if (safe) safe->setSidebarTabs (! safe->sidebarTabs()); } });
    out.push_back ({ "SIDEBAR: OPEN ALL PANELS", {}, "VIEW", [safe] { if (safe) { safe->proc.editorState.sidebarTabs = false; safe->proc.editorState.sidebarFold = 0; safe->resized(); } } });
    for (int b = 1; b <= 4; b <<= 1)
    {
        const juce::String n = b == 1 ? "SEQUENCE" : b == 2 ? "BLOCK" : "INSTRUMENTS";
        out.push_back ({ "SIDEBAR: FOLD / OPEN " + n, {}, "VIEW", [safe, b] { if (safe) safe->foldPanel (b, (safe->sidebarFold() & b) == 0); } });
    }
    if (addAppCommands) addAppCommands (out);
    return out;
}

void PaulaEditor::showPalette()
{
    palette.open (*this, paletteItems());
    setStatus ("COMMANDS: TYPE A FEW LETTERS, ENTER DOES IT");
}

// ---- CONDITIONS: ?xy on the highlighted notes
void PaulaEditor::showConditionMenu()
{
    static const std::pair<const char*, uint8_t> conds[] = {
        { "1ST OF EVERY 2 (?12)", 0x12 }, { "2ND OF EVERY 2 (?22)", 0x22 },
        { "1ST OF EVERY 4 (?14)", 0x14 }, { "2ND OF EVERY 4 (?24)", 0x24 }, { "3RD OF EVERY 4 (?34)", 0x34 }, { "4TH OF EVERY 4 (?44)", 0x44 },
        { "4TH OF EVERY 8 (?48)", 0x48 }, { "8TH OF EVERY 8 (?88)", 0x88 },
        { "ONLY IN A FILL - HOLD F2 (?01)", 0x01 }, { "NOT IN A FILL (?02)", 0x02 },
        { "THE FIRST TIME ONLY (?03)", 0x03 }, { "EVERY TIME BUT THE FIRST (?04)", 0x04 },
        { "NONE - ALWAYS PLAY", 0x00 } };
    juce::StringArray names;
    for (auto& c : conds) names.add (c.first);
    juce::Component::SafePointer<PaulaEditor> safe (this);
    choice.show (*this, "PLAY THE HIGHLIGHTED NOTES...", names, [safe] (int a)
    {
        if (safe == nullptr || a < 0 || a >= (int) std::size (conds)) return;
        safe->pattern.setCondition (conds[a].second);
        safe->refreshAll();
    });
}

void PaulaEditor::setFillLatch (bool on)
{
    fillLatched = on;
    setStatus (on ? "FILL ON (LATCHED) - ?01 STEPS PLAY, ?02 STEPS DON'T - SHIFT+F2 AGAIN TO LET GO" : "FILL OFF");
}

void PaulaEditor::openLive()
{
    if (proc.editorState.view == View::Live) { livePage.onClose(); return; }
    liveReturn = proc.editorState.view;
    setView (View::Live);
}

void PaulaEditor::openVisuals()
{
    if (proc.editorState.view == View::Visuals) { visualsPage.onClose(); return; }
    visualsReturn = proc.editorState.view;
    setView (View::Visuals);
    setStatus ("VISUALS - TAB FOR THE NEXT SCENE, ESC OR CMD+8 TO GO BACK");
}

void PaulaEditor::openBreak()
{
    if (proc.editorState.view == View::Break) { breakPage.onClose(); return; }
    breakReturn = proc.editorState.view;
    setView (View::Break);
}

void PaulaEditor::setScopes (bool on)
{
    proc.editorState.scopes = on;
    pattern.resized();
    pattern.repaint();
    setStatus (on ? "TRACK SCOPES ON (VIEW > TRACK SCOPES)" : "TRACK SCOPES OFF");
}

void PaulaEditor::setCrt (bool on)
{
    proc.editorState.crt = on;
    prv::ui::Theme::crt = on;
    crtOverlay.setVisible (on);
    crtOverlay.toFront (false);
    repaint();
    setStatus (on ? "CRT: SCANLINES ON" : "CRT OFF");
}

void PaulaEditor::showSkinMenu()
{
    juce::Component::SafePointer<PaulaEditor> safe (this);
    std::vector<prv::ui::MenuItem> items;
    for (int i = 0; i < prv::ui::Theme::kSkins; ++i)
        items.push_back ({ (proc.editorState.skin == i ? "> " : "  ") + prv::ui::Theme::skinName ((prv::ui::Theme::SkinId) i), [safe, i] { if (safe) safe->setSkin (i); } });
    items.push_back ({ juce::String ("  CRT: ") + (proc.editorState.crt ? "ON (TURN OFF)" : "OFF (TURN ON)"), [safe] { if (safe) safe->setCrt (! safe->proc.editorState.crt); } });
    prv::ui::showMenu ("SKIN", std::move (items));
}

void PaulaEditor::setFollowClock (bool on)
{
    proc.setFollowClock (on);
    setStatus (on ? "FOLLOWING MIDI CLOCK: THE TEMPO, START, STOP AND POSITION COME FROM THE MIDI INPUT (TURN ITS DEVICE ON IN AUDIO/MIDI SETTINGS)"
                  : "MIDI CLOCK OFF: THE SONG'S OWN TEMPO AGAIN");
}

bool PaulaEditor::resampleChop()
{
    prv::BounceRange r;
    if (! pattern.selectionArea (r.block, r.line0, r.line1, r.track0, r.track1))
    {
        // nothing highlighted: the whole block, every track
        r.block = proc.editorState.block; r.line0 = 0; r.line1 = proc.song.blocks[(size_t) r.block].lines - 1;
        r.track0 = 0; r.track1 = proc.song.numTracks - 1;
    }
    r.withFx = r.instrumentFx = true;
    int slot = -1;
    for (int i = 0; i < prv::kMaxInstruments && slot < 0; ++i)
    {
        const auto& in = proc.song.instruments[(size_t) i];
        if (! in.hasData() && ! in.isPlugin()) slot = i;
    }
    if (slot < 0) { setStatus ("CAN'T RESAMPLE: ALL 63 INSTRUMENTS ARE IN USE"); return false; }
    prv::Song copy;
    prv::PaulaOptions opts;
    proc.capturePluginStates();
    proc.withLock ([&] { copy = proc.song; opts = proc.player.options; });
    prv::BounceResult res;
    std::string err;
    const double rate = 44100.0;
    if (! prv::bounceRange (copy, opts, rate, r, res, proc.synthFactory(), err, proc.pluginFactory()))
    {
        setStatus ("CAN'T RESAMPLE: " + juce::String (err));
        return false;
    }
    // cut to exactly its lines (the tail would make it loop late), a slice a beat
    const int lines = r.line1 - r.line0 + 1;
    const size_t len = (size_t) std::lround (lines * res.secondsPerLine * rate);   // (exactly its lines: silence where it went quiet)
    res.left.resize (len, 0.0f); res.right.resize (len, 0.0f);
    const int per = lines >= 8 ? 4 : 1;
    prv::Instrument made;
    const juce::String name = "RESAMPLE B" + juce::String (r.block).paddedLeft ('0', 3) + " " + juce::String (r.line0).paddedLeft ('0', 3) + "-" + juce::String (r.line1).paddedLeft ('0', 3);
    made.name = name.toStdString();
    made.rate = rate;
    made.volume = 64;
    made.data.resize (len); made.dataR.resize (len);
    bool stereo = false;
    for (size_t k = 0; k < len; ++k)
    {
        // (a 2 ms fade at the end: no click where it's cut)
        const float g = std::min (1.0f, (float) (len - k) / (float) (rate * 0.002));
        made.data[k]  = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (res.left[k] * g * 32767.0f));
        made.dataR[k] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (res.right[k] * g * 32767.0f));
        stereo = stereo || made.data[k] != made.dataR[k];
    }
    if (! stereo) made.dataR.clear();
    for (int l = 0; l < lines; l += per) made.slices.push_back ((int) std::lround (l * res.secondsPerLine * rate));
    made.sliceKeys = true;
    made.syncLines = lines;
    proc.history.instrument (slot, "RESAMPLE " + name);
    proc.withLock ([&] { proc.song.instruments[(size_t) slot] = std::move (made); });
    proc.editorState.instrument = slot;
    setStatus ("RESAMPLED TO INSTRUMENT " + juce::String::toHexString (slot + 1).toUpperCase().paddedLeft ('0', 2) + ": " + juce::String ((lines + per - 1) / per)
               + " SLICES (A " + (per == 4 ? "BEAT" : "LINE") + " EACH), IN TIME AT ANY TEMPO - C-3, C#3... PLAY THE SLICES");
    refreshAll();
    return true;
}

// ---------------------------------------------------------------------------
// REC AUTO
float PaulaEditor::targetValue (const prv::AutoTarget& t)
{
    if (! t.valid() || t.param == prv::kAutoOnOff) return -1.0f;
    if (t.target < prv::kAutoInstrumentBase)
    {
        if (t.target >= prv::kTotalSlots) return -1.0f;
        const auto& s = proc.song.mixer.slot (t.target);
        if (s.type == prv::FxType::Plugin)
        {
            auto* pe = proc.pluginAt (t.target);
            if (pe == nullptr) return -1.0f;
            const auto& ps = pe->instance().getParameters();
            return t.param < ps.size() ? ps[t.param]->getValue() : -1.0f;
        }
        return t.param < prv::kFxParams ? s.p[t.param] : -1.0f;
    }
    const int i = t.target - prv::kAutoInstrumentBase;
    if (auto* m = proc.midiAt (i)) return m->knobValue (t.param);
    if (auto* s = proc.synthAt (i))
    {
        const auto& ps = s->instance().getParameters();
        return t.param < ps.size() ? ps[t.param]->getValue() : -1.0f;
    }
    return -1.0f;
}

void PaulaEditor::recordValue (int track, char letter, float value, int& lastWritten)
{
    const int v = juce::jlimit (0, 255, juce::roundToInt (value * 255.0f));
    if (v == lastWritten) return;   // (the same as the last value written: nothing new)
    const int b = proc.song.safeBlock (proc.player.uiBlock.load());
    const auto& blk = proc.song.blocks[(size_t) b];
    const int line = juce::jlimit (0, blk.lines - 1, proc.player.uiLine.load());
    const uint8_t cmd = prv::cmdLetter (letter);
    const auto& c = blk.at (line, track);
    int column = -1;
    if ((c.cmd == 0 && c.param == 0) || c.cmd == cmd) column = 0;
    else if ((c.cmd2 == 0 && c.param2 == 0) || c.cmd2 == cmd) column = 1;
    if (column < 0)
    {
        setStatus ("REC AUTO: LINE " + juce::String (line).paddedLeft ('0', 3) + " OF TRACK " + juce::String (track + 1) + " HAS NO FREE EFFECT COLUMN - SKIPPED");
        return;
    }
    if (! recPass) { proc.history.structure ("RECORD AUTOMATION"); recPass = true; }
    proc.withLock ([&]
    {
        auto& cell = proc.song.blocks[(size_t) b].at (line, track);
        if (column == 0) { cell.cmd = cmd; cell.param = (uint8_t) v; }
        else { cell.cmd2 = cmd; cell.param2 = (uint8_t) v; }
    });
    lastWritten = v;
    pattern.repaint();
}

void PaulaEditor::recordAutomation()
{
    const auto& st = proc.editorState;
    const bool on = st.recAuto && st.editMode && proc.player.isPlaying();
    const int nt = proc.song.numTracks;
    // track knobs (V / J)
    for (int t = 0; t < prv::kMaxTracks; ++t)
        for (int k = 0; k < prv::kTrackKnobs; ++k)
        {
            const float v = t < nt ? targetValue (proc.song.mixer.trackAuto[t][k]) : -1.0f;
            float& seen = recSeenTrack[t][k];
            if (! on || v < 0 || seen < 0) { seen = v; continue; }
            if (std::abs (v - seen) < 0.002f) continue;
            seen = v;
            // (the pattern moving it isn't the hand moving it)
            const float lane = proc.player.laneValue (0, t, k);
            if (lane >= 0 && std::abs (lane - v) < 0.006f) continue;
            recordValue (t, k == 0 ? 'V' : 'J', v, recLastTrack[t][k]);
        }
    // a plug-in / MIDI-out instrument's own knobs (L / Q / H / W) on the track playing it
    static const char letters[prv::kInstrumentKnobs] = { 'L', 'Q', 'H', 'W' };
    for (int i = 0; i < prv::kMaxInstruments; ++i)
    {
        const auto& in = proc.song.instruments[(size_t) i];
        if (! in.isPlugin()) continue;
        for (int k = 0; k < prv::kInstrumentKnobs; ++k)
        {
            const float v = targetValue (in.knobs[k]);
            float& seen = recSeenInst[i][k];
            if (! on || v < 0 || seen < 0) { seen = v; continue; }
            if (std::abs (v - seen) < 0.002f) continue;
            seen = v;
            const float lane = proc.player.laneValue (1, i, k);
            if (lane >= 0 && std::abs (lane - v) < 0.006f) continue;
            int track = -1;
            for (int t = 0; t < nt && track < 0; ++t) if (proc.player.uiTrackInstr[t].load() == i) track = t;
            if (track < 0) continue;
            recordValue (track, letters[k], v, recLastInst[i][k]);
        }
    }
    if (! on)
    {
        // a new pass next time (its own undo step), and nothing counts as already written
        recPass = false;
        for (auto& a : recLastTrack) for (auto& x : a) x = -1;
        for (auto& a : recLastInst) for (auto& x : a) x = -1;
    }
}

bool PaulaEditor::exportSamplesTo (const juce::File& folder, int& written, juce::String& error)
{
    written = 0;
    if (! folder.isDirectory() && ! folder.createDirectory()) { error = "Couldn't create " + folder.getFullPathName(); return false; }
    std::vector<prv::Instrument> copies;
    proc.withLock ([&] { copies.assign (proc.song.instruments.begin(), proc.song.instruments.end()); });
    for (int i = 0; i < (int) copies.size(); ++i)
    {
        const auto& in = copies[(size_t) i];
        if (! in.hasData()) continue;
        const juce::String name = juce::String (i + 1).paddedLeft ('0', 2) + " "
                                + juce::File::createLegalFileName (in.name.empty() ? juce::String ("SAMPLE") : juce::String (in.name)) + ".wav";
        auto f = folder.getChildFile (name);
        if (! prv::writeInstrumentWav (in, f, error)) return false;
        ++written;
    }
    return true;
}

void PaulaEditor::exportSamples()
{
    auto& st = proc.editorState;
    juce::File start = prv::startFolder (st.lastFolder, "songFolder");
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the samples", start);
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto dir = fc.getResult();
        if (dir == juce::File()) return;
        int n = 0;
        juce::String err;
        if (safe->exportSamplesTo (dir, n, err)) safe->setStatus ("EXPORTED " + juce::String (n) + " SAMPLES TO " + dir.getFileName());
        else safe->setStatus ("ERROR: " + err);
    });
}
