// Paula's Revenge - a built-in synth's page
#include "EnginePage.h"
#include <optional>
#include "Panels.h"
#include <limits>
#include "ChoiceBox.h"
#include "SynthPresets.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    std::vector<juce::String> choiceList (const SynthParam& q)
    {
        std::vector<juce::String> out;
        for (auto s : juce::StringArray::fromTokens (q.choices, "|", "")) out.push_back (s);
        return out;
    }
    // (v1.01 II) which controls are faders on a hardware panel: envelopes, and the 80s mono's sliders
    bool faderFor (const std::string& key, const char* section, const char* name)
    {
        const juce::String sec (section), nm (name);
        const auto box = sec.fromLastOccurrenceOf (">", false, false);
        const bool adsr = nm == "ATTACK" || nm == "DECAY" || nm == "SUSTAIN" || nm == "RELEASE" || nm == "DELAY" || nm == "A" || nm == "D" || nm == "S" || nm == "R";
        if (adsr && (box.contains ("ENV") || box.endsWith (" EG")) && key != kAcidKey) return true;
        if (key == kTg101Key && (box == "SOURCE MIXER" || box == "VCF" || nm == "VIBRATO" || nm == "PW")) return true;
        return false;
    }
}

EnginePage::EnginePage (PaulaProcessor& p) : proc (p)
{
    Theme::markDarkPage (*this);
    setWantsKeyboardFocus (true);
    setOpaque (true);
    playBtn.onStateChange = [this]
    {
        const bool down = playBtn.isDown();
        if (down == playingHeld) return;
        playingHeld = down;
        const int note = proc.editorState.octave * 12 + 1;
        proc.withLock ([&] { if (down) proc.player.jamNoteOn (note, proc.editorState.instrument); else proc.player.jamNoteOff (note); });
    };
    optionsBtn.onClick = [this] { if (onOptions) onOptions(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    presetsBtn.onClick = [this] { showPresets(); };
    for (auto* c : { &playBtn, &optionsBtn, &closeBtn, &presetsBtn }) addAndMakeVisible (c);
    for (int h = 0; h < 3; ++h)
    {
        drumHalf[h].setClickingTogglesState (false);
        drumHalf[h].onClick = [this, h] { drumHalfShown = h; resized(); repaint(); };
        addChildComponent (drumHalf[h]);
    }
    loadSampleBtn.onClick = [this] { if (onLoadSample) onLoadSample(); };
    addChildComponent (loadSampleBtn);
    for (int l = 0; l < 4; ++l)
    {
        padLoadBtn[l].onClick = [this, l] { if (onLoadPadSample) onLoadPadSample (l); };
        addChildComponent (padLoadBtn[l]);
    }
    for (int t = 0; t < kRomTones; ++t)
    {
        toneBtn[t].onClick = [this, t] { openRomBrowser (t); };
        sampleBtn[t].onClick = [this, t] { pickWavs (t); };
        addChildComponent (toneBtn[t]);
        addChildComponent (sampleBtn[t]);
    }
    for (int k = 0; k < 3; ++k)
    {
        pageBtn[k].setClickingTogglesState (false);
        pageBtn[k].onClick = [this, k] { closeRomBrowser (true); showPage (k == 2 ? 3 : k); };
        addChildComponent (pageBtn[k]);
    }
    romFolderBtn.onClick = [this] { showRomFolderMenu(); };
    addChildComponent (romFolderBtn);
    // the ROM browser: categories, then the sounds in the one picked
    romCats = std::make_unique<PixList>();
    romSounds = std::make_unique<PixList>();
    romCats->getNumRows = [this] { return (int) romCatNames.size(); };
    romCats->getRowText = [this] (int r)
    {
        if (r < 0 || r >= (int) romCatNames.size()) return juce::String();
        const auto& lib = romLibrary();
        const auto it = lib.find (romCatNames[(size_t) r]);
        return romCatNames[(size_t) r] + " (" + juce::String (it == lib.end() ? 0 : (int) it->second.size()) + ")";
    };
    romCats->onClick = [this] (int r, const juce::MouseEvent&) { romPickCategory (r); };
    romSounds->getNumRows = [this] { return (int) romSoundsNow.size(); };
    romSounds->getRowText = [this] (int r)
    {
        if (r < 0 || r >= (int) romSoundsNow.size()) return juce::String();
        const auto& s = romSoundsNow[(size_t) r];
        return s.first + (s.second.size() > 1 ? " (" + juce::String (s.second.size()) + ")" : juce::String());
    };
    romSounds->onClick = [this] (int r, const juce::MouseEvent&) { romPickSound (r); };
    romSounds->onDoubleClick = [this] (int r) { romPickSound (r); closeRomBrowser (true); };
    romKeepBtn.onClick = [this] { closeRomBrowser (true); };
    romCancelBtn.onClick = [this] { closeRomBrowser (false); };
    romEmptyBtn.onClick = [this] { const int t = browsingTone; closeRomBrowser (true); emptyTone (t); };
    romAgainBtn.onClick = [this]
    {
        romLibrary (true);
        const int t = browsingTone;
        browsingTone = -1;
        openRomBrowser (t);
        if (onStatus) onStatus ("ROM FOLDER LOOKED THROUGH AGAIN");
    };
    for (juce::Component* c : { (juce::Component*) romCats.get(), (juce::Component*) romSounds.get(), (juce::Component*) &romKeepBtn,
                                (juce::Component*) &romCancelBtn, (juce::Component*) &romEmptyBtn, (juce::Component*) &romAgainBtn })
        addChildComponent (c);
}

EnginePage::~EnginePage()
{
    proc.withLock ([&] { proc.player.jamAllOff(); });
}

Instrument& EnginePage::inst()
{
    return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
}

float EnginePage::valueOf (int param) const
{
    const auto v = readBuiltinState (proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)]);
    return param >= 0 && param < (int) v.size() ? v[(size_t) param] : 0.0f;
}

void EnginePage::edit (int param, float value, bool merge)
{
    const auto& ps = builtinParams (inst().pluginKey);
    if (param < 0 || param >= (int) ps.size()) return;
    if (onTouched) onTouched (kAutoInstrumentBase + proc.editorState.instrument, param);   // (v0.92) the AUTOMATION list follows
    const auto& q = ps[(size_t) param];
    value = juce::jlimit (std::min (q.min, q.max), std::max (q.min, q.max), value);
    proc.history.instrument (proc.editorState.instrument, juce::String (inst().pluginName) + ": " + q.name, merge);
    proc.withLock ([&]
    {
        auto v = readBuiltinState (inst());
        v[(size_t) param] = value;
        writeBuiltinState (inst(), v);
    });
    if (onStatus) onStatus (juce::String (inst().pluginName) + " " + q.name + ": " + juce::String (q.text (value)));
    sync();
    if ((inst().pluginKey == kSubKey && param == sub::BeatOn) || (inst().pluginKey == kGrainKey && param == grain::Source)
        || (inst().pluginKey == kPadKey && param < pad::SampleBase && param % pad::PerLayer == pad::Wave)) resized();   // (rows come and go)
    if (onChanged) onChanged();
}

void EnginePage::setValue (int param, float value) { edit (param, value, false); }

// ---- ROM's tones
juce::File EnginePage::romFolder()
{
    const auto p = prefs().getValue ("romFolder");
    return p.isEmpty() ? juce::File() : juce::File (p);
}

void EnginePage::setRomFolder (const juce::File& f)
{
    prefs().setValue ("romFolder", f.getFullPathName());
    prefs().saveIfNeeded();
    romLibrary (true);
}

const EnginePage::RomLibrary& EnginePage::romLibrary (bool rescan)
{
    static RomLibrary lib;
    static juce::String scanned;
    const auto dir = romFolder();
    if (rescan || scanned != dir.getFullPathName())
    {
        lib.clear();
        scanned = dir.getFullPathName();
        if (dir.isDirectory())
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV"))
            {
                const auto stem = f.getFileNameWithoutExtension().toStdString();
                const juce::String cat (romCategoryOf (stem)), grp (romGroupOf (stem));
                lib[cat][grp].add (f);
            }
    }
    return lib;
}

void EnginePage::updateToneButtons()
{
    const auto& rb = inst().rom;
    for (int t = 0; t < kRomTones; ++t)
    {
        const int n = rb != nullptr ? rb->zonesOf (t) : 0;
        // (v0.79: ROM... - a sound from the folder, the browser; the tone's sound is named in its box's title)
        juce::ignoreUnused (n);
        toneBtn[t].setButtonText ("ROM...");
    }
}

bool EnginePage::loadTone (int t, const juce::Array<juce::File>& wavs, const juce::String& name, bool undoable)
{
    if (t < 0 || t >= kRomTones || inst().pluginKey != kRomKey) return false;
    std::vector<RomZone> zones;
    juce::String firstError;
    for (const auto& f : wavs)
    {
        juce::MemoryBlock mb;
        RomZone z; std::string err;
        if (! f.loadFileAsData (mb) || ! romReadWav ((const uint8_t*) mb.getData(), mb.getSize(), z, err))
        { if (firstError.isEmpty()) firstError = f.getFileName().toUpperCase() + ": " + juce::String (err.empty() ? "COULDN'T READ IT" : err); continue; }
        z.name = f.getFileNameWithoutExtension().toUpperCase().toStdString();
        zones.push_back (std::move (z));
    }
    if (zones.empty()) { if (onStatus) onStatus ("NOTHING LOADED - " + firstError); return false; }
    const int count = (int) zones.size();
    int looped = 0;
    for (const auto& z : zones) looped += z.loops() ? 1 : 0;
    if (undoable) proc.history.instrument (proc.editorState.instrument, "ROM TONE " + juce::String (t + 1) + ": " + name);
    proc.withLock ([&] { inst().rom = romWithTone (inst().rom, t, std::move (zones), name.toUpperCase().toStdString()); });
    resized();
    if (onStatus) onStatus ("TONE " + juce::String (t + 1) + ": " + name.toUpperCase() + " - " + juce::String (count) + (count == 1 ? " SAMPLE" : " SAMPLES")
                            + ", " + juce::String (looped) + " LOOPED" + (firstError.isNotEmpty() ? " (SOME COULDN'T BE READ: " + firstError + ")" : juce::String()));
    if (onChanged) onChanged();
    return true;
}

void EnginePage::emptyTone (int t)
{
    if (inst().rom == nullptr || inst().rom->zonesOf (t) == 0) return;
    proc.history.instrument (proc.editorState.instrument, "ROM TONE " + juce::String (t + 1) + ": EMPTY");
    proc.withLock ([&] { inst().rom = romWithTone (inst().rom, t, {}, ""); });
    resized();
    if (onStatus) onStatus ("TONE " + juce::String (t + 1) + " EMPTIED");
    if (onChanged) onChanged();
}

// ROM's SAMPLE...: one sound's WAV files, picked
void EnginePage::pickWavs (int t)
{
    juce::Component::SafePointer<EnginePage> safe (this);
    closeRomBrowser (true);
    chooser = std::make_unique<juce::FileChooser> ("TONE " + juce::String (t + 1) + ": pick one sound's WAV files (its multisamples)", romFolder().isDirectory() ? romFolder() : juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe, t] (const juce::FileChooser& fc)
    {
        const auto res = fc.getResults();
        if (safe != nullptr && ! res.isEmpty())
            safe->loadTone (t, res, juce::String (romGroupOf (res[0].getFileNameWithoutExtension().toStdString())));
    });
}

// ROM FOLDER... (beside the page tabs): set it, look through it again
void EnginePage::showRomFolderMenu()
{
    juce::Component::SafePointer<EnginePage> safe (this);
    const auto dir = romFolder();
    std::vector<MenuItem> items;
    items.push_back ({ dir.isDirectory() ? "CHANGE THE ROM FOLDER..." : "SET THE ROM FOLDER (YOUR SAMPLES)...", [safe]
    {
        if (safe == nullptr) return;
        safe->chooser = std::make_unique<juce::FileChooser> ("Your samples' folder (ROM lists its WAVs by category and name)", romFolder());
        safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc)
        {
            if (! fc.getResult().isDirectory()) return;
            setRomFolder (fc.getResult());
            int n = 0; for (const auto& c : romLibrary()) n += (int) c.second.size();
            if (safe != nullptr && safe->onStatus) safe->onStatus ("ROM FOLDER: " + fc.getResult().getFileName().toUpperCase() + " - " + juce::String (n) + " SOUNDS IN " + juce::String ((int) romLibrary().size()) + " CATEGORIES");
            if (safe != nullptr && safe->romBrowserOpen()) { const int t = safe->browsingTone; safe->browsingTone = -1; safe->openRomBrowser (t); }
        });
    } });
    if (dir.isDirectory())
        items.push_back ({ "LOOK THROUGH IT AGAIN (NEW FILES)", [safe] { romLibrary (true); if (safe != nullptr && safe->onStatus) safe->onStatus ("ROM FOLDER LOOKED THROUGH AGAIN"); } });
    int n = 0; for (const auto& c : romLibrary()) n += (int) c.second.size();
    showMenu (dir.isDirectory() ? "ROM FOLDER: " + dir.getFileName().toUpperCase() + " (" + juce::String (n) + " SOUNDS)" : juce::String ("NO ROM FOLDER YET"), std::move (items));
}

// the ROM browser
void EnginePage::openRomBrowser (int t)
{
    if (t < 0 || t >= kRomTones || shownKey != kRomKey) return;
    if (browsingTone >= 0 && browsingTone != t) closeRomBrowser (true);
    const bool fresh = browsingTone < 0;
    browsingTone = t;
    if (fresh) { browseBefore = inst().rom; browseRecorded = false; }
    const auto& lib = romLibrary();
    romCatNames.clear();
    for (const auto& kv : lib) romCatNames.push_back (kv.first);
    if (romCatNames.empty())
    {
        browsingTone = -1;
        if (! romFolder().isDirectory()) { if (onStatus) onStatus ("ROM: SET THE ROM FOLDER FIRST (ROM FOLDER... AT THE TOP) - OR SAMPLE... FOR WAV FILES"); showRomFolderMenu(); }
        else if (onStatus) onStatus ("ROM: NO WAVS IN THE ROM FOLDER - SAMPLE... PICKS WAV FILES");
        return;
    }
    // start where the tone's sound is (its category and name), else the first
    int cat = 0, snd = -1;
    const juce::String now = inst().rom != nullptr ? juce::String (inst().rom->toneName[(size_t) t]).toUpperCase() : juce::String();
    for (int c = 0; c < (int) romCatNames.size() && snd < 0; ++c)
    {
        int k = 0;
        for (const auto& g : lib.at (romCatNames[(size_t) c]))
        {
            if (now.isNotEmpty() && g.first.toUpperCase() == now) { cat = c; snd = k; break; }
            ++k;
        }
    }
    romCat = -1;
    romPickCategory (cat);
    romSound = snd;
    romSounds->selected = snd;
    if (snd >= 0) romSounds->ensureVisible (snd);
    resized();
    repaint();
    if (onStatus) onStatus ("TONE " + juce::String (t + 1) + ": CLICK A SOUND, PLAY THE KEYS TO HEAR IT - UP / DOWN: THE NEXT ONE, LEFT / RIGHT: CATEGORY, ENTER: KEEP, ESC: CANCEL");
}

void EnginePage::romPickCategory (int c)
{
    if (romCatNames.empty()) return;
    c = juce::jlimit (0, (int) romCatNames.size() - 1, c);
    if (c == romCat) return;
    romCat = c;
    romCats->selected = c;
    romCats->ensureVisible (c);
    romSoundsNow.clear();
    const auto& lib = romLibrary();
    const auto it = lib.find (romCatNames[(size_t) c]);
    if (it != lib.end()) for (const auto& g : it->second) romSoundsNow.push_back ({ g.first, g.second });
    romSound = -1;
    romSounds->selected = -1;
    romSounds->scrollBy (-100000);
    romCats->repaint(); romSounds->repaint();
}

void EnginePage::romPickSound (int s)
{
    if (browsingTone < 0 || romSoundsNow.empty()) return;
    s = juce::jlimit (0, (int) romSoundsNow.size() - 1, s);
    romSound = s;
    romSounds->selected = s;
    romSounds->ensureVisible (s);
    const auto& snd = romSoundsNow[(size_t) s];
    // (one undo for the whole browse: the first sound tried)
    if (loadTone (browsingTone, snd.second, snd.first, ! browseRecorded)) browseRecorded = true;
    romSounds->repaint();
}

void EnginePage::closeRomBrowser (bool keep)
{
    if (browsingTone < 0) return;
    const int t = browsingTone;
    browsingTone = -1;
    if (! keep && browseRecorded)
    {
        auto before = browseBefore;
        proc.withLock ([&] { inst().rom = before; });
        if (onStatus) onStatus ("TONE " + juce::String (t + 1) + ": AS IT WAS");
        if (onChanged) onChanged();
    }
    browseBefore = nullptr;
    for (juce::Component* c : { (juce::Component*) romCats.get(), (juce::Component*) romSounds.get(), (juce::Component*) &romKeepBtn,
                                (juce::Component*) &romCancelBtn, (juce::Component*) &romEmptyBtn, (juce::Component*) &romAgainBtn })
        c->setVisible (false);
    romHeadArea = {};
    resized();
    repaint();
}

// ---- presets
void EnginePage::loadPreset (const std::string& name, const std::vector<float>& values)
{
    const auto key = inst().pluginKey;
    if (builtinParams (key).empty() || values.empty()) return;
    proc.history.instrument (proc.editorState.instrument, juce::String (inst().pluginName) + " PRESET: " + juce::String (name));
    proc.withLock ([&] { writeBuiltinState (inst(), values); });
    sync();
    resized();
    if (onStatus) onStatus (juce::String (inst().pluginName) + " PRESET: " + juce::String (name) + " - UNDO TO GO BACK");
    if (onChanged) onChanged();
}

void EnginePage::savePreset (const juce::String& name)
{
    const auto nm = name.toUpperCase().trim().substring (0, 24);
    if (nm.isEmpty() || builtinParams (inst().pluginKey).empty()) return;
    bool had = false;
    for (const auto& p : synthpresets::mine (inst().pluginKey)) had |= p.name == nm.toStdString();
    synthpresets::save ({ nm.toStdString(), inst().pluginKey, readBuiltinState (inst()) });
    if (onStatus) onStatus ("SAVED AS MY PRESET " + nm + (had ? " (REPLACING THE OLD ONE)" : "") + " - IN PRESETS... FOR ANY " + juce::String (inst().pluginName) + " IN ANY SONG");
}

void EnginePage::showPresets()
{
    const auto key = inst().pluginKey;
    if (builtinParams (key).empty()) return;
    juce::Component::SafePointer<EnginePage> safe (this);
    std::vector<MenuItem> items;
    items.push_back ({ "SAVE THESE SETTINGS AS MY PRESET...", [safe]
    {
        if (safe == nullptr) return;
        auto* w = new juce::AlertWindow ("Save preset", "A name for your " + juce::String (safe->inst().pluginName) + " preset", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", juce::String (safe->inst().name).toUpperCase().substring (0, 24));
        w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (result == 1 && safe != nullptr) safe->savePreset (w->getTextEditorContents ("name"));
        }), true);
    } });
    for (const auto& p : synthpresets::factory (key))
        items.push_back ({ juce::String (p.name), [safe, p] { if (safe != nullptr) safe->loadPreset (p.name, p.values); } });
    const auto my = synthpresets::mine (key);
    for (const auto& p : my)
        items.push_back ({ "MY " + juce::String (p.name), [safe, p] { if (safe != nullptr) safe->loadPreset (p.name, p.values); } });
    if (! my.empty())
        items.push_back ({ "DELETE ONE OF MY PRESETS...", [safe, my, key]
        {
            std::vector<MenuItem> del;
            for (const auto& p : my)
                del.push_back ({ "MY " + juce::String (p.name), [safe, p, key]
                {
                    synthpresets::remove (key, p.name);
                    if (safe != nullptr && safe->onStatus) safe->onStatus ("DELETED MY PRESET " + juce::String (p.name));
                } });
            showMenu ("DELETE WHICH OF MY PRESETS?", std::move (del));
        } });
    showMenu (juce::String (inst().pluginName) + " PRESETS" + (my.empty() ? " (NONE OF YOURS YET)" : ""), std::move (items));
}

juce::Component* EnginePage::controlFor (int param) const
{
    for (const auto& r : rows)
        if (r.param == param)
        {
            if (r.slider) return r.slider.get();
            if (r.number) return r.number.get();
            if (r.cycle) return r.cycle.get();
            if (! r.choices.empty()) return r.choices.front().get();
        }
    return nullptr;
}

void EnginePage::build()
{
    // one row per control, in boxes by section (in the synth's own order)
    for (auto& r : rows) { if (r.slider) removeChildComponent (r.slider.get()); if (r.number) removeChildComponent (r.number.get()); if (r.cycle) removeChildComponent (r.cycle.get()); if (r.lever) removeChildComponent (r.lever.get()); for (auto& b : r.choices) removeChildComponent (b.get()); }
    for (auto& d : drumPads) removeChildComponent (d.get());
    drumPads.clear();
    rows.clear(); boxes.clear();
    for (auto& t : genTabs) removeChildComponent (t.get());
    genTabs.clear(); genTabNames.clear();
    const auto oldKey = shownKey;
    shownKey = inst().pluginKey;
    look = lookFor (shownKey);
    Theme::markPanelLook (*this, look);
    const auto& ps = builtinParams (shownKey);
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        const auto& q = ps[(size_t) i];
        Row r; r.param = i;
        const bool panel = look != Theme::PanelLook::None;
        if (q.kind == 3 && panel && choiceList (q).size() >= 2 && choiceList (q).size() <= 3)
        {
            // (v1.01 II) a hardware panel: a lever, its settings beside it
            juce::StringArray opts;
            for (const auto& nm : choiceList (q)) opts.add (nm);
            r.lever = std::make_unique<LeverSwitch> (q.name, opts);
            r.lever->onPick = [this, i] (int k) { edit (i, (float) k, false); };
            r.lever->onRightClick = [this, i] { knobMenu (i); };
            addAndMakeVisible (*r.lever);
        }
        else if (q.kind == 3 && panel && choiceList (q).size() <= 12)
        {
            // (four to twelve settings: a rotary switch - it clicks round them, the one chosen written under it)
            const auto names = choiceList (q);
            r.slider = std::make_unique<PixSlider> (q.name);
            r.slider->knob = true;
            const int n = (int) names.size();
            r.slider->steps = n;
            r.slider->format = [names, n] (float v01) { return names[(size_t) juce::jlimit (0, n - 1, (int) std::lround (v01 * (float) (n - 1)))]; };
            r.slider->defaultValue = q.to01 (q.def);
            r.slider->onChange = [this, i, n] (float v01) { edit (i, (float) juce::jlimit (0, n - 1, (int) std::lround (v01 * (float) (n - 1))), true); };
            r.slider->onRightClick = [this, i] { knobMenu (i); };
            addAndMakeVisible (*r.slider);
        }
        else if (q.kind == 2 && panel)
        {
            // (a whole number on a hardware panel: a knob that clicks round in steps)
            r.slider = std::make_unique<PixSlider> (q.name);
            r.slider->knob = true;
            r.slider->steps = (int) (q.max - q.min) + 1;
            r.slider->format = [q] (float v01) { return juce::String (q.text ((float) std::lround (q.toValue (v01)))); };
            r.slider->defaultValue = q.to01 (q.def);
            r.slider->onChange = [this, i, q] (float v01) { edit (i, (float) std::lround (q.toValue (v01)), true); };
            r.slider->onRightClick = [this, i] { knobMenu (i); };
            addAndMakeVisible (*r.slider);
        }
        else if (q.kind == 3)
        {
            const auto names = choiceList (q);
            if (names.size() <= 4)
                for (int k = 0; k < (int) names.size(); ++k)
                {
                    auto b = std::make_unique<PixButton> (names[(size_t) k], true);
                    b->setClickingTogglesState (false);
                    b->onClick = [this, i, k] { edit (i, (float) k, false); };
                    b->onRightClick = [this, i] { knobMenu (i); };
                    addAndMakeVisible (*b);
                    r.choices.push_back (std::move (b));
                }
            else
            {
                r.cycle = std::make_unique<PixButton> ("");
                r.cycle->onClick = [this, i, names]
                {
                    juce::Component::SafePointer<EnginePage> safe (this);
                    std::vector<MenuItem> items;
                    for (int k = 0; k < (int) names.size(); ++k)
                        items.push_back ({ (std::lround (valueOf (i)) == k ? "> " : "  ") + names[(size_t) k], [safe, i, k] { if (safe) safe->edit (i, (float) k, false); } });
                    showMenu (builtinParams (shownKey)[(size_t) i].name, std::move (items));
                };
                r.cycle->onRightClick = [this, i] { knobMenu (i); };
                addAndMakeVisible (*r.cycle);
            }
        }
        else if (q.kind == 2)
        {
            r.number = std::make_unique<NumberBox> (q.name, (int) q.min, (int) q.max, (int) q.def);
            r.number->compact = true; r.number->labelChars = 9;
            r.number->valueChars = (int) std::max<size_t> (3, std::max ({ q.text (q.min).size(), q.text (q.max).size(), q.text (q.def).size(), q.text (0.5f * (q.min + q.max)).size() }));   // ("+12ST", "CENTRE" whole)
            r.number->format = [q] (int v) { return juce::String (q.text ((float) v)); };
            r.number->onChange = [this, i] (int v) { edit (i, (float) v, true); };
            addAndMakeVisible (*r.number);
        }
        else
        {
            r.slider = std::make_unique<PixSlider> (q.name);
            r.slider->labelChars = 9; r.slider->valueChars = 6.5f;
            r.slider->format = [q] (float v01) { return juce::String (q.text (q.toValue (v01))); };
            r.slider->defaultValue = q.to01 (q.def);
            r.slider->onChange = [this, i, q] (float v01) { edit (i, q.toValue (v01), true); };
            r.slider->onRightClick = [this, i] { knobMenu (i); };
            r.slider->knob = look != Theme::PanelLook::None;
            r.slider->fader = r.slider->knob && faderFor (shownKey, q.section, q.name);
            addAndMakeVisible (*r.slider);
        }
        const juce::String sec (q.section);
        auto box = std::find_if (boxes.begin(), boxes.end(), [&] (const Box& b) { return b.key == sec; });
        if (box == boxes.end() && sec.contains (">"))
        {
            // (v0.81) a tab of the synth's own: "0:LAYER A|A>SOURCE" (0: its place among the tabs, if not first come)
            auto tab = sec.upToFirstOccurrenceOf (">", false, false);
            if (tab.length() > 2 && tab[1] == ':') tab = tab.substring (2);
            const auto longName = tab.upToFirstOccurrenceOf ("|", false, false), shortName = tab.contains ("|") ? tab.fromFirstOccurrenceOf ("|", false, false) : longName;
            int t = 0;
            while (t < (int) genTabNames.size() && genTabNames[(size_t) t].first != longName) ++t;
            if (t == (int) genTabNames.size())
            {
                genTabNames.push_back ({ longName, shortName });
                auto b = std::make_unique<PixButton> (longName, true);
                b->setClickingTogglesState (false);
                b->onClick = [this, t] { showPage (10 + t); };
                addAndMakeVisible (*b);
                genTabs.push_back (std::move (b));
            }
            boxes.push_back ({ sec.fromFirstOccurrenceOf (">", false, false), {}, {}, 10 + t, sec });
            box = boxes.end() - 1;
        }
        if (box == boxes.end())
        {
            // (GRANULAR: SOURCE sits beside the sound; LFOS, ENV MOD and FILTER are on the second page)
            // (REESE: HOOVER, FILTER, WOBBLE and DRIVE are on its second page)
            const bool grainPage = shownKey == kGrainKey, reesePage = shownKey == kReeseKey;
            const bool second = (grainPage && (sec == "LFO 1" || sec == "LFO 2" || sec == "ENV MOD" || sec == "FILTER"))
                             || (reesePage && (sec == "HOOVER" || sec == "FILTER" || sec == "WOBBLE" || sec == "DRIVE"))
                             || (shownKey == kSubKey && (sec == "OSC 2" || sec == "808 DROP" || sec == "DRIVE"))   // (when SUB needs two pages)
                             || (shownKey == kRomKey && ! sec.startsWith ("TONE ") && sec != "MORPH FILTER")   // (ROM: its TONES, then the rest; MORPH FILTER a page of its own)
                             || (shownKey == kMorphKey && (sec == "ENVELOPE" || sec == "SOUND" || sec == "SPACE" || sec == "OUTPUT"))
                             || (shownKey == kAcidKey && (sec.startsWith ("DEVIL FISH") || sec == "SLIDE" || sec == "MUFFLER + DRIVE"));   // (ACID: the 303, then the Devil Fish)
            // (GRANULAR, v0.84: a third page - ENVELOPE, SPACE, OUTPUT)
            const bool third = (shownKey == kRomKey && sec == "MORPH FILTER") || (grainPage && (sec == "ENVELOPE" || sec == "SPACE" || sec == "OUTPUT"));
            boxes.push_back ({ sec, {}, {}, grainPage && sec == "SOURCE" ? 2 : third ? 3 : second ? 1 : 0, sec });
            box = boxes.end() - 1;
        }
        box->rowIndex.push_back ((int) rows.size());
        rows.push_back (std::move (r));
    }
    // (v1.01 II) the hardware panel: its name, its display (first, on every page), the knobs' caps, the drum pads
    model = tagline = {};
    if (look != Theme::PanelLook::None)
    {
        struct Name { const char* key; const char* model; const char* line; };
        static const Name names[] = {
            { kSubKey, "TG SUB-1", "SUB BASS SYNTHESIZER" }, { kReeseKey, "REESE RZ-2", "REESE + HOOVER BASS" }, { kRaveKey, "RV-9 RAVE KEYS", "DIGITAL KEYBOARD MODULE" },
            { kRomKey, "ROM-4", "SOUND MODULE - 4 TONES" }, { kMorphKey, "MF-12 MORPH", "MORPHING FILTER SYNTHESIZER" }, { kAcidKey, "BL-3 BASS LINE", "BASS LINE + DEVIL FISH" },
            { kTg101Key, "TG 101", "MONOPHONIC SYNTHESIZER" }, { kPadKey, "PD-4 PAD", "FOUR LAYER PAD SYNTHESIZER" }, { kGrainKey, "GR-8 GRANULAR", "GRANULAR SAMPLER" },
            { kHelixKey, "HELIX", "WAVETABLE SYNTHESIZER" }, { kCraftyKey, "CRAFTY", "WAVETABLE MONOSYNTH" },
            { kDrum808Key, "TG RHYTHM 8", "ANALOGUE RHYTHM COMPOSER" }, { kDrum909Key, "TG RHYTHM 9", "HYBRID RHYTHM COMPOSER" } };
        for (const auto& n : names) if (shownKey == n.key) { model = n.model; tagline = n.line; }
        if (! drumLayout() && shownKey != kGrainKey)
        {
            Box d; d.title = "DISPLAY"; d.key = "~DISPLAY"; d.page = -1; d.display = true;
            boxes.insert (boxes.begin(), d);
        }
        // the caps: the panel's, or a synth's own (MORPH blue; ACID's filter red; HELIX's wavetable tan; CRAFTY red pointers)
        for (const auto& b : boxes)
            for (int ri : b.rowIndex)
                if (auto& s = rows[(size_t) ri].slider)
                {
                    const juce::String nm (ps[(size_t) rows[(size_t) ri].param].name);
                    auto cap = [&] (juce::uint32 face, juce::uint32 hi) { s->capFace = juce::Colour (face); s->capHi = juce::Colour (hi); };
                    if (shownKey == kMorphKey) { cap (0xff2a74c4, 0xff9fd0ff); s->pointer = juce::Colours::white; }
                    if (shownKey == kAcidKey && b.title == "FILTER") cap (0xffc8281c, 0xffff8a7a);
                    if (shownKey == kHelixKey && nm == "WT POS") cap (0xffc39a5c, 0xfff5dcae);
                    if (shownKey == kCraftyKey) s->pointer = juce::Colour (0xffc8281c);
                    if (shownKey == kReeseKey && nm == "DETUNE") cap (0xffc8281c, 0xffff8a7a);
                    if (shownKey == kPadKey && nm == "DETUNE") cap (0xffc39a5c, 0xfff5dcae);
                    if (look == Theme::PanelLook::DrumBlack && nm == "ACCENT") cap (0xffc8281c, 0xffff8a7a);
                }
        if (drumLayout())
        {
            // a pad for each voice (its box), playing its note
            for (int b = 0; b < (int) boxes.size(); ++b)
            {
                if (boxes[(size_t) b].title == "OUTPUT") continue;
                const int voice = (int) drumPads.size();
                static const char* const k808[] = { "BD", "RS", "SD", "CP", "LT", "CH", "MT", "OH", "HT", "CY", "CB", "CL" };
                static const char* const k909[] = { "BD", "RS", "SD", "CP", "LT", "CH", "MT", "OH", "HT", "CC", "RC", "BD" };
                auto pad = std::make_unique<PixButton> (voice < 12 ? (shownKey == kDrum909Key ? k909 : k808)[voice] : "");
                pad->onStateChange = [this, voice, p = pad.get()]
                {
                    // (the voices are in the kit's key order: C the kick, C# the rim shot ... B the last)
                    const int note = proc.editorState.octave * 12 + voice + 1;
                    const bool down = p->isDown();
                    if (down == padDown[(size_t) voice]) return;
                    padDown[(size_t) voice] = down;
                    proc.withLock ([&] { if (down) proc.player.jamNoteOn (note, proc.editorState.instrument); else proc.player.jamNoteOff (note); });
                };
                // a pad's face: its group's colour (four groups across the kit), an LED, its short name
                const auto padName = pad->getButtonText();
                pad->setButtonText ("");
                pad->decorate = [this, voice, padName, p = pad.get()] (juce::Graphics& g, juce::Rectangle<float> a, const Theme::Palette&)
                {
                    const auto* sk = Theme::panelSkin (look);
                    const float px2 = Theme::px;
                    const int groups = std::max (1, (int) drumPads.size());
                    const auto c = sk != nullptr ? sk->stripe[juce::jlimit (0, 3, voice * 4 / groups)] : juce::Colours::grey;
                    const auto face = look == Theme::PanelLook::DrumCream ? juce::Colour (0xffe9e3d3) : c;
                    auto f = p->getLocalBounds().toFloat().reduced (px2 * 0.5f);
                    g.setGradientFill (juce::ColourGradient (face.brighter (0.25f), 0, f.getY(), face.darker (p->isDown() ? 0.5f : 0.25f), 0, f.getBottom(), false));
                    g.fillRoundedRectangle (f, px2 * 1.2f);
                    g.setColour (juce::Colours::black);
                    g.drawRoundedRectangle (f, px2 * 1.2f, std::max (1.0f, px2 * 0.5f));
                    const bool lit = padDown[(size_t) voice];
                    g.setColour (lit ? juce::Colour (0xffff3b2a) : juce::Colour (0xff3a1410));
                    g.fillEllipse (f.getCentreX() - px2 * 1.5f, f.getY() + px2 * 1.5f, px2 * 3, px2 * 3);
                    PixelFont::PanelType type;
                    PixelFont::drawCentred (g, padName, f.withTrimmedTop (px2 * 5), px2, face.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff141414) : juce::Colours::white);
                    juce::ignoreUnused (a);
                };
                addAndMakeVisible (*pad);
                drumPads.push_back (std::move (pad));
            }
        }
    }
    // (v0.84) every name whole: a box with a longer one ("PITCH TIME") gives all its rows that much room, so they line up
    for (const auto& b : boxes)
    {
        int chars = 9;
        for (int ri : b.rowIndex)
            if (rows[(size_t) ri].slider || rows[(size_t) ri].number)
                chars = std::max (chars, (int) juce::String (ps[(size_t) rows[(size_t) ri].param].name).length());
        for (int ri : b.rowIndex)
        {
            if (auto& s = rows[(size_t) ri].slider) s->labelChars = chars;
            if (auto& n = rows[(size_t) ri].number) n->labelChars = chars;
        }
    }
    // (the tabs in their places: "N:" first, else as they came; the boxes' pages follow)
    if (! genTabNames.empty())
    {
        std::vector<int> order ((size_t) genTabNames.size());
        std::vector<int> rank ((size_t) genTabNames.size(), 0);
        for (const auto& q : ps)
        {
            const juce::String sec (q.section);
            if (! sec.contains (">")) continue;
            auto tab = sec.upToFirstOccurrenceOf (">", false, false);
            const int want = tab.length() > 2 && tab[1] == ':' ? tab[0] - '0' : 100;
            tab = (tab.length() > 2 && tab[1] == ':' ? tab.substring (2) : tab).upToFirstOccurrenceOf ("|", false, false);
            for (size_t t = 0; t < genTabNames.size(); ++t) if (genTabNames[t].first == tab && rank[t] == 0) rank[t] = want * 100 + (int) t + 1;
        }
        for (size_t t = 0; t < order.size(); ++t) order[t] = (int) t;
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return rank[(size_t) a] < rank[(size_t) b]; });
        std::vector<int> placeOf (order.size());
        for (size_t k = 0; k < order.size(); ++k) placeOf[(size_t) order[k]] = (int) k;
        std::vector<std::unique_ptr<PixButton>> tabs2; std::vector<std::pair<juce::String, juce::String>> names2;
        for (int t : order) { tabs2.push_back (std::move (genTabs[(size_t) t])); names2.push_back (genTabNames[(size_t) t]); }
        genTabs = std::move (tabs2); genTabNames = std::move (names2);
        for (size_t k = 0; k < genTabs.size(); ++k) genTabs[k]->onClick = [this, k] { showPage (10 + (int) k); };
        for (auto& b : boxes) if (b.page >= 10) b.page = 10 + placeOf[(size_t) (b.page - 10)];
    }
    // (a synth of tabs of its own starts on its first; another synth's pages from their start)
    if (! genTabNames.empty()) { if (oldKey != shownKey || pageShown < 10 || pageShown >= 10 + (int) genTabNames.size()) pageShown = 10; }
    else if (pageShown >= 10) pageShown = 0;
}

void EnginePage::sync()
{
    const auto v = readBuiltinState (inst());
    shownValues = v;
    const auto& ps = builtinParams (shownKey);
    for (auto& r : rows)
    {
        const auto& q = ps[(size_t) r.param];
        const float x = v[(size_t) r.param];
        if (r.slider) r.slider->setValue (q.to01 (x));
        if (r.number) r.number->setValue ((int) std::lround (x));
        for (int k = 0; k < (int) r.choices.size(); ++k) r.choices[(size_t) k]->setToggleState (std::lround (x) == k, juce::dontSendNotification);
        if (r.cycle) r.cycle->setButtonText (juce::String (q.name) + ": " + juce::String (q.text (x)));
        if (r.lever) r.lever->setSelected ((int) std::lround (x));
    }
    repaint();
}

void EnginePage::refresh()
{
    if (browsingTone >= 0 && (shownInstrument != proc.editorState.instrument || inst().pluginKey != shownKey)) { browsingTone = -1; browseBefore = nullptr; closeRomBrowser (true); }
    if (inst().pluginKey != shownKey || rows.empty()) { build(); shownInstrument = proc.editorState.instrument; sync(); resized(); }
    shownInstrument = proc.editorState.instrument;
    sync();
}

void EnginePage::tick()
{
    if (shownInstrument != proc.editorState.instrument || inst().pluginKey != shownKey) refresh();
    if (shownKey == kGrainKey && (++waveTick % 3) == 0) readWave();
}

void EnginePage::showPage (int k)
{
    if (! genTabNames.empty()) pageShown = 10 + juce::jlimit (0, (int) genTabNames.size() - 1, k >= 10 ? k - 10 : k);   // (v0.81: tabs of its own)
    else pageShown = k == 3 && (shownKey == kRomKey || shownKey == kGrainKey) ? 3 : juce::jlimit (0, 1, k);   // (3: ROM's MORPH FILTER, GRANULAR's AMP + FX)
    resized();
    repaint();
}

void EnginePage::readWave()
{
    const int i = proc.editorState.instrument;
    const auto v = readBuiltinState (inst());
    const int count = std::max (16, (int) (waveArea.getWidth() / std::max (1.0f, Theme::px)));
    bool ok = false;
    proc.withLock ([&]
    {
        if (auto* g = dynamic_cast<GrainSynth*> (proc.player.synths().at (i)))
        {
            ok = g->sourceOverview (wavePeaks, count);
            wavePos = g->positionNow();
            waveGrains = g->activeGrains();
        }
    });
    waveOk = ok;
    const int kind = (int) std::lround (v[grain::Source]);
    const auto& ps = builtinParams (kGrainKey);
    if (kind == 0) waveName = "TABLE: " + juce::String (ps[grain::Table].text (v[grain::Table]));
    else if (kind == 1)
    {
        const int slot = juce::jlimit (1, kMaxInstruments, (int) std::lround (v[grain::SampleSlot]));
        const auto& from = proc.song.instruments[(size_t) slot - 1];
        if (from.data.empty()) waveName = slot - 1 == i ? juce::String ("NO SAMPLE - LOAD SAMPLE...") : "SAMPLE " + hex2 (slot) + ": (NO SAMPLE IN IT)";
        else waveName = (slot - 1 == i ? juce::String() : "SAMPLE " + hex2 (slot) + ": ") + juce::String (from.name).toUpperCase();
    }
    else waveName = std::lround (v[grain::Freeze]) == 1 ? "INPUT - FROZEN" : "INPUT - LAST 8 SECONDS";
    repaint (waveArea.toNearestInt());
}

void EnginePage::positionFrom (juce::Point<float> at)
{
    const auto in = waveArea.reduced (Theme::px * 4);
    if (in.getWidth() <= 1) return;
    edit (grain::Position, juce::jlimit (0.0f, 100.0f, 100.0f * (at.x - in.getX()) / in.getWidth()), true);
}

void EnginePage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (waveArea.contains (e.position)) positionFrom (e.position);   // click the sound: read from there
}

void EnginePage::mouseDrag (const juce::MouseEvent& e)
{
    if (waveArea.contains (e.mouseDownPosition)) positionFrom (e.position);
}

void EnginePage::knobMenu (int param)
{
    const auto& ps = builtinParams (shownKey);
    if (param < 0 || param >= (int) ps.size()) return;
    const int i = proc.editorState.instrument;
    const juce::String name = juce::String (inst().pluginName) + ": " + ps[(size_t) param].name;
    juce::Component::SafePointer<EnginePage> safe (this);
    static const char* letters[] = { "L", "Q", "H", "W" };
    std::vector<MenuItem> items;
    items.push_back ({ "AUTOMATE (DRAW ITS ENVELOPE)", [safe, i, param] { if (safe && safe->onAutomate) safe->onAutomate (kAutoInstrumentBase + i, param); } });
    for (int k = 0; k < kInstrumentKnobs; ++k)
        items.push_back ({ juce::String ("KNOB ") + letters[k] + " (" + letters[k] + "XX ON ITS NOTES)", [safe, i, k, param, name]
        {
            if (! safe) return;
            safe->proc.history.instrument (i, juce::String ("KNOB ") + letters[k] + " = " + name);
            safe->proc.withLock ([&] { safe->proc.song.instruments[(size_t) i].knobs[k] = AutoTarget { kAutoInstrumentBase + i, param, name.toStdString() }; });
            if (safe->onStatus) safe->onStatus (juce::String ("KNOB ") + letters[k] + " NOW MOVES " + name + " - " + letters[k] + "XX ON THIS INSTRUMENT'S NOTES (00-FF)");
        } });
    const int tr = proc.editorState.track;
    for (int k = 0; k < 2; ++k)
        items.push_back ({ "TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A (VXX)" : " AUTO B (JXX)"), [safe, tr, k, i, param, name]
        {
            if (! safe) return;
            safe->proc.history.mixer ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A = " : " AUTO B = ") + name);
            safe->proc.withLock ([&] { safe->proc.song.mixer.trackAuto[tr][k] = AutoTarget { kAutoInstrumentBase + i, param, name.toStdString() }; });
            if (safe->onStatus) safe->onStatus ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A (VXX) NOW MOVES " : " AUTO B (JXX) NOW MOVES ") + name);
        } });
    showMenu (ps[(size_t) param].name, std::move (items));
}

// (PAD, v0.85) the layer whose LOAD SAMPLE... a box starts with: its SOURCE (on its tab) and its box on SAMPLES; else -1
static int padSampleBox (const juce::String& key)
{
    if (key.startsWith ("7:SAMPLES") ) return juce::jlimit (0, 3, (int) (key.getLastCharacter() - 'A'));
    if (key.length() > 2 && key[1] == ':' && key[0] >= '0' && key[0] <= '3' && key.endsWith (">SOURCE")) return key[0] - '0';
    return -1;
}

Theme::PanelLook EnginePage::lookFor (const std::string& key)
{
    using L = Theme::PanelLook;
    if (key == kSubKey)     return L::Cream;      // (Jamie's picks, v1.01)
    if (key == kReeseKey)   return L::Walnut;
    if (key == kRaveKey)    return L::Membrane;
    if (key == kRomKey)     return L::Sampler;
    if (key == kMorphKey)   return L::Sampler;
    if (key == kAcidKey)    return L::Cream;
    if (key == kTg101Key)   return L::Walnut;
    if (key == kHelixKey)   return L::Rack;
    if (key == kCraftyKey)  return L::Sampler;
    if (key == kGrainKey)   return L::Sampler;
    if (key == kDrum808Key) return L::DrumBlack;   // (808 B; L::DrumWood is 808 A, charcoal and wood)
    if (key == kDrum909Key) return L::DrumCream;
    if (key == kPadKey)     return L::Walnut;   // (PAD: look A)
    return L::None;
}

void EnginePage::layoutOnce()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    const int title = (int) (px * 11);
    if (! relayout) subPages = false;
    for (auto& rw : rows) if (rw.slider) rw.slider->dialPx = knobDial;
    lastFit = true;
    for (auto& pb : padLoadBtn) pb.setVisible (false);
    for (auto& tb : toneBtn) tb.setVisible (false);
    for (auto& sb : sampleBtn) sb.setVisible (false);
    romFolderBtn.setVisible (false);
    updateToneButtons();
    // (a vintage panel: its cheeks or rack ears take the edges)
    const auto* ps = Theme::panelSkin (look);
    faceArea = getLocalBounds();
    if (ps != nullptr && ps->sides != 0) faceArea.reduce ((int) (px * (ps->sides == 1 ? 9 : 11)), 0);
    auto r = faceArea.reduced (gap);
    const bool knobs = ps != nullptr;
    auto top = r.removeFromTop (rowH);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    // the title row: as much as fits on one line - the title goes first, then PRESETS... gets shorter; only when even
    // that won't fit do HOLD TO PLAY and OUTPUT & KNOBS go on a line of their own
    const int buttons = charsW (5) + charsW (17) + charsW (14) + gap * 4;
    const int title0 = charsW (12);
    const bool full = top.getWidth() >= buttons + charsW (10) + title0, noTitle = top.getWidth() >= buttons + charsW (10),
               shortPresets = top.getWidth() >= buttons + charsW (7);
    presetsBtn.setButtonText (full || noTitle || ! shortPresets ? "PRESETS..." : "PRESETS");
    closeBtn.setBounds (top.removeFromRight (charsW (5))); top.removeFromRight (gap);
    presetsBtn.setBounds (top.removeFromRight (charsW (presetsBtn.getButtonText().length()))); top.removeFromRight (gap);
    if (full || noTitle || shortPresets)
    {
        optionsBtn.setBounds (top.removeFromRight (charsW (17))); top.removeFromRight (gap);
        playBtn.setBounds (top.removeFromRight (charsW (14))); top.removeFromRight (gap * 2);
        titleArea = full ? top.toFloat() : juce::Rectangle<float>();
    }
    else
    {
        titleArea = top.toFloat();
        r.removeFromTop (gap);
        auto second = r.removeFromTop (rowH);
        playBtn.setBounds (second.removeFromLeft (charsW (14))); second.removeFromLeft (gap);
        optionsBtn.setBounds (second.removeFromLeft (std::min (second.getWidth(), charsW (17))));
    }
    r.removeFromTop (gap * 2);

    // a row of choice buttons too narrow for its words takes more lines (fewer buttons on each): no word is ever cut
    auto choiceLines = [&] (const Row& rw, int width)
    {
        if (rw.choices.empty()) return 1;
        const int n = (int) rw.choices.size();
        int widest = 0;
        for (const auto& c : rw.choices) widest = std::max (widest, charsW (c->getButtonText().length()));
        for (int lines = 1; lines < n; ++lines)
        {
            const int per = (n + lines - 1) / lines;
            if (widest * per + gap * (per - 1) <= width) return lines;
            if (lines == 1)
            {
                // (one line still, if each button is as wide as its own label: "OFF" needn't be as wide as "BEAT HZ")
                int sum = gap * (n - 1);
                for (const auto& c : rw.choices) sum += charsW (c->getButtonText().length());
                if (sum <= width) return 1;
            }
        }
        return n;
    };
    // (SUB's BEAT: only what's in use - RATE for BEAT HZ, ST for BEAT ST, nothing more when it's OFF)
    std::vector<float> vals;
    if (inst().pluginKey == kSubKey || inst().pluginKey == kGrainKey || inst().pluginKey == kPadKey) vals = readBuiltinState (inst());
    auto hidden = [&] (const Row& rw)
    {
        // (PAD, v0.85: a layer's SAMPLE controls only while its WAVE is SAMPLE - LOAD SAMPLE... is always there)
        if (shownKey == kPadKey && vals.size() >= (size_t) pad::Count)
        {
            if (rw.param < pad::SampleBase) return false;
            const int l = (rw.param - pad::SampleBase) / pad::PerSample;
            return std::lround (vals[(size_t) pad::p (l, pad::Wave)]) != pad::Sample;
        }
        // (GRANULAR, v0.75: TABLE only for a table, SAMPLE only for a sample - room for PRESETS' line)
        if (shownKey == kGrainKey && vals.size() > (size_t) grain::SampleSlot)
        {
            const int source = (int) std::lround (vals[(size_t) grain::Source]);
            if (rw.param == grain::Table) return source != 0;
            if (rw.param == grain::SampleSlot) return source != 1;
            return false;
        }
        if (shownKey != kSubKey || vals.size() <= (size_t) sub::BeatSemis) return false;
        const int beat = (int) std::lround (vals[(size_t) sub::BeatOn]);
        switch (rw.param)
        {
            case sub::BeatRate:  return beat != 1;
            case sub::BeatSemis: return beat != 2;
            case sub::BeatDepth: return beat == 0;
            case sub::BeatDelay: return beat == 0;
            default: return false;
        }
    };
    // (a vintage panel: the box's sliders are knobs, flowing in rows under its buttons - each as wide as its own longest
    // word or value, the room left in a row shared out; the row's height its tallest name)
    // (v1.01 II: knobs, faders and levers flow together, each its own size; a row as tall as its tallest)
    struct KnobGrid { int count = 0, rowsN = 0, height = 0; std::vector<int> rowOf, xOf, wOf, yOf, hOf; };
    auto inGrid = [&] (const Row& rw) { return knobs && (rw.slider || rw.lever) && ! hidden (rw); };
    auto knobGrid = [&] (const Box& b, int width)
    {
        KnobGrid k;
        if (! knobs) return k;
        PixelFont::PanelType type;   // (measured as they're drawn: in BARLOW CONDENSED)
        const int inner = width - gap * 4;
        std::vector<int> want, tall;
        for (int ri : b.rowIndex)
        {
            const auto& rw = rows[(size_t) ri];
            if (! inGrid (rw)) continue;
            if (rw.lever)
            {
                want.push_back (std::min (inner, rw.lever->preferredWidth (px)));
                tall.push_back (rw.lever->preferredHeight (px));
                continue;
            }
            float w = rw.slider->fader ? px * 20 : px * (knobDial + 1);
            for (const auto& wd : juce::StringArray::fromTokens (rw.slider->getLabel(), " ", "")) w = std::max (w, PixelFont::labelWidth (wd, px));
            if (! rw.slider->fader) w = std::max (w, PixelFont::labelWidth (rw.slider->format (rw.slider->value), px));
            if (rw.slider->steps > 1 && rw.slider->steps <= 12)   // (a rotary switch: room for its longest setting)
                for (int st = 0; st < rw.slider->steps; ++st) w = std::max (w, PixelFont::labelWidth (rw.slider->format ((float) st / (float) (rw.slider->steps - 1)), px));
            const int cw = std::min (inner, (int) std::ceil (w + px * 3));
            const int lines = PixSlider::knobLines (rw.slider->getLabel(), (float) cw, px).size();
            want.push_back (cw);
            tall.push_back (rw.slider->fader ? PixSlider::faderHeight (px, lines, knobDial) : PixSlider::knobHeight (px, lines, knobDial));
        }
        k.count = (int) want.size();
        if (k.count == 0) return k;
        // flow into rows, then share each row's spare room among its controls
        size_t i = 0;
        int y = 0;
        while (i < want.size())
        {
            size_t j = i; int used = 0, rowH2 = 0;
            while (j < want.size() && (j == i || used + gap + want[j] <= inner)) { used += (j == i ? 0 : gap) + want[j]; rowH2 = std::max (rowH2, tall[j]); ++j; }
            const int spare = (inner - used) / (int) (j - i);
            int x = 0;
            for (size_t q = i; q < j; ++q)
            {
                k.rowOf.push_back (k.rowsN); k.xOf.push_back (x); k.wOf.push_back (want[q] + spare);
                k.yOf.push_back (y + (rowH2 - tall[q]) / 2); k.hOf.push_back (tall[q]);
                x += want[q] + spare + gap;
            }
            ++k.rowsN; i = j;
            y += rowH2 + gap;
        }
        k.height = y;
        return k;
    };
    const int foot = knobs && ps->titleStyle == 0 ? (int) (px * 2.5f) : 0;   // (a bar's strip at the foot)
    auto boxHeight = [&] (const Box& b, int width)
    {
        if (b.display) return (int) (px * 50);
        int lines = 0;
        const auto kg = knobGrid (b, width);
        for (int ri : b.rowIndex) if (! hidden (rows[(size_t) ri]) && ! inGrid (rows[(size_t) ri])) lines += choiceLines (rows[(size_t) ri], width - gap * 4);
        if (kg.count > 0)
        {
            if (shownKey == kRomKey && b.title.startsWith ("TONE ")) ++lines;
            if (shownKey == kPadKey && padSampleBox (b.key) >= 0) ++lines;
            return gap * 4 + title + foot + lines * (rowH + gap) + kg.height;
        }
        if (shownKey == kRomKey && b.title.startsWith ("TONE ")) ++lines;   // (its samples' button)
        if (shownKey == kPadKey && padSampleBox (b.key) >= 0) ++lines;      // (LOAD SAMPLE...)
        return gap * 4 + title + lines * (rowH + gap);
    };
    auto place = [&] (Box& b, juce::Rectangle<int> area)
    {
        b.area = area.toFloat();
        auto in = area.reduced (gap * 2).withTrimmedTop (title);
        if (shownKey == kRomKey && b.title.startsWith ("TONE "))
        {
            const int t = juce::jlimit (0, kRomTones - 1, b.title.getTrailingIntValue() - 1);
            // (v0.79) two ways to fill it: ROM... (the folder's sounds) and SAMPLE... (WAV files), side by side
            auto head = in.removeFromTop (rowH); in.removeFromTop (gap);
            const int half = (head.getWidth() - gap) / 2;
            sampleBtn[t].setButtonText (half >= charsW (9) ? "SAMPLE..." : "WAV...");
            toneBtn[t].setBounds (head.removeFromLeft (half)); head.removeFromLeft (gap);
            sampleBtn[t].setBounds (head);
            toneBtn[t].setVisible (true);
            sampleBtn[t].setVisible (true);
        }
        if (shownKey == kPadKey && padSampleBox (b.key) >= 0)
        {
            const int l = padSampleBox (b.key);
            padLoadBtn[l].setBounds (in.removeFromTop (rowH)); in.removeFromTop (gap);
            padLoadBtn[l].setVisible (true);
        }
        for (int ri : b.rowIndex)
        {
            auto& rw = rows[(size_t) ri];
            if (hidden (rw))
            {
                for (juce::Component* c : { (juce::Component*) rw.slider.get(), (juce::Component*) rw.number.get(), (juce::Component*) rw.cycle.get(), (juce::Component*) rw.lever.get() }) if (c) c->setVisible (false);
                for (auto& c : rw.choices) c->setVisible (false);
                continue;
            }
            if (inGrid (rw)) continue;   // (the knobs, faders and levers go in their grid, below)
            const int lines = choiceLines (rw, in.getWidth());
            auto row = in.removeFromTop (rowH); in.removeFromTop (gap);
            if (rw.slider) rw.slider->setBounds (row);
            if (rw.number) rw.number->setBounds (row.removeFromLeft (std::min (row.getWidth(), rw.number->preferredWidth (px))));
            if (rw.cycle) rw.cycle->setBounds (row);
            if (! rw.choices.empty())
            {
                const int n = (int) rw.choices.size();
                const int perLine = (n + lines - 1) / lines;
                int widest = 0, sum = gap * (n - 1);
                for (const auto& c : rw.choices) { widest = std::max (widest, charsW (c->getButtonText().length())); sum += charsW (c->getButtonText().length()); }
                if (lines == 1 && n > 1 && widest * n + gap * (n - 1) > row.getWidth())
                {
                    // (each as wide as its label, the room left shared out)
                    const int spare = std::max (0, row.getWidth() - sum) / n;
                    for (int j = 0; j < n; ++j)
                    {
                        const int w = j < n - 1 ? charsW (rw.choices[(size_t) j]->getButtonText().length()) + spare : row.getWidth();
                        rw.choices[(size_t) j]->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap);
                    }
                    continue;
                }
                int k = 0;
                for (int line = 0; line < lines; ++line)
                {
                    if (line > 0) { row = in.removeFromTop (rowH); in.removeFromTop (gap); }
                    const int m = std::min (perLine, n - k);
                    const int w = (row.getWidth() - gap * (perLine - 1)) / perLine;
                    for (int j = 0; j < m; ++j, ++k) { rw.choices[(size_t) k]->setBounds (row.removeFromLeft (j < perLine - 1 ? w : row.getWidth())); row.removeFromLeft (gap); }
                }
            }
        }
        // the knobs: a grid, spread across the box
        const auto kg = knobGrid (b, area.getWidth());
        if (kg.count > 0)
        {
            int k = 0;
            for (int ri : b.rowIndex)
            {
                auto& rw = rows[(size_t) ri];
                if (! inGrid (rw)) continue;
                const juce::Rectangle<int> cell (in.getX() + kg.xOf[(size_t) k], in.getY() + kg.yOf[(size_t) k], kg.wOf[(size_t) k], kg.hOf[(size_t) k]);
                if (rw.lever) rw.lever->setBounds (cell); else rw.slider->setBounds (cell);
                ++k;
            }
        }
    };

    // (v1.01 II) the drum machines: every voice in a column of its own, the pads under them
    padsArea = drumDispArea = {};
    for (auto& d : drumPads) d->setVisible (false);
    for (auto& h : drumHalf) h.setVisible (false);
    if (drumLayout() && knobs) { layoutDrums (r); return; }
    // GRANULAR: the sound it reads (with its SOURCE box beside it) across the top, then its controls on two pages
    waveArea = {};
    const bool generic = ! genTabNames.empty();
    const bool paged = generic || shownKey == kGrainKey || shownKey == kReeseKey || shownKey == kRomKey || shownKey == kMorphKey || shownKey == kAcidKey || (shownKey == kSubKey && subPages);
    for (auto& b : pageBtn) b.setVisible (paged && ! generic);
    pageBtn[2].setVisible (paged && ! generic && (shownKey == kRomKey || shownKey == kGrainKey));
    if (generic)
    {
        // (v0.81) the synth's own tabs: their names if they fit on a line, else the short ones; a second line if need be
        int longW = 0, shortW = 0;
        for (const auto& t : genTabNames) { longW += charsW (t.first.length()) + gap; shortW += charsW (t.second.length()) + gap; }
        const bool useLong = longW <= r.getWidth() + gap;
        auto line = r.removeFromTop (rowH);
        for (size_t k = 0; k < genTabs.size(); ++k)
        {
            const auto& nm = useLong ? genTabNames[k].first : genTabNames[k].second;
            const int w = charsW (nm.length());
            if (w > line.getWidth()) { r.removeFromTop (gap); line = r.removeFromTop (rowH); }
            genTabs[k]->setButtonText (nm);
            genTabs[k]->setBounds (line.removeFromLeft (w)); line.removeFromLeft (gap);
            genTabs[k]->setToggleState (pageShown == 10 + (int) k, juce::dontSendNotification);
            genTabs[k]->setVisible (true);
        }
        juce::ignoreUnused (shortW);
        r.removeFromTop (gap * 2);
    }
    loadSampleBtn.setVisible (shownKey == kGrainKey);
    if (generic) {}
    else if (shownKey == kReeseKey || shownKey == kRomKey || shownKey == kMorphKey || shownKey == kAcidKey || (shownKey == kSubKey && subPages))
    {
        // REESE, ROM (and SUB when it needs them): the two pages' tabs on a line of their own
        const bool sub = shownKey == kSubKey, romP = shownKey == kRomKey, mp = shownKey == kMorphKey, ac = shownKey == kAcidKey;
        pageBtn[0].setButtonText (romP ? "TONES" : mp ? "MORPH" : ac ? "303" : "SOUND");
        pageBtn[1].setButtonText (sub ? "OSC 2 + DRIVE" : romP ? "SOUND + FILTER" : mp ? "AMP + SPACE" : ac ? "DEVIL FISH" : "HOOVER + FILTER");
        auto tabs = r.removeFromTop (rowH); r.removeFromTop (gap * 2);
        if (romP)
        {
            // (v0.79) ROM: TONES, SOUND + FILTER, MORPH - and where its sounds are, ROM FOLDER..., at the right
            const bool roomy = tabs.getWidth() >= charsW (8) + charsW (17) + charsW (7) + charsW (13) + gap * 6;
            romFolderBtn.setButtonText (roomy ? "ROM FOLDER..." : "FOLDER...");
            romFolderBtn.setBounds (tabs.removeFromRight (charsW (romFolderBtn.getButtonText().length()))); tabs.removeFromRight (gap * 3);
            romFolderBtn.setVisible (true);
            const bool shortTabs = ! roomy && tabs.getWidth() < charsW (8) + charsW (17) + charsW (7) + gap * 2;
            pageBtn[1].setButtonText (shortTabs ? "SOUND" : "SOUND + FILTER");
            pageBtn[2].setButtonText ("MORPH");
            pageBtn[0].setBounds (tabs.removeFromLeft (charsW (5 + (shortTabs ? 0 : 3)))); tabs.removeFromLeft (gap);
            pageBtn[1].setBounds (tabs.removeFromLeft (charsW (pageBtn[1].getButtonText().length()))); tabs.removeFromLeft (gap);
            pageBtn[2].setBounds (tabs.removeFromLeft (charsW (5)));
            for (int k = 0; k < 3; ++k) pageBtn[k].setToggleState (browsingTone < 0 && pageShown == (k == 2 ? 3 : k), juce::dontSendNotification);
        }
        else
        {
            pageBtn[0].setBounds (tabs.removeFromLeft (charsW (8))); tabs.removeFromLeft (gap);
            pageBtn[1].setBounds (tabs.removeFromLeft (charsW (sub ? 15 : 17)));
            for (int k = 0; k < 2; ++k) pageBtn[k].setToggleState (pageShown == k, juce::dontSendNotification);
        }
    }
    else if (paged)
    {
        pageBtn[0].setButtonText ("GRAINS"); pageBtn[1].setButtonText ("LFOS + FILTER"); pageBtn[2].setButtonText ("AMP + FX");
        Box* src = nullptr;
        for (auto& b : boxes) if (b.page == 2) src = &b;
        const int srcW = juce::jlimit (charsW (14), std::max (charsW (14), r.getWidth() / 2), charsW (22));
        // (the SOURCE box ends with LOAD SAMPLE...: a sample straight into GRANULAR, as into any instrument)
        // (beside SAMPLE's number when there's room, else a line of its own)
        const Row* slotRow = nullptr;
        for (const auto& rw : rows) if (rw.param == grain::SampleSlot) slotRow = &rw;
        const int numW = slotRow != nullptr && slotRow->number ? slotRow->number->preferredWidth (px) : 0;
        const bool beside = slotRow != nullptr && ! hidden (*slotRow) && numW + gap * 2 + charsW (14) <= srcW - gap * 4;
        auto band = r.removeFromTop (std::max (title + rowH * 4 + gap * 5, src != nullptr ? boxHeight (*src, srcW) + (beside ? 0 : rowH + gap) : 0));
        r.removeFromTop (gap * 2);
        if (src != nullptr)
        {
            auto sa = band.removeFromRight (srcW);
            place (*src, sa);
            if (beside && slotRow != nullptr && slotRow->number)
            {
                auto nb = slotRow->number->getBounds();
                loadSampleBtn.setBounds (nb.getRight() + gap * 2, nb.getY(), sa.reduced (gap * 2).getRight() - nb.getRight() - gap * 2, rowH);
            }
            else loadSampleBtn.setBounds (sa.reduced (gap * 2).removeFromBottom (rowH));
            band.removeFromRight (gap * 3);
        }
        // the page tabs under the sound, beside the SOURCE box (no line of their own)
        auto tabs = band.removeFromBottom (rowH); band.removeFromBottom (gap * 2);
        waveArea = band.toFloat();
        // (v0.84: three - GRAINS, LFOS + FILTER (MOD when narrow), AMP + FX)
        const int w0 = charsW (6), w2 = charsW (8);
        const int rest = tabs.getWidth() - w0 - w2 - gap * 2;
        pageBtn[1].setButtonText (rest >= charsW (13) ? "LFOS + FILTER" : "MOD");
        pageBtn[0].setBounds (tabs.removeFromLeft (w0)); tabs.removeFromLeft (gap);
        pageBtn[2].setBounds (tabs.removeFromRight (w2)); tabs.removeFromRight (gap);
        pageBtn[1].setBounds (tabs);
        for (int k = 0; k < 3; ++k) pageBtn[k].setToggleState (pageShown == (k == 2 ? 3 : k), juce::dontSendNotification);
    }
    // (v0.79) the ROM browser: in place of the boxes
    if (browsingTone >= 0)
    {
        for (auto& b : boxes)
        {
            b.area = {};
            for (int ri : b.rowIndex)
            {
                auto& rw = rows[(size_t) ri];
                for (juce::Component* c : { (juce::Component*) rw.slider.get(), (juce::Component*) rw.number.get(), (juce::Component*) rw.cycle.get(), (juce::Component*) rw.lever.get() }) if (c) c->setVisible (false);
                for (auto& c : rw.choices) c->setVisible (false);
            }
        }
        for (auto& tb : toneBtn) tb.setVisible (false);
        for (auto& sb : sampleBtn) sb.setVisible (false);
        romHeadArea = r.removeFromTop (rowH).toFloat(); r.removeFromTop (gap);
        auto btns = r.removeFromBottom (rowH); r.removeFromBottom (gap * 2);
        const int bw = (btns.getWidth() - gap * 3) / 4;
        for (auto* b : { &romKeepBtn, &romCancelBtn, &romEmptyBtn, &romAgainBtn }) { b->setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (gap); b->setVisible (true); }
        const int catW = juce::jlimit (charsW (10), std::max (charsW (10), r.getWidth() / 2), std::max (charsW (16), r.getWidth() * 2 / 5));
        romCats->setBounds (r.removeFromLeft (catW)); r.removeFromLeft (gap * 3);
        romSounds->setBounds (r);
        romCats->setVisible (true); romSounds->setVisible (true);
        return;
    }
    // the boxes not on the page shown: hidden
    std::vector<Box*> shownBoxes;
    for (auto& b : boxes)
    {
        const bool on = (! paged || b.page == pageShown || b.page == 2 || b.page == -1) && ! (b.display && dropDisplay);
        if (on && b.page != 2) shownBoxes.push_back (&b);
        if (! on) b.area = {};
        for (int ri : b.rowIndex)
        {
            auto& rw = rows[(size_t) ri];
            const bool show = on && ! hidden (rw);
            for (juce::Component* c : { (juce::Component*) rw.slider.get(), (juce::Component*) rw.number.get(), (juce::Component*) rw.cycle.get(), (juce::Component*) rw.lever.get() }) if (c) c->setVisible (show);
            for (auto& c : rw.choices) c->setVisible (show);
        }
    }
    // the boxes in columns: as few as fit (but never so narrow that a slider has no room), each filled top to bottom
    // (knobs: a column needs room for two of them at least - so up to four columns)
    const int minColW = knobs ? (int) (px * 54) : charsW (16) + (int) (px * 24);
    const int maxCols = juce::jlimit (1, knobs ? 4 : 3, (r.getWidth() + gap * 3) / (minColW + gap * 3));
    int cols = 1, colW = r.getWidth();
    for (int pass = 0; pass < 2; ++pass)
    {
        int total = 0;
        for (const auto* b : shownBoxes) total += boxHeight (*b, colW) + gap * 2;
        cols = juce::jlimit (1, maxCols, (int) std::ceil ((double) total / std::max (1, r.getHeight())) + (pass == 1 ? extraCols : 0));
        colW = (r.getWidth() - gap * 3 * (cols - 1)) / std::max (1, cols);
    }
    // which column each box goes in: whatever makes the tallest column shortest (each column keeps the boxes' order;
    // a few boxes, so every way is tried - first the boxes in order, so a tie keeps the natural reading order)
    const int n = (int) shownBoxes.size();
    std::vector<int> hs ((size_t) n);
    for (int k = 0; k < n; ++k) hs[(size_t) k] = boxHeight (*shownBoxes[(size_t) k], colW) + gap * 2;
    std::vector<int> colOf ((size_t) n, 0), bestOf ((size_t) n, 0);
    int best = std::numeric_limits<int>::max();
    auto score = [&]
    {
        int h[4] {};
        for (int k = 0; k < n; ++k) h[colOf[(size_t) k]] += hs[(size_t) k];
        return std::max ({ h[0], h[1], h[2], h[3] });
    };
    {
        // in order first: a run per column, cut where the columns come out most even
        int total = 0;
        for (int x : hs) total += x;
        int c = 0, acc = 0;
        for (int k = 0; k < n; ++k)
        {
            if (c < cols - 1 && acc > 0 && acc + hs[(size_t) k] / 2 > total * (c + 1) / cols) ++c;
            colOf[(size_t) k] = c; acc += hs[(size_t) k];
        }
        best = score(); bestOf = colOf;
    }
    if (cols > 1 && n <= (cols > 3 ? 9 : 12))
    {
        long combos = 1;
        for (int k = 0; k < n; ++k) combos *= cols;
        for (long m = 0; m < combos; ++m)
        {
            long q = m;
            for (int k = 0; k < n; ++k) { colOf[(size_t) k] = (int) (q % cols); q /= cols; }
            const int sc = score();
            if (sc > best) continue;
            // (as short: the one nearest the reading order - fewest boxes in an earlier column than one before them)
            auto disorder = [&] (const std::vector<int>& co) { int d = 0; for (int x = 0; x < n; ++x) for (int y = x + 1; y < n; ++y) d += co[(size_t) x] > co[(size_t) y] ? 1 : 0; return d; };
            if (sc < best || disorder (colOf) < disorder (bestOf)) { best = sc; bestOf = colOf; }
        }
    }
    std::vector<int> colY ((size_t) cols, r.getY());
    for (int k = 0; k < n; ++k)
    {
        const int c = bestOf[(size_t) k];
        const int h = hs[(size_t) k] - gap * 2;
        place (*shownBoxes[(size_t) k], { r.getX() + c * (colW + gap * 3), colY[(size_t) c], colW, h });
        colY[(size_t) c] += h + gap * 2;
    }
    // SUB that doesn't all fit (BEAT's rows on, a small window): OSC 2, 808 DROP and DRIVE go to a second page
    if (shownKey == kSubKey && genTabNames.empty() && ! subPages && best - gap * 2 > r.getHeight())
    {
        subPages = relayout = true;
        layoutOnce();
        relayout = false;
        return;
    }
    if (std::getenv ("PRV_LAYOUT_DEBUG") != nullptr)
        std::fprintf (stderr, "layout %s dial %.0f cols %d best %d height %d colW %d boxes %d\n", shownKey.c_str(), knobDial, cols, best, r.getHeight(), colW, n);
    // (a hardware panel: did it all fit? resized() tries the next size if not)
    if (knobs && best - gap * 2 > r.getHeight()) lastFit = false;
    juce::ignoreUnused (shrinking);
}

void EnginePage::resized()
{
    // a hardware panel: the biggest knobs that fit (with the display; then without it; then smaller), a column more
    // when the boxes need it - the words always their size
    const auto* ps = Theme::panelSkin (look);
    if (ps == nullptr || drumLayout()) { knobDial = 28.0f; dropDisplay = false; extraCols = 0; layoutOnce(); return; }
    struct Try { bool display; float dial; };
    static const Try tries[] = { { true, 28 }, { true, 24 }, { true, 20 }, { false, 28 }, { false, 24 }, { false, 20 }, { true, 16 }, { false, 16 }, { false, 13 } };
    for (const auto& t : tries)
        for (int extra = 0; extra <= 2; ++extra)
        {
            dropDisplay = ! t.display; knobDial = t.dial; extraCols = extra;
            layoutOnce();
            if (lastFit) return;
        }
}

void EnginePage::layoutDrums (juce::Rectangle<int> r)
{
    const float px = Theme::px;
    const int gap = (int) std::max (2.0f, px), rowH = (int) (px * 11), title = (int) (px * 11);
    for (auto& t : genTabs) t->setVisible (false);
    for (auto& b : pageBtn) b.setVisible (false);
    Box* out = nullptr;
    std::vector<Box*> voices;
    for (auto& b : boxes) { if (b.title == "OUTPUT") out = &b; else voices.push_back (&b); }
    const int n = std::max (1, (int) voices.size());
    // the bottom: a pad for every voice, the voice's display at the right
    auto bottom = r.removeFromBottom ((int) (px * 30)); r.removeFromBottom (gap * 3);
    // every voice in a column (OUTPUT the last) when there's room for the names; else four at a time (VOICES 1-4,
    // 5-8, 9-12), each column two knobs wide
    const bool halves = (r.getWidth() - gap * n) / (n + 1) < (int) (px * 30);
    for (auto& h : drumHalf) h.setVisible (halves);
    if (halves)
    {
        auto line = r.removeFromTop (rowH); r.removeFromTop (gap * 2);
        for (int h = 0; h < 3; ++h)
        {
            drumHalf[h].setBounds (line.removeFromLeft ((int) (px * 6 * 11 + px * 4))); line.removeFromLeft (gap);
            drumHalf[h].setToggleState (drumHalfShown == h, juce::dontSendNotification);
        }
    }
    const int first = halves ? drumHalfShown * 4 : 0, count = halves ? std::max (0, std::min (4, n - first)) : n;
    const int perRow = (halves ? 4 : n) + (out != nullptr ? 1 : 0);
    const int colW = (r.getWidth() - gap * (perRow - 1)) / perRow;
    const int across = halves ? 2 : 1, down = halves ? 3 : 5;
    const int cellW = (colW - gap * (across - 1)) / across;
    const bool values = halves;   // (six at a time: room for the values too)
    auto cellH = [&] (float d) { return values ? PixSlider::knobHeight (px, 1, d) : PixSlider::knobHeightNoValue (px, d); };
    // the dials as big as the columns and the height let them (the words stay their size)
    float dial = 24.0f;
    for (const float d : { 26.0f, 24.0f, 20.0f, 16.0f, 13.0f })
    {
        dial = d;
        if (d * px <= (float) cellW - px * 2 && title + gap * 2 + down * (cellH (d) + gap) <= r.getHeight()) break;
    }
    auto column = [&] (Box& b, int colI, int acrossHere, int downHere)
    {
        const int x0 = r.getX() + colI * (colW + gap);
        const juce::Rectangle<int> col (x0, r.getY(), colI == perRow - 1 ? r.getRight() - x0 : colW, r.getHeight());
        b.area = col.toFloat();
        const int y0 = col.getY() + title + gap * 2;
        const int kh = cellH (dial);
        const int cw = (col.getWidth() - gap * (acrossHere - 1)) / acrossHere;
        const int spare = std::max (0, (col.getBottom() - y0) - downHere * (kh + gap)) / downHere;
        int k = 0;
        const int nk = (int) b.rowIndex.size();
        for (int ri : b.rowIndex)
        {
            auto& rw = rows[(size_t) ri];
            if (! rw.slider) continue;
            rw.slider->dialPx = dial;
            rw.slider->showValue = values;
            const int cx = k % acrossHere, cy = k / acrossHere;
            const bool lastOdd = acrossHere == 2 && k == nk - 1 && k % 2 == 0;   // (an odd last one: centred)
            const int w = lastOdd ? col.getWidth() : cw;
            const int x = lastOdd ? col.getX() : col.getX() + cx * (cw + gap);
            rw.slider->setBounds (x, y0 + cy * (kh + gap + spare) + spare / 2, w, kh);
            rw.slider->setVisible (true);
            ++k;
        }
    };
    for (int v = 0; v < (int) voices.size(); ++v)
    {
        auto& b = *voices[(size_t) v];
        if (v < first || v >= first + count)
        {
            b.area = {};
            for (int ri : b.rowIndex) if (auto& sl = rows[(size_t) ri].slider) sl->setVisible (false);
            continue;
        }
        column (b, v - first, across, down);
    }
    if (out != nullptr) column (*out, perRow - 1, 1, (int) out->rowIndex.size());
    // the pads, the display
    drumDispArea = bottom.removeFromRight ((int) (px * 70)).toFloat();
    bottom.removeFromRight (gap * 3);
    padsArea = bottom.toFloat();
    const int pn = std::max (1, (int) drumPads.size());
    auto pr = bottom.reduced (gap * 2);
    const int pw = (pr.getWidth() - gap * (pn - 1)) / pn;
    for (int k = 0; k < pn && k < (int) drumPads.size(); ++k)
    {
        drumPads[(size_t) k]->setBounds (pr.getX() + k * (pw + gap), pr.getY(), k == pn - 1 ? pr.getRight() - (pr.getX() + k * (pw + gap)) : pw, pr.getHeight());
        drumPads[(size_t) k]->setVisible (true);
    }
}

void EnginePage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    const auto* ps = Theme::panelSkin (look);
    if (ps != nullptr)
    {
        // a vintage panel: its face (brushed metal: fine lines across it), then the cheeks or the rack ears
        g.setGradientFill (juce::ColourGradient (ps->face, 0, (float) faceArea.getY(), ps->face2, 0, (float) faceArea.getBottom(), false));
        g.fillRect (faceArea);
        if (ps->brushed)
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            for (float y = (float) faceArea.getY(); y < (float) faceArea.getBottom(); y += 3.0f) g.fillRect ((float) faceArea.getX(), y, (float) faceArea.getWidth(), 1.0f);
        }
        for (const auto side : { juce::Rectangle<int> (0, 0, faceArea.getX(), getHeight()), juce::Rectangle<int> (faceArea.getRight(), 0, getWidth() - faceArea.getRight(), getHeight()) })
        {
            if (side.getWidth() <= 0) continue;
            const auto sf = side.toFloat();
            if (ps->sides == 1)
            {
                // cheeks: wood with its grain running down them (or black)
                const bool wood = ps->side.getRed() > ps->side.getBlue() + 20;
                if (wood) g.drawImage (woodGrain (side.getWidth(), side.getHeight()), sf);
                else
                {
                    g.setGradientFill (juce::ColourGradient (ps->side, sf.getX(), 0, ps->side2, sf.getCentreX(), 0, true));
                    g.fillRect (sf);
                }
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                if (! wood) for (int k = 1; k < 5; ++k) g.fillRect (sf.getX() + sf.getWidth() * (float) k / 5.0f, sf.getY(), 1.0f, sf.getHeight());
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (side.getX() == 0 ? sf.withLeft (sf.getRight() - 2.0f) : sf.withWidth (2.0f));
            }
            else
            {
                // rack ears: a slightly darker metal, two screws in each
                g.setGradientFill (juce::ColourGradient (ps->side, sf.getX(), 0, ps->side2, sf.getRight(), 0, false));
                g.fillRect (sf);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRect (side.getX() == 0 ? sf.withLeft (sf.getRight() - 1.5f) : sf.withWidth (1.5f));
                for (const float yy : { px * 14.0f, (float) getHeight() - px * 14.0f })
                {
                    const float d = px * 5.0f;
                    const juce::Rectangle<float> sc (sf.getCentreX() - d * 0.5f, yy - d * 0.5f, d, d);
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white, sc.getX(), sc.getY(), juce::Colour (0xff6b6e73), sc.getRight(), sc.getBottom(), false));
                    g.fillEllipse (sc);
                    g.setColour (juce::Colour (0xff3a3c40));
                    g.drawLine (sc.getX() + d * 0.2f, sc.getCentreY(), sc.getRight() - d * 0.2f, sc.getCentreY(), std::max (1.0f, px * 0.5f));
                }
            }
        }
        // the title in the panel's display (no name of its own: the LCD as before)
        if (! titleArea.isEmpty() && model.isEmpty())
        {
            auto lcd = titleArea.reduced (0, px * 0.5f);
            g.setColour (ps->lcdBg);
            g.fillRoundedRectangle (lcd, px * 1.2f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (lcd, px * 1.2f, std::max (1.0f, px * 0.6f));
        }
    }
    // the title: as much of it as fits (never cut)
    const juce::String name = juce::String (inst().pluginName);
    const juce::String what = inst().pluginKey == kSubKey ? " - CLICK-FREE SUB BASS" : inst().pluginKey == kGrainKey ? " - GRAIN SYNTH"
                            : inst().pluginKey == kReeseKey ? " - REESE / HOOVER BASS"
                            : inst().pluginKey == kRaveKey ? " - JUNGLE KEYS"
                            : inst().pluginKey == kRomKey ? " - ROMPLER"
                            : inst().pluginKey == kMorphKey ? " - MORPHING FILTERS"
                            : inst().pluginKey == kAcidKey ? " - 303 + DEVIL FISH"
                            : inst().pluginKey == kTg101Key ? " - CLASSIC MONOSYNTH"
                            : inst().pluginKey == kHelixKey ? " - WAVETABLE SYNTH"
                            : inst().pluginKey == kCraftyKey ? " - WAVETABLE MONOSYNTH"
                            : inst().pluginKey == kPadKey ? " - LAYERED PADS"
                            : inst().pluginKey == kDrum808Key ? " - TR-808 DRUM MACHINE"
                            : inst().pluginKey == kDrum909Key ? " - TR-909 DRUM MACHINE" : "";
    const juce::String num = hex2 (proc.editorState.instrument + 1);
    const auto titleIn = ps != nullptr ? titleArea.reduced (px * 3, 0) : titleArea;
    if (ps != nullptr && model.isNotEmpty() && ! titleArea.isEmpty())
    {
        // (v1.01 II) the panel's name, big, and its line under it; the instrument in a small display beside them
        auto ta = titleArea;
        const auto inkCol = ps->pal.ink;
        const float h = ta.getHeight();
        const auto logo = PixelFont::face (PixelFont::SpaceGrotesk, h * 1.15f);
        const float lw = juce::GlyphArrangement::getStringWidth (logo, model);
        g.setColour (inkCol);
        g.setFont (logo);
        g.drawText (model, ta.withWidth (lw + px * 2), juce::Justification::centredLeft, false);
        ta.removeFromLeft (lw + px * 4);
        const auto small = PixelFont::face (PixelFont::BarlowCondensed, h * 0.62f);
        const float tw = juce::GlyphArrangement::getStringWidth (small, tagline);
        if (tw + px * 4 < ta.getWidth() * 0.55f)
        {
            g.setColour (ps->scale.isTransparent() ? inkCol : ps->scale);
            g.setFont (small);
            g.drawText (tagline, ta.withWidth (tw + px * 2), juce::Justification::centredLeft, false);
            ta.removeFromLeft (tw + px * 6);
        }
        // the instrument, in the panel's LCD
        const juce::String lcdText = num + " " + name;
        const float lcdW = std::min (ta.getWidth(), PixelFont::textWidth (lcdText, px) + px * 8);
        if (lcdW > px * 30)
        {
            auto lcd = ta.removeFromRight (lcdW).reduced (0, px * 0.8f);
            g.setColour (ps->lcdBg);
            g.fillRoundedRectangle (lcd, px * 1.2f);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawRoundedRectangle (lcd, px * 1.2f, std::max (1.0f, px * 0.6f));
            PixelFont::drawCentred (g, lcdText, lcd, px, ps->lcdInk);
        }
    }
    else
    for (const auto& t : { "INSTRUMENT " + num + ": " + name + what, "INSTRUMENT " + num + ": " + name, num + " " + name, name })
        if (PixelFont::textWidth (t, px) <= titleIn.getWidth()) { PixelFont::drawLeft (g, t, titleIn, px, ps != nullptr ? ps->lcdInk : pal.ink); break; }
    for (const auto& b : boxes)
    {
        if (b.area.isEmpty()) continue;
        std::optional<PixelFont::PanelType> panelType;   // (on a panel: measure the titles in the type they're drawn in)
        if (ps != nullptr) panelType.emplace();
        const auto titleWidth = [&] (const juce::String& c) { return ps != nullptr ? PixelFont::labelWidth (c, px) : PixelFont::textWidth (c, px); };
        juce::String head = b.title;
        // (ROM: a TONE's box says what's in it - "TONE 1: WND ACORD (3)", as much as fits)
        if (shownKey == kRomKey && b.title.startsWith ("TONE "))
        {
            const int t = juce::jlimit (0, kRomTones - 1, b.title.getTrailingIntValue() - 1);
            const auto& rb = inst().rom;
            const int n = rb != nullptr ? rb->zonesOf (t) : 0;
            const juce::String nm = n == 0 ? juce::String ("EMPTY") : juce::String (rb->toneName[(size_t) t]).toUpperCase();
            const float room = b.area.getWidth() - px * (ps != nullptr ? 12 : 30);
            for (const auto& c : { b.title + ": " + nm + (n > 0 ? " (" + juce::String (n) + ")" : juce::String()), b.title + ": " + nm, nm })
                if (titleWidth (c) <= room) { head = c; break; }
        }
        // (PAD: a layer playing a sample says which - "SOURCE: BREATH", on SAMPLES "LAYER A: BREATH", "LAYER B: 05 STRINGS")
        const int padL = shownKey == kPadKey ? padSampleBox (b.key) : -1;
        const auto padV = padL >= 0 ? readBuiltinState (inst()) : std::vector<float>();
        if (padL >= 0 && padV.size() >= (size_t) pad::Count && std::lround (padV[(size_t) pad::p (padL, pad::Wave)]) == pad::Sample)
        {
            const int l = padL;
            const auto& v = padV;
            const int slot = (int) std::lround (v[(size_t) pad::s (l, pad::SmpSlot)]);
            const int from = slot == 0 ? proc.editorState.instrument : slot - 1;
            juce::String nm = "NO SAMPLE";
            if (from >= 0 && from < kMaxInstruments && ! proc.song.instruments[(size_t) from].data.empty())
                nm = (slot == 0 ? juce::String() : hex2 (slot) + " ") + juce::String (proc.song.instruments[(size_t) from].name).toUpperCase();
            const juce::String t = b.title;
            const float room = b.area.getWidth() - px * (ps != nullptr ? 12 : 30);
            const juce::String letter = t.startsWith ("LAYER ") ? t.getLastCharacters (1) + ": " + nm : nm;   // ("A: BREATH" - which layer, still)
            for (const auto& c : { t + ": " + nm, letter, nm, t })
                if (titleWidth (c) <= room) { head = c; break; }
        }
        else if (padL >= 0 && b.key.startsWith ("7:"))   // (its WAVE isn't SAMPLE: LOAD SAMPLE... makes it one)
        {
            for (const auto& c : { b.title + ": NOT A SAMPLE", b.title.getLastCharacters (1) + ": NOT A SAMPLE", b.title })
                if (titleWidth (c) <= b.area.getWidth() - px * (ps != nullptr ? 12 : 30)) { head = c; break; }
        }
        if (b.display) { paintDisplay (g, b.area); continue; }
        if (ps == nullptr) { Theme::panelBox (g, b.area, head, pal); continue; }
        const int k = (int) (&b - boxes.data()) - (boxes.front().display ? 1 : 0);
        const auto col = ps->stripe[(k % 5 + 5) % 5];
        const float th = px * 11;
        if (drumLayout() && b.title != "OUTPUT")
        {
            // a drum's column: a name plate on top (its group's colour along it), a rule between the columns
            const int v = k;
            const auto grp = ps->stripe[juce::jlimit (0, 3, v * 4 / std::max (1, (int) drumPads.size()))];
            auto plate = b.area.withHeight (th).reduced (px, 0);
            g.setColour (look == Theme::PanelLook::DrumWood ? juce::Colour (0xffefe7d4) : look == Theme::PanelLook::DrumBlack ? juce::Colour (0xff1e1c18) : juce::Colour (0xff2a74c4));
            g.fillRoundedRectangle (plate, px * 0.8f);
            g.setColour (grp);
            g.fillRect (plate.withHeight (px * 1.6f));
            const auto f = PixelFont::face (PixelFont::BarlowCondensed, px * 7.0f);
            g.setFont (f);
            const float need = juce::GlyphArrangement::getStringWidth (f, head);
            if (need > plate.getWidth() - px * 2) g.setFont (PixelFont::face (PixelFont::BarlowCondensed, px * 7.0f * (plate.getWidth() - px * 2) / need));
            g.setColour (look == Theme::PanelLook::DrumWood ? juce::Colour (0xff1a1712) : juce::Colours::white);
            g.drawText (head, plate.withTrimmedTop (px * 1.2f), juce::Justification::centred, false);
            g.setColour (pal.ink.withAlpha (0.16f));
            g.fillRect (b.area.getRight() + px * 0.2f, b.area.getY(), std::max (1.0f, px * 0.5f), b.area.getHeight());
            continue;
        }
        if (ps->titleStyle == 0)
        {
            // a coloured bar across the top with the name in it, a strip at the foot, faint rules down the sides
            auto bar = b.area.withHeight (th);
            g.setColour (col);
            g.fillRoundedRectangle (bar, px * 0.6f);
            PixelFont::drawCentred (g, head, bar, px, ps->barInk);
            g.setColour (col);
            g.fillRoundedRectangle (b.area.withTop (b.area.getBottom() - px * 2.0f), px * 0.5f);
            g.setColour (pal.ink.withAlpha (0.10f));
            g.fillRect (b.area.getX(), bar.getBottom(), std::max (1.0f, px * 0.5f), b.area.getHeight() - th - px * 2);
            g.fillRect (b.area.getRight() - std::max (1.0f, px * 0.5f), bar.getBottom(), std::max (1.0f, px * 0.5f), b.area.getHeight() - th - px * 2);
        }
        else
        {
            // a printed outline, the name set into its top line
            const float t = std::max (1.5f, px * 0.6f);
            auto box = b.area.withTrimmedTop (th * 0.5f);
            const float tw = titleWidth (head) + px * 3;
            g.setColour (col);
            juce::Path pth;
            pth.startNewSubPath (box.getX() + px * 4, box.getY());
            pth.lineTo (box.getX() + px, box.getY()); pth.lineTo (box.getX() + px, box.getBottom()); pth.lineTo (box.getRight() - px, box.getBottom());
            pth.lineTo (box.getRight() - px, box.getY()); pth.lineTo (std::min (box.getRight() - px, box.getX() + px * 4 + tw), box.getY());
            g.strokePath (pth, juce::PathStrokeType (t));
            PixelFont::drawLeft (g, head, { box.getX() + px * 5.5f, b.area.getY(), tw, th }, px, ps->barInk);
        }
    }
    if (! drumDispArea.isEmpty()) paintDisplay (g, drumDispArea);
    if (! padsArea.isEmpty() && ps != nullptr)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (padsArea.expanded (px * 2), px * 1.5f);
    }
    if (browsingTone >= 0 && ! romHeadArea.isEmpty())
    {
        // the ROM browser's line: which tone, and what's in it now
        const auto nm = inst().rom != nullptr ? juce::String (inst().rom->toneName[(size_t) browsingTone]).toUpperCase() : juce::String();
        const auto t = "TONE " + juce::String (browsingTone + 1);
        for (const auto& line : { t + ": " + (nm.isEmpty() ? juce::String ("PICK A SOUND") : nm) + " - PLAY THE KEYS TO HEAR IT",
                                  t + ": " + (nm.isEmpty() ? juce::String ("PICK A SOUND") : nm), t })
            if (PixelFont::textWidth (line, px) <= romHeadArea.getWidth()) { PixelFont::drawLeft (g, line, romHeadArea, px, pal.ink); break; }
    }
    if (! waveArea.isEmpty())
    {
        // GRANULAR: the sound, POSITION (and where SCAN has taken it), the grains playing
        // (too long for the box: up to its first " - " or ":", e.g. "INPUT", "SAMPLE 01")
        juce::String head = waveName;
        if (PixelFont::textWidth (head, px) * 1.15f > waveArea.getWidth() - px * 24)
            head = head.upToFirstOccurrenceOf (" - ", false, false).upToFirstOccurrenceOf (":", false, false);
        const float title = px * 11;
        auto in = waveArea.reduced (px * 4).withTrimmedTop (title);
        juce::Colour waveInk = pal.ink.withAlpha (0.75f), lineInk = pal.pressed, footInk = pal.ink;
        if (ps != nullptr)
        {
            // (a hardware panel: in its display - the sound in the display's own colours)
            PixelFont::PanelType type;
            const bool lcd = ps->display == 2;
            const juce::Colour bg (lcd ? 0xffa8c27c : 0xff04140a), fg (lcd ? 0xff1b2a10 : 0xff5cff8a);
            auto bez = waveArea.reduced (px);
            g.setColour (juce::Colour (0xff0b0b0b)); g.fillRoundedRectangle (bez.expanded (px * 1.2f), px * 1.5f);
            g.setColour (bg); g.fillRoundedRectangle (bez, px);
            PixelFont::drawLeft (g, head, bez.reduced (px * 2, 0).withHeight (title), px, fg);
            in = bez.reduced (px * 2).withTrimmedTop (title);
            waveInk = fg.withAlpha (0.85f); lineInk = fg; footInk = fg;
        }
        else
        {
            Theme::panelBox (g, waveArea, head, pal);
            g.setColour (pal.paper);
            g.fillRect (in);
        }
        const auto foot = in.removeFromBottom (px * 10).reduced (px * 2, 0);   // (the words under the sound, never over it)
        in.removeFromBottom (px);
        if (waveOk && ! wavePeaks.empty())
        {
            const float mid = in.getCentreY(), half = in.getHeight() * 0.46f;
            const float w = in.getWidth() / (float) wavePeaks.size();
            g.setColour (waveInk);
            for (size_t k = 0; k < wavePeaks.size(); ++k)
            {
                const float h = std::max (px * 0.5f, wavePeaks[k] * half);
                g.fillRect (in.getX() + (float) k * w, mid - h, std::max (1.0f, w), h * 2);
            }
        }
        else PixelFont::drawCentred (g, "NOTHING TO READ YET", in, px, pal.faint);
        const float pos = valueOf (grain::Position) * 0.01f;
        g.setColour (lineInk);
        g.fillRect (in.getX() + pos * in.getWidth() - px, in.getY(), px * 2, in.getHeight());
        if (waveGrains > 0)
        {
            g.setColour (pal.pressedInk);
            g.fillRect (in.getX() + wavePos * in.getWidth() - px * 0.5f, in.getY(), px, in.getHeight());
        }
        juce::String note = juce::String (waveGrains) + " GRAINS - CLICK TO SET POSITION";
        if (PixelFont::textWidth (note, px) > foot.getWidth()) note = juce::String (waveGrains) + " GRAINS";
        PixelFont::drawLeft (g, note, foot, px, footInk);
    }
    if (hasKeyboardFocus (true)) { g.setColour (pal.ink); g.drawRect (getLocalBounds().toFloat(), std::max (2.0f, px * 0.5f)); }
}


// (v1.01 II) a control's setting, by name (the nth of that name: ROM's TONE 2 LEVEL is LEVEL, 1)
float EnginePage::vraw (const char* name, int nth) const
{
    const auto& ps = builtinParams (shownKey);
    for (size_t i = 0; i < ps.size(); ++i)
        if (juce::String (ps[i].name) == name && nth-- == 0)
            return i < shownValues.size() ? shownValues[i] : ps[i].def;
    return 0.0f;
}

float EnginePage::vnorm (const char* name, int nth) const
{
    const auto& ps = builtinParams (shownKey);
    for (size_t i = 0; i < ps.size(); ++i)
        if (juce::String (ps[i].name) == name && nth-- == 0)
            return juce::jlimit (0.0f, 1.0f, ps[i].to01 (i < shownValues.size() ? shownValues[i] : ps[i].def));
    return 0.0f;
}

juce::Image EnginePage::woodGrain (int w, int h)
{
    // walnut: dark and light streaks running down the cheek, a few knots of darker grain (made once per size)
    static juce::Image img;
    if (img.isValid() && img.getWidth() == w && img.getHeight() == h) return img;
    img = juce::Image (juce::Image::RGB, std::max (1, w), std::max (1, h), false);
    juce::Random rnd (1984);
    std::vector<float> streak ((size_t) std::max (1, w));
    float acc = 0;
    for (auto& v : streak) { acc = acc * 0.82f + (rnd.nextFloat() - 0.5f); v = acc; }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float wave = std::sin ((float) y * 0.018f + streak[(size_t) x] * 2.2f + (float) x * 0.35f);
            const float fine = std::sin ((float) y * 0.11f + (float) x * 1.7f + streak[(size_t) x] * 6.0f) * 0.25f;
            const float t = juce::jlimit (0.0f, 1.0f, 0.5f + 0.32f * wave + 0.18f * fine + 0.12f * streak[(size_t) x]);
            const float edge = 1.0f - 0.35f * std::pow (std::abs ((float) x / std::max (1, w - 1) - 0.5f) * 2.0f, 3.0f);
            const auto c = juce::Colour (0xff3e2210).interpolatedWith (juce::Colour (0xff8a5530), t);
            img.setPixelAt (x, y, c.withMultipliedBrightness (edge));
        }
    return img;
}

void EnginePage::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float px = Theme::px;
    const auto* ps = Theme::panelSkin (look);
    int kind = ps != nullptr ? ps->display : 0;
    if (shownKey == kMorphKey || shownKey == kCraftyKey) kind = 3;
    struct Look { juce::uint32 bg, fg, grid; };
    static const Look looks[] = { { 0xff04140a, 0xff5cff8a, 0xff0f3a1e }, { 0xff140b02, 0xffffb43a, 0xff3a2408 },
                                  { 0xffa8c27c, 0xff1b2a10, 0xff90a866 }, { 0xff041218, 0xff4fe8ff, 0xff0d3442 } };
    const auto L = looks[juce::jlimit (0, 3, kind)];
    const juce::Colour bg (L.bg), fg (L.fg), grid (L.grid);
    const bool glass = kind != 2;
    // the bezel, the glass
    auto r = area.reduced (px);
    g.setColour (juce::Colour (0xff0b0b0b));
    g.fillRoundedRectangle (r.expanded (px * 1.2f), px * (glass ? 3.0f : 1.5f));
    g.setColour (juce::Colour (0xff45474b));
    g.drawRoundedRectangle (r.expanded (px * 1.2f), px * (glass ? 3.0f : 1.5f), std::max (1.0f, px * 0.6f));
    g.setColour (bg);
    g.fillRoundedRectangle (r, px * (glass ? 2.5f : 1.0f));
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    // the graticule
    g.setColour (grid);
    for (int k = 1; k < 10; ++k) g.fillRect (r.getX() + r.getWidth() * (float) k / 10.0f, r.getY(), 1.0f, r.getHeight());
    for (int k = 1; k < 6; ++k) g.fillRect (r.getX(), r.getY() + r.getHeight() * (float) k / 6.0f, r.getWidth(), 1.0f);
    // the trace: one function of x (0..1) giving -1..1, drawn twice (a soft glow, then the line)
    juce::String label;
    auto trace = [&] (juce::Rectangle<float> a, const std::function<float (float)>& f, float thick, float alpha, int n = 360)
    {
        juce::Path p;
        for (int i = 0; i <= n; ++i)
        {
            const float x = (float) i / (float) n;
            const float y = juce::jlimit (-1.2f, 1.2f, f (x));
            const juce::Point<float> pt (a.getX() + x * a.getWidth(), a.getCentreY() - y * a.getHeight() * 0.42f);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        if (glass) { g.setColour (fg.withAlpha (0.22f * alpha)); g.strokePath (p, juce::PathStrokeType (thick * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        g.setColour (fg.withAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    const float pi2 = juce::MathConstants<float>::twoPi;
    auto saw = [] (float ph) { ph -= std::floor (ph); return ph * 2.0f - 1.0f; };
    auto sq = [] (float ph, float w) { ph -= std::floor (ph); return ph < w ? 1.0f : -1.0f; };
    auto tri = [] (float ph) { ph -= std::floor (ph); return ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f; };
    const float thick = std::max (1.5f, px * 0.75f);
    auto in = r.reduced (px * 3, px * 2).withTrimmedTop (px * 6);
    const auto& key = shownKey;
    if (key == kSubKey)
    {
        const float k = 1.0f + vnorm ("SHAPE") * 7.0f, o2 = vnorm ("OSC 2"), h2 = vnorm ("HARM 2"), h3 = vnorm ("HARM 3");
        const float ratio[] = { 2.0f, 1.5f, 3.0f, 0.5f };
        const float rt = ratio[juce::jlimit (0, 3, (int) std::lround (vraw ("INTERVAL")))];
        const float norm = std::tanh (k * (1.0f + o2 + h2 + h3));
        trace (in, [&] (float x) { const float t = x * pi2 * 3; return std::tanh (k * (std::sin (t) + h2 * std::sin (2 * t) + h3 * std::sin (3 * t) + o2 * std::sin (rt * t))) / std::max (0.3f, norm); }, thick, 1.0f);
        label = "SCOPE - SHAPE " + juce::String (juce::roundToInt (vnorm ("SHAPE") * 100)) + "%";
    }
    else if (key == kReeseKey)
    {
        const int voices = juce::jlimit (1, 9, (int) std::lround (vraw ("VOICES")));
        const float det = 0.004f + vnorm ("DETUNE") * 0.06f;
        const int wave = (int) std::lround (vraw ("WAVE"));
        auto one = [&] (float ph) { return wave == 1 ? sq (ph, 0.5f) : wave == 2 ? 0.5f * (saw (ph) + sq (ph, 0.5f)) : wave == 3 ? tri (ph) : saw (ph); };
        trace (in, [&] (float x) { float s = 0; for (int i = 0; i < voices; ++i) { const float o = (float) i - (float) (voices - 1) * 0.5f; s += one (x * 3.0f * (1.0f + o * det) + o * 0.17f); } return s / std::sqrt ((float) voices) * 0.75f; }, thick, 1.0f, 480);
        label = "VOICES " + juce::String (voices) + " - DETUNE " + juce::String (juce::roundToInt (vnorm ("DETUNE") * 100)) + "%";
    }
    else if (key == kRaveKey)
    {
        static const float chords[][4] = { { 1, 1.189f, 1.498f, 0 }, { 1, 1.26f, 1.498f, 0 }, { 1, 1.189f, 1.498f, 1.782f }, { 1, 1.498f, 2, 0 }, { 1, 1.335f, 1.498f, 0 }, { 1, 1.26f, 1.498f, 1.888f } };
        const auto& ch = chords[juce::jlimit (0, 5, (int) std::lround (vraw ("CHORD")))];
        const float strum = vnorm ("STRUM"), dec = 0.5f + vnorm ("DECAY") * 4.0f;
        trace (in, [&] (float x) { float s = 0; for (int i = 0; i < 4; ++i) if (ch[i] > 0) { const float t0 = (float) i * strum * 0.16f; if (x > t0) s += saw ((x - t0) * ch[i] * 9.0f); } return s / 3.0f * std::exp (-x * 6.0f / dec); }, thick, 1.0f, 480);
        label = "STAB - STRUM " + juce::String (juce::roundToInt (strum * 100)) + "%";
    }
    else if (key == kRomKey)
    {
        // the four tones: each a lane, as loud as its LEVEL
        for (int t = 0; t < 4; ++t)
        {
            auto lane = in.withHeight (in.getHeight() / 4).translated (0, in.getHeight() / 4 * (float) t);
            const float lv = vnorm ("LEVEL", t), tn = 1.0f + vraw ("TUNE", t) / 24.0f;
            trace (lane.withTrimmedLeft (px * 9), [&] (float x) { return std::sin (x * (40.0f + (float) t * 13.0f) * tn) * std::exp (-x * (2.0f + (float) t)) * (0.4f + 0.6f * std::abs (std::sin (x * 7 + (float) t))) * lv * 1.6f; }, thick * 0.8f, 1.0f, 240);
            PixelFont::drawLeft (g, "T" + juce::String (t + 1), lane.withWidth (px * 9), px * 0.8f, fg);
        }
        label = "TONES 1-4";
    }
    else if (key == kMorphKey)
    {
        const float m = vnorm ("MORPH"), fq = vnorm ("FREQ"), em = 0.4f + vnorm ("EMPHASIS");
        const float shift = std::pow (2.0f, (fq - 0.5f) * 2.0f);
        const float f1 = (700 + (270 - 700) * m) * shift, f2 = (1220 + (2290 - 1220) * m) * shift, f3 = (2600 + (3010 - 2600) * m) * shift;
        auto pk = [] (float f, float c, float bw) { const float d = (f - c) / bw; return 1.0f / (1.0f + d * d); };
        trace (in, [&] (float x) { const float f = std::pow (10.0f, 2.0f + x * 2.0f); return (pk (f, f1, 90) * 1.1f + pk (f, f2, 160) * 0.9f + pk (f, f3, 260) * 0.55f) * em * 1.4f - 0.9f; }, thick, 1.0f, 300);
        label = juce::String ("MORPH ") + (m < 0.33f ? "AH" : m < 0.66f ? "EH" : "EE") + " - " + juce::String (juce::roundToInt (m * 100)) + "%";
    }
    else if (key == kAcidKey)
    {
        const float cut = vnorm ("CUTOFF"), res = vnorm ("RESONANCE");
        const bool square = std::lround (vraw ("WAVE")) == 1;
        const int n = 3 + (int) std::lround (cut * 26.0f);
        trace (in, [&] (float x) { float s = 0; for (int h = 1; h <= 40; ++h) { if (square && h % 2 == 0) continue; float gn = h <= n ? 1.0f : std::pow ((float) n / (float) h, 4.0f); if (std::abs (h - n) < 2) gn *= 1.0f + res * 5.0f; s += std::sin (x * pi2 * 3 * (float) h) / (float) h * gn; } return s * 0.45f; }, thick, 1.0f, 480);
        label = "SCOPE - CUTOFF " + juce::String (juce::roundToInt (cut * 100)) + "% RES " + juce::String (juce::roundToInt (res * 100)) + "%";
    }
    else if (key == kTg101Key)
    {
        const float pul = vnorm ("PULSE"), sw = vnorm ("SAW"), sb = vnorm ("SUB"), nz = vnorm ("NOISE"), pw = 0.5f - vnorm ("PW") * 0.45f;
        const int st = (int) std::lround (vraw ("SUB TYPE"));
        juce::Random rnd (7);
        trace (in, [&] (float x) { const float ph = x * 3.0f; const float sub = st == 2 ? sq (ph * 0.25f, 0.25f) : sq (ph * (st == 1 ? 0.25f : 0.5f), 0.5f);
                                   return (pul * sq (ph, pw) + sw * saw (ph) + sb * sub + nz * (rnd.nextFloat() * 2 - 1)) / std::max (0.6f, pul + sw + sb + nz) * 0.9f; }, thick, 1.0f, 600);
        label = "VCO - PW " + juce::String (juce::roundToInt (vnorm ("PW") * 100)) + "%";
    }
    else if (key == kPadKey)
    {
        // the four layers, a pane each (the dim ones are off)
        static const char* const waves[] = { "SAW", "SQUARE", "PWM", "TRI", "SINE", "ORGAN", "VOX", "GLASS", "AIR", "SAMPLE" };
        for (int l = 0; l < 4; ++l)
        {
            auto pane = in.withSize (in.getWidth() / 2, in.getHeight() / 2).translated (in.getWidth() / 2 * (float) (l % 2), in.getHeight() / 2 * (float) (l / 2)).reduced (px);
            const bool on = std::lround (vraw ("LAYER", l)) != 0;
            const int w = juce::jlimit (0, 9, (int) std::lround (vraw ("WAVE", l)));
            const float det = 0.003f + vnorm ("DETUNE", l) * 0.05f;
            const int vc = juce::jlimit (1, 7, (int) std::lround (vraw ("VOICES", l)));
            g.setColour (fg.withAlpha (on ? 0.6f : 0.2f));
            g.drawRect (pane, 1.0f);
            auto one = [&] (float ph) { switch (w) { case 1: return sq (ph, 0.5f); case 2: return sq (ph, 0.3f); case 3: return tri (ph); case 4: return std::sin (ph * pi2);
                                                    case 5: return 0.6f * std::sin (ph * pi2) + 0.3f * std::sin (ph * pi2 * 2) + 0.2f * std::sin (ph * pi2 * 4); case 6: return std::sin (ph * pi2) * std::sin (ph * pi2 * 3.1f);
                                                    case 7: return std::sin (ph * pi2 + 3 * std::sin (ph * pi2 * 2)); case 8: return std::sin (ph * 211.7f) * std::sin (ph * 97.1f); default: return saw (ph); } };
            trace (pane.reduced (px * 2).withTrimmedTop (px * 5), [&] (float x) { float s = 0; for (int i = 0; i < vc; ++i) { const float o = (float) i - (float) (vc - 1) * 0.5f; s += one (x * 3.0f * (1.0f + o * det) + o * 0.2f); } return s / std::sqrt ((float) vc) * 0.7f; }, thick * 0.8f, on ? 1.0f : 0.3f, 200);
            PixelFont::drawLeft (g, juce::String::charToString ((juce::juce_wchar) ('A' + l)) + " " + waves[w], pane.withHeight (px * 6).translated (px * 2, px), px * 0.75f, fg.withAlpha (on ? 1.0f : 0.4f));
        }
        label = "LAYERS A-D";
    }
    else if (key == kHelixKey)
    {
        // the wavetable as a stack of frames, the one WT POS plays lit
        const float pos = vnorm ("WT POS"), warp = vnorm ("WARP AMT");
        const int N = 14, cur = juce::roundToInt (pos * (float) (N - 1));
        const float stepX = in.getWidth() * 0.022f, stepY = in.getHeight() * 0.045f;
        const auto frameArea = juce::Rectangle<float> (in.getX(), in.getY() + stepY * (float) (N - 1) * 0.6f, in.getWidth() * 0.66f, in.getHeight() * 0.5f);
        for (int f = N - 1; f >= 0; --f)
        {
            const float t = (float) f / (float) (N - 1);
            const auto fa = frameArea.translated (stepX * (float) f, -stepY * (float) f * 0.6f);
            trace (fa, [&] (float x) { float y = 0; const float H = 1 + t * 14; for (int h = 1; h <= 15; ++h) { const float gn = (float) h <= H ? 1.0f : std::max (0.0f, 1.0f - ((float) h - H)); y += std::sin ((x + warp * 0.2f * std::sin (x * pi2)) * pi2 * (float) h) / (float) h * gn * ((h % 2) || t > 0.5f ? 1.0f : t * 2.0f); } return y * 0.75f; },
                   f == cur ? thick * 1.3f : thick * 0.5f, f == cur ? 1.0f : 0.38f, 120);
        }
        label = "WT POS " + juce::String (juce::roundToInt (pos * 255)).paddedLeft ('0', 3);
    }
    else if (key == kCraftyKey)
    {
        const float mix = vnorm ("MIX"), b1 = vnorm ("BANK 1"), w1 = vnorm ("WAVE 1"), b2 = vnorm ("BANK 2"), w2 = vnorm ("WAVE 2");
        auto family = [&] (float bank, float wave, float ph)
        {
            const int fam = (int) (bank * 4.99f);
            const float m = wave;
            switch (fam)
            {
                case 0: return saw (ph) * (1 - m) + sq (ph, 0.5f) * m;
                case 1: return std::sin (ph * pi2 + m * 4 * std::sin (ph * pi2 * 2));
                case 2: return std::round (std::sin (ph * pi2) * (2 + m * 6)) / (2 + m * 6);
                case 3: return std::sin (ph * pi2 * (1 + std::floor (m * 5))) * std::sin (ph * juce::MathConstants<float>::pi);
                default: return tri (ph) * (1 - m) + std::sin (ph * pi2 * 3) * m;
            }
        };
        auto left = in.withWidth (in.getWidth() * 0.48f).withHeight (in.getHeight() * 0.48f), right = left.translated (in.getWidth() * 0.52f, 0);
        g.setColour (fg.withAlpha (0.45f)); g.drawRect (left, 1.0f); g.drawRect (right, 1.0f);
        trace (left.reduced (px), [&] (float x) { return family (b1, w1, x * 2) * 0.8f; }, thick * 0.7f, 0.7f, 160);
        trace (right.reduced (px), [&] (float x) { return family (b2, w2, x * 2) * 0.8f; }, thick * 0.7f, 0.7f, 160);
        trace (in.withTrimmedTop (in.getHeight() * 0.52f), [&] (float x) { return (family (b1, w1, x * 3) * (1 - mix) + family (b2, w2, x * 3) * mix) * 0.85f; }, thick, 1.0f, 300);
        label = "MIX " + juce::String (juce::roundToInt (mix * 127));
    }
    else if (drumLayout())
    {
        // the kick: its pitch falling, ringing down for DECAY
        const float tune = vnorm ("TUNE"), dec = 0.15f + vnorm ("DECAY") * 0.85f;
        const bool nine = key == kDrum909Key;
        trace (in, [&] (float x) { const float t = x * 0.9f; const float f0 = 35.0f + tune * 40.0f;
                                   const float ph = pi2 * (f0 * t + (nine ? 260.0f : 120.0f) * (1.0f - std::exp (-t * 30.0f)) / 30.0f);
                                   return std::sin (ph) * std::exp (-t / (dec * 0.35f)) + (nine && t < 0.01f ? 0.6f : 0.0f); }, thick, 1.0f, 400);
        label = "KICK - DECAY " + juce::String (juce::roundToInt (vnorm ("DECAY") * 100)) + "%";
    }
    g.restoreState();
    // the display's line, top left (as much as fits: up to its " - " when it's narrow)
    {
        PixelFont::PanelType type;
        auto line = r.reduced (px * 2, px).withHeight (px * 7);
        if (PixelFont::labelWidth (label, px * 0.75f) > line.getWidth()) label = label.upToFirstOccurrenceOf (" - ", false, false);
        PixelFont::drawLeft (g, label, line, px * 0.75f, fg);
    }
    if (glass)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX(), r.getCentreY(), false));
        g.fillRoundedRectangle (r, px * 2.5f);
    }
}

// play it from the computer keyboard (as the pattern's keys)
bool EnginePage::keyPressed (const juce::KeyPress& k)
{
    if (browsingTone >= 0)
    {
        // (the ROM browser: step through the sounds and categories - the note keys still play)
        const int code = k.getKeyCode();
        if (code == juce::KeyPress::escapeKey) { closeRomBrowser (false); return true; }
        if (code == juce::KeyPress::returnKey) { closeRomBrowser (true); return true; }
        if (code == juce::KeyPress::upKey)     { romPickSound (romSound < 0 ? 0 : romSound - 1); return true; }
        if (code == juce::KeyPress::downKey)   { romPickSound (romSound < 0 ? 0 : romSound + 1); return true; }
        if (code == juce::KeyPress::leftKey)   { romPickCategory (romCat - 1); return true; }
        if (code == juce::KeyPress::rightKey)  { romPickCategory (romCat + 1); return true; }
    }
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
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

bool EnginePage::keyStateChanged (bool)
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
} // namespace prv::ui
