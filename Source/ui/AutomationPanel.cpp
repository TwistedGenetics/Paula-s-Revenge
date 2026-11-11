// Paula's Revenge - graphical AUTOMATION (see AutomationPanel.h)
#include "AutomationPanel.h"
#include "../engine/GearMaps.h"
#include "ModPage.h"
#include "MixerPage.h"
#include "../engine/Modulation.h"

namespace prv::ui
{
std::vector<AutoPoint> AutomationPanel::clip;

namespace
{
    juce::String fullName (PaulaProcessor& proc, int target, int param)
    {
        if (target >= kModMacroBase && target < kModMacroBase + kMods) return "MOD " + juce::String (target - kModMacroBase + 1) + " MACRO";
        if (target >= kInstMacroBase && target < kInstMacroBase + kMixInstruments * kInstMacros)
            return "INSTRUMENT " + juce::String::toHexString ((target - kInstMacroBase) / kInstMacros + 1).toUpperCase().paddedLeft ('0', 2)
                 + " MACRO " + juce::String ((target - kInstMacroBase) % kInstMacros + 1);
        if (target >= kAutoInstrumentBase && target < kAutoInstrumentBase + kMaxInstruments)
        {
            const int ins = target - kAutoInstrumentBase;
            juce::String p = "#" + juce::String (param);
            if (auto* s = proc.synthAt (ins)) p = prv::PluginHost::parameterName (s->instance(), param).toUpperCase();
            else if (param >= kGearAutoBase)
                if (const auto* map = gearMap (proc.song.instruments[(size_t) ins].midiGear))
                    if (param - kGearAutoBase < (int) map->params.size()) p = juce::String (map->params[(size_t) (param - kGearAutoBase)].label());
            return "INSTRUMENT " + juce::String::toHexString (ins + 1).toUpperCase().paddedLeft ('0', 2) + ": " + p;
        }
        return ModPage::targetName (proc, target, param);
    }
}

AutomationPanel::AutomationPanel (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    targetList.getNumRows = [this] { return (int) list.size(); };
    targetList.getRowText = [this] (int r)
    {
        if (r < 0 || r >= (int) list.size()) return juce::String();
        const auto* b = block() >= 0 ? &proc.song.blocks[(size_t) block()] : nullptr;
        const auto* l = b ? b->lane (list[(size_t) r].target, list[(size_t) r].param) : nullptr;
        return (l != nullptr && ! l->points.empty() ? "* " : "  ") + list[(size_t) r].shortName;
    };
    targetList.onClick = [this] (int r, const juce::MouseEvent&) { selectTarget (r); grabKeyboardFocus(); };
    addAndMakeVisible (targetList);
    onlyBtn.onClick = [this] { setAutomatedOnly (onlyBtn.getToggleState()); };
    for (int i = 0; i < 3; ++i)
    {
        shapeBtn[i].setClickingTogglesState (false);
        shapeBtn[i].onClick = [this, i] { setShape ((AutoShape) i); };
    }
    drawBtn.setClickingTogglesState (false);
    lineBtn.setClickingTogglesState (false);
    drawBtn.onClick = [this] { setTool (tool == Tool::Draw ? Tool::Edit : Tool::Draw); };
    lineBtn.onClick = [this] { setTool (tool == Tool::Line ? Tool::Edit : Tool::Line); };
    snapBtn.onClick = [this] { setSnap (snap == Snap::Lines ? Snap::Beats : snap == Snap::Beats ? Snap::Off : Snap::Lines); };
    copyBtn.onClick = [this] { copyLane(); };
    pasteBtn.onClick = [this] { pasteLane(); };
    clearBtn.onClick = [this] { clearLane(); };
    for (auto* c : std::initializer_list<juce::Component*> { &onlyBtn, &shapeBtn[0], &shapeBtn[1], &shapeBtn[2], &drawBtn, &lineBtn, &snapBtn, &copyBtn, &pasteBtn, &clearBtn })
        addAndMakeVisible (c);
    refresh();
}

int AutomationPanel::block() const
{
    const int b = proc.editorState.block;
    return b >= 0 && b < (int) proc.song.blocks.size() ? b : -1;
}

int AutomationPanel::lines() const { return block() >= 0 ? std::max (1, proc.song.blocks[(size_t) block()].lines) : 64; }

const AutoLane* AutomationPanel::lane() const
{
    if (block() < 0 || selRow < 0 || selRow >= (int) list.size()) return nullptr;
    return proc.song.blocks[(size_t) block()].lane (list[(size_t) selRow].target, list[(size_t) selRow].param);
}

// ---------------------------------------------------------------- the list
void AutomationPanel::rebuildList()
{
    const Target keep = selRow >= 0 && selRow < (int) list.size() ? list[(size_t) selRow] : Target {};
    list.clear();
    const int t = juce::jlimit (0, std::max (0, proc.song.numTracks - 1), proc.editorState.track);
    const auto* b = block() >= 0 ? &proc.song.blocks[(size_t) block()] : nullptr;
    const auto words = juce::StringArray::fromTokens (query.toUpperCase(), " ", "");
    auto add = [&] (int target, int param, const juce::String& shortName)
    {
        Target x { target, param, fullName (proc, target, param), shortName };
        if (automatedOnly)
        {
            const auto* l = b ? b->lane (target, param) : nullptr;
            if (l == nullptr || l->points.empty()) return;
        }
        for (const auto& w : words) if (! x.name.contains (w) && ! x.shortName.contains (w)) return;
        list.push_back (x);
    };
    add (kModStripBase + t, 0, "VOLUME");
    add (kModStripBase + t, 1, "SEND A");
    add (kModStripBase + t, 2, "SEND B");
    auto addSlot = [&] (int slot, const juce::String& prefix)
    {
        const auto& s = proc.song.mixer.slot (slot);
        if (s.type == FxType::None) return;
        if (s.type == FxType::Plugin)
        {
            auto* pe = proc.pluginAt (slot);
            if (pe == nullptr) return;
            const int n = std::min ((int) prv::PluginHost::parameterValues (pe->instance()).size(), 48);
            for (int i = 0; i < n; ++i)
                add (slot, i, prefix + juce::String (s.pluginName).toUpperCase().substring (0, 6) + " " + prv::PluginHost::parameterName (pe->instance(), i).toUpperCase());
            return;
        }
        const auto& ps = fxParams (s.type);
        for (int i = 0; i < (int) ps.size(); ++i) add (slot, i, prefix + juce::String (fxShortName (s.type)) + " " + juce::String (ps[(size_t) i].name));
    };
    for (int k = 0; k < kInserts; ++k) addSlot (trackSlot (t, k), juce::String (k + 1) + " ");
    // the instrument being played: its own inserts, and a plug-in's own parameters
    const int ins = juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument);
    const juce::String insName = "I" + juce::String::toHexString (ins + 1).toUpperCase().paddedLeft ('0', 2) + " ";
    for (int k = 0; k < kInstMacros; ++k)
        if (proc.song.mixer.instMacro[ins][k].routes() > 0) add (kInstMacroBase + ins * kInstMacros + k, 0, insName + "MACRO " + juce::String (k + 1));
    for (int k = 0; k < kInserts; ++k) addSlot (instrumentSlot (ins, k), insName);
    // a MIDI-out instrument's GEAR: every control, by name (sent to the synth as the lane moves)
    {
        const auto& in = proc.song.instruments[(size_t) ins];
        if (in.isMidiOut())
            if (const auto* map = gearMap (in.midiGear))
                for (size_t n = 0; n < map->params.size() && n < 256; ++n)
                    add (kAutoInstrumentBase + ins, kGearAutoBase + (int) n, insName + juce::String (map->params[n].label()));
    }
    if (auto* sy = proc.synthAt (ins))
    {
        const int n = std::min ((int) prv::PluginHost::parameterValues (sy->instance()).size(), 48);
        for (int i = 0; i < n; ++i) add (kAutoInstrumentBase + ins, i, insName + prv::PluginHost::parameterName (sy->instance(), i).toUpperCase());
    }
    for (int m = 0; m < kMods; ++m)
        if (proc.song.mixer.mods.mod[m].source == ModSource::Macro) add (kModMacroBase + m, 0, "MOD " + juce::String (m + 1) + " MACRO");
    // stay on the same control if it's still there
    selRow = 0;
    for (int i = 0; i < (int) list.size(); ++i) if (list[(size_t) i].target == keep.target && list[(size_t) i].param == keep.param) selRow = i;
    if (list.empty()) selRow = -1;
    targetList.selected = std::max (0, selRow);
}

void AutomationPanel::refresh()
{
    shownBlock = block(); shownTrack = proc.editorState.track;
    rebuildList();
    const auto* l = lane();
    const AutoShape sh = l != nullptr ? l->shape : newShape;
    for (int i = 0; i < 3; ++i) shapeBtn[i].setToggleState ((int) sh == i, juce::dontSendNotification);
    drawBtn.setToggleState (tool == Tool::Draw, juce::dontSendNotification);
    lineBtn.setToggleState (tool == Tool::Line, juce::dontSendNotification);
    snapBtn.setButtonText (snap == Snap::Lines ? "SNAP: LINES" : snap == Snap::Beats ? "SNAP: BEATS" : "SNAP: OFF");
    onlyBtn.setToggleState (automatedOnly, juce::dontSendNotification);
    copyBtn.setEnabled (l != nullptr && ! l->points.empty());
    pasteBtn.setEnabled (! clip.empty() && selRow >= 0);
    clearBtn.setEnabled (l != nullptr);
    targetList.repaint();
    repaint();
}

void AutomationPanel::tick()
{
    if (block() != shownBlock || proc.editorState.track != shownTrack) { selPoint = -1; refresh(); }
    float p = -1;
    if (proc.player.uiEnvBlock.load (std::memory_order_relaxed) == block()) p = proc.player.uiEnvPos.load (std::memory_order_relaxed);
    if (std::abs (p - playPos) > 0.01f) { playPos = p; repaint (graph.toNearestInt().expanded (4)); }
}

void AutomationPanel::selectTarget (int row)
{
    selRow = row >= 0 && row < (int) list.size() ? row : -1;
    targetList.selected = std::max (0, selRow);
    selPoint = -1;
    if (selRow >= 0 && onStatus) onStatus ("AUTOMATION: " + list[(size_t) selRow].name);
    refresh();
}

void AutomationPanel::setTool (Tool t) { tool = t; refresh(); if (onStatus) onStatus (t == Tool::Draw ? "DRAW: DRAG ACROSS THE GRAPH TO PAINT POINTS" : t == Tool::Line ? "LINE: DRAG A STRAIGHT RAMP" : "EDIT: DOUBLE-CLICK ADDS OR REMOVES A POINT, DRAG MOVES ONE"); }
void AutomationPanel::setSnap (Snap s) { snap = s; refresh(); }
void AutomationPanel::setQuery (const juce::String& q) { query = q.toUpperCase(); rebuildList(); refresh(); }
void AutomationPanel::setAutomatedOnly (bool on) { automatedOnly = on; rebuildList(); refresh(); }

// ---------------------------------------------------------------- editing
AutoLane& AutomationPanel::laneForEdit (const juce::String& what, bool merge)
{
    proc.history.block (block(), "AUTOMATION " + what, merge);
    auto& b = proc.song.blocks[(size_t) block()];
    const auto& tg = list[(size_t) selRow];
    if (auto* l = b.lane (tg.target, tg.param)) return *l;
    proc.withLock ([&]
    {
        AutoLane l; l.target = tg.target; l.param = tg.param; l.shape = newShape; l.name = tg.name.toStdString();
        b.lanes.push_back (l);
    });
    return b.lanes.back();
}

void AutomationPanel::tidy (AutoLane& l)
{
    l.sort();
    for (auto& p : l.points) { p.pos = juce::jlimit (0.0f, (float) lines(), p.pos); p.value = juce::jlimit (0.0f, 1.0f, p.value); }
}

void AutomationPanel::setShape (AutoShape s)
{
    newShape = s;
    if (lane() != nullptr && block() >= 0)
    {
        auto& l = laneForEdit (s == AutoShape::Points ? "POINTS" : s == AutoShape::Lines ? "LINES" : "CURVES", false);
        proc.withLock ([&] { l.shape = s; });
    }
    refresh();
}

void AutomationPanel::copyLane()
{
    if (const auto* l = lane()) { clip = l->points; if (onStatus) onStatus ("COPIED " + juce::String ((int) clip.size()) + " POINTS"); }
    refresh();
}

void AutomationPanel::pasteLane()
{
    if (clip.empty() || selRow < 0 || block() < 0) return;
    auto& l = laneForEdit ("PASTE", false);
    proc.withLock ([&]
    {
        l.points.clear();
        for (const auto& p : clip) if (p.pos <= (float) lines()) l.points.push_back (p);
        tidy (l);
    });
    if (onStatus) onStatus ("PASTED ON " + list[(size_t) selRow].name);
    refresh();
}

void AutomationPanel::clearLane()
{
    if (lane() == nullptr) return;
    proc.history.block (block(), "AUTOMATION CLEAR", false);
    const auto tg = list[(size_t) selRow];
    proc.withLock ([&]
    {
        auto& v = proc.song.blocks[(size_t) block()].lanes;
        v.erase (std::remove_if (v.begin(), v.end(), [&] (const AutoLane& l) { return l.target == tg.target && l.param == tg.param; }), v.end());
    });
    selPoint = -1;
    refresh();
}

// ---------------------------------------------------------------- coordinates
juce::Point<float> AutomationPanel::toScreen (double pos, float value) const
{
    const float pad = Theme::px * 3;
    const auto in = graph.reduced (pad);
    return { in.getX() + (float) (pos / lines()) * in.getWidth(), in.getBottom() - juce::jlimit (0.0f, 1.0f, value) * in.getHeight() };
}

double AutomationPanel::posAt (float x) const
{
    const auto in = graph.reduced (Theme::px * 3);
    return juce::jlimit (0.0, (double) lines(), (double) (x - in.getX()) / std::max (1.0f, in.getWidth()) * lines());
}

float AutomationPanel::valueAt (float y) const
{
    const auto in = graph.reduced (Theme::px * 3);
    return juce::jlimit (0.0f, 1.0f, (in.getBottom() - y) / std::max (1.0f, in.getHeight()));
}

double AutomationPanel::snapPos (double pos) const
{
    if (snap == Snap::Lines) return std::round (pos);
    if (snap == Snap::Beats) return std::round (pos / 4.0) * 4.0;
    return pos;
}

int AutomationPanel::pointNear (juce::Point<float> p) const
{
    const auto* l = lane();
    if (l == nullptr) return -1;
    int best = -1; float bestD = Theme::px * 5;
    for (int i = 0; i < (int) l->points.size(); ++i)
    {
        const float d = toScreen (l->points[(size_t) i].pos, l->points[(size_t) i].value).getDistanceFrom (p);
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

juce::String AutomationPanel::valueText (float v) const
{
    if (selRow < 0 || selRow >= (int) list.size()) return {};
    const auto& tg = list[(size_t) selRow];
    if (tg.target >= kModStripBase && tg.target < kModStripBase + kMixTracks)
    {
        if (tg.param == 0) { const float db = faderPosToDb (v); return db <= -59.9f ? juce::String ("-INF") : juce::String (db, 1) + " DB"; }
        return juce::String (juce::roundToInt (v * 100)) + "%";
    }
    if (tg.target >= 0 && tg.target < kTotalSlots)
    {
        const auto& s = proc.song.mixer.slot (tg.target);
        if (isBuiltinFx (s.type) && tg.param < kFxParams)
        {
            float tmp[kFxParams];
            std::copy (s.p, s.p + kFxParams, tmp);
            tmp[tg.param] = v;
            return juce::String (fxParamText (s.type, tg.param, tmp));
        }
    }
    return juce::String (juce::roundToInt (v * 100)) + "%";
}

// ---------------------------------------------------------------- mouse
void AutomationPanel::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (e.position.y < kGrab * Theme::px ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void AutomationPanel::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None;
    if (e.position.y < kGrab * Theme::px) { drag = Drag::Resize; resizeFrom = getHeight(); return; }
    if (searchArea.contains (e.position)) { grabKeyboardFocus(); return; }
    if (! graph.contains (e.position) || selRow < 0 || block() < 0) return;
    const double pos = snapPos (posAt (e.position.x));
    const float val = valueAt (e.position.y);
    if (tool == Tool::Draw)
    {
        drag = Drag::Paint;
        paintLastPos = -1;
        laneForEdit ("DRAW", false);
        mouseDrag (e);
        return;
    }
    if (tool == Tool::Line)
    {
        drag = Drag::Line;
        lineFromPos = pos; lineFromValue = val; lineTo = e.position;
        return;
    }
    const int i = pointNear (e.position);
    if (i >= 0)
    {
        drag = Drag::Point; dragPoint = selPoint = i;
        laneForEdit ("MOVE POINT", false);
        repaint();
        return;
    }
    // CURVES: drag a line up or down to bend it
    if (const auto* l = lane(); l != nullptr && l->shape == AutoShape::Curves && l->points.size() >= 2)
    {
        const double at = posAt (e.position.x);
        for (int k = 0; k + 1 < (int) l->points.size(); ++k)
            if (at > l->points[(size_t) k].pos && at < l->points[(size_t) k + 1].pos)
            {
                drag = Drag::Bend; dragPoint = k;
                lineFromValue = l->points[(size_t) k].curve;
                laneForEdit ("BEND", false);
                return;
            }
    }
    selPoint = -1;
    repaint();
}

void AutomationPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::Resize)
    {
        if (onResize) onResize (resizeFrom - e.getDistanceFromDragStartY());
        return;
    }
    if (drag == Drag::None || selRow < 0 || block() < 0) return;
    auto& b = proc.song.blocks[(size_t) block()];
    const auto& tg = list[(size_t) selRow];
    auto* l = b.lane (tg.target, tg.param);
    if (l == nullptr && drag != Drag::Line) return;
    const bool fine = e.mods.isShiftDown();
    if (drag == Drag::Point && l != nullptr && dragPoint >= 0 && dragPoint < (int) l->points.size())
    {
        double pos = fine || snap == Snap::Off ? posAt (e.position.x) : snapPos (posAt (e.position.x));
        const float lo = dragPoint > 0 ? l->points[(size_t) dragPoint - 1].pos : 0.0f;
        const float hi = dragPoint + 1 < (int) l->points.size() ? l->points[(size_t) dragPoint + 1].pos : (float) lines();
        pos = juce::jlimit ((double) lo, (double) hi, pos);
        const float v = valueAt (e.position.y);
        proc.withLock ([&] { l->points[(size_t) dragPoint].pos = (float) pos; l->points[(size_t) dragPoint].value = v; });
        readoutText = "LINE " + juce::String (pos, 2) + "  " + valueText (v);
        readoutAt = e.position;
    }
    else if (drag == Drag::Bend && l != nullptr && dragPoint >= 0 && dragPoint < (int) l->points.size())
    {
        const float c = juce::jlimit (-1.0f, 1.0f, lineFromValue - (float) e.getDistanceFromDragStartY() / std::max (1.0f, graph.getHeight() * 0.5f));
        proc.withLock ([&] { l->points[(size_t) dragPoint].curve = c; });
    }
    else if (drag == Drag::Paint && l != nullptr)
    {
        // paint: one point a line (a beat with SNAP: BEATS, a quarter-line with SNAP OFF), filling
        // in every one the mouse went past since the last move
        const double step = snap == Snap::Beats ? 4.0 : snap == Snap::Lines ? 1.0 : 0.25;
        const double raw = posAt (e.position.x);
        const double pos = std::round (raw / step) * step;
        const float v = valueAt (e.position.y);
        const double from = paintLastPos < 0 ? pos : paintLastPos;
        const float fromV = paintLastPos < 0 ? v : paintLastValue;
        proc.withLock ([&]
        {
            const int n = (int) std::lround (std::abs (pos - from) / step);
            for (int k = 0; k <= n; ++k)
            {
                const double q = n == 0 ? pos : from + (pos - from) * k / n;
                const float qv = n == 0 ? v : fromV + (v - fromV) * (float) k / (float) n;
                if (q < 0 || q > lines()) continue;
                bool found = false;
                for (auto& p : l->points) if (std::abs (p.pos - q) < 1e-4) { p.value = qv; found = true; }
                if (! found) l->points.push_back ({ (float) q, qv, 0.0f });
            }
            tidy (*l);
        });
        paintLastPos = pos; paintLastValue = v;
        readoutText = "LINE " + juce::String (pos, 2) + "  " + valueText (v);
        readoutAt = e.position;
    }
    else if (drag == Drag::Line) { lineTo = e.position; readoutText = valueText (valueAt (e.position.y)); readoutAt = e.position; }
    repaint();
}

void AutomationPanel::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::Line && selRow >= 0 && block() >= 0)
    {
        double a = lineFromPos, b = snapPos (posAt (e.position.x));
        float va = lineFromValue, vb = valueAt (e.position.y);
        if (b < a) { std::swap (a, b); std::swap (va, vb); }
        if (b - a > 1e-3)
        {
            auto& l = laneForEdit ("LINE", false);
            proc.withLock ([&]
            {
                l.points.erase (std::remove_if (l.points.begin(), l.points.end(), [a, b] (const AutoPoint& p) { return p.pos >= a - 1e-4 && p.pos <= b + 1e-4; }), l.points.end());
                l.points.push_back ({ (float) a, va, 0.0f });
                l.points.push_back ({ (float) b, vb, 0.0f });
                if (l.shape == AutoShape::Points) l.shape = AutoShape::Lines;
                tidy (l);
            });
        }
    }
    if (drag != Drag::None && drag != Drag::Resize) refresh();
    drag = Drag::None;
    readoutText.clear();
    repaint();
}

void AutomationPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! graph.contains (e.position) || selRow < 0 || block() < 0 || tool != Tool::Edit) return;
    const int i = pointNear (e.position);
    if (i >= 0)
    {
        auto& l = laneForEdit ("REMOVE POINT", false);
        proc.withLock ([&] { l.points.erase (l.points.begin() + i); });
        selPoint = -1;
    }
    else
    {
        const double pos = snapPos (posAt (e.position.x));
        const float v = valueAt (e.position.y);
        auto& l = laneForEdit ("ADD POINT", false);
        proc.withLock ([&] { l.points.push_back ({ (float) pos, v, 0.0f }); tidy (l); });
        for (int k = 0; k < (int) l.points.size(); ++k) if (std::abs (l.points[(size_t) k].pos - pos) < 1e-4) selPoint = k;
        if (onStatus) onStatus ("POINT AT LINE " + juce::String (pos, 2) + ": " + valueText (v));
    }
    refresh();
}

bool AutomationPanel::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::deleteKey) && selPoint >= 0 && lane() != nullptr && selPoint < (int) lane()->points.size())
    {
        auto& l = laneForEdit ("REMOVE POINT", false);
        proc.withLock ([&] { l.points.erase (l.points.begin() + selPoint); });
        selPoint = -1;
        refresh();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::backspaceKey)) { if (query.isNotEmpty()) setQuery (query.dropLastCharacters (1)); return true; }
    if (k.isKeyCode (juce::KeyPress::escapeKey)) { if (query.isNotEmpty()) setQuery ({}); else if (auto* p = getParentComponent()) p->grabKeyboardFocus(); return true; }
    const auto c = k.getTextCharacter();
    if (c >= ' ' && c < 127 && ! k.getModifiers().isCommandDown()) { setQuery (query + juce::String::charToString (c)); return true; }
    return false;
}

// ---------------------------------------------------------------- drawing
void AutomationPanel::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    const auto& P = Theme::palFor (*this);
    const juce::Colour accent = Theme::skin.accentMod;
    g.fillAll (P.panel);
    // the grab strip along the top
    g.setColour (P.ink.withAlpha (0.35f));
    for (int i = 0; i < 3; ++i) g.fillRect ((float) getWidth() * 0.5f - px * 12, px * (1.5f + i * 1.5f), px * 24, px * 0.6f);
    const int blk = block();
    PixelFont::drawLeft (g, "TRACK " + juce::String (proc.editorState.track + 1) + "  BLOCK " + juce::String (blk).paddedLeft ('0', 3), header, px, P.ink);
    // search
    g.setColour (P.paper); g.fillRect (searchArea);
    Theme::bevel (g, searchArea, false, std::max (1.0f, px * 0.5f), P);
    PixelFont::drawLeft (g, query.isEmpty() ? juce::String ("TYPE TO SEARCH") : "SEARCH: " + query + (hasKeyboardFocus (false) ? "_" : ""),
                         searchArea.reduced (px * 2, 0), px, query.isEmpty() ? P.ink.withAlpha (0.5f) : P.ink);
    // the graph
    g.setColour (P.paper); g.fillRect (graph);
    Theme::bevel (g, graph, false, std::max (1.0f, px * 0.5f), P);
    if (blk < 0) return;
    const int n = lines();
    const auto in = graph.reduced (px * 3);
    for (int l = 0; l <= n; ++l)
    {
        const float x = toScreen (l, 0).x;
        const bool bar = l % 16 == 0, beat = l % 4 == 0;
        if (! beat && in.getWidth() / n < px * 3) continue;
        g.setColour (P.ink.withAlpha (bar ? 0.45f : beat ? 0.25f : 0.1f));
        g.fillRect (x - (bar ? px * 0.5f : px * 0.25f), in.getY(), bar ? px : px * 0.5f, in.getHeight());
        if (bar && l < n) PixelFont::drawLeft (g, juce::String (l).paddedLeft ('0', 2), juce::Rectangle<float> (x + px * 2, in.getY(), px * 20, px * 9), px, P.ink.withAlpha (0.6f));
    }
    g.setColour (P.ink.withAlpha (0.2f));
    g.fillRect (in.getX(), toScreen (0, 0.5f).y, in.getWidth(), px * 0.5f);
    const auto* l = lane();
    if (l == nullptr || l->points.empty())
        PixelFont::drawCentred (g, selRow < 0 ? juce::String ("NOTHING TO AUTOMATE ON THIS TRACK") : juce::String ("DOUBLE-CLICK TO ADD A POINT, OR DRAW"), in, px, P.ink.withAlpha (0.6f));
    if (l != nullptr && ! l->points.empty())
    {
        // the envelope, then its points
        juce::Path path;
        const int steps = (int) in.getWidth() / 2;
        for (int i = 0; i <= steps; ++i)
        {
            const double pos = (double) i / steps * n;
            const auto pt = toScreen (pos, l->valueAt (pos));
            if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        g.setColour (accent);
        g.strokePath (path, juce::PathStrokeType (px * 1.5f));
        for (int i = 0; i < (int) l->points.size(); ++i)
        {
            const auto pt = toScreen (l->points[(size_t) i].pos, l->points[(size_t) i].value);
            const float s = px * (i == selPoint ? 5.0f : 3.5f);
            g.setColour (P.ink); g.fillRect (pt.x - s * 0.5f - px * 0.5f, pt.y - s * 0.5f - px * 0.5f, s + px, s + px);
            g.setColour (i == selPoint ? accent : P.paper); g.fillRect (pt.x - s * 0.5f + px * 0.5f, pt.y - s * 0.5f + px * 0.5f, s - px, s - px);
        }
    }
    if (drag == Drag::Line)
    {
        g.setColour (accent.withAlpha (0.8f));
        g.drawLine (juce::Line<float> (toScreen (lineFromPos, lineFromValue), lineTo), px * 1.5f);
    }
    // where the song is
    if (playPos >= 0)
    {
        const float x = toScreen (playPos, 0).x;
        g.setColour (P.ink); g.fillRect (x - px, in.getY(), px * 2, in.getHeight());
    }
    if (readoutText.isNotEmpty())
    {
        auto r = juce::Rectangle<float> (readoutAt.x + px * 6, readoutAt.y - px * 14, PixelFont::textWidth (readoutText, px) + px * 6, px * 11);
        r = r.constrainedWithin (graph);
        g.setColour (P.ink); g.fillRect (r);
        PixelFont::drawCentred (g, readoutText, r, px, P.paper);
    }
}

void AutomationPanel::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds();
    r.removeFromTop ((int) (kGrab * px));
    // left: what it is, the search, the controls, AUTOMATED ONLY
    auto left = r.removeFromLeft (std::min (charsW (30), (int) (r.getWidth() * 0.4f)));
    r.removeFromLeft (gap * 2);
    header = left.removeFromTop (rowH).toFloat();
    left.removeFromTop (gap);
    searchArea = left.removeFromTop (rowH).toFloat();
    left.removeFromTop (gap);
    onlyBtn.setBounds (left.removeFromBottom (rowH));
    left.removeFromBottom (gap);
    targetList.setBounds (left);
    // right: the tools (in as many rows as they need), then the graph
    struct Item { juce::Component* c; int chars; int gapAfter; };
    const Item items[] = { { &shapeBtn[0], 6, 1 }, { &shapeBtn[1], 5, 1 }, { &shapeBtn[2], 6, 3 }, { &drawBtn, 4, 1 }, { &lineBtn, 4, 3 },
                           { &snapBtn, 11, 3 }, { &copyBtn, 4, 1 }, { &pasteBtn, 5, 1 }, { &clearBtn, 5, 0 } };
    auto row = r.removeFromTop (rowH);
    int x = row.getX();
    for (const auto& it : items)
    {
        const int w = charsW (it.chars);
        if (x + w > row.getRight() && x > row.getX())
        {
            r.removeFromTop (gap);
            row = r.removeFromTop (rowH);
            x = row.getX();
        }
        it.c->setBounds (x, row.getY(), w, rowH);
        x += w + gap * it.gapAfter;
    }
    r.removeFromTop (gap);
    graph = r.toFloat();
}
} // namespace prv::ui
