// Paula's Revenge - your own effect presets (MY PRESETS), kept on this computer in the app's settings
#pragma once

#include <JuceHeader.h>
#include "../Prefs.h"
#include "../engine/MixerTypes.h"

namespace prv::ui::fxpresets
{
inline juce::String encode (const std::vector<FxPreset>& all)
{
    juce::Array<juce::var> arr;
    for (const auto& pr : all)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (pr.name));
        o->setProperty ("type", (int) pr.type);
        juce::Array<juce::var> vals;
        for (float v : pr.p) vals.add ((double) v);
        o->setProperty ("p", vals);
        arr.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (arr), true);
}

inline std::vector<FxPreset> decode (const juce::String& text)
{
    std::vector<FxPreset> out;
    const auto v = juce::JSON::parse (text);
    if (auto* arr = v.getArray())
        for (const auto& e : *arr)
        {
            const int t = (int) e.getProperty ("type", 0);
            const auto name = e.getProperty ("name", "").toString().toUpperCase().trim();
            if (t <= 0 || t >= (int) FxType::Count || ! isBuiltinFx ((FxType) t) || name.isEmpty()) continue;
            FxPreset pr;
            pr.name = name.substring (0, 24).toStdString();
            pr.type = (FxType) t;
            if (auto* vals = e.getProperty ("p", {}).getArray())
                for (const auto& x : *vals) pr.p.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) x));
            pr.p.resize (fxParams (pr.type).size(), 0.0f);
            out.push_back (std::move (pr));
        }
    return out;
}

inline std::vector<FxPreset> all() { return decode (prefs().getValue ("fxPresets")); }
inline void store (const std::vector<FxPreset>& v) { prefs().setValue ("fxPresets", encode (v)); prefs().saveIfNeeded(); }

// yours for one effect, in name order
inline std::vector<FxPreset> mine (FxType t)
{
    std::vector<FxPreset> out;
    for (auto& pr : all()) if (pr.type == t) out.push_back (pr);
    std::sort (out.begin(), out.end(), [] (const FxPreset& a, const FxPreset& b) { return a.name < b.name; });
    return out;
}

// a slot's settings as a preset of yours (one of the same name for that effect is replaced)
inline FxPreset fromSlot (const FxSlot& s, const juce::String& name)
{
    FxPreset pr;
    pr.name = name.toUpperCase().trim().substring (0, 24).toStdString();
    pr.type = s.type;
    const auto n = fxParams (s.type).size();
    pr.p.assign (s.p, s.p + std::min (n, (size_t) kFxParams));
    return pr;
}

inline void save (const FxPreset& pr)
{
    if (pr.name.empty() || ! isBuiltinFx (pr.type)) return;
    auto v = all();
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const FxPreset& x) { return x.type == pr.type && x.name == pr.name; }), v.end());
    v.push_back (pr);
    store (v);
}

inline void remove (FxType t, const std::string& name)
{
    auto v = all();
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const FxPreset& x) { return x.type == t && x.name == name; }), v.end());
    store (v);
}
} // namespace prv::ui::fxpresets
