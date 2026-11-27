// Paula's Revenge - undo for what happens inside a plug-in's own window (see PluginUndo.h)
#include "PluginUndo.h"
#include "PluginProcessor.h"
#include "PluginHost.h"
#include "ChoppageProcessor.h"

PluginUndoWatch::~PluginUndoWatch()
{
    stopTimer();
}

std::vector<uint8_t> PluginUndoWatch::stateOf (bool fx, int index) const
{
    if (fx) { if (auto* p = proc.pluginAt (index)) return p->state(); }
    else if (auto* s = proc.synthAt (index)) return s->state();
    return {};
}

void PluginUndoWatch::watch (bool fx, int index, void* wrapper, juce::AudioProcessor* inst, const std::string& key, const juce::String& name)
{
    const int id = fx ? 100000 + index : index;
    auto it = taps.find (id);
    const uint32_t now = juce::Time::getMillisecondCounter();
    if (it == taps.end() || it->second->wrapper != wrapper || it->second->inst != inst || it->second->key != key)
    {
        // a plug-in came (or changed): listen to it, and its settings now are where an undo goes back to
        if (it != taps.end()) { retired.push_back (std::move (it->second)); taps.erase (it); }
        auto t = std::make_unique<Tap>();
        t->wrapper = wrapper; t->inst = inst; t->key = key;
        t->baseline = stateOf (fx, index);
        t->baselineAt = now;
        t->ignoreUntil = now + 1500;     // (a plug-in loading sends changes of its own)
        inst->addListener (t.get());
        taps[id] = std::move (t);
        return;
    }
    auto& t = *it->second;
    if (t.edited.load())
    {
        if (now < t.ignoreUntil) { t.edited.store (false); t.touching.store (false); return; }
        if (t.touching.load() || now - t.lastMs.load() < (uint32_t) kSettleMs) return;   // still turning
        t.edited.store (false);
        auto after = stateOf (fx, index);
        if (after == t.baseline || t.baseline.empty()) { t.baseline = std::move (after); return; }
        proc.history.pluginState (fx, index, std::move (t.baseline), name.toUpperCase() + ": PLUG-IN SETTINGS");
        t.baseline = std::move (after);
        t.baselineAt = now;
        return;
    }
    // stopped and nobody touching: keep the starting point fresh (automation may have moved it), now and then
    if (! proc.player.isPlaying() && now - t.baselineAt > 5000)
    {
        t.baseline = stateOf (fx, index);
        t.baselineAt = now;
    }
}

void PluginUndoWatch::timerCallback()
{
    const auto& song = proc.song;
    for (int i = 0; i < prv::kMaxInstruments; ++i)
    {
        const auto& in = song.instruments[(size_t) i];
        if (! in.isPlugin() || in.isBuiltin() || in.isMidiOut() || in.isCvOut()) continue;
        auto* s = proc.synthAt (i);
        if (s == nullptr) continue;
        if (dynamic_cast<ChoppageProcessor*> (&s->instance()) != nullptr) continue;   // (its own undo)
        watch (false, i, s, &s->instance(), in.pluginKey, juce::String (in.pluginName));
    }
    for (int k = 0; k < prv::kTotalSlots; ++k)
    {
        const auto& slot = song.mixer.slot (k);
        if (slot.type != prv::FxType::Plugin) continue;
        auto* p = proc.pluginAt (k);
        if (p == nullptr) continue;
        watch (true, k, p, &p->instance(), slot.pluginKey, juce::String (slot.pluginName));
    }
}

void PluginUndoWatch::rebaseline (bool fx, int index)
{
    auto it = taps.find (fx ? 100000 + index : index);
    if (it == taps.end()) return;
    auto& t = *it->second;
    t.baseline = stateOf (fx, index);
    t.baselineAt = juce::Time::getMillisecondCounter();
    t.ignoreUntil = t.baselineAt + 800;
    t.edited.store (false);
    t.touching.store (false);
}
