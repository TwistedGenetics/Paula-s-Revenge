// Paula's Revenge - the MODULATION page (see ModPage.h)
#include "ModPage.h"
#include "MixerPage.h"
#include "ChoiceBox.h"
#include "../engine/Modulation.h"

namespace prv::ui
{
struct ModPage::Row
{
    PixButton name { "-" };
    MiniBar amount { "AMOUNT" };
    PixButton remove { "X" };
};

namespace
{
    juce::String amountText (float a) { const int v = juce::roundToInt (a * 100); return (v > 0 ? "+" : "") + juce::String (v) + "%"; }
}

ModPage::ModPage (PaulaProcessor& p, MixerPage& m) : proc (p), mixer (m)
{
    for (int i = 0; i < kMods; ++i)
    {
        modBtn[i] = std::make_unique<PixButton> ("", true);
        modBtn[i]->setClickingTogglesState (false);
        modBtn[i]->onClick = [this, i] { select (i); };
        addAndMakeVisible (*modBtn[i]);
    }
    sourceBtn.onClick = [this] { showSourceMenu(); };
    clearBtn.onClick = [this]
    {
        edit ("MOD " + juce::String (sel + 1) + " CLEARED", false, [] (Modulator& md) { md = Modulator {}; });
        if (onStatus) onStatus ("MOD " + juce::String (sel + 1) + " CLEARED");
    };
    addBtn.onClick = [this] { showTargetMenu(); };
    macroA.onClick = [this] { setMacroKnob (0); };
    macroB.onClick = [this] { setMacroKnob (1); };
    for (auto* c : std::initializer_list<juce::Component*> { &sourceBtn, &clearBtn, &addBtn, &macroA, &macroB, &macroTrack }) addAndMakeVisible (c);
    for (int i = 0; i < kModParams; ++i)
    {
        param[i] = std::make_unique<PixSlider> ("");
        param[i]->labelChars = 10;
        param[i]->valueChars = 10;
        param[i]->onDragStart = [this, i]
        {
            const auto& ps = modParams (cur().source);
            if (i < (int) ps.size()) proc.history.mixer ("MOD " + juce::String (sel + 1) + " " + juce::String (ps[(size_t) i].name), true);
        };
        param[i]->onChange = [this, i] (float v) { proc.withLock ([&] { cur().p[i] = v; }); refresh(); };
        param[i]->format = [this, i] (float v)
        {
            const auto& md = cur();
            float tmp[kModParams];
            std::copy (md.p, md.p + kModParams, tmp);
            tmp[i] = v;
            return juce::String (modParamText (md.source, i, tmp));
        };
        paramToggle[i] = std::make_unique<PixButton> ("", true);
        paramToggle[i]->onClick = [this, i]
        {
            const bool on = paramToggle[i]->getToggleState();
            const auto& ps = modParams (cur().source);
            if (i >= (int) ps.size()) return;
            edit ("MOD " + juce::String (sel + 1) + " " + juce::String (ps[(size_t) i].name) + (on ? " ON" : " OFF"), false, [i, on] (Modulator& md) { md.p[i] = on ? 1.0f : 0.0f; });
        };
        addChildComponent (*param[i]);
        addChildComponent (*paramToggle[i]);
    }
    for (int k = 0; k < kModRoutes; ++k)
    {
        rows[k] = std::make_unique<Row>();
        auto& r = *rows[k];
        r.amount.bipolar = true;
        r.amount.format = [] (float v) { return amountText (v * 2.0f - 1.0f); };
        r.amount.onDragStart = [this, k] { proc.history.mixer ("MOD " + juce::String (sel + 1) + " AMOUNT " + juce::String (k + 1), true); };
        r.amount.onChange = [this, k] (float v) { proc.withLock ([&] { cur().route[k].amount = juce::jlimit (-1.0f, 1.0f, v * 2.0f - 1.0f); }); };
        r.amount.onDoubleClick = [this, k] { edit ("MOD " + juce::String (sel + 1) + " AMOUNT " + juce::String (k + 1), false, [k] (Modulator& md) { md.route[k].amount = 0.5f; }); };
        r.remove.onClick = [this, k] { removeRoute (k); };
        r.name.onClick = [this, k]
        {
            const auto& rt = cur().route[k];
            if (! rt.valid()) return;
            juce::Component::SafePointer<ModPage> safe (this);
            std::vector<MenuItem> items;
            items.push_back ({ "TURN IT THE OTHER WAY", [safe, k] { if (safe) safe->edit ("MOD AMOUNT", false, [k] (Modulator& md) { md.route[k].amount = -md.route[k].amount; }); } });
            if (rt.target < kTotalSlots)
                items.push_back ({ "GO TO THE EFFECT", [safe, t = rt.target] { if (safe) safe->mixer.openEffect (t); } });
            items.push_back ({ "REMOVE THIS TARGET", [safe, k] { if (safe) safe->removeRoute (k); } });
            showMenu (juce::String (rt.name), std::move (items));
        };
        addChildComponent (r.name); addChildComponent (r.amount); addChildComponent (r.remove);
    }
    refresh();
}

ModPage::~ModPage() = default;

Modulator& ModPage::cur() { return proc.song.mixer.mods.mod[juce::jlimit (0, kMods - 1, sel)]; }

void ModPage::edit (const juce::String& label, bool merge, std::function<void (Modulator&)> fn)
{
    const int m = sel;
    mixer.editMixer (label, merge, [m, fn] (MixerSettings& s) { fn (s.mods.mod[m]); });
    refresh();
}

void ModPage::select (int m)
{
    sel = juce::jlimit (0, kMods - 1, m);
    refresh();
    resized();
}

juce::String ModPage::modButtonText (int m) const
{
    const auto& md = proc.song.mixer.mods.mod[m];
    const juce::String n = juce::String (m + 1).paddedLeft (' ', 2);
    if (! md.on()) return n + " -";
    const int r = md.routes();
    return n + " " + juce::String (modSummary (md)) + (r > 0 ? "  >" + juce::String (r) : juce::String());
}

juce::String ModPage::routeText (int k) const
{
    const auto& rt = proc.song.mixer.mods.mod[sel].route[k];
    return rt.valid() ? juce::String (rt.name) : juce::String();
}

juce::String ModPage::targetName (PaulaProcessor& proc, int target, int param)
{
    if (target >= kModStripBase && target < kModStripBase + kMixTracks)
        return "TRACK " + juce::String (target - kModStripBase + 1) + ": " + (param == 0 ? "VOLUME" : param == 1 ? "SEND A" : "SEND B");
    if (target < 0 || target >= kTotalSlots) return "-";
    const auto& s = proc.song.mixer.slot (target);
    if (s.type == FxType::Plugin)
    {
        auto* pe = proc.pluginAt (target);
        return juce::String (s.pluginName).toUpperCase() + ": " + (pe ? prv::PluginHost::parameterName (pe->instance(), param).toUpperCase() : "#" + juce::String (param));
    }
    const auto& ps = fxParams (s.type);
    return MixerPage::slotOwner (target) + " " + juce::String (fxName (s.type)) + ": " + (param < (int) ps.size() ? juce::String (ps[(size_t) param].name) : juce::String (param));
}

void ModPage::setSource (ModSource src)
{
    const int m = sel;
    edit ("MOD " + juce::String (m + 1) + " " + juce::String (modSourceName (src)), false, [src] (Modulator& md)
    {
        initModulator (md, src);
        if (src == ModSource::Off) for (auto& r : md.route) r = ModRoute {};
    });
    resized();
    if (onStatus) onStatus ("MOD " + juce::String (m + 1) + ": " + juce::String (modSourceName (src))
                            + (cur().routes() == 0 && src != ModSource::Off ? " - NOW ADD A TARGET" : ""));
}

void ModPage::addRoute (ModRoute r)
{
    auto& md = cur();
    int free = -1;
    for (int k = 0; k < kModRoutes; ++k)
    {
        if (md.route[k].valid() && md.route[k].target == r.target && md.route[k].param == r.param) { free = -2; break; }
        if (free == -1 && ! md.route[k].valid()) free = k;
    }
    if (free == -2) { if (onStatus) onStatus ("MOD " + juce::String (sel + 1) + " ALREADY MOVES " + juce::String (r.name)); return; }
    if (free < 0) { if (onStatus) onStatus ("MOD " + juce::String (sel + 1) + " HAS 8 TARGETS ALREADY - REMOVE ONE FIRST"); return; }
    // a level from a FOLLOWER turns a track down (ducking); everything else swings by half
    if (r.target >= kModStripBase && md.source == ModSource::Follower && r.param == 0) r.amount = -1.0f;
    const bool wasOff = ! md.on();
    edit ("MOD " + juce::String (sel + 1) + " > " + juce::String (r.name), false, [free, r, wasOff] (Modulator& m)
    {
        if (wasOff) initModulator (m, ModSource::Lfo);
        m.route[free] = r;
    });
    resized();
    if (onStatus) onStatus ("MOD " + juce::String (sel + 1) + " NOW MOVES " + juce::String (r.name) + (wasOff ? " (AN LFO - PICK ANOTHER SOURCE IF YOU LIKE)" : ""));
}

void ModPage::removeRoute (int k)
{
    if (! cur().route[k].valid()) return;
    const auto name = juce::String (cur().route[k].name);
    edit ("MOD " + juce::String (sel + 1) + " REMOVE " + name, false, [k] (Modulator& md)
    {
        for (int j = k; j + 1 < kModRoutes; ++j) md.route[j] = md.route[j + 1];   // (the rest move up)
        md.route[kModRoutes - 1] = ModRoute {};
    });
    resized();
}

void ModPage::setStep (int step, float v, bool merge)
{
    if (step < 0 || step >= kModSteps) return;
    if (! merge) proc.history.mixer ("MOD " + juce::String (sel + 1) + " STEPS", false);
    proc.withLock ([&] { cur().steps[step] = juce::jlimit (0.0f, 1.0f, v); });
    repaint (stepsArea.toNearestInt());
}

void ModPage::setMacroKnob (int knob)
{
    const int tr = macroTrack.getValue() - 1, m = sel;
    const AutoTarget t { kModMacroBase + m, 0, ("MOD " + juce::String (m + 1) + " MACRO").toStdString() };
    mixer.editMixer ("TRACK " + juce::String (tr + 1) + (knob == 0 ? " AUTO A = " : " AUTO B = ") + "MOD " + juce::String (m + 1) + " MACRO", false,
                     [tr, knob, t] (MixerSettings& s) { s.trackAuto[tr][knob] = t; });
    if (onStatus) onStatus ("TRACK " + juce::String (tr + 1) + (knob == 0 ? " VXX" : " JXX") + " NOW TURNS MOD " + juce::String (m + 1) + " (00 = ITS LOWEST, FF = ITS HIGHEST)");
    repaint();
}

void ModPage::showSourceMenu()
{
    juce::Component::SafePointer<ModPage> safe (this);
    static const std::pair<ModSource, const char*> list[] = {
        { ModSource::Lfo, "LFO - A WAVE, FREE OR IN TIME WITH THE SONG" },
        { ModSource::Follower, "FOLLOWER - A TRACK'S LEVEL (DUCKING, SIDECHAIN)" },
        { ModSource::Stepper, "STEPPER - 16 STEPS YOU DRAW, IN TIME" },
        { ModSource::Random, "RANDOM - NEW VALUES, STEPPED OR SMOOTH" },
        { ModSource::Key, "KEY - HOW HIGH THE NOTES ARE" },
        { ModSource::Velocity, "VELOCITY - HOW LOUD THE NOTES ARE" },
        { ModSource::Macro, "MACRO - ONE KNOB FOR MANY CONTROLS" },
        { ModSource::Off, "OFF" } };
    std::vector<MenuItem> items;
    for (const auto& [src, text] : list) items.push_back ({ text, [safe, src = src] { if (safe) safe->setSource (src); } });
    showMenu ("MOD " + juce::String (sel + 1) + " SOURCE", std::move (items));
}

void ModPage::showTargetMenu()
{
    juce::Component::SafePointer<ModPage> safe (this);
    auto& pr = proc;
    auto add = [safe, &pr] (int target, int param, float base)
    {
        if (! safe) return;
        ModRoute r; r.target = target; r.param = param; r.base = base; r.amount = 0.5f;
        r.name = targetName (pr, target, param).toStdString();
        safe->addRoute (r);
    };
    std::vector<MenuItem> groups;
    groups.push_back ({ "A TRACK'S VOLUME OR SENDS...", [safe, add, &pr]
    {
        std::vector<MenuItem> tracks;
        for (int t = 0; t < pr.song.numTracks && t < kMixTracks; ++t)
            tracks.push_back ({ "TRACK " + juce::String (t + 1), [add, t]
            {
                std::vector<MenuItem> ps;
                for (int k = 0; k < kModStripParams; ++k)
                    ps.push_back ({ juce::String (k == 0 ? "VOLUME" : k == 1 ? "SEND A" : "SEND B"), [add, t, k] { add (kModStripBase + t, k, 0.5f); } });
                showMenu ("TRACK " + juce::String (t + 1), std::move (ps));
            } });
        showMenu ("WHICH TRACK?", std::move (tracks));
    } });
    // every effect in the song
    int listed = 0;
    for (int slot = 0; slot < kTotalSlots && listed < 14; ++slot)
    {
        const auto& s = proc.song.mixer.slot (slot);
        if (s.type == FxType::None) continue;
        if (slot < kTrackSlots && slot / kInserts >= proc.song.numTracks) continue;
        ++listed;
        const juce::String owner = MixerPage::slotOwner (slot) + " INSERT " + juce::String (slot % kInserts + 1) + ": "
                                 + (s.type == FxType::Plugin ? juce::String (s.pluginName).toUpperCase() : juce::String (fxName (s.type)));
        groups.push_back ({ owner + "...", [add, slot, owner, &pr]
        {
            const auto& sl = pr.song.mixer.slot (slot);
            std::vector<MenuItem> ps;
            if (sl.type == FxType::Plugin)
            {
                if (auto* pe = pr.pluginAt (slot))
                {
                    const auto vals = prv::PluginHost::parameterValues (pe->instance());
                    for (int i = 0; i < (int) vals.size() && i < 16; ++i)
                        ps.push_back ({ prv::PluginHost::parameterName (pe->instance(), i).toUpperCase(), [add, slot, i, b = vals[(size_t) i]] { add (slot, i, b); } });
                }
            }
            else
            {
                const auto& info = fxParams (sl.type);
                for (int i = 0; i < (int) info.size(); ++i)
                    ps.push_back ({ juce::String (info[(size_t) i].name), [add, slot, i] { add (slot, i, 0.5f); } });
            }
            if (ps.empty()) return;
            showMenu (owner, std::move (ps));
        } });
    }
    showMenu ("WHAT SHOULD MOD " + juce::String (sel + 1) + " MOVE?", std::move (groups));
}

// ---------------------------------------------------------------- refresh / live values
void ModPage::refresh()
{
    const auto& md = cur();
    for (int i = 0; i < kMods; ++i)
    {
        modBtn[i]->setButtonText (modButtonText (i));
        modBtn[i]->setToggleState (i == sel, juce::dontSendNotification);
    }
    sourceBtn.setButtonText ("SOURCE: " + juce::String (modSourceName (md.source)));
    clearBtn.setEnabled (md.on() || md.routes() > 0);
    const auto& ps = modParams (md.source);
    bool layoutChanged = false;
    for (int i = 0; i < kModParams; ++i)
    {
        const bool has = i < (int) ps.size();
        const bool tog = has && ps[(size_t) i].toggle;
        if (param[i]->isVisible() != (has && ! tog) || paramToggle[i]->isVisible() != tog) layoutChanged = true;
        param[i]->setVisible (has && ! tog);
        paramToggle[i]->setVisible (tog);
        if (! has) continue;
        if (tog)
        {
            paramToggle[i]->setButtonText (juce::String (ps[(size_t) i].name) + ": " + (md.p[i] > 0.5f ? "ON" : "OFF"));
            paramToggle[i]->setToggleState (md.p[i] > 0.5f, juce::dontSendNotification);
        }
        else
        {
            param[i]->setLabel (ps[(size_t) i].name);
            param[i]->defaultValue = ps[(size_t) i].def;
            param[i]->setValue (md.p[i]);
            param[i]->repaint();
        }
    }
    const bool macro = md.source == ModSource::Macro;
    if (macroA.isVisible() != macro) layoutChanged = true;
    macroA.setVisible (macro); macroB.setVisible (macro); macroTrack.setVisible (macro);
    for (int k = 0; k < kModRoutes; ++k)
    {
        auto& r = *rows[k];
        const auto& rt = md.route[k];
        const bool v = rt.valid();
        if (r.name.isVisible() != v) layoutChanged = true;
        r.name.setVisible (v); r.amount.setVisible (v); r.remove.setVisible (v);
        if (! v) continue;
        r.name.setButtonText (juce::String (rt.name));
        r.amount.setValue ((rt.amount + 1.0f) * 0.5f);
        r.amount.defaultValue = 0.75f;
    }
    addBtn.setEnabled (md.routes() < kModRoutes);
    if (layoutChanged) resized();
    repaint();
}

void ModPage::tick()
{
    bool changed = false;
    for (int m = 0; m < kMods; ++m)
    {
        const float v = proc.player.mixer().mods.uiOut[m].load (std::memory_order_relaxed);
        if (std::abs (v - shown[m]) > 0.003f) { shown[m] = v; changed = true; }
    }
    if (changed) repaint();
}

// ---------------------------------------------------------------- drawing
void ModPage::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    const auto& P = Theme::palFor (*this);
    const juce::Colour accent (0xffffa030);
    auto meter = [&] (juce::Rectangle<float> r, float v, bool bipolar)
    {
        g.setColour (P.paper); g.fillRect (r);
        Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f), P);
        auto in = r.reduced (px);
        g.setColour (accent);
        if (bipolar)
        {
            const float c = in.getCentreX(), x = c + in.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, v);
            g.fillRect (juce::Rectangle<float> (std::min (c, x), in.getY(), std::abs (x - c) + 1.0f, in.getHeight()));
            g.setColour (P.ink); g.fillRect (c - px * 0.5f, in.getY(), px, in.getHeight());
        }
        else g.fillRect (in.withWidth (in.getWidth() * juce::jlimit (0.0f, 1.0f, v)));
    };
    const auto& mods = proc.song.mixer.mods;
    for (int m = 0; m < kMods; ++m)
        if (mods.mod[m].on()) meter (listMeters[m], shown[m], modBipolar (mods.mod[m].source));
    const auto& md = cur();
    if (md.on())
    {
        meter (meterArea, shown[sel], modBipolar (md.source));
        PixelFont::drawLeft (g, "NOW", infoArea, px, P.ink);
    }
    if (md.source == ModSource::Stepper)
    {
        // the steps: bars to draw with the mouse; the one playing lit
        const int steps = 1 + fxChoice (md.p[1], kModSteps);
        const int now = proc.player.mixer().mods.uiStep[sel].load (std::memory_order_relaxed);
        const float w = stepsArea.getWidth() / kModSteps;
        for (int i = 0; i < kModSteps; ++i)
        {
            auto cell = juce::Rectangle<float> (stepsArea.getX() + i * w, stepsArea.getY(), w, stepsArea.getHeight()).reduced (px * 0.5f, 0);
            g.setColour (P.paper); g.fillRect (cell);
            Theme::bevel (g, cell, false, std::max (1.0f, px * 0.5f), P);
            auto in = cell.reduced (px);
            const float h = in.getHeight() * md.steps[i];
            g.setColour (i >= steps ? P.ink.withAlpha (0.25f) : (i == now && proc.player.isPlaying() ? accent : P.ink));
            g.fillRect (in.withTop (in.getBottom() - h));
        }
    }
    if (md.source == ModSource::Macro)
        PixelFont::drawLeft (g, "V / J MOVE IT:", macroInfo, px, P.ink);
    const int n = md.routes();
    PixelFont::drawLeft (g, ! md.on() ? juce::String ("PICK A SOURCE, THEN ADD TARGETS")
                                      : n == 0 ? juce::String ("NO TARGETS YET")
                                               : "TARGETS (" + juce::String (n) + " OF 8)", routesHead, px, P.ink);
}

void ModPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11), bigH = (int) (px * 13);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds();
    // the list of 16
    auto list = r.removeFromLeft (charsW (31));
    const int lh = std::min (rowH, (list.getHeight() - gap * (kMods - 1)) / kMods);
    for (int m = 0; m < kMods; ++m)
    {
        auto row = list.removeFromTop (lh); list.removeFromTop (gap);
        listMeters[m] = row.removeFromRight (charsW (4)).reduced (0, (int) px * 2).toFloat();
        row.removeFromRight (gap);
        modBtn[m]->setBounds (row);
    }
    r.removeFromLeft (gap * 6);
    // the selected one: its source and what it's doing now
    auto top = r.removeFromTop (rowH);
    sourceBtn.setBounds (top.removeFromLeft (charsW (19))); top.removeFromLeft (gap);
    clearBtn.setBounds (top.removeFromLeft (charsW (6))); top.removeFromLeft (gap * 4);
    infoArea = top.removeFromLeft (charsW (4)).toFloat();
    meterArea = top.withWidth (std::min (top.getWidth(), charsW (30))).toFloat();
    r.removeFromTop (gap * 3);
    // its controls (up to 6, in a column)
    const auto& md = cur();
    const auto& ps = modParams (md.source);
    const int n = std::min ((int) ps.size(), kModParams);
    auto area = r.withWidth (std::min (r.getWidth(), charsW (72)));
    for (int i = 0; i < n; ++i)
    {
        auto row = area.removeFromTop (bigH); area.removeFromTop (gap * 2);
        if (ps[(size_t) i].toggle) paramToggle[i]->setBounds (row.withWidth (charsW (14)));
        else param[i]->setBounds (row);
    }
    r.removeFromTop (n * (bigH + gap * 2) + gap);
    stepsArea = {};
    if (md.source == ModSource::Stepper) { stepsArea = r.removeFromTop ((int) (px * 36)).withWidth (std::min (r.getWidth(), charsW (100))).toFloat(); r.removeFromTop (gap * 3); }
    if (md.source == ModSource::Macro)
    {
        auto row = r.removeFromTop (rowH);
        macroInfo = row.removeFromLeft (charsW (15)).toFloat();
        macroTrack.setBounds (row.removeFromLeft (macroTrack.preferredWidth (px))); row.removeFromLeft (gap * 2);
        macroA.setBounds (row.removeFromLeft (charsW (11))); row.removeFromLeft (gap);
        macroB.setBounds (row.removeFromLeft (charsW (11)));
        r.removeFromTop (gap * 3);
    }
    // its targets
    auto head = r.removeFromTop (rowH);
    addBtn.setBounds (head.removeFromRight (charsW (13)));
    routesHead = head.toFloat();
    r.removeFromTop (gap * 2);
    for (int k = 0; k < kModRoutes; ++k)
    {
        auto row = r.removeFromTop (rowH); r.removeFromTop (gap);
        auto& rw = *rows[k];
        rw.remove.setBounds (row.removeFromRight (charsW (1) + gap)); row.removeFromRight (gap);
        rw.amount.setBounds (row.removeFromRight (std::min (charsW (24), row.getWidth() / 3))); row.removeFromRight (gap * 2);
        rw.name.setBounds (row);
    }
}

void ModPage::stepAt (juce::Point<float> p, bool merge)
{
    if (stepsArea.isEmpty()) return;
    const int i = juce::jlimit (0, kModSteps - 1, (int) ((p.x - stepsArea.getX()) / (stepsArea.getWidth() / kModSteps)));
    const float v = 1.0f - (p.y - stepsArea.getY() - Theme::px) / std::max (1.0f, stepsArea.getHeight() - Theme::px * 2);
    setStep (i, v, merge);
}

void ModPage::mouseDown (const juce::MouseEvent& e)
{
    dragStep = stepsArea.contains (e.position) ? 1 : -1;
    if (dragStep > 0) stepAt (e.position, false);
}

void ModPage::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep > 0) stepAt (e.position, true);
}
} // namespace prv::ui
