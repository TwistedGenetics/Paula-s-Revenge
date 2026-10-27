// Paula's Revenge - sample browser that auditions files as you move through them
#include "SampleBrowser.h"
#include "../SongFile.h"

namespace prv::ui
{
bool SampleBrowser::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;8svx;iff;svx");
}

SampleBrowser::SampleBrowser (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    list.getNumRows = [this] { return (int) entries.size(); };
    list.getRowText = [this] (int r) { return entries[(size_t) r].label; };
    list.isRowDim   = [] (int) { return false; };
    list.onClick = [this] (int r, const juce::MouseEvent&) { grabKeyboardFocus(); selectRow (r, true); };
    list.onDoubleClick = [this] (int r)
    {
        selected = r;
        if (entries[(size_t) r].isDir) setDirectory (entries[(size_t) r].file);
        else loadSelected();
    };
    addAndMakeVisible (list);

    auto go = [this] (juce::File::SpecialLocationType t) { setDirectory (juce::File::getSpecialLocation (t)); };
    upBtn.onClick        = [this] { if (dir.getParentDirectory() != dir) setDirectory (dir.getParentDirectory()); };
    homeBtn.onClick      = [go] { go (juce::File::userHomeDirectory); };
    musicBtn.onClick     = [go] { go (juce::File::userMusicDirectory); };
    desktopBtn.onClick   = [go] { go (juce::File::userDesktopDirectory); };
    docsBtn.onClick      = [go] { go (juce::File::userDocumentsDirectory); };
    downloadsBtn.onClick = [this] { setDirectory (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads")); };
    volumesBtn.onClick   = [this]
    {
       #if JUCE_MAC
        setDirectory (juce::File ("/Volumes"));
       #else
        setDirectory (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getParentDirectory());
       #endif
    };
    finderBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder or sample", dir, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.8svx;*.iff;*.svx");
        juce::Component::SafePointer<SampleBrowser> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f.isDirectory()) safe->setDirectory (f);
            else if (f.existsAsFile())
            {
                safe->setDirectory (f.getParentDirectory());
                for (int i = 0; i < (int) safe->entries.size(); ++i)
                    if (safe->entries[(size_t) i].file == f) { safe->selectRow (i, true); break; }
            }
        });
    };

    playBtn.onClick   = [this] { if (selected < (int) entries.size() && ! entries[(size_t) selected].isDir) audition (entries[(size_t) selected].file); };
    stopBtn.onClick   = [this] { stopAudition(); };
    loadBtn.onClick   = [this] { loadSelected(); };
    cancelBtn.onClick = [this] { stopAudition(); if (onClose) onClose(); };
    autoBtn.setToggleState (true, juce::dontSendNotification);
    volSlider.labelChars = 4;
    volSlider.setValue (proc.previewGain.load());
    volSlider.defaultValue = 0.8f;
    volSlider.onChange = [this] (float v) { proc.previewGain = v; };

    for (auto* c : std::initializer_list<juce::Component*> { &upBtn, &homeBtn, &musicBtn, &desktopBtn, &downloadsBtn, &docsBtn, &volumesBtn, &finderBtn,
                                                             &playBtn, &stopBtn, &autoBtn, &loadBtn, &cancelBtn, &volSlider, &keepRateBtn })
        addAndMakeVisible (c);
}

SampleBrowser::~SampleBrowser() { stopAudition(); }

void SampleBrowser::opened()
{
    const auto& st = proc.editorState;
    juce::File start = st.lastFolder.isNotEmpty() ? juce::File (st.lastFolder) : juce::File();
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    if (start != dir || entries.empty()) setDirectory (start);
    updateLoadLabel();
    grabKeyboardFocus();
}

void SampleBrowser::updateLoadLabel()
{
    loadBtn.setButtonText ("LOAD INTO " + juce::String::toHexString (proc.editorState.instrument + 1).toUpperCase().paddedLeft ('0', 2));
}

void SampleBrowser::setDirectory (const juce::File& d)
{
    if (! d.isDirectory()) { if (onStatus) onStatus ("CAN'T OPEN " + d.getFullPathName()); return; }
    const auto previous = dir;
    dir = d;
    proc.editorState.lastFolder = d.getFullPathName();
    entries.clear();

    juce::Array<juce::File> dirs, files;
    for (const auto& e : juce::RangedDirectoryIterator (d, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
    {
        const auto f = e.getFile();
        if (f.getFileName().startsWithChar ('.')) continue;
        if (e.isDirectory()) { if (! f.hasFileExtension ("app;vst3;component;bundle")) dirs.add (f); }
        else if (isAudioFile (f)) files.add (f);
    }
    auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
    std::sort (dirs.begin(), dirs.end(), byName);
    std::sort (files.begin(), files.end(), byName);

    if (d.getParentDirectory() != d) entries.push_back ({ d.getParentDirectory(), true, "/.. (UP)" });
    for (auto& f : dirs) entries.push_back ({ f, true, "/" + f.getFileName() });
    for (auto& f : files)
    {
        const auto kb = f.getSize() / 1024;
        const juce::String size = kb >= 1024 ? juce::String (kb / 1024.0, 1) + "MB" : juce::String (kb) + "KB";
        entries.push_back ({ f, false, f.getFileName() + "  " + size });
    }

    selected = 0;
    // when going up, land on the folder we came from
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].isDir && entries[(size_t) i].file == previous) { selected = i; break; }
    list.selected = selected;
    list.ensureVisible (selected);
    repaint();
}

void SampleBrowser::selectRow (int row, bool doAudition)
{
    if (entries.empty()) return;
    selected = juce::jlimit (0, (int) entries.size() - 1, row);
    list.selected = selected;
    list.ensureVisible (selected);
    const auto& e = entries[(size_t) selected];
    if (! e.isDir && doAudition && autoBtn.getToggleState()) audition (e.file);
    else if (e.isDir) { info = "FOLDER"; peaks.clear(); }
    repaint();
}

void SampleBrowser::audition (const juce::File& f)
{
    std::vector<float> L, R;
    double rate = 0;
    Instrument meta;
    juce::String err;
    if (! decodeAudioFile (f, L, R, rate, meta, err)) { info = "CAN'T READ THIS FILE"; peaks.clear(); stopAudition(); repaint(); return; }

    info = juce::String (juce::roundToInt (rate)) + " HZ  " + (R.empty() ? "MONO" : "STEREO") + "  "
         + juce::String (L.size() / rate, 2) + " SEC";
    // peaks for the little waveform
    const int cols = std::max (64, (int) waveArea.getWidth());
    peaks.assign ((size_t) cols, { 0.0f, 0.0f });
    for (int x = 0; x < cols; ++x)
    {
        const size_t s0 = (size_t) ((int64_t) x * (int64_t) L.size() / cols);
        const size_t s1 = std::max (s0 + 1, (size_t) ((int64_t) (x + 1) * (int64_t) L.size() / cols));
        float lo = 0, hi = 0;
        for (size_t i = s0; i < s1 && i < L.size(); ++i)
        {
            const float v = R.empty() ? L[i] : 0.5f * (L[i] + R[i]);
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        peaks[(size_t) x] = { lo, hi };
    }
    auditionLength = (int) L.size();

    auto audio = std::make_unique<PaulaProcessor::PreviewAudio>();
    audio->left = std::move (L);
    audio->right = std::move (R);
    audio->rate = rate;
    auto old = proc.startPreview (std::move (audio));
    old.reset();
    auditioning = true;
    auditionFile = f;
    repaint();
}

void SampleBrowser::stopAudition()
{
    if (auditioning) { auto old = proc.stopPreview(); old.reset(); }
    auditioning = false;
    repaint();
}

bool SampleBrowser::loadSelected()
{
    if (selected >= (int) entries.size()) return false;
    const auto& e = entries[(size_t) selected];
    if (e.isDir) { setDirectory (e.file); return false; }
    Instrument in;
    juce::String err;
    if (! importSample (e.file, in, err, keepRateBtn.getToggleState())) { if (onStatus) onStatus ("ERROR: " + err); return false; }
    stopAudition();
    const int idx = proc.editorState.instrument;
    proc.withLock ([&] { proc.player.jamAllOff(); std::swap (proc.song.instruments[(size_t) idx], in); });
    if (onStatus) onStatus ("LOADED " + e.file.getFileName().toUpperCase() + " INTO "
                            + juce::String::toHexString (idx + 1).toUpperCase().paddedLeft ('0', 2)
                            + (proc.song.instruments[(size_t) idx].isStereo() ? " (STEREO)" : ""));
    if (onLoaded) onLoaded();
    return true;
}

void SampleBrowser::tick()
{
    if (auditioning)
    {
        if (proc.previewPos.load() < 0) auditioning = false;
        repaint (waveArea.toNearestInt().expanded (2));
    }
    updateLoadLabel();
}

// ------------------------------------------------------------------ layout & paint
void SampleBrowser::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    auto r = getLocalBounds().reduced (gap);

    auto layoutRow = [&] (juce::Rectangle<int> row, std::initializer_list<juce::Component*> comps)
    {
        const int n = (int) comps.size();
        const int w = (row.getWidth() - gap * (n - 1)) / n;
        for (auto* c : comps) { c->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
    };
    layoutRow (r.removeFromTop (rowH), { &upBtn, &homeBtn, &musicBtn, &desktopBtn });
    r.removeFromTop (gap);
    layoutRow (r.removeFromTop (rowH), { &downloadsBtn, &docsBtn, &volumesBtn, &finderBtn });
    r.removeFromTop (gap);
    pathArea = r.removeFromTop (rowH).toFloat();
    r.removeFromTop (gap);

    auto bottom2 = r.removeFromBottom (rowH);
    r.removeFromBottom (gap);
    auto bottom1 = r.removeFromBottom (rowH);
    {
        const int w = (bottom1.getWidth() - gap * 3) / 5;
        playBtn.setBounds (bottom1.removeFromLeft (w)); bottom1.removeFromLeft (gap);
        stopBtn.setBounds (bottom1.removeFromLeft (w)); bottom1.removeFromLeft (gap);
        autoBtn.setBounds (bottom1.removeFromLeft (w)); bottom1.removeFromLeft (gap);
        volSlider.setBounds (bottom1);
    }
    cancelBtn.setBounds (bottom2.removeFromRight (bottom2.getWidth() / 5));
    bottom2.removeFromRight (gap);
    keepRateBtn.setBounds (bottom2.removeFromRight (bottom2.getWidth() / 3));
    bottom2.removeFromRight (gap);
    loadBtn.setBounds (bottom2);
    r.removeFromBottom (gap * 2);
    waveArea = r.removeFromBottom ((int) (px * 22)).toFloat();
    r.removeFromBottom (gap);
    infoArea = r.removeFromBottom (rowH).toFloat();
    r.removeFromBottom (gap);
    list.setBounds (r);
}

void SampleBrowser::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    g.fillAll (Theme::panel);

    // current folder (cut from the left if it's long)
    juce::String path = dir.getFullPathName();
    const int maxChars = std::max (8, (int) (pathArea.getWidth() / (px * PixelFont::kCellW)));
    if (path.length() > maxChars) path = "..." + path.getLastCharacters (maxChars - 3);
    PixelFont::drawLeft (g, path, pathArea.withTrimmedLeft (px), px, Theme::ink);

    juce::String line = info.isEmpty() ? juce::String ("UP/DOWN TO BROWSE - ENTER TO LOAD - SPACE PLAYS") : info;
    if (auditioning && auditionFile.exists()) line = auditionFile.getFileName() + "   " + info;
    float ip = px;
    while (ip > 1.5f && PixelFont::textWidth (line, ip) > infoArea.getWidth()) ip -= 0.5f;
    PixelFont::drawLeft (g, line, infoArea.withTrimmedLeft (px), ip, Theme::ink);

    // waveform of the auditioned file with a playhead
    g.setColour (sk.bg);
    g.fillRect (waveArea);
    if (! peaks.empty())
    {
        const float mid = waveArea.getCentreY(), half = waveArea.getHeight() * 0.45f;
        const float colW = waveArea.getWidth() / (float) peaks.size();
        g.setColour (sk.track[0]);
        for (size_t x = 0; x < peaks.size(); ++x)
        {
            const float y1 = mid - peaks[x].second * half, y2 = mid - peaks[x].first * half;
            g.fillRect (waveArea.getX() + x * colW, y1, std::max (1.0f, colW), std::max (1.0f, y2 - y1));
        }
        const int pos = proc.previewPos.load();
        if (auditioning && pos >= 0 && auditionLength > 0)
        {
            g.setColour (sk.hot);
            g.fillRect (waveArea.getX() + waveArea.getWidth() * pos / (float) auditionLength - 1.0f, waveArea.getY(), 2.0f, waveArea.getHeight());
        }
    }
    else PixelFont::drawCentred (g, "SELECT A SAMPLE TO HEAR IT", waveArea, px, sk.dim);
    Theme::bevel (g, waveArea, false, std::max (1.0f, px * 0.5f));

    if (hasKeyboardFocus (true))
    {
        g.setColour (Theme::ink);
        g.drawRect (getLocalBounds().toFloat(), std::max (2.0f, px * 0.5f));
    }
}

void SampleBrowser::mouseDown (const juce::MouseEvent&) { grabKeyboardFocus(); }

bool SampleBrowser::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::upKey)       { selectRow (selected - 1, true); return true; }
    if (code == juce::KeyPress::downKey)     { selectRow (selected + 1, true); return true; }
    if (code == juce::KeyPress::pageUpKey)   { selectRow (selected - 16, true); return true; }
    if (code == juce::KeyPress::pageDownKey) { selectRow (selected + 16, true); return true; }
    if (code == juce::KeyPress::homeKey)     { selectRow (0, true); return true; }
    if (code == juce::KeyPress::endKey)      { selectRow ((int) entries.size() - 1, true); return true; }
    if (code == juce::KeyPress::rightKey)
    {
        if (selected < (int) entries.size() && entries[(size_t) selected].isDir) setDirectory (entries[(size_t) selected].file);
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::backspaceKey) { upBtn.triggerClick(); return true; }
    if (code == juce::KeyPress::returnKey)
    {
        if (selected < (int) entries.size())
        {
            if (entries[(size_t) selected].isDir) setDirectory (entries[(size_t) selected].file);
            else loadSelected();
        }
        return true;
    }
    if (code == juce::KeyPress::spaceKey)
    {
        if (auditioning) stopAudition();
        else playBtn.triggerClick();
        return true;
    }
    if (code == juce::KeyPress::escapeKey) { cancelBtn.triggerClick(); return true; }
    // type a letter to jump to the next entry starting with it
    const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    if (ch > ' ' && ! k.getModifiers().isCommandDown())
    {
        for (int n = 1; n <= (int) entries.size(); ++n)
        {
            const int i = (selected + n) % (int) entries.size();
            const auto name = entries[(size_t) i].file.getFileName().toUpperCase();
            if (name.isNotEmpty() && name[0] == ch) { selectRow (i, true); return true; }
        }
        return true;
    }
    return false;
}
} // namespace prv::ui
