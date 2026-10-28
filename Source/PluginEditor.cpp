// Paula's Revenge - main window
#include "PluginEditor.h"
#include "ui/MeterColours.h"
#include "engine/Capture.h"
#include "SongFile.h"
#include "engine/Bounce.h"
#include "engine/ModuleIO.h"
#include "Prefs.h"

using namespace prv::ui;

namespace { const juce::String kHelpText = "SPACE=PLAY/STOP  ESC=EDIT  CMD+Z=UNDO"; }

PaulaEditor::PaulaEditor (PaulaProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      pattern (p),
      sequence (p, [this] { refreshAll(); }),
      blockPanel (p, [this] { refreshAll(); }),
      instruments (p, [this] { refreshAll(); }, [this] (const juce::String& s) { setStatus (s); }),
      spectrum (p), sampleEditor (p), browser (p), params (p), synthPage (p), midiPage (p), arrange (p), mixer (p), chords (p), samplePage (p), outMeter (p)
{
    auto& st = proc.editorState;
    Theme::setSkin (st.darkSkin);
    skinBtn.onClick = [this, &st] { st.darkSkin = skinBtn.getToggleState(); Theme::setSkin (st.darkSkin); repaint(); refreshAll(); };
    specBtn.onClick = [this, &st] { st.showSpectrum = specBtn.getToggleState(); resized(); };

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
    chords.onStopSong = [this] { pattern.stopPlayback(); refreshAll(); setStatus ("STOPPED"); };
    sampleEditor.onStopSong = [this] { pattern.stopPlayback(); refreshAll(); setStatus ("STOPPED"); };
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
    // an instrument's own effects: edited on the mixer's effect page, BACK returns here
    auto openInstrumentFx = [this] (int slot)
    {
        fxReturn = proc.editorState.view;
        setView (View::Mixer);
        mixer.openEffect (slot);
    };
    params.onOpenFx = openInstrumentFx;
    params.onFitStretch = [this] (int lines) { setView (View::SampleEdit); sampleEditor.openFit (lines); };
    synthPage.onOpenFx = openInstrumentFx;
    mixer.onBackFromInstrument = [this] { setView (fxReturn == View::Mixer ? View::Params : fxReturn); };
    instruments.openSample = [this] { openSample(); };
    pattern.onEditChords = [this] (int slot) { openChords (slot); };
    for (auto& a : recSeenTrack) for (auto& v : a) v = -1.0f;
    for (auto& a : recSeenInst) for (auto& v : a) v = -1.0f;
    for (auto& a : recLastTrack) for (auto& v : a) v = -1;
    for (auto& a : recLastInst) for (auto& v : a) v = -1;
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
    playSongBtn.onClick  = [this, &st] { proc.withLock ([&] { proc.player.playSong (st.seqPos, 0); }); refreshAll(); };
    playBlockBtn.onClick = [this, &st] { proc.withLock ([&] { proc.player.playBlock (st.block, 0); }); refreshAll(); };
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
                                           proc.fullVelocity.load() ? "FULL VEL: ON (TURN OFF - USE THE KEYBOARD'S VELOCITY)" : "FULL VEL: OFF (TURN ON - EVERY NOTE AT 127)" },
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

    // ---- song strip
    viewBtn.onClick = [this, &st] { st.fourTrackView = viewBtn.getToggleState(); refreshAll(); };

    pattern.onStateChanged = [this] { refreshAll(); };
    pattern.onBounce = [this] { askBounce(); };
    pattern.onSelectionMenu = [this] (int line, int track, int group)
    {
        juce::Component::SafePointer<PaulaEditor> safe (this);
        choice.show (*this, "THE HIGHLIGHTED NOTES", { "BOUNCE: NO FX", "BOUNCE: INSTRUMENT FX ONLY", "BOUNCE: MIXER FX ONLY", "BOUNCE: ALL FX", "EFFECT COMMANDS..." },
                     [safe, line, track, group] (int a)
        {
            if (safe == nullptr) return;
            if (a >= 0 && a <= 3) safe->bounceSelection (a);
            else if (a == 4) safe->pattern.openCommandMenu (line, track, group);
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
        setView (in.isMidiOut() ? View::MidiOut : in.isPlugin() ? View::Synth : View::Params);
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
            case View::Pattern:    pattern.grabKeyboardFocus(); break;
        }
    };
    arrange.onClose = [this] { setView (View::Pattern); };
    arrange.onChanged = [this] { sequence.refresh(); blockPanel.refresh(); };
    arrange.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (arrange);
    mixer.onClose = [this] { setView (View::Pattern); };
    mixer.onStatus = [this] (const juce::String& m) { setStatus (m); };
    mixer.onPlayStop = [this] (bool song) { pattern.togglePlayStop (song); refreshAll(); };
    mixer.onOctave = [this] { refreshAll(); mixer.repaint(); };
    addChildComponent (mixer);
    mixerBtn.onClick = [this] { setView (proc.editorState.view == View::Mixer ? View::Pattern : View::Mixer); };
    fx2Btn.onClick = [this, &st] { st.showFx2 = fx2Btn.getToggleState(); if (! st.showFx2) st.column = std::min (st.column, 5); refreshAll(); };
    params.onClose = [this] { setView (View::Pattern); };
    params.onUsePlugin = [this]
    {
        juce::Component::SafePointer<PaulaEditor> safe (this);
        choice.show (*this, "PLAY THIS INSTRUMENT WITH", { "A VST3 / AU INSTRUMENT", "MIDI OUT TO HARDWARE" }, [safe] (int a)
        {
            if (safe == nullptr || a < 0) return;
            if (a == 0) { safe->setView (View::Synth); safe->synthPage.opened (true); }
            else { safe->setView (View::MidiOut); safe->midiPage.opened (true); }
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
    instruments.openSampleEditor = [this]
    {
        // EDIT on a plug-in instrument opens the plug-in's own window
        const auto& in = proc.song.instruments[(size_t) proc.editorState.instrument];
        if (in.isMidiOut()) setView (View::MidiOut);
        else if (in.isPlugin()) synthPage.openWindow();
        else setView (View::SampleEdit);
    };
    sampleEditor.onChanged = [this] { instruments.refresh(); };
    sampleEditor.onClose = [this] { setView (View::Pattern); };
    sampleEditor.onStatus = [this] (const juce::String& m) { setStatus (m); };
    browser.onLoaded = [this] { refreshAll(); setView (returnView); };
    browser.onClose = [this] { setView (returnView); };
    browser.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (sampleEditor);
    addChildComponent (browser);

    for (auto* c : std::initializer_list<juce::Component*> {
            &helpBtn, &newBtn, &loadBtn, &saveBtn, &saveAsBtn, &undoBtn, &redoBtn, &fullVelBtn, &noteOffsBtn, &zoomOut, &zoomIn, &outMeter,
            &playSongBtn, &playBlockBtn, &stopBtn, &editBtn, &captureBtn, &loopBtn, &followBtn, &panelsBtn, &tempoBox, &speedBox, &octBox, &stepBox, &posLabel,
            &a500Btn, &a1200Btn, &a4000Btn, &cleanBtn, &s950Btn, &s1000Btn, &steppedBtn, &eightBitBtn, &crunchBtn, &ledBtn, &ledLabel, &sepSlider, &volSlider,
            &viewBtn, &mixerBtn, &statusLabel,
            &skinBtn, &specBtn, &fx2Btn, &recAutoBtn, &spectrum,
            &pattern, &sequence, &blockPanel, &instruments })
        addAndMakeVisible (c);

    panelsBtn.setToggleState (true, juce::dontSendNotification);
    setStatus (kHelpText);

    setResizable (true, true);
    setResizeLimits (1000, 640, 8000, 5000);   // (the transport row at full size)
    setSize (1440, 900);
    refreshAll();
    setView (st.view == View::Browser ? View::Pattern : st.view);
    startTimerHz (30);
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
    proc.pluginHost.onWindowKey = nullptr;
    proc.pluginHost.onWindowKeyState = nullptr;
    stopTimer();
    proc.history.onRestored = nullptr;
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
    auto wide = [] (View w) { return w == View::Mixer || w == View::Help || w == View::Chords; };
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
    skinBtn.setToggleState (st.darkSkin, juce::dontSendNotification);
    specBtn.setToggleState (st.showSpectrum, juce::dontSendNotification);
    fx2Btn.setToggleState (st.showFx2, juce::dontSendNotification);
    recAutoBtn.setToggleState (st.recAuto, juce::dontSendNotification);
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
    // the pattern gets the keyboard back when nothing else has it (after a dialog, a plug-in
    // window, the app coming to the front...), so typing always lands in the pattern
    if (proc.editorState.view == View::Pattern && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0
        && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        if ((f == nullptr || f == this) && isShowing() && getPeer() != nullptr && getPeer()->isFocused()) pattern.grabKeyboardFocus();
    }
    pattern.tick();
    if (spectrum.isVisible()) spectrum.tick();
    if (sampleEditor.isVisible()) sampleEditor.tick();
    if (browser.isVisible()) browser.tick();
    if (params.isVisible()) params.tick();
    if (synthPage.isVisible()) synthPage.tick();
    // SAMPLE: its meter, and a take finishing even while another page is showing
    if (samplePage.isVisible() || proc.recorder.state() != prv::TakeRecorder::State::Idle) samplePage.tick();
    if (midiPage.isVisible()) midiPage.tick();
    // the PARAMS page follows the instrument: a plug-in or MIDI-out instrument shows its own page
    {
        const auto& cur = proc.song.instruments[(size_t) proc.editorState.instrument];
        const auto view = proc.editorState.view;
        if ((view == View::Params || view == View::Synth) && cur.isMidiOut()) setView (View::MidiOut);
        else if ((view == View::Params || view == View::MidiOut) && cur.isPlugin() && ! cur.isMidiOut()) setView (View::Synth);
    }
    recordAutomation();
    if (arrange.isVisible()) arrange.tick();
    if (mixer.isVisible()) mixer.tick();
    outMeter.tick();
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
    g.fillAll (Theme::paper);
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();

    // title bar: horizontal rules with the name knocked out (drawn in black on white)
    auto title = r.removeFromTop (px * 14);
    g.setColour (Theme::panel);
    g.fillRect (title);
    Theme::bevel (g, title.reduced (1.0f), true, std::max (1.0f, px * 0.5f));
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
    Theme::px = juce::jlimit (1.5f, 5.0f, std::floor (base * 2.0f) / 2.0f + st.uiZoom * 0.5f);
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
    zoomIn.setBounds (title.removeFromRight (charsW (2) + gap)); title.removeFromRight (gap);
    zoomOut.setBounds (title.removeFromRight (charsW (2) + gap)); title.removeFromRight (wide);
    captureBtn.setBounds (title.removeFromRight (charsW (7))); title.removeFromRight (gap);
    noteOffsBtn.setBounds (title.removeFromRight (charsW (9))); title.removeFromRight (gap);
    fullVelBtn.setBounds (title.removeFromRight (charsW (8))); title.removeFromRight (wide);
    redoBtn.setBounds (title.removeFromRight (charsW (4))); title.removeFromRight (gap);
    undoBtn.setBounds (title.removeFromRight (charsW (4))); title.removeFromRight (wide);
    saveAsBtn.setBounds (title.removeFromRight (charsW (7))); title.removeFromRight (gap);
    saveBtn.setBounds (title.removeFromRight (charsW (5))); title.removeFromRight (gap);
    loadBtn.setBounds (title.removeFromRight (charsW (4))); title.removeFromRight (gap);
    newBtn.setBounds (title.removeFromRight (charsW (3))); title.removeFromRight (wide);
    helpBtn.setBounds (title.removeFromRight (charsW (4)));

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
    auto paula2 = r.removeFromBottom (rowH).reduced (gap, 0);   // SEP + VOL
    r.removeFromBottom (gap);
    auto paula = r.removeFromBottom (rowH).reduced (gap, 0);    // models, options, meter
    r.removeFromBottom (gap);

    viewBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    panelsBtn.setBounds (status.removeFromLeft (charsW (6))); status.removeFromLeft (gap);
    skinBtn.setBounds (status.removeFromLeft (charsW (4))); status.removeFromLeft (gap);
    specBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    fx2Btn.setBounds (status.removeFromLeft (charsW (3))); status.removeFromLeft (gap);
    recAutoBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
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
    outMeter.setBounds (paula.withTrimmedLeft (std::max (0, paula.getWidth() - charsW (24))));
    const int sliderW = (paula2.getWidth() - gap * 2) / 2;
    sepSlider.setBounds (paula2.removeFromLeft (sliderW)); paula2.removeFromLeft (gap * 2);
    volSlider.setBounds (paula2);

    // main area
    auto main = r.reduced (gap, 0);
    // the mixer uses the whole width
    // the mixer and HELP use the whole width
    if (st.view == View::Mixer) mixer.setBounds (main);
    help.setBounds (main);
    chords.setBounds (main);
    const bool wideView = st.view == View::Mixer || st.view == View::Help || st.view == View::Chords;
    const bool panels = showPanels && ! wideView;
    sequence.setVisible (panels);
    blockPanel.setVisible (panels);
    instruments.setVisible (panels);
    if (panels)
    {
        auto left = main.removeFromLeft (std::min (charsW (32), main.getWidth() / 2));
        main.removeFromLeft (gap * 2);
        const int seqH = std::max (rowH * 8, (int) (left.getHeight() * 0.40f));
        sequence.setBounds (left.removeFromTop (seqH));
        left.removeFromTop (gap);
        blockPanel.setBounds (left.removeFromTop ((int) (px * 14) + rowH * 3 + gap * 3));
        left.removeFromTop (gap);
        instruments.setBounds (left);
    }
    spectrum.setVisible (st.showSpectrum && ! wideView);
    if (st.showSpectrum)
    {
        spectrum.setBounds (main.removeFromBottom ((int) (px * 22)));
        main.removeFromBottom (gap);
    }
    pattern.setBounds (main);
    sampleEditor.setBounds (main);
    browser.setBounds (main);
    params.setBounds (main);
    synthPage.setBounds (main);
    midiPage.setBounds (main);
    arrange.setBounds (main);
    samplePage.setBounds (main);
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
        if (k.getKeyCode() == juce::KeyPress::spaceKey && ! mm.isCommandDown() && ! mm.isCtrlDown() && ! mm.isAltDown()
            && v != View::Pattern && v != View::Help)
        {
            const bool previewOnly = ! proc.player.isPlaying() && proc.previewPos.load() >= 0;
            sampleEditor.stop();
            proc.stopPreview();
            if (! previewOnly) pattern.togglePlayStop (mm.isShiftDown());
            refreshAll();
            setStatus (proc.player.isPlaying() ? "PLAYING (SPACE STOPS IT)" : "STOPPED");
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
        if (ch == 'r' && ! m.isShiftDown()) { captureNow(); return true; }
    }
    // keys that land on the window (e.g. after clicking a button) go to the active view
    switch (proc.editorState.view)
    {
        case View::SampleEdit: return sampleEditor.keyPressed (k);
        case View::Browser:    return browser.keyPressed (k);
        case View::Params:     return params.keyPressed (k);
        case View::Synth:      return synthPage.keyPressed (k);
        case View::MidiOut:    return midiPage.keyPressed (k);
        case View::Arrange:    return arrange.keyPressed (k);
        case View::Mixer:      return mixer.keyPressed (k);
        case View::Help:       return help.keyPressed (k);
        case View::Chords:     return chords.keyPressed (k);
        case View::Sample:     return samplePage.keyPressed (k);
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
    prv::rememberFolder (proc.editorState.lastFolder, "songFolder", file.getParentDirectory());
    prv::Song s;
    juce::String err;
    juce::StringArray notes;
    if (! prv::loadSongFile (file, s, err, notes)) { setStatus ("ERROR: " + err); return false; }
    if (s.chordsLegacy || s.chordsSaved) notes.add (proc.chordReport (s));
    if (file.hasFileExtension ("prv")) s.name = songNameFor (file).toStdString();   // (a MOD keeps its own name)
    proc.replaceSong (std::move (s));
    auto& st2 = proc.editorState;
    st2.block = proc.song.safeBlock (proc.song.sequence[0]); st2.line = 0; st2.seqPos = 0; st2.track = 0;
    st2.currentFile = file.hasFileExtension ("prv") ? file.getFullPathName() : juce::String();
    setStatus ("LOADED " + file.getFileName() + (notes.isEmpty() ? juce::String() : "  -  " + notes.joinIntoString ("  ")));
    refreshAll();
    repaint();
    return true;
}

bool PaulaEditor::writeSongTo (const juce::File& file)
{
    // the song is called what its file is called
    proc.withLock ([&] { proc.song.name = songNameFor (file).toStdString(); });
    juce::MemoryBlock data;
    proc.capturePluginStates();
    proc.withLock ([&] { data = prv::writeSong (proc.song); });
    if (! file.replaceWithData (data.getData(), data.getSize()))
    {
        setStatus ("ERROR: COULDN'T WRITE " + file.getFullPathName());
        return false;
    }
    auto& st = proc.editorState;
    st.currentFile = file.getFullPathName();
    prv::rememberFolder (st.lastFolder, "songFolder", file.getParentDirectory());
    proc.history.markSaved();
    setStatus ("SAVED " + file.getFileName() + " IN " + file.getParentDirectory().getFullPathName());
    repaint();
    return true;
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

void PaulaEditor::saveSongAs()
{
    auto& st = proc.editorState;
    const juce::File current (juce::File::isAbsolutePath (st.currentFile) ? st.currentFile : juce::String());
    juce::File start = current != juce::File() ? current
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
        safe->writeSongTo (file);
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
    juce::StringArray leftOut;
    const double rate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    if (prv::renderSongToWav (copy, opts, rate, file, err, proc.pluginFactory(), proc.synthFactory(), stems, &leftOut))
    {
        juce::String m = stems ? "RENDERED STEMS INTO " + prv::stemFolderFor (file).getFileName() : "RENDERED " + file.getFileName();
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
                 { "NO FX (DRY)", "INSTRUMENT FX ONLY (THE INSTRUMENTS' OWN INSERTS)", "MIXER FX ONLY (EQ, INSERTS, FADER, SENDS)", "ALL FX (INSTRUMENTS AND MIXER)" }, [safe] (int a)
    {
        if (safe != nullptr && a >= 0) safe->bounceSelection (a);
    });
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
