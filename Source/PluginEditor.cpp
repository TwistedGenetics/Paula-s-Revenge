// Paula's Revenge - main window
#include "PluginEditor.h"
#include "SongFile.h"
#include "engine/ModuleIO.h"

using namespace prv::ui;

namespace { const juce::String kHelpText = "SPACE=EDIT  ENTER=PLAY"; }

PaulaEditor::PaulaEditor (PaulaProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      pattern (p),
      sequence (p, [this] { refreshAll(); }),
      blockPanel (p, [this] { refreshAll(); }, [this] (int b) { pattern.pushUndo (b); }),
      instruments (p, [this] { refreshAll(); }, [this] (const juce::String& s) { setStatus (s); }),
      spectrum (p), sampleEditor (p), browser (p)
{
    auto& st = proc.editorState;
    Theme::setSkin (st.darkSkin);
    skinBtn.onClick = [this, &st] { st.darkSkin = skinBtn.getToggleState(); Theme::setSkin (st.darkSkin); repaint(); refreshAll(); };
    specBtn.onClick = [this, &st] { st.showSpectrum = specBtn.getToggleState(); resized(); };

    // ---- title bar
    newBtn.onClick  = [this] { newSong(); };
    loadBtn.onClick = [this] { loadSong(); };
    saveBtn.onClick = [this] { saveSong(); };
    modBtn.onClick  = [this] { exportMod(); };
    wavBtn.onClick  = [this] { exportWav(); };
    zoomOut.onClick = [this, &st] { st.uiZoom = std::max (-3, st.uiZoom - 1); resized(); repaint(); };
    zoomIn.onClick  = [this, &st] { st.uiZoom = std::min (4, st.uiZoom + 1); resized(); repaint(); };

    // ---- transport
    playSongBtn.onClick  = [this, &st] { proc.withLock ([&] { proc.player.playSong (st.seqPos, 0); }); refreshAll(); };
    playBlockBtn.onClick = [this, &st] { proc.withLock ([&] { proc.player.playBlock (st.block, 0); }); refreshAll(); };
    stopBtn.onClick      = [this] { pattern.stopPlayback(); refreshAll(); };
    editBtn.onClick      = [this, &st] { st.editMode = editBtn.getToggleState(); pattern.grabKeyboardFocus(); refreshAll(); };
    followBtn.onClick    = [this, &st] { st.follow = followBtn.getToggleState(); refreshAll(); };
    panelsBtn.onClick    = [this] { showPanels = panelsBtn.getToggleState(); resized(); };
    tempoBox.onChange = [this] (int v) { proc.withLock ([&] { proc.song.tempo = v; }); };
    speedBox.onChange = [this] (int v) { proc.withLock ([&] { proc.song.speed = v; }); };
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
    a500Btn.onClick  = [setModel] { setModel (prv::PaulaOptions::Model::A500); };
    a1200Btn.onClick = [setModel] { setModel (prv::PaulaOptions::Model::A1200); };
    cleanBtn.onClick = [setModel] { setModel (prv::PaulaOptions::Model::Clean); };
    steppedBtn.onClick  = [this] { applyOptions(); };
    eightBitBtn.onClick = [this] { applyOptions(); };
    crunchBtn.onClick   = [this] { applyOptions(); };
    ledBtn.onClick = [this] { proc.withLock ([&] { proc.song.ledFilterAtStart = ledBtn.getToggleState(); proc.player.setLed (ledBtn.getToggleState()); }); };
    sepSlider.defaultValue = 0.75f;
    sepSlider.labelChars = volSlider.labelChars = 4;
    volSlider.defaultValue = 0.5f;
    volSlider.format = [] (float v) { return juce::String (juce::roundToInt (v * 200)) + "%"; };
    sepSlider.onChange = [this] (float) { applyOptions(); };
    volSlider.onChange = [this] (float) { applyOptions(); };

    // ---- song strip
    tracksBtn.onClick = [this]
    {
        proc.withLock ([&] { proc.song.numTracks = tracksBtn.getToggleState() ? 8 : 4; });
        proc.editorState.track = std::min (proc.editorState.track, proc.song.numTracks - 1);
        refreshAll();
    };
    viewBtn.onClick = [this, &st] { st.fourTrackView = viewBtn.getToggleState(); refreshAll(); };
    hostPlayBtn.onClick  = [this] { proc.followHostTransport = hostPlayBtn.getToggleState(); };
    hostTempoBtn.onClick = [this] { proc.followHostTempo = hostTempoBtn.getToggleState(); };
    nameBtn.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Song name", "Name this song", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", juce::String (proc.song.name));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PaulaEditor> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            if (r != 1 || safe == nullptr) return;
            const auto n = w->getTextEditorContents ("name").toUpperCase().substring (0, 20).toStdString();
            safe->proc.withLock ([&] { safe->proc.song.name = n.empty() ? "UNTITLED" : n; });
            safe->refreshAll();
            safe->repaint();
        }), true);
    };

    pattern.onStateChanged = [this] { refreshAll(); };

    instruments.openBrowser = [this] { setView (View::Browser); };
    instruments.openSampleEditor = [this] { setView (View::SampleEdit); };
    sampleEditor.onChanged = [this] { instruments.refresh(); };
    sampleEditor.onClose = [this] { setView (View::Pattern); };
    sampleEditor.onStatus = [this] (const juce::String& m) { setStatus (m); };
    browser.onLoaded = [this] { refreshAll(); setView (returnView); };
    browser.onClose = [this] { setView (returnView); };
    browser.onStatus = [this] (const juce::String& m) { setStatus (m); };
    addChildComponent (sampleEditor);
    addChildComponent (browser);

    for (auto* c : std::initializer_list<juce::Component*> {
            &newBtn, &loadBtn, &saveBtn, &modBtn, &wavBtn, &zoomOut, &zoomIn,
            &playSongBtn, &playBlockBtn, &stopBtn, &editBtn, &followBtn, &panelsBtn, &tempoBox, &speedBox, &octBox, &stepBox, &posLabel,
            &a500Btn, &a1200Btn, &cleanBtn, &steppedBtn, &eightBitBtn, &crunchBtn, &ledBtn, &ledLabel, &sepSlider, &volSlider,
            &tracksBtn, &viewBtn, &hostPlayBtn, &hostTempoBtn, &nameBtn, &statusLabel,
            &skinBtn, &specBtn, &spectrum,
            &pattern, &sequence, &blockPanel, &instruments })
        addAndMakeVisible (c);
    if (proc.isRunningStandalone()) { hostPlayBtn.setVisible (false); hostTempoBtn.setVisible (false); }

    panelsBtn.setToggleState (true, juce::dontSendNotification);
    setStatus (kHelpText);

    setResizable (true, true);
    setResizeLimits (960, 640, 4000, 2400);
    setSize (1440, 900);
    refreshAll();
    setView (st.view == View::Browser ? View::Pattern : st.view);
    startTimerHz (30);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PaulaEditor> (this)] { if (safe) safe->pattern.grabKeyboardFocus(); });
}

PaulaEditor::~PaulaEditor() { stopTimer(); }

void PaulaEditor::setView (View v)
{
    auto& st = proc.editorState;
    if (v == View::Browser && st.view != View::Browser) returnView = st.view;
    if (st.view == View::SampleEdit && v != View::SampleEdit) sampleEditor.stop();
    st.view = v;
    pattern.setVisible (v == View::Pattern);
    sampleEditor.setVisible (v == View::SampleEdit);
    browser.setVisible (v == View::Browser);
    if (v == View::SampleEdit) { sampleEditor.instrumentSelected(); sampleEditor.grabKeyboardFocus(); }
    else if (v == View::Browser) browser.opened();
    else pattern.grabKeyboardFocus();
    refreshAll();
}

// ---------------------------------------------------------------------------
void PaulaEditor::applyOptions()
{
    proc.withLock ([&] {
        auto& o = proc.player.options;
        o.zeroOrderHold = steppedBtn.getToggleState();
        o.eightBit = eightBitBtn.getToggleState();
        o.crunch8ch = crunchBtn.getToggleState();
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

    const auto& o = proc.player.options;
    a500Btn.setToggleState (o.model == prv::PaulaOptions::Model::A500, juce::dontSendNotification);
    a1200Btn.setToggleState (o.model == prv::PaulaOptions::Model::A1200, juce::dontSendNotification);
    cleanBtn.setToggleState (o.model == prv::PaulaOptions::Model::Clean, juce::dontSendNotification);
    steppedBtn.setToggleState (o.zeroOrderHold, juce::dontSendNotification);
    eightBitBtn.setToggleState (o.eightBit, juce::dontSendNotification);
    crunchBtn.setToggleState (o.crunch8ch, juce::dontSendNotification);
    crunchBtn.setEnabled (song.numTracks > 4);
    ledBtn.setToggleState (song.ledFilterAtStart, juce::dontSendNotification);
    sepSlider.setValue (o.stereoSep);
    volSlider.setValue (o.master * 0.5f);

    editBtn.setToggleState (st.editMode, juce::dontSendNotification);
    followBtn.setToggleState (st.follow, juce::dontSendNotification);
    tempoBox.setValue (song.tempo);
    speedBox.setValue (song.speed);
    octBox.setValue (st.octave);
    stepBox.setValue (st.step);

    tracksBtn.setToggleState (song.numTracks > 4, juce::dontSendNotification);
    tracksBtn.setButtonText (song.numTracks > 4 ? "8 TRK" : "4 TRK");
    viewBtn.setToggleState (st.fourTrackView, juce::dontSendNotification);
    skinBtn.setToggleState (st.darkSkin, juce::dontSendNotification);
    specBtn.setToggleState (st.showSpectrum, juce::dontSendNotification);
    viewBtn.setEnabled (song.numTracks > 4);
    hostPlayBtn.setToggleState (proc.followHostTransport.load(), juce::dontSendNotification);
    hostTempoBtn.setToggleState (proc.followHostTempo.load(), juce::dontSendNotification);

    proc.jamInstrument = st.instrument;
    sequence.refresh();
    blockPanel.refresh();
    instruments.refresh();
    pattern.repaint();
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
    pattern.tick();
    if (spectrum.isVisible()) spectrum.tick();
    if (sampleEditor.isVisible()) sampleEditor.tick();
    if (browser.isVisible()) browser.tick();
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
        const double hostBpm = proc.hostBpm.load();
        const bool host = ! proc.isRunningStandalone() && proc.followHostTempo.load() && hostBpm > 0;
        tempoBox.setValue (host ? juce::roundToInt (hostBpm) : proc.player.uiTempo.load());
        speedBox.setValue (proc.player.uiSpeed.load());
        ledBtn.setToggleState (proc.player.uiLed.load(), juce::dontSendNotification);
    }


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
    PixelFont::drawLeft (g, name, nameArea, tpx, Theme::ink);
    auto rules = title.withLeft (nameArea.getRight() + px * 3).withRight (newBtn.getX() - px * 4.0f).reduced (0, px * 4);
    g.setColour (Theme::ink);
    for (float y = rules.getY(); y < rules.getBottom(); y += px * 2)
        g.fillRect (rules.getX(), y, std::max (0.0f, rules.getWidth()), std::max (1.0f, px * 0.5f));
    const juce::String songName = "\"" + juce::String (proc.song.name) + "\"";
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
    const float base = std::min ((float) getHeight() / 300.0f, (float) getWidth() / 470.0f);
    Theme::px = juce::jlimit (1.5f, 5.0f, std::floor (base * 2.0f) / 2.0f + st.uiZoom * 0.5f);
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };

    auto r = getLocalBounds();

    // title bar buttons (right side)
    auto title = r.removeFromTop ((int) (px * 14)).reduced (gap, (int) (px * 1.5f));
    zoomIn.setBounds (title.removeFromRight (charsW (2) + gap)); title.removeFromRight (gap);
    zoomOut.setBounds (title.removeFromRight (charsW (2) + gap)); title.removeFromRight (gap * 3);
    wavBtn.setBounds (title.removeFromRight (charsW (10))); title.removeFromRight (gap);
    modBtn.setBounds (title.removeFromRight (charsW (8))); title.removeFromRight (gap);
    saveBtn.setBounds (title.removeFromRight (charsW (4))); title.removeFromRight (gap);
    loadBtn.setBounds (title.removeFromRight (charsW (4))); title.removeFromRight (gap);
    newBtn.setBounds (title.removeFromRight (charsW (3)));

    r.removeFromTop (gap);

    // transport row
    auto tr = r.removeFromTop (rowH).reduced (gap, 0);
    playSongBtn.setBounds (tr.removeFromLeft (charsW (6))); tr.removeFromLeft (gap);
    playBlockBtn.setBounds (tr.removeFromLeft (charsW (7))); tr.removeFromLeft (gap);
    stopBtn.setBounds (tr.removeFromLeft (charsW (6))); tr.removeFromLeft (gap);
    editBtn.setBounds (tr.removeFromLeft (charsW (6))); tr.removeFromLeft (gap * 4);
    tempoBox.setBounds (tr.removeFromLeft (tempoBox.preferredWidth (px))); tr.removeFromLeft (gap * 3);
    speedBox.setBounds (tr.removeFromLeft (speedBox.preferredWidth (px))); tr.removeFromLeft (gap * 3);
    octBox.setBounds (tr.removeFromLeft (octBox.preferredWidth (px))); tr.removeFromLeft (gap * 3);
    stepBox.setBounds (tr.removeFromLeft (stepBox.preferredWidth (px)));
    posLabel.setVisible (false);

    r.removeFromTop (gap);

    // bottom strips
    auto status = r.removeFromBottom (rowH).reduced (gap, 0);
    r.removeFromBottom (gap);
    auto paula = r.removeFromBottom (rowH).reduced (gap, 0);
    r.removeFromBottom (gap);

    tracksBtn.setBounds (status.removeFromLeft (charsW (5))); status.removeFromLeft (gap);
    viewBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    followBtn.setBounds (status.removeFromLeft (charsW (6))); status.removeFromLeft (gap);
    panelsBtn.setBounds (status.removeFromLeft (charsW (6))); status.removeFromLeft (gap);
    nameBtn.setBounds (status.removeFromLeft (charsW (4))); status.removeFromLeft (gap);
    skinBtn.setBounds (status.removeFromLeft (charsW (4))); status.removeFromLeft (gap);
    specBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    if (! proc.isRunningStandalone())
    {
        hostPlayBtn.setBounds (status.removeFromLeft (charsW (9))); status.removeFromLeft (gap);
        hostTempoBtn.setBounds (status.removeFromLeft (charsW (8))); status.removeFromLeft (gap);
    }
    status.removeFromLeft (gap * 2);
    ledLabel.setVisible (false);
    statusLabel.setBounds (status);

    a500Btn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    a1200Btn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    cleanBtn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap * 3);
    steppedBtn.setBounds (paula.removeFromLeft (charsW (7))); paula.removeFromLeft (gap);
    eightBitBtn.setBounds (paula.removeFromLeft (charsW (5))); paula.removeFromLeft (gap);
    crunchBtn.setBounds (paula.removeFromLeft (charsW (6))); paula.removeFromLeft (gap);
    ledBtn.setBounds (paula.removeFromLeft (charsW (4))); paula.removeFromLeft (gap * 3);
    const int sliderW = (paula.getWidth() - gap * 2) / 2;
    sepSlider.setBounds (paula.removeFromLeft (sliderW)); paula.removeFromLeft (gap * 2);
    volSlider.setBounds (paula);

    // main area
    auto main = r.reduced (gap, 0);
    sequence.setVisible (showPanels);
    blockPanel.setVisible (showPanels);
    instruments.setVisible (showPanels);
    if (showPanels)
    {
        auto left = main.removeFromLeft (std::min (charsW (32), main.getWidth() / 2));
        main.removeFromLeft (gap * 2);
        const int seqH = std::max (rowH * 5, (int) (left.getHeight() * 0.24f));
        sequence.setBounds (left.removeFromTop (seqH));
        left.removeFromTop (gap);
        blockPanel.setBounds (left.removeFromTop ((int) (px * 14) + rowH * 3 + gap * 3));
        left.removeFromTop (gap);
        instruments.setBounds (left);
    }
    spectrum.setVisible (st.showSpectrum);
    if (st.showSpectrum)
    {
        spectrum.setBounds (main.removeFromBottom ((int) (px * 22)));
        main.removeFromBottom (gap);
    }
    pattern.setBounds (main);
    sampleEditor.setBounds (main);
    browser.setBounds (main);
}

bool PaulaEditor::keyPressed (const juce::KeyPress& k)
{
    // keys that land on the window (e.g. after clicking a button) go to the active view
    switch (proc.editorState.view)
    {
        case View::SampleEdit: return sampleEditor.keyPressed (k);
        case View::Browser:    return browser.keyPressed (k);
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
            safe->proc.replaceSong (std::move (s));
            auto& st = safe->proc.editorState;
            st.block = 0; st.line = 0; st.seqPos = 0; st.track = 0;
            safe->currentFile = juce::File();
            safe->setStatus ("NEW SONG - INSTRUMENTS KEPT (CLEAR THEM IN THE INSTRUMENT PANEL)");
            safe->refreshAll();
            safe->repaint();
        }));
}

void PaulaEditor::loadSong()
{
    auto& st = proc.editorState;
    juce::File start = st.lastFolder.isNotEmpty() ? juce::File (st.lastFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Load a song or module", start, "*.prv;*.mod;*.med;*.mmd0;*.mmd1;*");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto file = fc.getResult();
        if (! file.existsAsFile()) return;
        auto& self = *safe;
        self.proc.editorState.lastFolder = file.getParentDirectory().getFullPathName();
        prv::Song s;
        juce::String err;
        juce::StringArray notes;
        if (! prv::loadSongFile (file, s, err, notes)) { self.setStatus ("ERROR: " + err); return; }
        self.proc.replaceSong (std::move (s));
        auto& st2 = self.proc.editorState;
        st2.block = self.proc.song.safeBlock (self.proc.song.sequence[0]); st2.line = 0; st2.seqPos = 0; st2.track = 0;
        self.currentFile = file.hasFileExtension ("prv") ? file : juce::File();
        self.setStatus ("LOADED " + file.getFileName() + (notes.isEmpty() ? juce::String() : "  -  " + notes.joinIntoString ("  ")));
        self.refreshAll();
        self.repaint();
    });
}

void PaulaEditor::saveSong()
{
    auto& st = proc.editorState;
    juce::File start = currentFile != juce::File() ? currentFile
                     : (st.lastFolder.isNotEmpty() ? juce::File (st.lastFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory))
                           .getChildFile (juce::File::createLegalFileName (juce::String (proc.song.name).toLowerCase()) + ".prv");
    chooser = std::make_unique<juce::FileChooser> ("Save song", start, "*.prv");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension ("prv")) file = file.withFileExtension ("prv");
        auto& self = *safe;
        juce::MemoryBlock data;
        self.proc.withLock ([&] { data = prv::writeSong (self.proc.song); });
        if (file.replaceWithData (data.getData(), data.getSize()))
        {
            self.currentFile = file;
            self.proc.editorState.lastFolder = file.getParentDirectory().getFullPathName();
            self.setStatus ("SAVED " + file.getFileName());
        }
        else self.setStatus ("ERROR: COULDN'T WRITE " + file.getFileName());
    });
}

void PaulaEditor::exportMod()
{
    auto& st = proc.editorState;
    juce::File start = (st.lastFolder.isNotEmpty() ? juce::File (st.lastFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory))
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
    auto& st = proc.editorState;
    juce::File start = (st.lastFolder.isNotEmpty() ? juce::File (st.lastFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory))
                           .getChildFile (juce::File::createLegalFileName (juce::String (proc.song.name).toLowerCase()) + ".wav");
    chooser = std::make_unique<juce::FileChooser> ("Render song to WAV", start, "*.wav");
    juce::Component::SafePointer<PaulaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (! file.hasFileExtension ("wav")) file = file.withFileExtension ("wav");
        auto& self = *safe;
        prv::Song copy;
        prv::PaulaOptions opts;
        self.proc.withLock ([&] { copy = self.proc.song; opts = self.proc.player.options; });
        juce::String err;
        const double rate = self.proc.getSampleRate() > 0 ? self.proc.getSampleRate() : 48000.0;
        if (prv::renderSongToWav (copy, opts, rate, file, err)) self.setStatus ("RENDERED " + file.getFileName());
        else self.setStatus ("ERROR: " + err);
    });
}
