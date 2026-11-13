// Paula's Revenge - a built-in synth's page
#include "EnginePage.h"
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
    loadSampleBtn.onClick = [this] { if (onLoadSample) onLoadSample(); };
    addChildComponent (loadSampleBtn);
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
    if ((inst().pluginKey == kSubKey && param == sub::BeatOn) || (inst().pluginKey == kGrainKey && param == grain::Source)) resized();   // (rows come and go)
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
    for (auto& r : rows) { if (r.slider) removeChildComponent (r.slider.get()); if (r.number) removeChildComponent (r.number.get()); if (r.cycle) removeChildComponent (r.cycle.get()); for (auto& b : r.choices) removeChildComponent (b.get()); }
    rows.clear(); boxes.clear();
    for (auto& t : genTabs) removeChildComponent (t.get());
    genTabs.clear(); genTabNames.clear();
    const auto oldKey = shownKey;
    shownKey = inst().pluginKey;
    const auto& ps = builtinParams (shownKey);
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        const auto& q = ps[(size_t) i];
        Row r; r.param = i;
        if (q.kind == 3)
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
            boxes.push_back ({ sec, {}, {}, grainPage && sec == "SOURCE" ? 2 : shownKey == kRomKey && sec == "MORPH FILTER" ? 3 : second ? 1 : 0, sec });
            box = boxes.end() - 1;
        }
        box->rowIndex.push_back ((int) rows.size());
        rows.push_back (std::move (r));
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
    const auto& ps = builtinParams (shownKey);
    for (auto& r : rows)
    {
        const auto& q = ps[(size_t) r.param];
        const float x = v[(size_t) r.param];
        if (r.slider) r.slider->setValue (q.to01 (x));
        if (r.number) r.number->setValue ((int) std::lround (x));
        for (int k = 0; k < (int) r.choices.size(); ++k) r.choices[(size_t) k]->setToggleState (std::lround (x) == k, juce::dontSendNotification);
        if (r.cycle) r.cycle->setButtonText (juce::String (q.name) + ": " + juce::String (q.text (x)));
    }
    repaint();
}

void EnginePage::refresh()
{
    if (browsingTone >= 0 && (shownInstrument != proc.editorState.instrument || inst().pluginKey != shownKey)) { browsingTone = -1; browseBefore = nullptr; closeRomBrowser (true); }
    if (inst().pluginKey != shownKey || rows.empty()) { build(); resized(); }
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
    else pageShown = k == 3 && shownKey == kRomKey ? 3 : juce::jlimit (0, 1, k);   // (3: ROM's MORPH FILTER)
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

void EnginePage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    const int title = (int) (px * 11);
    if (! relayout) subPages = false;
    for (auto& tb : toneBtn) tb.setVisible (false);
    for (auto& sb : sampleBtn) sb.setVisible (false);
    romFolderBtn.setVisible (false);
    updateToneButtons();
    auto r = getLocalBounds().reduced (gap);
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
    if (inst().pluginKey == kSubKey || inst().pluginKey == kGrainKey) vals = readBuiltinState (inst());
    auto hidden = [&] (const Row& rw)
    {
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
    auto boxHeight = [&] (const Box& b, int width)
    {
        int lines = 0;
        for (int ri : b.rowIndex) if (! hidden (rows[(size_t) ri])) lines += choiceLines (rows[(size_t) ri], width - gap * 4);
        if (shownKey == kRomKey && b.title.startsWith ("TONE ")) ++lines;   // (its samples' button)
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
        for (int ri : b.rowIndex)
        {
            auto& rw = rows[(size_t) ri];
            if (hidden (rw))
            {
                for (juce::Component* c : { (juce::Component*) rw.slider.get(), (juce::Component*) rw.number.get(), (juce::Component*) rw.cycle.get() }) if (c) c->setVisible (false);
                for (auto& c : rw.choices) c->setVisible (false);
                continue;
            }
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
    };

    // GRANULAR: the sound it reads (with its SOURCE box beside it) across the top, then its controls on two pages
    waveArea = {};
    const bool generic = ! genTabNames.empty();
    const bool paged = generic || shownKey == kGrainKey || shownKey == kReeseKey || shownKey == kRomKey || shownKey == kMorphKey || shownKey == kAcidKey || (shownKey == kSubKey && subPages);
    for (auto& b : pageBtn) b.setVisible (paged && ! generic);
    pageBtn[2].setVisible (paged && ! generic && shownKey == kRomKey);
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
        pageBtn[0].setButtonText ("GRAINS"); pageBtn[1].setButtonText ("LFOS + FILTER");
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
        const int tw = std::max (charsW (8), (tabs.getWidth() - gap) * 2 / 5);
        pageBtn[0].setBounds (tabs.removeFromLeft (tw)); tabs.removeFromLeft (gap);
        pageBtn[1].setBounds (tabs);
        for (int k = 0; k < 2; ++k) pageBtn[k].setToggleState (pageShown == k, juce::dontSendNotification);
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
                for (juce::Component* c : { (juce::Component*) rw.slider.get(), (juce::Component*) rw.number.get(), (juce::Component*) rw.cycle.get() }) if (c) c->setVisible (false);
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
        const bool on = ! paged || b.page == pageShown || b.page == 2;
        if (on && b.page != 2) shownBoxes.push_back (&b);
        if (! on) b.area = {};
        for (int ri : b.rowIndex)
        {
            auto& rw = rows[(size_t) ri];
            const bool show = on && ! hidden (rw);
            for (juce::Component* c : { (juce::Component*) rw.slider.get(), (juce::Component*) rw.number.get(), (juce::Component*) rw.cycle.get() }) if (c) c->setVisible (show);
            for (auto& c : rw.choices) c->setVisible (show);
        }
    }
    // the boxes in columns: as few as fit (but never so narrow that a slider has no room), each filled top to bottom
    const int minColW = charsW (16) + (int) (px * 24);
    const int maxCols = juce::jlimit (1, 3, (r.getWidth() + gap * 3) / (minColW + gap * 3));
    int cols = 1, colW = r.getWidth();
    for (int pass = 0; pass < 2; ++pass)
    {
        int total = 0;
        for (const auto* b : shownBoxes) total += boxHeight (*b, colW) + gap * 2;
        cols = juce::jlimit (1, maxCols, (int) std::ceil ((double) total / std::max (1, r.getHeight())));
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
        int h[3] {};
        for (int k = 0; k < n; ++k) h[colOf[(size_t) k]] += hs[(size_t) k];
        return std::max ({ h[0], h[1], h[2] });
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
    if (cols > 1 && n <= 12)
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
        resized();
        relayout = false;
    }
}

void EnginePage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    // the title: as much of it as fits (never cut)
    const juce::String name = juce::String (inst().pluginName);
    const juce::String what = inst().pluginKey == kSubKey ? " - CLICK-FREE SUB BASS" : inst().pluginKey == kGrainKey ? " - GRAIN SYNTH"
                            : inst().pluginKey == kReeseKey ? " - REESE / HOOVER BASS"
                            : inst().pluginKey == kRaveKey ? " - JUNGLE KEYS"
                            : inst().pluginKey == kRomKey ? " - ROMPLER"
                            : inst().pluginKey == kMorphKey ? " - MORPHING FILTERS"
                            : inst().pluginKey == kAcidKey ? " - 303 + DEVIL FISH"
                            : inst().pluginKey == kPadKey ? " - LAYERED PADS" : "";
    const juce::String num = hex2 (proc.editorState.instrument + 1);
    for (const auto& t : { "INSTRUMENT " + num + ": " + name + what, "INSTRUMENT " + num + ": " + name, num + " " + name, name })
        if (PixelFont::textWidth (t, px) <= titleArea.getWidth()) { PixelFont::drawLeft (g, t, titleArea, px, pal.ink); break; }
    for (const auto& b : boxes)
    {
        if (b.area.isEmpty()) continue;
        juce::String head = b.title;
        // (ROM: a TONE's box says what's in it - "TONE 1: WND ACORD (3)", as much as fits)
        if (shownKey == kRomKey && b.title.startsWith ("TONE "))
        {
            const int t = juce::jlimit (0, kRomTones - 1, b.title.getTrailingIntValue() - 1);
            const auto& rb = inst().rom;
            const int n = rb != nullptr ? rb->zonesOf (t) : 0;
            const juce::String nm = n == 0 ? juce::String ("EMPTY") : juce::String (rb->toneName[(size_t) t]).toUpperCase();
            const float room = b.area.getWidth() - px * 30;
            for (const auto& c : { b.title + ": " + nm + (n > 0 ? " (" + juce::String (n) + ")" : juce::String()), b.title + ": " + nm, nm })
                if (PixelFont::textWidth (c, px) <= room) { head = c; break; }
        }
        Theme::panelBox (g, b.area, head, pal);
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
        Theme::panelBox (g, waveArea, head, pal);
        const float title = px * 11;
        auto in = waveArea.reduced (px * 4).withTrimmedTop (title);
        g.setColour (pal.paper);
        g.fillRect (in);
        const auto foot = in.removeFromBottom (px * 10).reduced (px * 2, 0);   // (the words under the sound, never over it)
        in.removeFromBottom (px);
        if (waveOk && ! wavePeaks.empty())
        {
            const float mid = in.getCentreY(), half = in.getHeight() * 0.46f;
            const float w = in.getWidth() / (float) wavePeaks.size();
            g.setColour (pal.ink.withAlpha (0.75f));
            for (size_t k = 0; k < wavePeaks.size(); ++k)
            {
                const float h = std::max (px * 0.5f, wavePeaks[k] * half);
                g.fillRect (in.getX() + (float) k * w, mid - h, std::max (1.0f, w), h * 2);
            }
        }
        else PixelFont::drawCentred (g, "NOTHING TO READ YET", in, px, pal.faint);
        const float pos = valueOf (grain::Position) * 0.01f;
        g.setColour (pal.pressed);
        g.fillRect (in.getX() + pos * in.getWidth() - px, in.getY(), px * 2, in.getHeight());
        if (waveGrains > 0)
        {
            g.setColour (pal.pressedInk);
            g.fillRect (in.getX() + wavePos * in.getWidth() - px * 0.5f, in.getY(), px, in.getHeight());
        }
        juce::String note = juce::String (waveGrains) + " GRAINS - CLICK TO SET POSITION";
        if (PixelFont::textWidth (note, px) > foot.getWidth()) note = juce::String (waveGrains) + " GRAINS";
        PixelFont::drawLeft (g, note, foot, px, pal.ink);
    }
    if (hasKeyboardFocus (true)) { g.setColour (pal.ink); g.drawRect (getLocalBounds().toFloat(), std::max (2.0f, px * 0.5f)); }
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
